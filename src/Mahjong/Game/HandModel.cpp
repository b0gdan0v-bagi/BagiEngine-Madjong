#include "HandModel.h"

#include <EASTL/sort.h>
#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/TileSet.h>
#include <cstdarg>
#include <cstdio>

namespace BECore::Mahjong {

    namespace {

        /// Whether the hand of `counts` (its concealed tiles) and `meldCount` / `meldTiles` (its melds) is waiting without one copy of the
        /// concealed `tile`: the counts are changed and put back.
        bool IsWaitingWithout(TileCounts& counts, Tile tile, int meldCount, const TileCounts& meldTiles) {
            --counts[tile.Id()];
            const bool waiting = IsWaiting(counts, meldCount, meldTiles);
            ++counts[tile.Id()];
            return waiting;
        }

        /// The open pung of `tile` in `hand`, nullptr when there is none (a pung is the meld an upgraded kong grows from).
        const Meld* FindPung(const PlayerHand& hand, Tile tile) {
            for (const Meld& meld : hand.melds) {
                if (meld.kind == MeldKind::Pung && !meld.concealed && meld.first == tile) {
                    return &meld;
                }
            }
            return nullptr;
        }

        /// The same, for a hand that is changed (`CompleteKong` turns the pung it finds into a kong).
        Meld* FindPung(PlayerHand& hand, Tile tile) {
            return const_cast<Meld*>(FindPung(static_cast<const PlayerHand&>(hand), tile));
        }

        /// The move a carried-out claim of `kind` is logged as (a mahjong claim is no move: it ends the hand).
        MoveKind MoveKindOf(ClaimKind kind) {
            ASSERT(kind != ClaimKind::Mahjong, "MoveKindOf: a mahjong claim ends the hand and is not logged");
            switch (kind) {
                case ClaimKind::Chow:
                    return MoveKind::ClaimChow;
                case ClaimKind::Pung:
                    return MoveKind::ClaimPung;
                case ClaimKind::Kong:
                case ClaimKind::Mahjong:
                    break;
            }
            return MoveKind::ClaimKong;
        }

        /// Whether `hand` is a winning hand with `tile` added (the declaration is not looked at here).
        bool WinsWith(const PlayerHand& hand, Tile tile) {
            TileCounts counts = CountTiles(hand.concealed);
            ++counts[tile.Id()];
            return IsWinningShape(counts, static_cast<int>(hand.melds.size()));
        }

        /// Whether the concealed tiles of `hand` with its melds form a winning hand (the declaration is not looked at here).
        bool IsWinningHand(const PlayerHand& hand) {
            return IsWinningShape(CountTiles(hand.concealed), static_cast<int>(hand.melds.size()));
        }

        /// The sequences of `tile` that `hand` can complete from its concealed tiles, as the lowest tile of each.
        eastl::vector<Tile> ChowStarts(const PlayerHand& hand, Tile tile) {
            eastl::vector<Tile> starts;
            if (!tile.IsSuit()) {
                return starts;
            }
            const int firstRank = eastl::max(1, tile.Rank() - (kTilesPerSet - 1));
            const int lastRank = eastl::min(kLastChowStartRank, tile.Rank());
            for (int rank = firstRank; rank <= lastRank; ++rank) {
                const Tile first{static_cast<uint8_t>(tile.Id() - tile.Rank() + rank)};
                bool complete = true;
                for (const Tile member : ChowTiles(first)) {
                    complete = complete && (member == tile || hand.CountConcealed(member) > 0);
                }
                if (complete) {
                    starts.push_back(first);
                }
            }
            return starts;
        }

