#pragma once

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

}  // namespace BECore::Mahjong
