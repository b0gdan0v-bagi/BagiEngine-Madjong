#pragma once

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/ComputerTurn.h>

namespace BECore::Mahjong {

    /**
     * @file
     * The weighted player (`LevelProfile::weighted`, `docs/bot-weights.md`): step 5 of the own turn and steps 3 and 4 of a claim window
     * choose by a sum of weights times features instead of Normal's lexicographic order. **With the default weights (`kNormalWeight...`)
     * the choices and the draws from the rng are Normal's exactly**; with `weighted` off none of this is called.
     *
     * Steps 1 to 4 of the turn and steps 1 and 2 of a claim window do not read the weights. The hints and the coach (`ExplainAdvice`)
     * explain Normal's order: a weighted profile is not supported there and is never passed to it.
     */

    /// One discard of a hand of `3n + 2` tiles with the features of the `3n + 1` tiles left and its weighted `rating`.
    struct WeightedDiscard {
        Tile tile;
        int shanten = 0;
        int acceptance = 0;
        int keep = 0;
        /// The `Danger` grade; 0 for every kind unless `profile.wSafe > 0` and an opponent has declared.
        int danger = 0;
        int64_t rating = 0;
    };

    /// The rating of a discard from its features: `wShanten * (kShantenCeiling - shanten) + wAcceptance * acceptance + wKeep *
    /// (kKeepValueTile - keep) + wSafe * (kDangerWorst - danger)`. The weights are non-negative, so a larger weight always means more of the
    /// good thing.
    [[nodiscard]] int64_t RateDiscardWeighted(const LevelProfile& profile, int shanten, int acceptance, int keep, int danger) noexcept;

    /// Every kind held, with its features and rating, in the order of the tile ids. Unlike Normal's `BestDiscard` it rates every kind,
    /// not only the lowest-shanten ones: a weight may trade shanten for something else.
    [[nodiscard]] Vector<WeightedDiscard> RateDiscardsWeighted(const TableView& view, const TileCounts& counts, int meldCount, const LevelProfile& profile);

    /// The discard of the highest rating; kinds with an equal rating are the ties, in the order of the tile ids, and `PickOne` takes one
    /// (a number is drawn from `rng` exactly when there is more than one, as in `BestDiscard`). An empty hand gives an invalid tile.
    [[nodiscard]] Tile ChooseWeightedDiscard(const TableView& view, MahjongRng& rng, const LevelProfile& profile);

    /// The rating of a hand of 13 tiles (as held, or after a claim and its best discard): `wShanten * (kShantenCeiling - shanten) +
    /// wAcceptance * acceptance`.
    [[nodiscard]] int64_t RateHandWeighted(const LevelProfile& profile, int shanten, int acceptance) noexcept;

    /// Whether a claim is acceptable: `claim - pass + bonus > wClaimMargin + wClaimWall * liveCount / kWallWeightUnit`, where `bonus` is
    /// `wClaimValue` for a pung or kong of a value tile at an equal shanten and 0 otherwise. Compared without a division, so the slope
    /// is not rounded.
    [[nodiscard]] bool ClaimPassesGate(const LevelProfile& profile, int64_t claimRating, int64_t passRating, bool valueAtEqualShanten, int liveCount) noexcept;

    /// A pung, kong or chow of the window with the hand after it: the shanten and acceptance of the best discard by the discard rating
    /// (the first of equal ratings by tile id, no rng; a kong has no discard yet and is the hand as it stands, as `RateClaim`), its
    /// rating `claim` = `RateHandWeighted + wClaimRank * ClaimRank` and the gate.
    struct WeightedClaim {
        HandAction action;
        int shanten = 0;
        int acceptance = 0;
        int64_t rating = 0;
        bool acceptable = false;
    };

    /// Every pung, kong and chow of `view.legal` (no mahjong, no pass), in the order of `view.legal`, rated against the pass: the hand
    /// as held, `shantenBefore` and its acceptance. `takeValueClaimsAtEqualShanten` is not read: `wClaimValue` is its weight.
    [[nodiscard]] Vector<WeightedClaim> RateClaimsWeighted(const TableView& view, int shantenBefore, const LevelProfile& profile);

    /// The acceptable claim of the highest rating (not of the gate's number: the bonus is not in the order); equal ratings are the
    /// ties in the order of `view.legal`, and `PickOne` takes one (a number is drawn exactly when there is more than one). nullopt when
    /// none is acceptable. The roll of `skipClaimPercent` is the caller's (`DecideClaim`).
    [[nodiscard]] Optional<HandAction> ChooseWeightedClaim(const TableView& view, MahjongRng& rng, const LevelProfile& profile, int shantenBefore);

}  // namespace BECore::Mahjong
