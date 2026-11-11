#include "ComputerTurn.h"

#include <EASTL/sort.h>

namespace BECore::Mahjong {

    namespace {

        /// What a discard that does not leave a waiting hand scores in `UnseenWinningCopies`.
        constexpr int kNotWaiting = -1;
        /// Below every acceptance (an acceptance is a count of tiles), so the first discard examined is always better.
        constexpr int kNoAcceptance = -1;
        /// Above every shanten a hand can have (the worst is `2 * kSetsInHand`, with no melds and no two tiles that belong together).
        constexpr int kAboveWorstShanten = 2 * kSetsInHand + 1;
        /// The tiles a concealed kong takes out of the hand: all four copies (an upgraded kong takes the one drawn tile).
        constexpr int kConcealedKongTiles = kCopiesPerTile;

        /// A discard to pick from: the tile, how good it is (higher is better) and `KeepValue` (lower is discarded first).
        struct Candidate {
            Tile tile;
            int score = 0;
            int keep = 0;
        };

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

        /// Step 2 and 4: the discard that declares the calling hand (`declaring`), or keeps the declared hand waiting.
        std::optional<HandAction> ChooseWaitingDiscard(const TableView& view, MahjongRng& rng, bool declaring) {
            eastl::vector<Candidate> candidates;
            for (const WaitingDiscard& discard : WaitingDiscards(view, declaring)) {
                candidates.push_back({discard.tile, discard.copies, discard.keep});
            }
            if (candidates.empty()) {
                return std::nullopt;
            }
            return HandAction::MakeDiscard(PickBest(candidates, rng), declaring);
        }

