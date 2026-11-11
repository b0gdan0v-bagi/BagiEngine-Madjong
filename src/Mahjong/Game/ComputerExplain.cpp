#include "ComputerExplain.h"

#include <EASTL/sort.h>

namespace BECore::Mahjong {

    namespace {

        /// The winning tiles `tiles` with their unseen copies.
        eastl::vector<TileCopies> CopiesOf(const TableView& view, const eastl::vector<Tile>& tiles) {
            eastl::vector<TileCopies> copies;
            for (const Tile tile : tiles) {
                copies.push_back({tile, UnseenCopies(view, tile)});
            }
            return copies;
        }

        /// The winning tiles of the 13 that stay when `counts` (the concealed tiles) lose `discard`.
        eastl::vector<TileCopies> WaitsAfterDiscard(const TableView& view, TileCounts counts, Tile discard) {
            --counts[discard.Id()];
            return CopiesOf(view, WinningTiles(counts, MeldCountOf(view), CountMeldTiles(view.own.melds)));
        }

        /// The rated discard of kind `tile`, with the waits of the hand that stays.
        TurnOption MakeTurnOption(const TableView& view, const TileCounts& counts, const eastl::vector<DiscardOption>& rated, Tile tile) {
            TurnOption option;
            option.tile = tile;
            for (const DiscardOption& entry : rated) {
                if (entry.tile == tile) {
                    option.shanten = entry.shanten;
                    option.acceptance = entry.acceptance;
                    option.keep = entry.keep;
                    break;
                }
            }
            for (const TileCopies& wait : WaitsAfterDiscard(view, counts, tile)) {
                ++option.waitKinds;
                option.waitCopies += wait.unseen;
            }
            return option;
        }

        /// The discards that leave a waiting hand, best first as the chooser ranks them: the most unseen winning copies, then the lowest
        /// keep value, then the lowest tile id.
        eastl::vector<WaitingDiscard> RankWaitingDiscards(const TableView& view, bool declaring) {
            eastl::vector<WaitingDiscard> discards = WaitingDiscards(view, declaring);
            eastl::sort(discards.begin(), discards.end(), [](const WaitingDiscard& a, const WaitingDiscard& b) {
                if (a.copies != b.copies) {
                    return a.copies > b.copies;
                }
                return a.keep != b.keep ? a.keep < b.keep : a.tile.Id() < b.tile.Id();
            });
            return discards;
        }

        void ExplainWaitingDiscard(const TableView& view, const TileCounts& counts, const eastl::vector<DiscardOption>& rated, TurnExplanation& explanation) {
            const bool declaring = explanation.step == TurnStep::Declare;
            const Tile advised = explanation.action.tile;
            explanation.chosen = MakeTurnOption(view, counts, rated, advised);
            explanation.waits = WaitsAfterDiscard(view, counts, advised);
            for (const WaitingDiscard& discard : RankWaitingDiscards(view, declaring)) {
                if (discard.tile == advised) {
                    continue;
                }
                explanation.equalChoices += discard.copies == explanation.chosen.waitCopies ? 1 : 0;
                if (static_cast<int>(explanation.alternatives.size()) < kExplainAlternatives) {
                    explanation.alternatives.push_back(MakeTurnOption(view, counts, rated, discard.tile));
                }
            }
        }

        void ExplainDiscard(const TableView& view, const TileCounts& counts, const eastl::vector<DiscardOption>& rated, TurnExplanation& explanation) {
            const Tile advised = explanation.action.tile;
            explanation.chosen = MakeTurnOption(view, counts, rated, advised);
            for (const DiscardOption& entry : rated) {
                if (entry.tile == advised) {
                    continue;
                }
                explanation.equalChoices += entry.shanten == explanation.chosen.shanten && entry.acceptance == explanation.chosen.acceptance ? 1 : 0;
                if (static_cast<int>(explanation.alternatives.size()) < kExplainAlternatives) {
                    explanation.alternatives.push_back(MakeTurnOption(view, counts, rated, entry.tile));
                }
            }
        }

