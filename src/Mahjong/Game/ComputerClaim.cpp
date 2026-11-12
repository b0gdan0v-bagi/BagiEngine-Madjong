#include "ComputerClaim.h"

#include <Mahjong/Game/OwnDraws.h>

namespace BECore::Mahjong {

    namespace {

        /// Whether a claim of `kind` at a rating that is no better than the hand as it is (`s1 == s0`) is still worth it.
        bool KeepsValueAtEqualShanten(const TableView& view, ClaimKind kind) noexcept {
            return (kind == ClaimKind::Pung || kind == ClaimKind::Kong) && IsValueTile(view.pendingTile, view.seatWind, view.roundWind);
        }

        bool IsAcceptable(const TableView& view, const HandAction& action, const DiscardRating& after, int shantenBefore, const LevelProfile& profile) noexcept {
            if (after.shanten != shantenBefore) {
                return after.shanten < shantenBefore;
            }
            return profile.takeValueClaimsAtEqualShanten && KeepsValueAtEqualShanten(view, action.claim);
        }

        /// The loose tile a kong draws: one draw more than the wall leaves the seat.
        constexpr int kKongLooseDraws = 1;

        /// The view of the seat after it claimed `view.pendingTile` as a kong: three concealed copies out, an open kong in the melds.
        /// The copies the seat sees do not change (the fourth was in the pile, now it lies in the meld).
        TableView ViewAfterKong(const TableView& view) {
            TableView after = view;
            int left = kKongTilesFromHand;
            eastl::vector<Tile> concealed;
            for (const Tile tile : view.own.concealed) {
                if (left > 0 && tile == view.pendingTile) {
                    --left;
                    continue;
                }
                concealed.push_back(tile);
            }
            after.own.concealed = eastl::move(concealed);
            Meld kong;
            kong.kind = MeldKind::Kong;
            kong.concealed = false;
            kong.first = view.pendingTile;
            kong.from = view.currentSeat;
            kong.fromDiscard = true;
            kong.claimed = view.pendingTile;
            after.own.melds.push_back(kong);
            return after;
        }

        /// The acceptable claims no other acceptable claim beats (several when they are equal in every respect).
        eastl::vector<HandAction> BestClaims(const TableView& view, int shantenBefore, const LevelProfile& profile) {
            eastl::vector<RatedClaim> best;
            for (const RatedClaim& candidate : RateClaims(view, shantenBefore, profile)) {
                if (!candidate.acceptable) {
                    continue;
                }
                if (!best.empty() && IsBetterClaim(best.front(), candidate)) {
                    continue;
                }
                if (!best.empty() && IsBetterClaim(candidate, best.front())) {
                    best.clear();
                }
                best.push_back(candidate);
            }
            eastl::vector<HandAction> actions;
            for (const RatedClaim& candidate : best) {
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

    bool IsBetterClaim(const RatedClaim& a, const RatedClaim& b) noexcept {
        if (a.after.shanten != b.after.shanten) {
            return a.after.shanten < b.after.shanten;
        }
        if (a.after.acceptance != b.after.acceptance) {
            return a.after.acceptance > b.after.acceptance;
        }
        return a.rank > b.rank;
    }

    eastl::vector<RatedClaim> RateClaims(const TableView& view, int shantenBefore, const LevelProfile& profile) {
        eastl::vector<RatedClaim> options;
        for (const HandAction& action : view.legal) {
            if (action.kind != ActionKind::Claim || action.claim == ClaimKind::Mahjong) {
                continue;
            }
            const DiscardRating after = RateClaim(view, action);
            options.push_back({action, after, ClaimRank(action.claim), IsAcceptable(view, action, after, shantenBefore, profile)});
        }
        return options;
    }

    std::optional<WaitingKong> RateWaitingKong(const TableView& view) {
        for (const HandAction& action : view.legal) {
            if (action.kind != ActionKind::Claim || action.claim != ClaimKind::Kong) {
                continue;
            }
            if (RateClaim(view, action).shanten > kWaitingShanten) {
                return std::nullopt;
            }
            WaitingKong rated;
            rated.kong = action;
            rated.passValue = ValueOfHeldHand(view);
            rated.kongValue = ValueOfHeldHand(ViewAfterKong(view), kKongLooseDraws);
            return rated;
        }
        return std::nullopt;
    }

    ClaimDecision DecideClaim(const TableView& view, MahjongRng& rng, const LevelProfile& profile, SearchHandoff* handoff) {
        if (const HandAction* mahjong = FindMahjong(view)) {
            return {*mahjong, ClaimStep::Mahjong};
        }
        const int shantenBefore = Shanten(CountTiles(view.own.concealed), MeldCountOf(view));
        if (shantenBefore <= kWaitingShanten) {
            if (profile.waitingKongByPoints) {
                if (const std::optional<WaitingKong> kong = RateWaitingKong(view)) {
                    return kong->KongIsBetter() ? ClaimDecision{kong->kong, ClaimStep::WaitingKong} : ClaimDecision{HandAction::MakePass(), ClaimStep::WaitingPass};
                }
            }
            return {HandAction::MakePass(), ClaimStep::WaitingHand};
        }
        if (profile.searchWorlds > 0) {
            if (handoff != nullptr) {
                if ((handoff->run = BeginClaimSearch(view, rng, profile))) {
                    return {HandAction::MakePass(), ClaimStep::Claim};
                }
            } else if (const std::optional<HandAction> searched = SearchClaimAction(view, rng, profile)) {
                return {*searched, searched->kind == ActionKind::Claim ? ClaimStep::Claim : ClaimStep::NoGain};
            }
        }
        const eastl::vector<HandAction> best = BestClaims(view, shantenBefore, profile);
        if (best.empty()) {
            return {HandAction::MakePass(), ClaimStep::NoGain};
        }
        const HandAction& chosen = PickOne(best, rng);
        return {Roll(rng, profile.skipClaimPercent) ? HandAction::MakePass() : chosen, ClaimStep::Claim};
    }

    HandAction ChooseClaimAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile, SearchHandoff* handoff) {
        return DecideClaim(view, rng, profile, handoff).action;
    }

}  // namespace BECore::Mahjong
