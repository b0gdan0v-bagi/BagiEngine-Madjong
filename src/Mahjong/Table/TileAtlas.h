#pragma once

#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /**
     * The tile atlas: one PNG of kAtlasColumns x kAtlasRows cells, the cell of a tile is its id (Tile.h), the back is the cell after
     * the last id and the wall stack (two backs side by side seen from above) the cell after the back. The numbers are those of tools/make_tiles.py (CELL_W, CELL_H, COLUMNS, ROWS), which draws the
     * atlas and writes config/TextureLibrary.xml; tests/TileSpritesTest.cpp compares them with the XML and the PNG, so they cannot drift apart.
     */
    inline constexpr const char* kAtlasPath = "assets/tiles/tiles.png";
    inline constexpr int kAtlasColumns = 8;
    inline constexpr int kAtlasRows = 6;
    inline constexpr int kCellWidth = 96;
    inline constexpr int kCellHeight = 128;
    inline constexpr int kAtlasWidth = kAtlasColumns * kCellWidth;
    inline constexpr int kAtlasHeight = kAtlasRows * kCellHeight;
    /// The cell after the tile ids.
    inline constexpr int kBackCell = kTileIdCount;
    /// The cell after the back: a wall stack seen from above, a kStackSize square at the top left corner of the cell, the rest transparent.
    inline constexpr int kStackCell = kBackCell + 1;
    inline constexpr int kStackSize = 96;
    static_assert(kStackSize <= kCellWidth && kStackSize <= kCellHeight);
    /// Cells drawn in the atlas: every tile id, the back and the stack.
    inline constexpr int kDrawnCells = kTileIdCount + 2;
    static_assert(kDrawnCells <= kAtlasColumns * kAtlasRows);

    inline constexpr const char* kBackSpriteName = "Tile_Back";
    inline constexpr const char* kStackSpriteName = "Tile_Stack";
    /// The sprite name of a tile in TextureLibrary.xml is this prefix and the notation of Tile.h: Tile_5p, Tile_Ew, Tile_F1.
    inline constexpr const char* kSpritePrefix = "Tile_";

    /// The source rectangle of a cell in the atlas, in pixels.
    [[nodiscard]] constexpr Rect CellRect(int cell) noexcept {
        return Rect(static_cast<float>(cell % kAtlasColumns * kCellWidth), static_cast<float>(cell / kAtlasColumns * kCellHeight), static_cast<float>(kCellWidth), static_cast<float>(kCellHeight));
    }

    /// The face of a tile; an invalid tile gives the back (it is never drawn face up).
    [[nodiscard]] constexpr Rect SourceRect(Tile tile) noexcept {
        return CellRect(tile.IsValid() ? tile.Id() : kBackCell);
    }

    /// The back of a tile.
    [[nodiscard]] constexpr Rect BackRect() noexcept {
        return CellRect(kBackCell);
    }

    /// The wall stack seen from above: the square of its cell, not the whole cell.
    [[nodiscard]] constexpr Rect StackRect() noexcept {
        const Rect cell = CellRect(kStackCell);
        return Rect(cell.x, cell.y, static_cast<float>(kStackSize), static_cast<float>(kStackSize));
    }

    /// The name of the sprite of a tile in TextureLibrary.xml (an invalid tile has none: the back's name).
    [[nodiscard]] inline eastl::string SpriteName(Tile tile) {
        if (!tile.IsValid()) {
            return kBackSpriteName;
        }
        return Format("{}{}", kSpritePrefix, ToString(tile));
    }

}  // namespace BECore::Mahjong
