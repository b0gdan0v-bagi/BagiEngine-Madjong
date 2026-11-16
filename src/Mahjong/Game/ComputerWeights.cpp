#include "ComputerWeights.h"

#include <EASTL/sort.h>

namespace BECore::Mahjong {

    int64_t RateDiscardWeighted(const LevelProfile& profile, int shanten, int acceptance, int keep, int danger, int64_t value) noexcept {
        return static_cast<int64_t>(profile.wShanten) * (kShantenCeiling - shanten) + static_cast<int64_t>(profile.wAcceptance) * acceptance +
               static_cast<int64_t>(profile.wKeep) * (kKeepValueTile - keep) + static_cast<int64_t>(profile.wSafe) * (kDangerWorst - danger) + static_cast<int64_t>(profile.wValueDiscard) * value;
    }

    Array<int64_t, kKindCount> DiscardValueAbove(const TableView& view, const LevelProfile& profile) {
        Array<int64_t, kKindCount> above{};
        if (profile.wValueDiscard <= 0 || profile.weightedValueDepth <= 0) {
            return above;
        }
        const Vector<DiscardValue> values = ValueDiscards(view, OwnDrawsModel{.depth = profile.weightedValueDepth});
        if (values.empty()) {
            return above;
        }
        int64_t lowest = values.front().expected;
        for (const DiscardValue& value : values) {
            lowest = value.expected < lowest ? value.expected : lowest;
        }
        for (const DiscardValue& value : values) {
            above[value.tile.Id()] = value.expected - lowest;
        }
        return above;
    }

    Vector<WeightedDiscard> RateDiscardsWeighted(const TableView& view, const TileCounts& counts, int meldCount, const LevelProfile& profile, bool withValue) {
        const Array<int64_t, kKindCount> above = withValue ? DiscardValueAbove(view, profile) : Array<int64_t, kKindCount>{};
        // `RateDiscards` reads the danger only with a declared opponent, so the weight alone decides whether it is looked at.
        Vector<DiscardOption> options = RateDiscards(counts, meldCount, view, profile.wSafe > 0);
        eastl::sort(options.begin(), options.end(), [](const DiscardOption& a, const DiscardOption& b) { return a.tile.Id() < b.tile.Id(); });
        Vector<WeightedDiscard> rated;
        rated.reserve(options.size());
        for (const DiscardOption& option : options) {
            const int64_t value = above[option.tile.Id()];
            rated.push_back({option.tile, option.shanten, option.acceptance, option.keep, option.danger, value,
                             RateDiscardWeighted(profile, option.shanten, option.acceptance, option.keep, option.danger, value)});
        }
        return rated;
    }

    Tile ChooseWeightedDiscard(const TableView& view, MahjongRng& rng, const LevelProfile& profile) {
        const Vector<WeightedDiscard> rated = RateDiscardsWeighted(view, CountTiles(view.own.concealed), MeldCountOf(view), profile, true);
        if (rated.empty()) {
            return Tile{};
        }
        int64_t best = rated.front().rating;
        for (const WeightedDiscard& discard : rated) {
            best = discard.rating > best ? discard.rating : best;
        }
        Vector<Tile> ties;
        for (const WeightedDiscard& discard : rated) {
            if (discard.rating == best) {
                ties.push_back(discard.tile);
            }
        }
        return PickOne(ties, rng);
    }

    int64_t RateHandWeighted(const LevelProfile& profile, int shanten, int acceptance) noexcept {
        return static_cast<int64_t>(profile.wShanten) * (kShantenCeiling - shanten) + static_cast<int64_t>(profile.wAcceptance) * acceptance;
    }

    bool ClaimPassesGate(const LevelProfile& profile, int64_t claimRating, int64_t passRating, bool valueAtEqualShanten, int liveCount) noexcept {
        const int64_t gain = claimRating - passRating + (valueAtEqualShanten ? profile.wClaimValue : 0);
        return (gain - profile.wClaimMargin) * kWallWeightUnit > static_cast<int64_t>(profile.wClaimWall) * liveCount;
    }

    namespace {

        /// The shanten and acceptance of the hand after `claim`, as the weighted seat plays it on.
        DiscardRating HandAfterClaim(const TableView& view, const HandAction& claim, const LevelProfile& profile) {
            if (claim.claim == ClaimKind::Kong) {
                return RateClaim(view, claim);
            }
            const Vector<WeightedDiscard> rated = RateDiscardsWeighted(view, ConcealedAfterClaim(view, claim), MeldCountOf(view) + 1, profile);
            const WeightedDiscard* best = nullptr;
            for (const WeightedDiscard& discard : rated) {
                best = best == nullptr || discard.rating > best->rating ? &discard : best;
            }
            return best == nullptr ? DiscardRating{} : DiscardRating{best->tile, best->shanten, best->acceptance};
        }

    }  // namespace

    Vector<WeightedClaim> RateClaimsWeighted(const TableView& view, int shantenBefore, const LevelProfile& profile) {
        const int meldCount = MeldCountOf(view);
        const int64_t pass = RateHandWeighted(profile, shantenBefore, AcceptanceOf(view, CountTiles(view.own.concealed), meldCount));
        Vector<WeightedClaim> claims;
        for (const HandAction& action : view.legal) {
            if (action.kind != ActionKind::Claim || action.claim == ClaimKind::Mahjong) {
                continue;
            }
            const DiscardRating after = HandAfterClaim(view, action, profile);
            WeightedClaim claim;
            claim.action = action;
            claim.shanten = after.shanten;
            claim.acceptance = after.acceptance;
            claim.rating = RateHandWeighted(profile, after.shanten, after.acceptance) + static_cast<int64_t>(profile.wClaimRank) * ClaimRank(action.claim);
            claim.acceptable = ClaimPassesGate(profile, claim.rating, pass, after.shanten == shantenBefore && IsValueClaim(view, action.claim), view.liveCount);
            claims.push_back(claim);
        }
        return claims;
    }

    Optional<HandAction> ChooseWeightedClaim(const TableView& view, MahjongRng& rng, const LevelProfile& profile, int shantenBefore) {
        const Vector<WeightedClaim> claims = RateClaimsWeighted(view, shantenBefore, profile);
        const WeightedClaim* best = nullptr;
        for (const WeightedClaim& claim : claims) {
            if (claim.acceptable && (best == nullptr || claim.rating > best->rating)) {
                best = &claim;
            }
        }
        if (best == nullptr) {
            return std::nullopt;
        }
        Vector<HandAction> ties;
        for (const WeightedClaim& claim : claims) {
            if (claim.acceptable && claim.rating == best->rating) {
                ties.push_back(claim.action);
            }
        }
        return PickOne(ties, rng);
    }

}  // namespace BECore::Mahjong