        /// Every claim `seat` could make on `tile` discarded by `from`, or added to a kong of `from` (`mahjongOnly`: a kong can be
        /// robbed only to win). A mahjong claim needs the calling-hand declaration.
        eastl::vector<ClaimOption> ClaimOptionsFor(const PlayerHand& hand, Seat seat, Seat from, Tile tile, bool mahjongOnly) {
            eastl::vector<ClaimOption> options;
            if (seat == from || !tile.IsPlayable()) {
                return options;
            }
            if (hand.callingDeclared && WinsWith(hand, tile)) {
                options.push_back({ClaimKind::Mahjong, Tile{}});
            }
            if (mahjongOnly) {
                return options;
            }
            const int copies = hand.CountConcealed(tile);
            if (copies >= kPungTilesFromHand) {
                options.push_back({ClaimKind::Pung, Tile{}});
            }
            if (copies >= kKongTilesFromHand) {
                options.push_back({ClaimKind::Kong, Tile{}});
            }
            if (seat == NextSeat(from)) {
                for (const Tile first : ChowStarts(hand, tile)) {
                    options.push_back({ClaimKind::Chow, first});
                }
            }
            return options;
        }

        /// What beats what in the claim window: mahjong, then pung or kong, then chow.
        constexpr int kChowPriority = 1;
        constexpr int kPungPriority = 2;
        constexpr int kMahjongPriority = 3;

        int ClaimPriority(ClaimKind kind) noexcept {
            switch (kind) {
                case ClaimKind::Mahjong:
                    return kMahjongPriority;
                case ClaimKind::Pung:
                case ClaimKind::Kong:
                    return kPungPriority;
                case ClaimKind::Chow:
                    return kChowPriority;
            }
            return kChowPriority;
        }

        /// The longest text `Restore` gives (cut when longer).
        constexpr size_t kRefusalTextSize = 256;

        /// The failure of `Restore`: a text, built like printf.
        std::unexpected<eastl::string> Refuse(const char* format, ...) {
            char buffer[kRefusalTextSize];
            va_list args;
            va_start(args, format);
            std::vsnprintf(buffer, sizeof(buffer), format, args);
            va_end(args);
            return std::unexpected<eastl::string>(eastl::string(buffer));
        }

        /// The notation of a tile as an `eastl::string` (to print with `%s`).
        eastl::string NameOf(Tile tile) {
            const std::string_view name = ToString(tile);
            return eastl::string(name.data(), name.size());
        }

        /// How many tiles a seat holds in a hand of this phase, the way the model counts a hand (a meld is three tiles, a kong too):
        /// 13, and one more for the seat that acts and for a winner.
        int HeldTilesExpected(const HandState& state, Seat seat) {
            switch (state.phase) {
                case Phase::Discard:
                case Phase::RobKong:
                    return seat == state.current ? kDealtTiles + 1 : kDealtTiles;
                case Phase::Claim:
                    return kDealtTiles;
                case Phase::Finished:
                    return state.result.has_value() && state.result->kind == ResultKind::Win && state.result->winner == seat ? kDealtTiles + 1 : kDealtTiles;
            }
            return kDealtTiles;
        }

    }  // namespace

    HandStatus HandModel::Begin(const MahjongSettings& settings, Wall wall, const SeatWinds& seatWinds, Wind roundWind) {
        const Seat east = EastOf(seatWinds);
        if (east == kNoSeat) {
            return std::unexpected(HandError::BadSeatWinds);
        }

        *this = HandModel{};
        _result.reset();
        _settings = settings;
        _seatWinds = seatWinds;
        _roundWind = roundWind;
        _east = east;
        _wall = eastl::move(wall);
        _hands = DealHands(_wall, _east, _settings.flowers);
        StartTurn(_east);
        return {};
    }

    HandState HandModel::Capture() const {
        HandState state;
        state.settings = _settings;
        state.seatWinds = _seatWinds;
        state.roundWind = _roundWind;
        state.east = _east;
        state.loose = _wall.LooseTiles();
        state.live = _wall.LiveTiles();
        state.dead = _wall.DeadWall();
        state.hands = _hands;
        state.discards = _discards;
        state.moves = _moves;
        state.phase = _phase;
        state.current = _current;
        state.drawnTile = _drawnTile;
        state.drawnLoose = _drawnLoose;
        state.claimStatus = _claimStatus;
        state.claims = _claims;
        state.pendingTile = _pendingTile;
        state.result = _result;
        return state;
    }

