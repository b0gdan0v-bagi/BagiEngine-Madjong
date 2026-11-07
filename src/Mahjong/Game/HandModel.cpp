#include "HandModel.h"

#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/HandShape.h>

namespace BECore::Mahjong {

    namespace {

        bool IsSeat(Seat seat) noexcept {
            return seat >= 0 && seat < kSeatCount;
        }

        /// Whether `hand` is waiting without its concealed tile number `index`.
        bool IsWaitingWithout(const PlayerHand& hand, size_t index) {
            PlayerHand rest = hand;
            rest.concealed.erase(rest.concealed.begin() + index);
            return !WinningTiles(rest).empty();
        }

        /// Whether `hand` is a winning hand with `tile` added (the declaration is not looked at here).
        bool WinsWith(const PlayerHand& hand, Tile tile) {
            TileCounts counts = CountTiles(hand.concealed);
            ++counts[tile.Id()];
            return IsWinningShape(counts, static_cast<int>(hand.melds.size()));
        }

        /// The sequences of `tile` that `hand` can complete from its concealed tiles, as the lowest tile of each.
        eastl::vector<Tile> ChowStarts(const PlayerHand& hand, Tile tile) {
            eastl::vector<Tile> starts;
            if (!tile.IsSuit()) {
                return starts;
            }
            const int lastStart = kRanksPerSuit - kTilesPerSet + 1;
            const int firstRank = eastl::max(1, tile.Rank() - (kTilesPerSet - 1));
            const int lastRank = eastl::min(lastStart, tile.Rank());
            for (int rank = firstRank; rank <= lastRank; ++rank) {
                const Tile first{static_cast<uint8_t>(tile.Id() - tile.Rank() + rank)};
                bool complete = true;
                for (int step = 0; step < kTilesPerSet; ++step) {
                    const Tile member{static_cast<uint8_t>(first.Id() + step)};
                    complete = complete && (member == tile || hand.CountConcealed(member) > 0);
                }
                if (complete) {
                    starts.push_back(first);
                }
            }
            return starts;
        }

        /// Every claim `seat` could make on `tile` discarded by `from`. A mahjong claim needs the calling-hand declaration.
        eastl::vector<ClaimOption> ClaimOptionsFor(const PlayerHand& hand, Seat seat, Seat from, Tile tile) {
            eastl::vector<ClaimOption> options;
            if (seat == from || !tile.IsPlayable()) {
                return options;
            }
            if (hand.callingDeclared && WinsWith(hand, tile)) {
                options.push_back({ClaimKind::Mahjong, Tile{}});
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

        /// What beats what in the claim window.
        int ClaimPriority(ClaimKind kind) noexcept {
            switch (kind) {
                case ClaimKind::Mahjong:
                    return 3;
                case ClaimKind::Pung:
                case ClaimKind::Kong:
                    return 2;
                case ClaimKind::Chow:
                    break;
            }
            return 1;
        }

        /// Takes one copy of `tile` out of the concealed tiles.
        void RemoveConcealed(PlayerHand& hand, Tile tile) {
            const auto found = eastl::find(hand.concealed.begin(), hand.concealed.end(), tile);
            if (found != hand.concealed.end()) {
                hand.concealed.erase(found);
            }
        }

    }  // namespace

    void HandModel::Begin(const MahjongSettings& settings, Wall wall, const SeatWinds& seatWinds, Wind roundWind) {
        *this = HandModel{};
        _settings = settings;
        _seatWinds = seatWinds;
        _roundWind = roundWind;
        _wall = eastl::move(wall);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (_seatWinds[seat] == Wind::East) {
                _east = seat;
                break;
            }
        }
        _hands = DealHands(_wall, _east, _settings.flowers);
        StartTurn(_east);
    }

    HandStatus HandModel::Discard(Seat seat, Tile tile, bool declareCalling) {
        if (_phase != Phase::Discard) {
            return std::unexpected(HandError::WrongPhase);
        }
        if (seat != _current || !IsSeat(seat)) {
            return std::unexpected(HandError::NotYourTurn);
        }
        PlayerHand& hand = _hands[seat];
        const auto found = eastl::find(hand.concealed.begin(), hand.concealed.end(), tile);
        if (found == hand.concealed.end()) {
            return std::unexpected(HandError::TileNotInHand);
        }
        if (declareCalling) {
            if (hand.callingDeclared) {
                return std::unexpected(HandError::AlreadyCalling);
            }
            if (!IsWaitingWithout(hand, static_cast<size_t>(found - hand.concealed.begin()))) {
                return std::unexpected(HandError::NotWaiting);
            }
        }

        hand.concealed.erase(found);
        if (declareCalling) {
            hand.callingDeclared = true;
            hand.callingAtDiscardCount = hand.discardCount;
        }
        ++hand.discardCount;
        _discards.push_back({tile, seat});
        _drawnTile = Tile{};
        _drawnLoose = false;
        AfterDiscard(seat, tile);
        return {};
    }

    HandStatus HandModel::DeclareWin(Seat seat) {
        if (_phase != Phase::Discard) {
            return std::unexpected(HandError::WrongPhase);
        }
        if (seat != _current || !IsSeat(seat)) {
            return std::unexpected(HandError::NotYourTurn);
        }
        const PlayerHand& hand = _hands[seat];
        if (!hand.callingDeclared) {
            return std::unexpected(HandError::NotCalling);
        }
        if (!_drawnTile.IsValid() || !IsWinningShape(CountTiles(hand.concealed), static_cast<int>(hand.melds.size()))) {
            return std::unexpected(HandError::CannotWin);
        }
        FinishWin(seat, _drawnTile, WinSource::SelfDraw, seat, _drawnLoose);
        return {};
    }

