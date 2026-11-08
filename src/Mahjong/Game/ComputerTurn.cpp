#include "ComputerTurn.h"

namespace BECore::Mahjong {

    namespace {

        /// What a discard that does not leave a waiting hand scores in `UnseenWinningCopies`.
        constexpr int kNotWaiting = -1;

        /// A discard to pick from: the tile, how good it is (higher is better) and `KeepValue` (lower is discarded first).
        struct Candidate {
            Tile tile;
            int score = 0;
            int keep = 0;
        };

        int MeldCountOf(const TableView& view) noexcept {
            return static_cast<int>(view.own.melds.size());
        }

        bool IsValueTile(Tile tile, Wind seatWind, Wind roundWind) noexcept {
            if (tile.IsDragon()) {
                return true;
            }
            const int wind = tile.Id() - kFirstWindId;
            return tile.IsWind() && (wind == static_cast<int>(seatWind) || wind == static_cast<int>(roundWind));
        }

        const HandAction* FindAction(const TableView& view, ActionKind kind) noexcept {
            for (const HandAction& action : view.legal) {
                if (action.kind == kind) {
                    return &action;
                }
            }
            return nullptr;
        }

        /// One of `ties` by `rng` (the first when there is only one: no number is drawn).
        template <typename T>
        const T& PickOne(const eastl::vector<T>& ties, MahjongRng& rng) {
            return ties.size() == 1 ? ties.front() : ties[rng.Below(static_cast<uint32_t>(ties.size()))];
        }

        /// Whether `a` is a better discard than `b`: the higher score, then the lower keep value.
        bool IsBetter(const Candidate& a, const Candidate& b) noexcept {
            return a.score != b.score ? a.score > b.score : a.keep < b.keep;
        }

        /// The best of non-empty `candidates` by `IsBetter`; equal ones go to `rng`.
        Tile PickBest(const eastl::vector<Candidate>& candidates, MahjongRng& rng) {
            size_t best = 0;
            for (size_t i = 1; i < candidates.size(); ++i) {
                best = IsBetter(candidates[i], candidates[best]) ? i : best;
            }
            eastl::vector<Tile> ties;
            for (const Candidate& candidate : candidates) {
                if (!IsBetter(candidates[best], candidate)) {
                    ties.push_back(candidate.tile);
                }
            }
            return PickOne(ties, rng);
        }

        /// The unseen copies of the tiles that complete `counts` (13 - 3 * `meldCount` concealed tiles); `kNotWaiting` when none does.
        int UnseenWinningCopies(const TableView& view, const TileCounts& counts, int meldCount, const TileCounts& meldTiles) {
            const eastl::vector<Tile> winning = WinningTiles(counts, meldCount, meldTiles);
            if (winning.empty()) {
                return kNotWaiting;
            }
            int copies = 0;
            for (const Tile tile : winning) {
                copies += UnseenCopies(view, tile);
            }
            return copies;
        }

        /// The listed discards (with the declaration, or without it) that leave a waiting hand, scored by the unseen winning copies.
        eastl::vector<Candidate> WaitingDiscards(const TableView& view, bool declaring) {
            TileCounts counts = CountTiles(view.own.concealed);
            const TileCounts meldTiles = CountMeldTiles(view.own.melds);
            eastl::vector<Candidate> candidates;
            for (const HandAction& action : view.legal) {
                if (action.kind != ActionKind::Discard || action.declareCalling != declaring) {
                    continue;
                }
                --counts[action.tile.Id()];
                const int copies = UnseenWinningCopies(view, counts, MeldCountOf(view), meldTiles);
                ++counts[action.tile.Id()];
                if (copies != kNotWaiting) {
                    candidates.push_back({action.tile, copies, KeepValue(action.tile, view.seatWind, view.roundWind)});
                }
            }
            return candidates;
        }

        /// Step 2 and 4: the discard that declares the calling hand (`declaring`), or keeps the declared hand waiting.
        std::optional<HandAction> ChooseWaitingDiscard(const TableView& view, MahjongRng& rng, bool declaring) {
            const eastl::vector<Candidate> candidates = WaitingDiscards(view, declaring);
            if (candidates.empty()) {
                return std::nullopt;
            }
            return HandAction::MakeDiscard(PickBest(candidates, rng), declaring);
        }

        /// Whether the hand without the kong of `action` is no worse than the hand as it is (`current`, its best discard).
        bool KongIsNoWorse(const TableView& view, const TileCounts& counts, const HandAction& action, const DiscardRating& current) {
            TileCounts rest = counts;
            int melds = MeldCountOf(view);
            if (action.kind == ActionKind::ConcealedKong) {
                rest[action.tile.Id()] = static_cast<uint8_t>(rest[action.tile.Id()] - kKongTilesFromHand - 1);
                ++melds;
            } else {
                --rest[action.tile.Id()];
            }
            const int shanten = Shanten(rest, melds);
            if (shanten != current.shanten) {
                return shanten < current.shanten;
            }
            return shanten != 0 || AcceptanceOf(view, rest, melds) >= current.acceptance;
        }

