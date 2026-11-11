#pragma once

#include <Mahjong/Game/ComputerLevel.h>

namespace BECore::Mahjong {

    /**
     * The seat emblem atlas: one PNG of kEmblemColumns x kEmblemRows square cells, drawn by tools/make_emblems.py (CELL, COLUMNS, ROWS,
     * DISC_RADIUS), which these numbers mirror; tests/EmblemSpritesTest.cpp compares them with the PNG header. Cell 0 is the person,
     * cells 1..10 the computer of level 1..10 (the numeral is in the picture), then the turn ring and the calling ring. Every picture
     * shares the centre of its cell: the emblem is a disc of kEmblemDiscRadius, the rings lie outside it (the turn ring solid, the
     * calling ring dotted), so a ring cell drawn over the same rectangle as an emblem cell rings it. The rings are white: the table tints them.
     */
    inline constexpr const char* kEmblemAtlasPath = "assets/emblems/emblems.png";
    inline constexpr int kEmblemColumns = 4;
    inline constexpr int kEmblemRows = 4;
    inline constexpr int kEmblemCellSize = 64;
    inline constexpr int kEmblemAtlasWidth = kEmblemColumns * kEmblemCellSize;
    inline constexpr int kEmblemAtlasHeight = kEmblemRows * kEmblemCellSize;
    /// The radius of the emblem disc in cell pixels (half of the cell is the outer edge of the rings).
    inline constexpr int kEmblemDiscRadius = 24;

    inline constexpr int kPersonEmblemCell = 0;
    /// The cell of the computer of level `kMinLevel` is the one after the person; the others follow in level order.
    inline constexpr int kFirstComputerEmblemCell = kPersonEmblemCell + 1;
    inline constexpr int kTurnRingCell = kFirstComputerEmblemCell + kLevelCount;
    inline constexpr int kCallingRingCell = kTurnRingCell + 1;
    /// Cells drawn in the atlas.
    inline constexpr int kDrawnEmblemCells = kCallingRingCell + 1;
    static_assert(kDrawnEmblemCells <= kEmblemColumns * kEmblemRows);
    static_assert(kEmblemDiscRadius < kEmblemCellSize / 2);

    /// The source rectangle of a cell in the atlas, in pixels.
    [[nodiscard]] constexpr Rect EmblemRect(int cell) noexcept {
        constexpr float size = static_cast<float>(kEmblemCellSize);
        return Rect(static_cast<float>(cell % kEmblemColumns) * size, static_cast<float>(cell / kEmblemColumns) * size, size, size);
    }

    [[nodiscard]] constexpr int PersonCell() noexcept {
        return kPersonEmblemCell;
    }

    /// The cell of the computer of `level` (1..10).
    [[nodiscard]] constexpr int ComputerCell(ComputerLevel level) noexcept {
        return kFirstComputerEmblemCell + LevelNumber(level) - kMinLevel;
    }

}  // namespace BECore::Mahjong