    HandStatus HandModel::Claim(Seat seat, ClaimKind kind, Tile chowFirst) {
        if (_phase != Phase::Claim) {
            return std::unexpected(HandError::WrongPhase);
        }
        if (!IsSeat(seat) || seat == _current) {
            return std::unexpected(HandError::NotYourTurn);
        }
        if (_claimStatus[seat] == ClaimStatus::Passed || _claimStatus[seat] == ClaimStatus::Claimed) {
            return std::unexpected(HandError::AlreadyDecided);
        }
        const PlayerHand& hand = _hands[seat];
        const Tile tile = _discards.back().tile;
        if (kind == ClaimKind::Mahjong) {
            if (!hand.callingDeclared) {
                return std::unexpected(HandError::NotCalling);
            }
            if (!tile.IsPlayable() || !WinsWith(hand, tile)) {
                return std::unexpected(HandError::CannotWin);
            }
        }
        if (_claimStatus[seat] != ClaimStatus::Waiting) {
            return std::unexpected(HandError::NotYourTurn);
        }
        ClaimOption claim{kind, kind == ClaimKind::Chow ? chowFirst : Tile{}};
        const eastl::vector<ClaimOption> options = ClaimOptionsFor(hand, seat, _current, tile);
        if (eastl::find(options.begin(), options.end(), claim) == options.end()) {
            return std::unexpected(HandError::IllegalClaim);
        }
        _claims[seat] = claim;
        _claimStatus[seat] = ClaimStatus::Claimed;
        ResolveClaims();
        return {};
    }

    HandStatus HandModel::Pass(Seat seat) {
        if (_phase != Phase::Claim) {
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
        if (_phase == Phase::Claim) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (_claimStatus[seat] == ClaimStatus::Waiting) {
                    seats.push_back(seat);
                }
            }
        }
        return seats;
    }

    eastl::vector<ClaimOption> HandModel::LegalClaims(Seat seat) const {
        if (_phase != Phase::Claim || !IsSeat(seat) || _claimStatus[seat] != ClaimStatus::Waiting) {
            return {};
        }
        return ClaimOptionsFor(_hands[seat], seat, _current, _discards.back().tile);
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
        if (!OpenClaimWindow(seat, tile)) {
            StartTurn(NextSeat(seat));
        }
    }

    bool HandModel::OpenClaimWindow(Seat from, Tile tile) {
        bool open = false;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (!ClaimOptionsFor(_hands[seat], seat, from, tile).empty()) {
                _claimStatus[seat] = ClaimStatus::Waiting;
                open = true;
            }
        }
        if (open) {
            _phase = Phase::Claim;
            _current = from;
        }
        return open;
    }

    void HandModel::ResolveClaims() {
        for (const ClaimStatus status : _claimStatus) {
            if (status == ClaimStatus::Waiting) {
                return;
            }
        }
        // Seats are looked at in counter-clockwise order from the discarder, so on a tie of priority (two mahjong claims) the farther
        // seat is the one that comes last.
        const Seat from = _current;
        Seat winner = kNoSeat;
        for (int step = 1; step < kSeatCount; ++step) {
            const Seat seat = (from + step) % kSeatCount;
            if (_claimStatus[seat] != ClaimStatus::Claimed) {
                continue;
            }
            if (winner == kNoSeat || ClaimPriority(_claims[seat].kind) >= ClaimPriority(_claims[winner].kind)) {
                winner = seat;
            }
        }
        const ClaimOption claim = winner == kNoSeat ? ClaimOption{} : _claims[winner];
        _claimStatus = {};
        _claims = {};
        if (winner == kNoSeat) {
            StartTurn(NextSeat(from));
            return;
        }

        const Tile tile = _discards.back().tile;
        _discards.pop_back();
        if (claim.kind == ClaimKind::Mahjong) {
            _hands[winner].AddConcealed(tile);
            FinishWin(winner, tile, WinSource::Discard, from, false);
            return;
        }
        ExposeClaim(winner, claim, from, tile);
        if (claim.kind == ClaimKind::Kong) {
            Receive(winner, _wall.DrawLoose(), true);
            return;
        }
        _current = winner;
        _drawnTile = Tile{};
        _drawnLoose = false;
        _phase = Phase::Discard;
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
            for (int step = 0; step < kTilesPerSet; ++step) {
                const Tile member{static_cast<uint8_t>(claim.chowFirst.Id() + step)};
                if (member != tile) {
                    RemoveConcealed(hand, member);
                }
            }
        } else {
            meld.kind = claim.kind == ClaimKind::Kong ? MeldKind::Kong : MeldKind::Pung;
            meld.first = tile;
            const int fromHand = claim.kind == ClaimKind::Kong ? kKongTilesFromHand : kPungTilesFromHand;
            for (int copy = 0; copy < fromHand; ++copy) {
                RemoveConcealed(hand, tile);
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
        result.lastLiveTile = _wall.LiveCount() == 0;
        _result = result;
        _phase = Phase::Finished;
    }

}  // namespace BECore::Mahjong
