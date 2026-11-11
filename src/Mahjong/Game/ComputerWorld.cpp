#include "ComputerWorld.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/HandShape.h>

namespace BECore::Mahjong {

    namespace {

        /// The other seats' unseen copies, per kind, as signed numbers (the pile arithmetic goes below 0 on a view no hand makes).
        using KindNumbers = eastl::array<int, kKindCount>;

        /// What is drawn for one seat that is not the viewer: its hidden tiles and its melds (the open ones, then the concealed kongs).
        struct SeatSample {
            eastl::vector<Meld> melds;
            /// How many concealed tiles the seat has to be given from the pool.
            int hiddenCount = 0;
            eastl::vector<Tile> hidden;
        };

        /// The seat that holds East when `view.seat` has `view.seatWind`.
        Seat EastSeatOf(const TableView& view) noexcept {
            return (view.seat - static_cast<int>(view.seatWind) + kSeatCount) % kSeatCount;
        }

        /// The tiles a hand of this phase holds, melds counted three each (as `HandModel` counts): 13, one more for the seat that acts.
        int HeldTiles(const TableView& view, Seat seat) noexcept {
            const bool acts = (view.phase == Phase::Discard || view.phase == Phase::RobKong) && seat == view.currentSeat;
            return acts ? kDealtTiles + 1 : kDealtTiles;
        }

        /// Whether `melds` holds an open pung of `tile` (what an upgraded kong grows from).
        bool HasOpenPung(const eastl::vector<Meld>& melds, Tile tile) noexcept {
            for (const Meld& meld : melds) {
                if (meld.kind == MeldKind::Pung && !meld.concealed && meld.first == tile) {
                    return true;
                }
            }
            return false;
        }

        /// Takes `count` random tiles out of `pool[begin..)` and puts them at `pool[begin..begin + count)` (a partial Fisher-Yates; the
        /// pool stays a bag of the same tiles, so a rejected draw needs no undoing). `count` must not exceed what is left.
        void DrawToFront(eastl::vector<Tile>& pool, size_t begin, size_t count, MahjongRng& rng) {
            for (size_t i = 0; i < count; ++i) {
                const size_t left = pool.size() - begin - i;
                const size_t pick = begin + i + rng.Below(static_cast<uint32_t>(left));
                eastl::swap(pool[begin + i], pool[pick]);
            }
        }

        /// Whether the hidden tiles `pool[begin..begin + count)` and the melds make a waiting hand of 13 tiles.
        bool DrawnHandWaits(const eastl::vector<Tile>& pool, size_t begin, const SeatSample& sample, const TileCounts& meldTiles) {
            TileCounts counts{};
            for (size_t i = 0; i < static_cast<size_t>(sample.hiddenCount); ++i) {
                ++counts[pool[begin + i].Id()];
            }
            return IsWaiting(counts, static_cast<int>(sample.melds.size()), meldTiles);
        }

        /// The tile a self-draw win was made with: random among the tiles whose removal leaves a hand that waits for exactly it.
        Tile PickWinningDraw(const TableView& view, MahjongRng& rng) {
            TileCounts counts = CountTiles(view.own.concealed);
            const TileCounts meldTiles = CountMeldTiles(view.own.melds);
            const int meldCount = static_cast<int>(view.own.melds.size());
            eastl::vector<Tile> candidates;
            for (const Tile tile : view.own.concealed) {
                if (!candidates.empty() && candidates.back() == tile) {
                    continue;
                }
                --counts[tile.Id()];
                for (const Tile wait : WinningTiles(counts, meldCount, meldTiles)) {
                    if (wait == tile) {
                        candidates.push_back(tile);
                        break;
                    }
                }
                ++counts[tile.Id()];
            }
            if (candidates.empty()) {
                return view.own.concealed.back();
            }
            return candidates[rng.Below(static_cast<uint32_t>(candidates.size()))];
        }

    }  // namespace

