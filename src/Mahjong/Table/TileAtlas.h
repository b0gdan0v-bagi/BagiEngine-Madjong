#pragma once

#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /**
     * The tile atlas: one PNG of cells; the cell of a tile is its id (Tile.h), the back the cell after the last id and the wall stack
     * (two backs side by side seen from above) the cell after the back; every further theme has a back and a stack of its own colours in
     * the two cells after the previous pair (`kTileBackSets`). The numbers are those of tools/make_tiles.py, which draws the atlas and
     * writes config/TextureLibrary.xml; `TileSpritesTest` compares them with the XML and the PNG.
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
    /// The cell after the back: a wall stack seen from above, a square at the top left corner of the cell, the rest transparent.
    inline constexpr int kStackCell = kBackCell + 1;
    inline constexpr int kStackSize = 96;
    static_assert(kStackSize <= kCellWidth && kStackSize <= kCellHeight);
    /// The back and the stack of a theme: the cells of its pair in the atlas.
    struct TileBackSet {
        const char* theme;
        int backCell;
        int stackCell;
    };
    /// One set per theme with its own back; the first is the classic one and answers for a theme with no row.
    inline constexpr TileBackSet kTileBackSets[] = {{"classic", kBackCell, kStackCell}, {"navy", kBackCell + 2, kStackCell + 2}};
    inline constexpr int kTileBackSetCount = static_cast<int>(sizeof(kTileBackSets) / sizeof(kTileBackSets[0]));
    /// Cells drawn in the atlas: every tile id and a back and a stack per set.
    inline constexpr int kDrawnCells = kTileIdCount + 2 * kTileBackSetCount;
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

    /// The set of a theme id; the classic one for an id with no row.
    [[nodiscard]] constexpr const TileBackSet& BackSetOf(StringView theme) noexcept {
        for (const TileBackSet& set : kTileBackSets) {
            if (theme == StringView(set.theme)) {
                return set;
            }
        }
        return kTileBackSets[0];
    }

    /// The back of a tile in the colours of a set.
    [[nodiscard]] constexpr Rect BackRect(const TileBackSet& set) noexcept {
        return CellRect(set.backCell);
    }

    /// The back of a tile, classic.
    [[nodiscard]] constexpr Rect BackRect() noexcept {
        return BackRect(kTileBackSets[0]);
    }

    /// The wall stack seen from above: the square of its cell, not the whole cell.
    [[nodiscard]] constexpr Rect StackRect(const TileBackSet& set) noexcept {
        const Rect cell = CellRect(set.stackCell);
        return Rect(cell.x, cell.y, static_cast<float>(kStackSize), static_cast<float>(kStackSize));
    }

    /// The wall stack, classic.
    [[nodiscard]] constexpr Rect StackRect() noexcept {
        return StackRect(kTileBackSets[0]);
    }

    /// The sprite names of a set in TextureLibrary.xml: `Tile_Back` / `Tile_Stack` for the first, `Tile_Back_<theme>` / `Tile_Stack_<theme>` for the others.
    [[nodiscard]] inline String BackSpriteName(const TileBackSet& set) {
        return set.backCell == kBackCell ? String(kBackSpriteName) : Format("{}_{}", kBackSpriteName, set.theme);
    }
    [[nodiscard]] inline String StackSpriteName(const TileBackSet& set) {
        return set.backCell == kBackCell ? String(kStackSpriteName) : Format("{}_{}", kStackSpriteName, set.theme);
    }

    /// The name of the sprite of a tile in TextureLibrary.xml (an invalid tile has none: the back's name).
    [[nodiscard]] inline String SpriteName(Tile tile) {
        if (!tile.IsValid()) {
            return kBackSpriteName;
        }
        return Format("{}{}", kSpritePrefix, ToString(tile));
    }

}  // namespace BECore::Mahjong
