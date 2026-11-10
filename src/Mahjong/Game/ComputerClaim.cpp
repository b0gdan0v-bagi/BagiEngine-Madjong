#include "ComputerClaim.h"

namespace BECore::Mahjong {

    namespace {

        /// A claim that may be made: the action, the rating of the hand after it and its rank at a tie.
        struct ClaimCandidate {
            HandAction action;
            DiscardRating after;
            int rank = 0;
        };

        /// Whether a claim of `kind` at a rating that is no better than the hand as it is (`s1 == s0`) is still worth it.
        bool KeepsValueAtEqualShanten(const TableView& view, ClaimKind kind) noexcept {
            return (kind == ClaimKind::Pung || kind == ClaimKind::Kong) && IsValueTile(view.pendingTile, view.seatWind, view.roundWind);
        }

        bool IsAcceptable(const TableView& view, const ClaimCandidate& candidate, int shantenBefore, const LevelProfile& profile) noexcept {
            if (candidate.after.shanten != shantenBefore) {
                return candidate.after.shanten < shantenBefore;
            }
            return profile.takeValueClaimsAtEqualShanten && KeepsValueAtEqualShanten(view, candidate.action.claim);
        }

        /// Whether `a` is a better claim than `b`: the lower shanten, then the higher acceptance, then the higher rank.
        bool IsBetter(const ClaimCandidate& a, const ClaimCandidate& b) noexcept {
            if (a.after.shanten != b.after.shanten) {
                return a.after.shanten < b.after.shanten;
            }
            if (a.after.acceptance != b.after.acceptance) {
                return a.after.acceptance > b.after.acceptance;
            }
            return a.rank > b.rank;
        }

        /// The acceptable claims no other acceptable claim beats (several when they are equal in every respect).
        eastl::vector<HandAction> BestClaims(const TableView& view, int shantenBefore, const LevelProfile& profile) {
            eastl::vector<ClaimCandidate> best;
            for (const HandAction& action : view.legal) {
                if (action.kind != ActionKind::Claim || action.claim == ClaimKind::Mahjong) {
                    continue;
                }
                const ClaimCandidate candidate{action, RateClaim(view, action), ClaimRank(action.claim)};
                if (!IsAcceptable(view, candidate, shantenBefore, profile)) {
                    continue;
                }
                if (!best.empty() && IsBetter(best.front(), candidate)) {
                    continue;
                }
                if (!best.empty() && IsBetter(candidate, best.front())) {
                    best.clear();
                }
                best.push_back(candidate);
            }
            eastl::vector<HandAction> actions;
            for (const ClaimCandidate& candidate : best) {
                actions.push_back(candidate.action);
            }
            return actions;
        }

    }  // namespace

    int ClaimRank(ClaimKind kind) noexcept {
        switch (kind) {
            case ClaimKind::Kong:
                return kClaimRankKong;
            case ClaimKind::Pung:
                return kClaimRankPung;
            case ClaimKind::Chow:
                return kClaimRankChow;
            case ClaimKind::Mahjong:
                break;
        }
        return kClaimRankMahjong;
    }

    TileCounts ConcealedAfterClaim(const TableView& view, const HandAction& claim) {
        TileCounts counts = CountTiles(view.own.concealed);
        if (claim.claim == ClaimKind::Chow) {
            for (const Tile member : ChowTiles(claim.chowFirst)) {
                if (member != view.pendingTile) {
                    --counts[member.Id()];
                }
            }
            return counts;
        }
        const int fromHand = claim.claim == ClaimKind::Kong ? kKongTilesFromHand : kPungTilesFromHand;
        counts[view.pendingTile.Id()] = static_cast<uint8_t>(counts[view.pendingTile.Id()] - fromHand);
        return counts;
    }

    DiscardRating RateClaim(const TableView& view, const HandAction& claim) {
        const TileCounts counts = ConcealedAfterClaim(view, claim);
        const int melds = MeldCountOf(view) + 1;
        if (claim.claim == ClaimKind::Kong) {
            return {Tile{}, Shanten(counts, melds), AcceptanceOf(view, counts, melds)};
        }
        return BestDiscardRating(counts, melds, view);
    }

    HandAction ChooseClaimAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile, SearchHandoff* handoff) {
        if (const HandAction* mahjong = FindMahjong(view)) {
            return *mahjong;
        }
        const int shantenBefore = Shanten(CountTiles(view.own.concealed), MeldCountOf(view));
        if (shantenBefore <= kWaitingShanten) {
            return HandAction::MakePass();
        }
        if (profile.searchWorlds > 0) {
            if (handoff != nullptr) {
                if ((handoff->run = BeginClaimSearch(view, rng, profile))) {
                    return HandAction::MakePass();
                }
            } else if (const std::optional<HandAction> searched = SearchClaimAction(view, rng, profile)) {
                return *searched;
            }
        }
        const eastl::vector<HandAction> best = BestClaims(view, shantenBefore, profile);
        if (best.empty()) {
            return HandAction::MakePass();
        }
        const HandAction& chosen = PickOne(best, rng);
        return Roll(rng, profile.skipClaimPercent) ? HandAction::MakePass() : chosen;
    }

}  // namespace BECore::Mahjong