        /// Step 3: the first listed kong that does not hurt the hand.
        std::optional<HandAction> ChooseKong(const TableView& view) {
            const TileCounts counts = CountTiles(view.own.concealed);
            std::optional<DiscardRating> current;
            for (const HandAction& action : view.legal) {
                if (action.kind != ActionKind::ConcealedKong && action.kind != ActionKind::UpgradeKong) {
                    continue;
                }
                if (!current.has_value()) {
                    current = BestDiscardRating(counts, MeldCountOf(view), view);
                }
                if (KongIsNoWorse(view, counts, action, *current)) {
                    return action;
                }
            }
            return std::nullopt;
        }

        /// The kinds whose discard leaves the lowest shanten, and that shanten.
        struct LowestShanten {
            int shanten = 0;
            eastl::vector<uint8_t> kinds;
        };

        LowestShanten FindLowestShanten(const TileCounts& counts, int meldCount) {
            LowestShanten lowest;
            lowest.shanten = 2 * kSetsInHand + 1;
            TileCounts rest = counts;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (counts[id] == 0) {
                    continue;
                }
                --rest[id];
                const int shanten = Shanten(rest, meldCount);
                ++rest[id];
                if (shanten < lowest.shanten) {
                    lowest.shanten = shanten;
                    lowest.kinds.clear();
                }
                if (shanten == lowest.shanten) {
                    lowest.kinds.push_back(id);
                }
            }
            return lowest;
        }

        /// The discards of the lowest shanten and the highest acceptance, each with its `KeepValue`; `acceptance` is theirs.
        struct BestDiscards {
            int shanten = 0;
            int acceptance = 0;
            eastl::vector<Candidate> candidates;
        };

        BestDiscards FindBestDiscards(const TileCounts& counts, int meldCount, const TableView& view) {
            const LowestShanten lowest = FindLowestShanten(counts, meldCount);
            BestDiscards best;
            best.shanten = lowest.shanten;
            best.acceptance = -1;
            TileCounts rest = counts;
            for (const uint8_t id : lowest.kinds) {
                --rest[id];
                const int acceptance = AcceptanceOf(view, rest, meldCount);
                ++rest[id];
                if (acceptance > best.acceptance) {
                    best.acceptance = acceptance;
                    best.candidates.clear();
                }
                if (acceptance == best.acceptance) {
                    const Tile tile{id};
                    best.candidates.push_back({tile, acceptance, KeepValue(tile, view.seatWind, view.roundWind)});
                }
            }
            return best;
        }

        /// Step 5: the discard of the best rating that is not a declaration.
        HandAction ChooseDiscard(const TableView& view, MahjongRng& rng) {
            const DiscardRating best = BestDiscard(CountTiles(view.own.concealed), MeldCountOf(view), view, rng);
            return HandAction::MakeDiscard(best.tile);
        }

    }  // namespace

    int UnseenCopies(const TableView& view, Tile tile) noexcept {
        if (!tile.IsPlayable()) {
            return 0;
        }
        const int unseen = kCopiesPerTile - view.visible[tile.Id()];
        return unseen > 0 ? unseen : 0;
    }

    int KeepValue(Tile tile, Wind seatWind, Wind roundWind) noexcept {
        if (IsValueTile(tile, seatWind, roundWind)) {
            return kKeepValueTile;
        }
        if (tile.IsSimple()) {
            return kKeepSimple;
        }
        return tile.IsTerminal() ? kKeepTerminal : kKeepWorthless;
    }

    int AcceptanceOf(const TableView& view, const TileCounts& counts, int meldCount) {
        int copies = 0;
        for (const Tile tile : ImprovingTiles(counts, meldCount)) {
            copies += UnseenCopies(view, tile);
        }
        return copies;
    }

    DiscardRating BestDiscard(const TileCounts& counts, int meldCount, const TableView& view, MahjongRng& rng) {
        const BestDiscards best = FindBestDiscards(counts, meldCount, view);
        if (best.candidates.empty()) {
            return {};
        }
        // The score of a candidate is its acceptance, equal for all of them: the keep value decides, then the rng.
        return {PickBest(best.candidates, rng), best.shanten, best.acceptance};
    }

    DiscardRating BestDiscardRating(const TileCounts& counts, int meldCount, const TableView& view) {
        const BestDiscards best = FindBestDiscards(counts, meldCount, view);
        Tile first;
        int keep = kKeepValueTile + 1;
        for (const Candidate& candidate : best.candidates) {
            if (candidate.keep < keep) {
                keep = candidate.keep;
                first = candidate.tile;
            }
        }
        return {first, best.shanten, best.acceptance};
    }

    HandAction ChooseTurnAction(const TableView& view, MahjongRng& rng) {
        if (const HandAction* win = FindAction(view, ActionKind::DeclareWin)) {
            return *win;
        }
        if (const std::optional<HandAction> declaration = ChooseWaitingDiscard(view, rng, true)) {
            return *declaration;
        }
        if (const std::optional<HandAction> kong = ChooseKong(view)) {
            return *kong;
        }
        if (view.own.callingDeclared) {
            if (const std::optional<HandAction> keeping = ChooseWaitingDiscard(view, rng, false)) {
                return *keeping;
            }
        }
        return ChooseDiscard(view, rng);
    }

}  // namespace BECore::Mahjong
