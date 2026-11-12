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
        TurnOption MakeTurnOption(const TableView& view, const TileCounts& counts, const eastl::vector<DiscardOption>& rated, const DangerTable& dangers, Tile tile) {
            TurnOption option;
            option.tile = tile;
            option.danger = dangers[tile.Id()];
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
        /// danger (with the knob), then the lowest keep value, then the lowest tile id.
        eastl::vector<WaitingDiscard> RankWaitingDiscards(const TableView& view, bool declaring, bool readDanger) {
            eastl::vector<WaitingDiscard> discards = WaitingDiscards(view, declaring, readDanger);
            eastl::sort(discards.begin(), discards.end(), [](const WaitingDiscard& a, const WaitingDiscard& b) {
                if (a.copies != b.copies) {
                    return a.copies > b.copies;
                }
                if (a.danger != b.danger) {
                    return a.danger < b.danger;
                }
                return a.keep != b.keep ? a.keep < b.keep : a.tile.Id() < b.tile.Id();
            });
            return discards;
        }

        void ExplainWaitingDiscard(const TableView& view, const TileCounts& counts, const eastl::vector<DiscardOption>& rated, const DangerTable& dangers, bool readDanger,
                                   TurnExplanation& explanation) {
            const bool declaring = explanation.step == TurnStep::Declare;
            const Tile advised = explanation.action.tile;
            explanation.chosen = MakeTurnOption(view, counts, rated, dangers, advised);
            explanation.waits = WaitsAfterDiscard(view, counts, advised);
            for (const WaitingDiscard& discard : RankWaitingDiscards(view, declaring, readDanger)) {
                if (discard.tile == advised) {
                    continue;
                }
                explanation.equalChoices += discard.copies == explanation.chosen.waitCopies ? 1 : 0;
                if (static_cast<int>(explanation.alternatives.size()) < kExplainAlternatives) {
                    explanation.alternatives.push_back(MakeTurnOption(view, counts, rated, dangers, discard.tile));
                }
            }
        }

        void ExplainDiscard(const TableView& view, const TileCounts& counts, const eastl::vector<DiscardOption>& rated, const DangerTable& dangers, TurnExplanation& explanation) {
            const Tile advised = explanation.action.tile;
            explanation.chosen = MakeTurnOption(view, counts, rated, dangers, advised);
            for (const DiscardOption& entry : rated) {
                if (entry.tile == advised) {
                    continue;
                }
                explanation.equalChoices += entry.shanten == explanation.chosen.shanten && entry.acceptance == explanation.chosen.acceptance ? 1 : 0;
                if (static_cast<int>(explanation.alternatives.size()) < kExplainAlternatives) {
                    explanation.alternatives.push_back(MakeTurnOption(view, counts, rated, dangers, entry.tile));
                }
            }
        }

        TurnExplanation ExplainTurn(const TableView& view, const LevelProfile& profile) {
            MahjongRng rng(kAdviceSeed);
            const TurnDecision decision = DecideTurn(view, rng, profile);
            TurnExplanation explanation;
            explanation.action = decision.action;
            explanation.step = decision.step;
            const TileCounts counts = CountTiles(view.own.concealed);
            const int melds = MeldCountOf(view);
            const DangerTable dangers = DiscardDangers(view);
            explanation.shantenBefore = Shanten(counts, melds);
            explanation.readDanger = profile.readDanger;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                explanation.declaredOpponents += seat != view.seat && view.seats[seat].callingDeclared ? 1 : 0;
            }
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
                    ExplainWaitingDiscard(view, counts, RateDiscards(counts, melds, view, profile.readDanger), dangers, profile.readDanger, explanation);
                    break;
                case TurnStep::Discard:
                    ExplainDiscard(view, counts, RateDiscards(counts, melds, view, profile.readDanger), dangers, explanation);
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

        ClaimExplanation ExplainClaim(const TableView& view, const LevelProfile& profile) {
            MahjongRng rng(kAdviceSeed);
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
                case ClaimStep::WaitingKong:
                case ClaimStep::WaitingPass: {
                    explanation.waits = CopiesOf(view, WinningTiles(view.own));
                    explanation.waitingKong = RateWaitingKong(view);
                    if (explanation.waitingKong.has_value()) {
                        const HandAction& kong = explanation.waitingKong->kong;
                        const RatedClaim rated{kong, RateClaim(view, kong), ClaimRank(kong.claim), true};
                        if (decision.step == ClaimStep::WaitingKong) {
                            explanation.chosen = rated;
                        } else {
                            explanation.alternatives.push_back(rated);
                        }
                    }
                    break;
                }
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

    std::optional<Explanation> ExplainAdvice(const TableView& view, const LevelProfile& profile) {
        if (view.legal.empty()) {
            return std::nullopt;
        }
        Explanation explanation;
        switch (view.phase) {
            case Phase::Claim:
            case Phase::RobKong:
                explanation.claim = ExplainClaim(view, profile);
                return explanation;
            case Phase::Discard:
                explanation.turn = ExplainTurn(view, profile);
                return explanation;
            case Phase::Finished:
                break;
        }
        return std::nullopt;
    }

}  // namespace BECore::Mahjong