        /// Whether the hand without the kong of `action` is no worse than the hand as it is (`current`, its best discard).
        bool KongIsNoWorse(const TableView& view, const TileCounts& counts, const HandAction& action, const DiscardRating& current) {
            const DiscardRating after = RateKong(view, counts, action);
            if (after.shanten != current.shanten) {
                return after.shanten < current.shanten;
            }
            return after.shanten != 0 || after.acceptance >= current.acceptance;
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

        /// The shanten of `counts` after the discard of kind `id` (`counts` is left as it was).
        int ShantenAfterDiscard(TileCounts& counts, int meldCount, uint8_t id) {
            --counts[id];
            const int shanten = Shanten(counts, meldCount);
            ++counts[id];
            return shanten;
        }

        /// The rating of the discard of kind `id`, whose shanten after it is `shanten`: the one place that rates a discard
        /// (`FindBestDiscards` and `RateDiscards` both call it). `counts` is left as it was.
        DiscardOption RateDiscard(TileCounts& counts, int meldCount, const TableView& view, uint8_t id, int shanten) {
            --counts[id];
            const int acceptance = AcceptanceOf(view, counts, meldCount);
            ++counts[id];
            const Tile tile{id};
            return {tile, shanten, acceptance, KeepValue(tile, view.seatWind, view.roundWind)};
        }

        /// The kinds whose discard leaves the lowest shanten, and that shanten.
        struct LowestShanten {
            int shanten = 0;
            eastl::vector<uint8_t> kinds;
        };

        LowestShanten FindLowestShanten(const TileCounts& counts, int meldCount) {
            LowestShanten lowest;
            lowest.shanten = kAboveWorstShanten;
            TileCounts rest = counts;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (counts[id] == 0) {
                    continue;
                }
                const int shanten = ShantenAfterDiscard(rest, meldCount, id);
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

        /// Whether discard `a` is better than `b`: the lower shanten after it, then `IsBetter` (the higher acceptance, the lower keep).
        bool IsBetterDiscard(const DiscardOption& a, const DiscardOption& b) noexcept {
            if (a.shanten != b.shanten) {
                return a.shanten < b.shanten;
            }
            return IsBetter({a.tile, a.acceptance, a.keep}, {b.tile, b.acceptance, b.keep});
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
            best.acceptance = kNoAcceptance;
            TileCounts rest = counts;
            for (const uint8_t id : lowest.kinds) {
                const DiscardOption option = RateDiscard(rest, meldCount, view, id, lowest.shanten);
                if (option.acceptance > best.acceptance) {
                    best.acceptance = option.acceptance;
                    best.candidates.clear();
                }
                if (option.acceptance == best.acceptance) {
                    best.candidates.push_back({option.tile, option.acceptance, option.keep});
                }
            }
            return best;
        }

        /// The discard of an inattentive seat: of the discards that leave the lowest shanten the lowest `KeepValue`, then `rng`; the
        /// acceptance is not looked at.
        Tile DiscardWithoutAcceptance(const TileCounts& counts, const TableView& view, MahjongRng& rng) {
            eastl::vector<Candidate> candidates;
            for (const uint8_t id : FindLowestShanten(counts, MeldCountOf(view)).kinds) {
                const Tile tile{id};
                candidates.push_back({tile, 0, KeepValue(tile, view.seatWind, view.roundWind)});
            }
            return candidates.empty() ? Tile{} : PickBest(candidates, rng);
        }

        /// Step 5: the discard of the best rating that is not a declaration.
        HandAction ChooseDiscard(const TableView& view, MahjongRng& rng, const LevelProfile& profile) {
            const TileCounts counts = CountTiles(view.own.concealed);
            if (Roll(rng, profile.skipAcceptancePercent)) {
                return HandAction::MakeDiscard(DiscardWithoutAcceptance(counts, view, rng));
            }
            const DiscardRating best = BestDiscard(counts, MeldCountOf(view), view, rng);
            return HandAction::MakeDiscard(best.tile);
        }

        /// The step a searched choice stands for: the rule whose kind of action it is (the search picks among the candidates of those
        /// rules; the advice never searches, so this only has to be defined).
        TurnStep StepOfSearched(const TableView& view, const HandAction& action) noexcept {
            if (action.kind == ActionKind::ConcealedKong || action.kind == ActionKind::UpgradeKong) {
                return TurnStep::Kong;
            }
            if (action.kind != ActionKind::Discard) {
                return TurnStep::Discard;
            }
            if (action.declareCalling) {
                return TurnStep::Declare;
            }
            return view.own.callingDeclared ? TurnStep::KeepWaiting : TurnStep::Discard;
        }

    }  // namespace

    int UnseenCopies(const TableView& view, Tile tile) noexcept {
        if (!tile.IsPlayable()) {
            return 0;
        }
        const int unseen = kCopiesPerTile - view.visible[tile.Id()];
        return unseen > 0 ? unseen : 0;
    }

    int MeldCountOf(const TableView& view) noexcept {
        return static_cast<int>(view.own.melds.size());
    }

    const HandAction* FindAction(const TableView& view, ActionKind kind) noexcept {
        for (const HandAction& action : view.legal) {
            if (action.kind == kind) {
                return &action;
            }
        }
        return nullptr;
    }

    const HandAction* FindMahjong(const TableView& view) noexcept {
        for (const HandAction& action : view.legal) {
            if (action.kind == ActionKind::Claim && action.claim == ClaimKind::Mahjong) {
                return &action;
            }
        }
        return nullptr;
    }

    bool IsValueTile(Tile tile, Wind seatWind, Wind roundWind) noexcept {
        return tile.IsDragon() || tile == Tile::FromWind(static_cast<int>(seatWind)) || tile == Tile::FromWind(static_cast<int>(roundWind));
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

    eastl::vector<WaitingDiscard> WaitingDiscards(const TableView& view, bool declaring) {
        TileCounts counts = CountTiles(view.own.concealed);
        const TileCounts meldTiles = CountMeldTiles(view.own.melds);
        eastl::vector<WaitingDiscard> discards;
        for (const HandAction& action : view.legal) {
            if (action.kind != ActionKind::Discard || action.declareCalling != declaring) {
                continue;
            }
            --counts[action.tile.Id()];
            const int copies = UnseenWinningCopies(view, counts, MeldCountOf(view), meldTiles);
            ++counts[action.tile.Id()];
            if (copies != kNotWaiting) {
                discards.push_back({action.tile, copies, KeepValue(action.tile, view.seatWind, view.roundWind)});
            }
        }
        return discards;
    }

    eastl::vector<DiscardOption> RateDiscards(const TileCounts& counts, int meldCount, const TableView& view) {
        eastl::vector<DiscardOption> options;
        TileCounts rest = counts;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            if (counts[id] != 0) {
                options.push_back(RateDiscard(rest, meldCount, view, id, ShantenAfterDiscard(rest, meldCount, id)));
            }
        }
        // The chooser's order, then the lowest tile id: the first entry is what `BestDiscardRating` reports.
        eastl::sort(options.begin(), options.end(), [](const DiscardOption& a, const DiscardOption& b) { return IsBetterDiscard(a, b) || (!IsBetterDiscard(b, a) && a.tile.Id() < b.tile.Id()); });
        return options;
    }

    DiscardRating RateKong(const TableView& view, const TileCounts& counts, const HandAction& action) {
        TileCounts rest = counts;
        int melds = MeldCountOf(view);
        if (action.kind == ActionKind::ConcealedKong) {
            rest[action.tile.Id()] = static_cast<uint8_t>(rest[action.tile.Id()] - kConcealedKongTiles);
            ++melds;
        } else {
            --rest[action.tile.Id()];
        }
        return {Tile{}, Shanten(rest, melds), AcceptanceOf(view, rest, melds)};
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

    TurnDecision DecideTurn(const TableView& view, MahjongRng& rng, const LevelProfile& profile, SearchHandoff* handoff) {
        if (const HandAction* win = FindAction(view, ActionKind::DeclareWin)) {
            return {*win, TurnStep::DeclareWin};
        }
        if (profile.searchWorlds > 0) {
            if (handoff != nullptr) {
                if ((handoff->run = BeginTurnSearch(view, rng, profile))) {
                    return {HandAction::MakePass(), TurnStep::Discard};
                }
            } else if (const std::optional<HandAction> searched = SearchTurnAction(view, rng, profile)) {
                return {*searched, StepOfSearched(view, *searched)};
            }
        }
        if (const std::optional<HandAction> declaration = ChooseWaitingDiscard(view, rng, true)) {
            if (!Roll(rng, profile.skipDeclarationPercent)) {
                return {*declaration, TurnStep::Declare};
            }
        }
        if (const std::optional<HandAction> kong = ChooseKong(view)) {
            return {*kong, TurnStep::Kong};
        }
        if (view.own.callingDeclared) {
            if (const std::optional<HandAction> keeping = ChooseWaitingDiscard(view, rng, false)) {
                return {*keeping, TurnStep::KeepWaiting};
            }
        }
        return {ChooseDiscard(view, rng, profile), TurnStep::Discard};
    }

    HandAction ChooseTurnAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile, SearchHandoff* handoff) {
        return DecideTurn(view, rng, profile, handoff).action;
    }

}  // namespace BECore::Mahjong
