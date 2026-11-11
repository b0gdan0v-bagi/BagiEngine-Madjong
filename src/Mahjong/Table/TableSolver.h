#pragma once

#include <Mahjong/Table/TableLayoutSpec.h>

namespace BECore::Mahjong {

    /**
     * Finds the largest tile that a zone holds for what the rules allow it to hold. Pure functions in design units over plain numbers: they
     * know no seat, no shape and no XML (`TableLayout.cpp` knows which zone is which). Deterministic, and a zone too small, or a number that
     * is NaN, infinite or negative, gives a result of size 0: never a negative size, never NaN.
     *
     * **The tile rule:** an integer width `w`, height `TileHeightOf(w)` (the 3 : 4 proportions of the atlas cell, rounded down). The
     * solver takes the largest `w` that fits, so a zone twice as large does not give a tile twice as large, and one more unit of width is
     * always a tile that does not fit.
     */

    /// The grid that `SnapZone` rounds the edges of a zone to, in design units.
    inline constexpr float kZoneSnapGrid = 0.25f;

    /// The widest tile any solver returns (a guard against a zone of absurd size; no real tile comes near it).
    inline constexpr int kSolverMaxTileW = 1024;

    /// The height of a tile `tileW` units wide: `floor(tileW * 4 / 3)`; 0 for a width under 1.
    [[nodiscard]] constexpr int TileHeightOf(int tileW) noexcept {
        return tileW < 1 ? 0 : tileW * 4 / 3;
    }

    /// The rectangle of a zone in design units: the fractions times the design size, each edge rounded to `kZoneSnapGrid` (the left edge and
    /// the right edge, the width being their difference), so that the float noise of a fraction such as 8 / 720 never reaches a tile size.
    /// An edge that is not a number, or a width or height under 0, gives a rectangle of size 0.
    [[nodiscard]] Rect SnapZone(const ZoneRect& zone, float designWidth, float designHeight) noexcept;

    /// A strip of tiles in a zone. `tileW` x `tileH` is the upright tile; `slotW` x `slotH` the rectangle of one tile on the screen (the
    /// upright size, or swapped for a vertical strip, whose tiles are turned). Tile `i` of a strip lies at (`x`, `y`) plus `i` times the
    /// pitch along the line (`PitchOf`); `capacity` is the most the line may span, which the squeeze keeps.
    struct SolvedStrip {
        float tileW = 0.0f;
        float tileH = 0.0f;
        float slotW = 0.0f;
        float slotH = 0.0f;
        /// The top-left corner of the first tile (the zone's corner, moved across the line by the alignment).
        float x = 0.0f;
        float y = 0.0f;
        float capacity = 0.0f;
        bool vertical = false;

        [[nodiscard]] bool Fits() const noexcept {
            return tileW > 0.0f;
        }

        /// The distance between two neighbouring tiles of a group along the line.
        [[nodiscard]] float PitchOf() const noexcept {
            return vertical ? slotH : slotW;
        }
    };

    /// The largest tile for `tiles` tiles in a line in `zone` with `gaps` gaps of `kMeldGap` (the tiles side by side, the squeeze never
    /// needed). A horizontal line runs along the width and needs the tile's height across; a vertical line runs along the height and its
    /// tiles are turned, so the zone's width must hold the tile's height. `align` puts the tile across the line when the zone is wider.
    [[nodiscard]] SolvedStrip SolveStrip(const Rect& zone, ZoneAlign align, bool vertical, int tiles, int gaps) noexcept;

    /// A horizontal strip whose tile is asked for, not solved (the phone profile's hand): tiles `tileW` x `tileH`, the zone's corner moved down
    /// by `align` across the line, `capacity` the zone's width. Size 0 when the tile is not a usable size; a tile higher than the zone rests
    /// on the zone's top edge.
    [[nodiscard]] SolvedStrip PlaceStrip(const Rect& zone, ZoneAlign align, float tileW, float tileH) noexcept;

    /// A block of discards. `tileW` x `tileH` is the upright tile; the block, on the screen, is `blockW` x `blockH` at (`x`, `y`) of the zone's
    /// corner. A horizontal block has `kDiscardsPerRow` tiles in a row and `rows` rows below one another; a vertical one (a side seat)
    /// is the same block turned, a row running along the height.
    struct SolvedBlock {
        float tileW = 0.0f;
        float tileH = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float blockW = 0.0f;
        float blockH = 0.0f;
        int rows = 0;

        [[nodiscard]] bool Fits() const noexcept {
            return tileW > 0.0f;
        }
    };

    /// The largest tile for a block of `rows` full rows of `kDiscardsPerRow` tiles in `zone`.
    [[nodiscard]] SolvedBlock SolveDiscardBlock(const Rect& zone, bool vertical, int rows) noexcept;

    /// The wall ring: a rectangle of `kWallRingColumns` x `kWallRingRows` square cells of side `stack`.
    struct SolvedWall {
        float stack = 0.0f;
        float x = 0.0f;
        float y = 0.0f;
        float w = 0.0f;
        float h = 0.0f;

        [[nodiscard]] bool Fits() const noexcept {
            return stack > 0.0f;
        }
    };

    /// The largest stack that makes the ring fit `zone`.
    [[nodiscard]] SolvedWall SolveWall(const Rect& zone) noexcept;

}  // namespace BECore::Mahjong
