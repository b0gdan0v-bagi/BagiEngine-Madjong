#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /// A hand is four sets and a pair.
    inline constexpr int kSetsInHand = 4;
    inline constexpr int kTilesPerSet = 3;
    inline constexpr int kTilesInPair = 2;
    /// Tiles of a winning hand, the concealed ones and the melds together; a kong counts as a set of 3 (its fourth tile is not counted).
    inline constexpr int kWinningHandTiles = kSetsInHand * kTilesPerSet + kTilesInPair;
    /// Pairs of the seven-pairs shape.
    inline constexpr int kPairsInSevenPairs = 7;

    /// One set of a decomposition: a chow (`first` is its lowest tile) or a pung (`first` is the tile). Never a kong: kongs are melds.
    struct ShapeSet {
        MeldKind kind = MeldKind::Pung;
        Tile first;

        constexpr bool operator==(const ShapeSet&) const noexcept = default;
    };

    /// The concealed tiles split into sets and a pair. `sets` are in the order of `first` (a pung before the chows of the same `first`).
    struct Decomposition {
        Tile pair;
        eastl::vector<ShapeSet> sets;

        bool operator==(const Decomposition&) const = default;
    };

    /// Counts the playable tiles of `tiles` (a bonus or an invalid tile is skipped).
    [[nodiscard]] TileCounts CountTiles(const eastl::vector<Tile>& tiles) noexcept;

    /// The number of tiles `counts` holds.
    [[nodiscard]] int TotalTiles(const TileCounts& counts) noexcept;

    /**
     * @brief Every way to split `counts` into `setsNeeded` sets and one pair.
     *
     * A set is a chow (three consecutive ranks of one suit; never an honour, never wrapping from 9 to 1) or a pung. `counts` has to
     * hold exactly `3 * setsNeeded + 2` tiles, otherwise (or for `setsNeeded` outside 0..4) the result is empty. Two decompositions
     * with the same pair and the same sets are one: each split is returned once. Scoring (stage 3) picks the best of them.
     */
    [[nodiscard]] eastl::vector<Decomposition> Decompose(const TileCounts& counts, int setsNeeded);

    /// Thirteen orphans: every terminal and every honour at least once, 14 tiles, nothing else (so one of them twice).
    [[nodiscard]] bool IsThirteenOrphans(const TileCounts& counts) noexcept;

    /// Seven pairs: seven different kinds twice each (a four of a kind is not two pairs), and either all of one suit (no honours) or
    /// all honours. The restriction to one suit or honours is this repository's reading; stage 3 confirms it with the limit hands.
    [[nodiscard]] bool IsSevenPairs(const TileCounts& counts) noexcept;

    /**
     * @brief Whether `counts` (the concealed tiles after the winning tile) plus `meldCount` melds is a winning hand.
     *
     * The concealed part has to hold `3 * (4 - meldCount) + 2` tiles and split into `4 - meldCount` sets and a pair. A kong is a meld
     * like any other, so it does not change the count. With no melds thirteen orphans and seven pairs win as well (stage 3 scores
     * them as limit hands). `meldCount` outside 0..4 is never a win.
     */
    [[nodiscard]] bool IsWinningShape(const TileCounts& counts, int meldCount);

    /**
     * @brief The kinds that complete a waiting hand: `counts` holds the 13 - 3 * `meldCount` concealed tiles and one more tile of
     * the returned kind makes `IsWinningShape`. In the order of the tile ids; a kind already held four times is not offered. Empty
     * when the hand is not waiting or the tile count is wrong.
     */
    [[nodiscard]] eastl::vector<Tile> WinningTiles(const TileCounts& counts, int meldCount);

}  // namespace BECore::Mahjong