    std::optional<HandModel> SampleWorld(const TableView& view, MahjongRng& rng) {
        BE_PROFILE_SCOPE("Search::SampleWorld");
        const bool window = view.phase == Phase::Claim || view.phase == Phase::RobKong;
        if (!IsSeat(view.seat) || view.legal.empty() || view.phase == Phase::Finished || view.own.concealed.empty()) {
            return std::nullopt;
        }
        if (window ? (!IsSeat(view.currentSeat) || view.currentSeat == view.seat || !view.pendingTile.IsPlayable()) : view.currentSeat != view.seat) {
            return std::nullopt;
        }
        if (view.phase == Phase::RobKong && !HasOpenPung(view.seats[view.currentSeat].openMelds, view.pendingTile)) {
            return std::nullopt;
        }
        const Seat east = EastSeatOf(view);

        // The pool: the copies nobody here can see.
        KindNumbers unseen{};
        for (uint8_t id = 0; id < kKindCount; ++id) {
            unseen[id] = UnseenCopies(view, Tile{id});
        }

        // The other seats: the melds (the concealed kongs are kinds of the pool with all four copies) and the number of hidden tiles.
        eastl::array<SeatSample, kSeatCount> samples;
        int hiddenTotal = 0;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (seat == view.seat) {
                continue;
            }
            SeatSample& sample = samples[seat];
            const PublicSeat& info = view.seats[seat];
            sample.melds = info.openMelds;
            for (int kong = 0; kong < info.concealedKongs; ++kong) {
                eastl::vector<Tile> kinds;
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    if (unseen[id] == kCopiesPerTile) {
                        kinds.push_back(Tile{id});
                    }
                }
                if (kinds.empty()) {
                    return std::nullopt;
                }
                const Tile kind = kinds[rng.Below(static_cast<uint32_t>(kinds.size()))];
                unseen[kind.Id()] = 0;
                Meld meld;
                meld.kind = MeldKind::Kong;
                meld.concealed = true;
                meld.first = kind;
                sample.melds.push_back(meld);
            }
            // The owner of the kong that is being robbed holds the pending tile besides; it is visible, so it is not drawn.
            const bool robbed = view.phase == Phase::RobKong && seat == view.currentSeat;
            sample.hiddenCount = HeldTiles(view, seat) - kTilesPerSet * static_cast<int>(sample.melds.size()) - (robbed ? 1 : 0);
            if (sample.hiddenCount < 0) {
                return std::nullopt;
            }
            hiddenTotal += sample.hiddenCount;
        }

        eastl::vector<Tile> pool;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            for (int copy = 0; copy < unseen[id]; ++copy) {
                pool.push_back(Tile{id});
            }
        }
        if (pool.size() < static_cast<size_t>(hiddenTotal)) {
            return std::nullopt;
        }

        // The bonus tiles: whether the set has them, and the ones nobody has laid open.
        eastl::array<bool, kTileIdCount> bonusOpen{};
        int bonusOpenCount = 0;
        for (const PublicSeat& info : view.seats) {
            for (const Tile tile : info.bonus) {
                if (!tile.IsBonus() || bonusOpen[tile.Id()]) {
                    return std::nullopt;
                }
                bonusOpen[tile.Id()] = true;
                ++bonusOpenCount;
            }
        }
        const int wallPlayable = static_cast<int>(pool.size()) - hiddenTotal;
        const int wallWithoutBonus = wallPlayable - view.liveCount - static_cast<int>(kDeadWallTiles);
        const auto fitsReserve = [](int loose) { return loose >= 0 && loose <= static_cast<int>(kLooseTiles); };
        const bool flowers = bonusOpenCount > 0 || !fitsReserve(wallWithoutBonus);
        const int unseenBonus = flowers ? static_cast<int>(kBonusCount) - bonusOpenCount : 0;
        const int looseCount = wallWithoutBonus + unseenBonus;
        if (!fitsReserve(looseCount)) {
            return std::nullopt;
        }

        // The hidden hands, seat by seat; a declared seat draws again until its 13 tiles wait.
        size_t begin = 0;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (seat == view.seat) {
                continue;
            }
            SeatSample& sample = samples[seat];
            const size_t count = static_cast<size_t>(sample.hiddenCount);
            const TileCounts meldTiles = CountMeldTiles(sample.melds);
            DrawToFront(pool, begin, count, rng);
            if (view.seats[seat].callingDeclared) {
                for (int attempt = 1; attempt < kWorldDeclaredTries && !DrawnHandWaits(pool, begin, sample, meldTiles); ++attempt) {
                    DrawToFront(pool, begin, count, rng);
                }
            }
            sample.hidden.assign(pool.begin() + begin, pool.begin() + begin + count);
            begin += count;
        }

        // The wall: what is left of the pool and the unseen bonus tiles, in random order.
        eastl::vector<Tile> wall(pool.begin() + begin, pool.end());
        for (uint8_t id = kFirstFlowerId; id < kTileIdCount; ++id) {
            if (flowers && !bonusOpen[id]) {
                wall.push_back(Tile{id});
            }
        }
        rng.Shuffle(wall);

        // The pile: the kinds are what is visible and neither held by the seat nor in an open meld; the number of tiles per seat is its
        // discards less the ones claimed from it.
        KindNumbers pileCounts{};
        const TileCounts ownTiles = CountTiles(view.own.concealed);
        const TileCounts ownMeldTiles = CountMeldTiles(view.own.melds);
        eastl::array<TileCounts, kSeatCount> openMeldTiles{};
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            openMeldTiles[seat] = CountMeldTiles(view.seats[seat].openMelds);
        }
        eastl::array<int, kSeatCount> quota{};
        int pileSize = 0;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            int count = view.visible[id] - ownTiles[id] - ownMeldTiles[id];
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                count -= seat == view.seat ? 0 : openMeldTiles[seat][id];
            }
            if (view.phase == Phase::RobKong && view.pendingTile.Id() == id) {
                --count;
            }
            if (count < 0) {
                return std::nullopt;
            }
            pileCounts[id] = count;
            pileSize += count;
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            quota[seat] += view.seats[seat].discardCount;
            for (const Meld& meld : view.seats[seat].openMelds) {
                if (meld.fromDiscard && IsSeat(meld.from)) {
                    --quota[meld.from];
                }
            }
        }
        int quotaTotal = 0;
        for (const int share : quota) {
            if (share < 0) {
                return std::nullopt;
            }
            quotaTotal += share;
        }
        if (quotaTotal != pileSize) {
            return std::nullopt;
        }
        if (view.phase == Phase::Claim) {
            // The pending discard is the last one of the pile, by the discarder; it is placed after the others.
            if (pileCounts[view.pendingTile.Id()] == 0 || quota[view.currentSeat] == 0) {
                return std::nullopt;
            }
            --pileCounts[view.pendingTile.Id()];
            --quota[view.currentSeat];
        }
        eastl::vector<Tile> pileTiles;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            for (int copy = 0; copy < pileCounts[id]; ++copy) {
                pileTiles.push_back(Tile{id});
            }
        }
        rng.Shuffle(pileTiles);

        HandState state;
        state.settings.flowers = flowers;
        state.seatWinds = MakeSeatWinds(east);
        state.roundWind = view.roundWind;
        state.east = east;
        const size_t liveCount = static_cast<size_t>(view.liveCount);
        state.loose.assign(wall.begin(), wall.begin() + looseCount);
        state.live.assign(wall.begin() + looseCount, wall.begin() + looseCount + liveCount);
        state.dead.assign(wall.begin() + looseCount + liveCount, wall.end());

        size_t nextPile = 0;
        for (bool placed = true; placed;) {
            placed = false;
            for (int step = 0; step < kSeatCount; ++step) {
                const Seat seat = (east + step) % kSeatCount;
                if (quota[seat] > 0) {
                    --quota[seat];
                    state.discards.push_back({pileTiles[nextPile++], seat});
                    placed = true;
                }
            }
        }
        if (view.phase == Phase::Claim) {
            state.discards.push_back({view.pendingTile, view.currentSeat});
        }

        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            PlayerHand& hand = state.hands[seat];
            if (seat == view.seat) {
                hand = view.own;
                continue;
            }
            const PublicSeat& info = view.seats[seat];
            hand.concealed = samples[seat].hidden;
            if (view.phase == Phase::RobKong && seat == view.currentSeat) {
                hand.concealed.push_back(view.pendingTile);
            }
            eastl::sort(hand.concealed.begin(), hand.concealed.end());
            hand.melds = samples[seat].melds;
            hand.bonus = info.bonus;
            hand.callingDeclared = info.callingDeclared;
            hand.callingAtDiscardCount = info.callingDeclared && info.discardCount >= 2 ? kWorldLateDeclarationDiscards : 0;
            hand.discardCount = info.discardCount;
        }

        state.phase = view.phase;
        state.current = view.currentSeat;
        state.pendingTile = window ? view.pendingTile : Tile{};
        if (view.phase == Phase::RobKong) {
            state.drawnTile = view.pendingTile;
        } else if (view.phase == Phase::Discard) {
            // The view does not say which tile was drawn; what the legal actions need of it is all that is set.
            if (const HandAction* upgrade = FindAction(view, ActionKind::UpgradeKong)) {
                state.drawnTile = upgrade->tile;
            } else if (FindAction(view, ActionKind::DeclareWin) != nullptr) {
                state.drawnTile = PickWinningDraw(view, rng);
            }
        }

        if (!window) {
            std::expected<HandModel, eastl::string> model = HandModel::Restore(eastl::move(state));
            if (!model || model->LegalActions(view.seat) != view.legal) {
                return std::nullopt;
            }
            return eastl::move(*model);
        }

        // The window: the seat of the view is in it and decides first; which of the others could claim is fixed by their hands, and
        // `Restore` refuses a wrong guess, so the guesses are tried from "nobody else" up.
        eastl::vector<Seat> others;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (seat != view.seat && seat != view.currentSeat) {
                others.push_back(seat);
            }
        }
        const unsigned guesses = 1U << others.size();
        for (unsigned mask = 0; mask < guesses; ++mask) {
            HandState trial = state;
            trial.claimStatus[view.seat] = ClaimStatus::Waiting;
            for (size_t i = 0; i < others.size(); ++i) {
                trial.claimStatus[others[i]] = (mask >> i) & 1U ? ClaimStatus::Waiting : ClaimStatus::NotInWindow;
            }
            std::expected<HandModel, eastl::string> model = HandModel::Restore(eastl::move(trial));
            if (model) {
                if (model->LegalActions(view.seat) != view.legal) {
                    return std::nullopt;
                }
                return eastl::move(*model);
            }
        }
        return std::nullopt;
    }

}  // namespace BECore::Mahjong