    std::expected<HandModel, eastl::string> HandModel::Restore(HandState state) {
        if (EastOf(state.seatWinds) == kNoSeat) {
            return Refuse("the seat winds do not hold East exactly once");
        }
        if (state.east != EastOf(state.seatWinds)) {
            return Refuse("east is seat %d but the seat winds make seat %d East", state.east, EastOf(state.seatWinds));
        }
        if (!IsSeat(state.current)) {
            return Refuse("the current seat %d is no seat", state.current);
        }

        // Every tile of the set, once: counted over everything the state holds.
        eastl::array<int, kTileIdCount> counts{};
        const auto add = [&counts](Tile tile, int copies = 1) {
            if (!tile.IsValid()) {
                return false;
            }
            counts[tile.Id()] += copies;
            return true;
        };
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            PlayerHand& hand = state.hands[seat];
            eastl::sort(hand.concealed.begin(), hand.concealed.end());
            for (const Tile tile : hand.concealed) {
                if (!tile.IsPlayable() || !add(tile)) {
                    return Refuse("seat %d holds a concealed tile that is not a playable tile (id %d)", seat, static_cast<int>(tile.Id()));
                }
            }
            for (const Tile tile : hand.bonus) {
                if (!tile.IsBonus() || !add(tile)) {
                    return Refuse("seat %d has a bonus tile that is not a flower or a season (id %d)", seat, static_cast<int>(tile.Id()));
                }
            }
            for (const Meld& meld : hand.melds) {
                if (meld.concealed && meld.kind != MeldKind::Kong) {
                    return Refuse("seat %d has a concealed meld that is no kong", seat);
                }
                if (meld.kind == MeldKind::Chow) {
                    if (!meld.first.IsSuit() || meld.first.Rank() > kLastChowStartRank) {
                        return Refuse("seat %d has a chow that cannot exist (first tile id %d)", seat, static_cast<int>(meld.first.Id()));
                    }
                    for (const Tile member : ChowTiles(meld.first)) {
                        add(member);
                    }
                } else {
                    if (!meld.first.IsPlayable()) {
                        return Refuse("seat %d has a pung or kong of a tile that is not playable (id %d)", seat, static_cast<int>(meld.first.Id()));
                    }
                    add(meld.first, meld.kind == MeldKind::Kong ? kCopiesPerTile : kTilesPerSet);
                }
            }
            if (hand.melds.size() > static_cast<size_t>(kSetsInHand)) {
                return Refuse("seat %d has %d melds, at most four fit", seat, static_cast<int>(hand.melds.size()));
            }
        }
        for (const DiscardEntry& entry : state.discards) {
            if (!entry.tile.IsPlayable() || !IsSeat(entry.seat)) {
                return Refuse("the pile holds a discard that is not a playable tile of a seat (tile id %d, seat %d)", static_cast<int>(entry.tile.Id()), entry.seat);
            }
            add(entry.tile);
        }
        for (const eastl::vector<Tile>* part : {&state.loose, &state.live, &state.dead}) {
            for (const Tile tile : *part) {
                if (!add(tile)) {
                    return Refuse("the wall holds a tile that is not a tile (id %d)", static_cast<int>(tile.Id()));
                }
            }
        }
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const Tile tile{id};
            const int expected = tile.IsBonus() ? (state.settings.flowers ? 1 : 0) : kCopiesPerTile;
            if (counts[id] > expected) {
                return Refuse("%s is there %d times, only %d exist", NameOf(tile).c_str(), counts[id], expected);
            }
            if (counts[id] < expected) {
                return Refuse("%s is missing: there are %d of %d copies", NameOf(tile).c_str(), counts[id], expected);
            }
        }
        if (state.loose.size() > kLooseTiles) {
            return Refuse("the loose reserve holds %d tiles, at most %d", static_cast<int>(state.loose.size()), static_cast<int>(kLooseTiles));
        }
        if (state.dead.size() != kDeadWallTiles) {
            return Refuse("the dead wall holds %d tiles, not %d", static_cast<int>(state.dead.size()), static_cast<int>(kDeadWallTiles));
        }

        if (state.result.has_value() != (state.phase == Phase::Finished)) {
            return Refuse("a result exists exactly when the hand is finished");
        }
        if (state.result.has_value()) {
            const HandResult& result = *state.result;
            if (result.kind == ResultKind::Win ? (!IsSeat(result.winner) || !IsSeat(result.from) || !result.winningTile.IsPlayable()) : result.winner != kNoSeat) {
                return Refuse("the result does not fit its kind (a win needs a winner, an origin and a tile; a drawn hand has no winner)");
            }
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const PlayerHand& hand = state.hands[seat];
            const int held = static_cast<int>(hand.concealed.size()) + kTilesPerSet * static_cast<int>(hand.melds.size());
            if (held != HeldTilesExpected(state, seat)) {
                return Refuse("seat %d holds %d tiles, a hand in this phase holds %d", seat, held, HeldTilesExpected(state, seat));
            }
        }
        for (const HandMove& move : state.moves) {
            if (!IsSeat(move.seat)) {
                return Refuse("a move of the log has a seat that is no seat (%d)", move.seat);
            }
        }

        if (state.drawnTile.IsValid() && (state.phase == Phase::Discard || state.phase == Phase::RobKong) && state.hands[state.current].CountConcealed(state.drawnTile) == 0) {
            return Refuse("the drawn tile is not in the hand of seat %d", state.current);
        }

        const bool window = state.phase == Phase::Claim || state.phase == Phase::RobKong;
        if (!window) {
            if (state.pendingTile.IsValid()) {
                return Refuse("a tile is pending but no window is open");
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (state.claimStatus[seat] != ClaimStatus::NotInWindow || state.claims[seat] != ClaimOption{}) {
                    return Refuse("seat %d has a claim status but no window is open", seat);
                }
            }
        } else {
            if (!state.pendingTile.IsPlayable()) {
                return Refuse("a window is open but no playable tile is pending");
            }
            if (state.phase == Phase::Claim) {
                if (state.drawnTile.IsValid()) {
                    return Refuse("a drawn tile is kept in a claim window");
                }
                if (state.discards.empty() || state.discards.back().tile != state.pendingTile || state.discards.back().seat != state.current) {
                    return Refuse("the pending tile is not the last discard of seat %d", state.current);
                }
            } else if (FindPung(state.hands[state.current], state.pendingTile) == nullptr || state.hands[state.current].CountConcealed(state.pendingTile) == 0) {
                return Refuse("seat %d is adding a tile to a kong but has no open pung of it and the fourth tile", state.current);
            }
        }

        HandModel model;
        model._settings = state.settings;
        model._seatWinds = state.seatWinds;
        model._roundWind = state.roundWind;
        model._east = state.east;
        model._wall = Wall::FromParts(eastl::move(state.loose), eastl::move(state.live), eastl::move(state.dead));
        model._hands = eastl::move(state.hands);
        model._discards = eastl::move(state.discards);
        model._moves = eastl::move(state.moves);
        model._phase = state.phase;
        model._current = state.current;
        model._drawnTile = state.drawnTile;
        model._drawnLoose = state.drawnLoose;
        model._claimStatus = state.claimStatus;
        model._claims = state.claims;
        model._pendingTile = state.pendingTile;
        model._result = state.result;

        if (window) {
            bool someoneToDecide = false;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const ClaimStatus status = model._claimStatus[seat];
                if (seat == model._current) {
                    if (status != ClaimStatus::NotInWindow || model._claims[seat] != ClaimOption{}) {
                        return Refuse("seat %d, whose tile is pending, has a claim status", seat);
                    }
                    continue;
                }
                // Who is in the window is fixed by the hands when it opens, and no hand changes meanwhile: the seats that could claim.
                model._claimStatus[seat] = ClaimStatus::Waiting;
                const eastl::vector<ClaimOption> options = model.LegalClaims(seat);
                model._claimStatus[seat] = status;
                if (status == ClaimStatus::NotInWindow) {
                    if (!options.empty()) {
                        return Refuse("seat %d could claim the pending tile but is not in the window", seat);
                    }
                    continue;
                }
                if (options.empty()) {
                    return Refuse("seat %d is in the window but has no claim", seat);
                }
                someoneToDecide = someoneToDecide || status == ClaimStatus::Waiting;
                if (status == ClaimStatus::Claimed ? eastl::find(options.begin(), options.end(), model._claims[seat]) == options.end() : model._claims[seat] != ClaimOption{}) {
                    return Refuse("seat %d has a claim that does not fit its status or that it cannot make", seat);
                }
            }
            if (!someoneToDecide) {
                return Refuse("the window is open but every seat has decided");
            }
        }
        return model;
    }

    HandStatus HandModel::CheckOwnTurn(Seat seat) const {
        if (_phase != Phase::Discard) {
            return std::unexpected(HandError::WrongPhase);
        }
        if (seat != _current || !IsSeat(seat)) {
            return std::unexpected(HandError::NotYourTurn);
        }
        return {};
    }

    HandStatus HandModel::Discard(Seat seat, Tile tile, bool declareCalling) {
        if (const HandStatus turn = CheckOwnTurn(seat); !turn) {
            return turn;
        }
        PlayerHand& hand = _hands[seat];
        if (hand.CountConcealed(tile) == 0) {
            return std::unexpected(HandError::TileNotInHand);
        }
        if (declareCalling) {
            if (hand.callingDeclared) {
                return std::unexpected(HandError::AlreadyCalling);
            }
            TileCounts counts = CountTiles(hand.concealed);
            if (!IsWaitingWithout(counts, tile, static_cast<int>(hand.melds.size()), CountMeldTiles(hand.melds))) {
                return std::unexpected(HandError::NotWaiting);
            }
        }

        hand.RemoveConcealed(tile);
        if (declareCalling) {
            hand.callingDeclared = true;
            hand.callingAtDiscardCount = hand.discardCount;
        }
        ++hand.discardCount;
        _discards.push_back({tile, seat});
        _moves.push_back({.kind = MoveKind::Discard, .seat = seat, .tile = tile, .declaredCalling = declareCalling});
        _drawnTile = Tile{};
        _drawnLoose = false;
        AfterDiscard(seat, tile);
        return {};
    }

    HandStatus HandModel::DeclareWin(Seat seat) {
        if (const HandStatus turn = CheckOwnTurn(seat); !turn) {
            return turn;
        }
        const PlayerHand& hand = _hands[seat];
        if (!hand.callingDeclared) {
            return std::unexpected(HandError::NotCalling);
        }
        if (!_drawnTile.IsValid() || !IsWinningHand(hand)) {
            return std::unexpected(HandError::CannotWin);
        }
        FinishWin(seat, _drawnTile, WinSource::SelfDraw, seat, _drawnLoose);
        return {};
    }

    HandStatus HandModel::DeclareConcealedKong(Seat seat, Tile tile) {
        if (const HandStatus turn = CheckOwnTurn(seat); !turn) {
            return turn;
        }
        PlayerHand& hand = _hands[seat];
        if (hand.CountConcealed(tile) < kCopiesPerTile) {
            return std::unexpected(HandError::IllegalKong);
        }
        for (int copy = 0; copy < kCopiesPerTile; ++copy) {
            hand.RemoveConcealed(tile);
        }
        Meld meld;
        meld.kind = MeldKind::Kong;
        meld.concealed = true;
        meld.first = tile;
        hand.melds.push_back(meld);
        _moves.push_back({.kind = MoveKind::ConcealedKong, .seat = seat, .meld = static_cast<int>(hand.melds.size()) - 1});
        Receive(seat, _wall.DrawLoose(), true);
        return {};
    }

    HandStatus HandModel::UpgradeKong(Seat seat, Tile tile) {
        if (const HandStatus turn = CheckOwnTurn(seat); !turn) {
            return turn;
        }
        PlayerHand& hand = _hands[seat];
        // Only the tile drawn from the wall this turn upgrades a pung: not a copy kept from earlier turns, not right after a claim.
        const Meld* pung = FindPung(hand, tile);
        if (pung == nullptr || _drawnTile != tile) {
            return std::unexpected(HandError::IllegalKong);
        }
        _moves.push_back({.kind = MoveKind::UpgradeKong, .seat = seat, .tile = tile, .meld = static_cast<int>(pung - hand.melds.data())});
        if (!OpenWindow(Phase::RobKong, seat, tile)) {
            CompleteKong(seat, tile);
        }
        return {};
    }

    HandStatus HandModel::Claim(Seat seat, ClaimKind kind, Tile chowFirst) {
        if (_phase != Phase::Claim && _phase != Phase::RobKong) {
            return std::unexpected(HandError::WrongPhase);
        }
        if (!IsSeat(seat) || seat == _current) {
            return std::unexpected(HandError::NotYourTurn);
        }
        // Whether the seat is in the window comes first, so that a seat outside it gets the same answer for every claim.
        if (_claimStatus[seat] == ClaimStatus::NotInWindow) {
            return std::unexpected(HandError::NotYourTurn);
        }
        if (_claimStatus[seat] != ClaimStatus::Waiting) {
            return std::unexpected(HandError::AlreadyDecided);
        }
        const PlayerHand& hand = _hands[seat];
        const Tile tile = _pendingTile;
        if (kind == ClaimKind::Mahjong) {
            if (!hand.callingDeclared) {
                return std::unexpected(HandError::NotCalling);
            }
            if (!tile.IsPlayable() || !WinsWith(hand, tile)) {
                return std::unexpected(HandError::CannotWin);
            }
        }
        ClaimOption claim{kind, kind == ClaimKind::Chow ? chowFirst : Tile{}};
        const eastl::vector<ClaimOption> options = ClaimOptionsFor(hand, seat, _current, tile, _phase == Phase::RobKong);
        if (eastl::find(options.begin(), options.end(), claim) == options.end()) {
            return std::unexpected(HandError::IllegalClaim);
        }
        _claims[seat] = claim;
        _claimStatus[seat] = ClaimStatus::Claimed;
        ResolveClaims();
        return {};
    }

    HandStatus HandModel::Pass(Seat seat) {
        if (_phase != Phase::Claim && _phase != Phase::RobKong) {
            return std::unexpected(HandError::WrongPhase);
        }
        if (!IsSeat(seat) || _claimStatus[seat] == ClaimStatus::NotInWindow) {
            return std::unexpected(HandError::NotYourTurn);
        }
        if (_claimStatus[seat] != ClaimStatus::Waiting) {
            return std::unexpected(HandError::AlreadyDecided);
        }
        _claimStatus[seat] = ClaimStatus::Passed;
        ResolveClaims();
        return {};
    }

    eastl::vector<Seat> HandModel::PendingClaimSeats() const {
        eastl::vector<Seat> seats;
        if (_phase == Phase::Claim || _phase == Phase::RobKong) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (_claimStatus[seat] == ClaimStatus::Waiting) {
                    seats.push_back(seat);
                }
            }
        }
        return seats;
    }

    eastl::vector<ClaimOption> HandModel::LegalClaims(Seat seat) const {
        if ((_phase != Phase::Claim && _phase != Phase::RobKong) || !IsSeat(seat) || _claimStatus[seat] != ClaimStatus::Waiting) {
            return {};
        }
        return ClaimOptionsFor(_hands[seat], seat, _current, _pendingTile, _phase == Phase::RobKong);
    }

    eastl::vector<HandAction> HandModel::LegalActions(Seat seat) const {
        eastl::vector<HandAction> actions;
        if (!IsSeat(seat)) {
            return actions;
        }
        const PlayerHand& hand = _hands[seat];
        if (_phase == Phase::Discard && seat == _current) {
            TileCounts counts = CountTiles(hand.concealed);
            const TileCounts meldTiles = CountMeldTiles(hand.melds);
            const int meldCount = static_cast<int>(hand.melds.size());
            for (size_t i = 0; i < hand.concealed.size(); ++i) {
                const Tile tile = hand.concealed[i];
                if (i > 0 && hand.concealed[i - 1] == tile) {
                    continue;
                }
                actions.push_back(HandAction::MakeDiscard(tile));
                if (!hand.callingDeclared && IsWaitingWithout(counts, tile, meldCount, meldTiles)) {
                    actions.push_back(HandAction::MakeDiscard(tile, true));
                }
            }
            if (hand.callingDeclared && _drawnTile.IsValid() && IsWinningHand(hand)) {
                actions.push_back(HandAction::MakeDeclareWin());
            }
            for (size_t i = 0; i < hand.concealed.size(); ++i) {
                const Tile tile = hand.concealed[i];
                if (i > 0 && hand.concealed[i - 1] == tile) {
                    continue;
                }
                if (hand.CountConcealed(tile) == kCopiesPerTile) {
                    actions.push_back(HandAction::MakeConcealedKong(tile));
                }
                if (FindPung(hand, tile) != nullptr && _drawnTile == tile) {
                    actions.push_back(HandAction::MakeUpgradeKong(tile));
                }
            }
        } else if (_claimStatus[seat] == ClaimStatus::Waiting) {
            for (const ClaimOption& option : LegalClaims(seat)) {
                actions.push_back(HandAction::MakeClaim(option.kind, option.chowFirst));
            }
            actions.push_back(HandAction::MakePass());
        }
        return actions;
    }

    HandStatus HandModel::Apply(Seat seat, const HandAction& action) {
        if (!IsSeat(seat)) {
            return std::unexpected(HandError::NotYourTurn);
        }
        switch (action.kind) {
            case ActionKind::Discard:
                return Discard(seat, action.tile, action.declareCalling);
            case ActionKind::DeclareWin:
                return DeclareWin(seat);
            case ActionKind::ConcealedKong:
                return DeclareConcealedKong(seat, action.tile);
            case ActionKind::UpgradeKong:
                return UpgradeKong(seat, action.tile);
            case ActionKind::Claim:
                return Claim(seat, action.claim, action.chowFirst);
            case ActionKind::Pass:
                return Pass(seat);
        }
        return std::unexpected(HandError::WrongPhase);
    }

    void HandModel::StartTurn(Seat seat) {
        Receive(seat, _wall.Draw(), false);
    }

    void HandModel::Receive(Seat seat, std::optional<Tile> first, bool loose) {
        std::optional<Tile> tile = first;
        while (tile.has_value() && _settings.flowers && tile->IsBonus()) {
            _hands[seat].bonus.push_back(*tile);
            tile = _wall.DrawLoose();
            loose = true;
        }
        if (!tile.has_value()) {
            FinishDrawn();
            return;
        }
        _hands[seat].AddConcealed(*tile);
        _current = seat;
        _drawnTile = *tile;
        _drawnLoose = loose;
        _phase = Phase::Discard;
    }

    void HandModel::AfterDiscard(Seat seat, Tile tile) {
        if (!OpenWindow(Phase::Claim, seat, tile)) {
            StartTurn(NextSeat(seat));
        }
    }

    bool HandModel::OpenWindow(Phase phase, Seat from, Tile tile) {
        bool open = false;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (!ClaimOptionsFor(_hands[seat], seat, from, tile, phase == Phase::RobKong).empty()) {
                _claimStatus[seat] = ClaimStatus::Waiting;
                open = true;
            }
        }
        if (open) {
            _phase = phase;
            _current = from;
            _pendingTile = tile;
        }
        return open;
    }

    Seat HandModel::BestClaimant() const {
        // Seats are looked at in counter-clockwise order from `_current`, so on a tie of priority (two mahjong claims) the farther
        // seat is the one that comes last.
        Seat winner = kNoSeat;
        for (int step = 1; step < kSeatCount; ++step) {
            const Seat seat = (_current + step) % kSeatCount;
            if (_claimStatus[seat] != ClaimStatus::Claimed) {
                continue;
            }
            if (winner == kNoSeat || ClaimPriority(_claims[seat].kind) >= ClaimPriority(_claims[winner].kind)) {
                winner = seat;
            }
        }
        return winner;
    }

    void HandModel::ResolveClaims() {
        for (const ClaimStatus status : _claimStatus) {
            if (status == ClaimStatus::Waiting) {
                return;
            }
        }
        const Seat from = _current;
        const Seat winner = BestClaimant();
        const ClaimOption claim = winner == kNoSeat ? ClaimOption{} : _claims[winner];
        const bool robbing = _phase == Phase::RobKong;
        const Tile tile = _pendingTile;
        _claimStatus = {};
        _claims = {};
        _pendingTile = Tile{};
        if (robbing) {
            ResolveRobbery(from, winner, tile);
            return;
        }
        if (winner == kNoSeat) {
            StartTurn(NextSeat(from));
            return;
        }

        _discards.pop_back();  // the pending tile is the last discard
        if (claim.kind == ClaimKind::Mahjong) {
            _hands[winner].AddConcealed(tile);
            FinishWin(winner, tile, WinSource::Discard, from, false);
            return;
        }
        ExposeClaim(winner, claim, from, tile);
        _moves.push_back({.kind = MoveKindOf(claim.kind), .seat = winner, .tile = tile, .from = from, .meld = static_cast<int>(_hands[winner].melds.size()) - 1});
        if (claim.kind == ClaimKind::Kong) {
            Receive(winner, _wall.DrawLoose(), true);
            return;
        }
        _current = winner;
        _drawnTile = Tile{};
        _drawnLoose = false;
        _phase = Phase::Discard;
    }

    void HandModel::ResolveRobbery(Seat owner, Seat robber, Tile tile) {
        if (robber == kNoSeat) {
            CompleteKong(owner, tile);
            return;
        }
        // The tile is taken from the kong: the owner keeps its pung and loses the fourth tile.
        _hands[owner].RemoveConcealed(tile);
        _hands[robber].AddConcealed(tile);
        FinishWin(robber, tile, WinSource::RobbedKong, owner, false);
    }

    void HandModel::CompleteKong(Seat seat, Tile tile) {
        PlayerHand& hand = _hands[seat];
        // The pung is there, and so is the fourth tile: a valid `_drawnTile` lies in the hand (see the declaration). So a pung that
        // is missing is skipped silently and the result of `RemoveConcealed` is not looked at.
        if (Meld* pung = FindPung(hand, tile)) {
            pung->kind = MeldKind::Kong;
        }
        hand.RemoveConcealed(tile);
        Receive(seat, _wall.DrawLoose(), true);
    }

    void HandModel::ExposeClaim(Seat claimer, const ClaimOption& claim, Seat from, Tile tile) {
        PlayerHand& hand = _hands[claimer];
        Meld meld;
        meld.concealed = false;
        meld.from = from;
        meld.fromDiscard = true;
        meld.claimed = tile;
        if (claim.kind == ClaimKind::Chow) {
            meld.kind = MeldKind::Chow;
            meld.first = claim.chowFirst;
            for (const Tile member : ChowTiles(claim.chowFirst)) {
                if (member != tile) {
                    hand.RemoveConcealed(member);
                }
            }
        } else {
            meld.kind = claim.kind == ClaimKind::Kong ? MeldKind::Kong : MeldKind::Pung;
            meld.first = tile;
            const int fromHand = claim.kind == ClaimKind::Kong ? kKongTilesFromHand : kPungTilesFromHand;
            for (int copy = 0; copy < fromHand; ++copy) {
                hand.RemoveConcealed(tile);
            }
        }
        hand.melds.push_back(meld);
    }

    void HandModel::FinishDrawn() {
        _result = HandResult{};
        _phase = Phase::Finished;
    }

    void HandModel::FinishWin(Seat winner, Tile tile, WinSource source, Seat from, bool loose) {
        HandResult result;
        result.kind = ResultKind::Win;
        result.winner = winner;
        result.winningTile = tile;
        result.source = source;
        result.from = from;
        result.loose = loose;
        result.lastLiveTile = source == WinSource::SelfDraw && !loose && _wall.LiveCount() == 0;
        _result = result;
        _phase = Phase::Finished;
    }

}  // namespace BECore::Mahjong