        TurnExplanation ExplainTurn(const TableView& view) {
            MahjongRng rng(kAdviceSeed);
            const TurnDecision decision = DecideTurn(view, rng, ProfileOf(kDefaultLevel));
            TurnExplanation explanation;
            explanation.action = decision.action;
            explanation.step = decision.step;
            const TileCounts counts = CountTiles(view.own.concealed);
            const int melds = MeldCountOf(view);
            explanation.shantenBefore = Shanten(counts, melds);
            switch (decision.step) {
                case TurnStep::DeclareWin:
                    break;
                case TurnStep::Kong: {
                    const DiscardRating current = BestDiscardRating(counts, melds, view);
                    const DiscardRating after = RateKong(view, counts, decision.action);
                    explanation.shantenBefore = current.shanten;
                    explanation.kongAcceptanceBefore = current.acceptance;
                    explanation.kongShantenAfter = after.shanten;
                    explanation.kongAcceptanceAfter = after.acceptance;
                    explanation.chosen.tile = decision.action.tile;
                    break;
                }
                case TurnStep::Declare:
                case TurnStep::KeepWaiting:
                    ExplainWaitingDiscard(view, counts, RateDiscards(counts, melds, view), explanation);
                    break;
                case TurnStep::Discard:
                    ExplainDiscard(view, counts, RateDiscards(counts, melds, view), explanation);
                    break;
            }
            return explanation;
        }

        /// The claims of `options` with `acceptable` as given, apart from `chosen`, best first by `IsBetterClaim` (equal ones keep the
        /// order of the window).
        eastl::vector<RatedClaim> RankedClaims(const eastl::vector<RatedClaim>& options, bool acceptable, const std::optional<RatedClaim>& chosen) {
            eastl::vector<RatedClaim> ranked;
            for (const RatedClaim& option : options) {
                if (option.acceptable != acceptable || (chosen.has_value() && option.action == chosen->action)) {
                    continue;
                }
                size_t at = ranked.size();
                while (at > 0 && IsBetterClaim(option, ranked[at - 1])) {
                    --at;
                }
                ranked.insert(ranked.begin() + static_cast<ptrdiff_t>(at), option);
            }
            return ranked;
        }

        ClaimExplanation ExplainClaim(const TableView& view) {
            MahjongRng rng(kAdviceSeed);
            const LevelProfile& profile = ProfileOf(kDefaultLevel);
            const ClaimDecision decision = DecideClaim(view, rng, profile);
            ClaimExplanation explanation;
            explanation.action = decision.action;
            explanation.step = decision.step;
            explanation.shantenBefore = Shanten(CountTiles(view.own.concealed), MeldCountOf(view));
            switch (decision.step) {
                case ClaimStep::Mahjong:
                    break;
                case ClaimStep::WaitingHand:
                    explanation.waits = CopiesOf(view, WinningTiles(view.own));
                    break;
                case ClaimStep::Claim:
                case ClaimStep::NoGain: {
                    const eastl::vector<RatedClaim> options = RateClaims(view, explanation.shantenBefore, profile);
                    if (decision.step == ClaimStep::Claim) {
                        for (const RatedClaim& option : options) {
                            if (option.action == decision.action) {
                                explanation.chosen = option;
                            }
                        }
                    }
                    for (const bool acceptable : {true, false}) {
                        for (const RatedClaim& option : RankedClaims(options, acceptable, explanation.chosen)) {
                            if (static_cast<int>(explanation.alternatives.size()) < kExplainAlternatives) {
                                explanation.alternatives.push_back(option);
                            }
                        }
                    }
                    break;
                }
            }
            return explanation;
        }

    }  // namespace

    std::optional<Explanation> ExplainAdvice(const TableView& view) {
        if (view.legal.empty()) {
            return std::nullopt;
        }
        Explanation explanation;
        switch (view.phase) {
            case Phase::Claim:
            case Phase::RobKong:
                explanation.claim = ExplainClaim(view);
                return explanation;
            case Phase::Discard:
                explanation.turn = ExplainTurn(view);
                return explanation;
            case Phase::Finished:
                break;
        }
        return std::nullopt;
    }

}  // namespace BECore::Mahjong
