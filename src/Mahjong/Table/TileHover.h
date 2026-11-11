#pragma once

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    /**
     * The name of a tile under the mouse pointer, as pure rules (no scene, no renderer): which slot lies under a point, which tile that
     * slot shows, where the plate with the name goes. The table component runs them each frame on the layout it has just built, so the
     * answer can never index a hand that changed after the layout.
     */

    /// The topmost slot of `layout` whose screen `rect` contains the point (x, y), in view units; null when none. Slots are drawn in layout
    /// order, so the last match is the one on top (squeezed hands, melds and discard rows overlap). A wall stack is never returned: it
    /// has no name. The raised rectangle of a selected tile is not special: the region stays the unraised slot, as `HitHandTile` does.
    [[nodiscard]] const TileSlot* SlotAt(const TableLayout& layout, float x, float y) noexcept;

    /// The tile `slot` shows face up, none for a back, a wall stack and an index outside the model. It is exactly the rule the table draws
    /// with (`drawSlot` of `MahjongTableComponent`): a hand tile of a seat whose hand is face up (`seatFaceUp`: `Faces::Shows(seat)`), a
    /// meld tile unless the meld is a concealed kong and the hand is face down, every bonus tile and every discard. The hover reads what
    /// the draw puts face up and nothing else, so it cannot peek.
    [[nodiscard]] std::optional<Tile> TileOfSlot(const HandModel& hand, const TileSlot& slot, bool seatFaceUp) noexcept;

    /// Where the name plate of a tile at `tile` goes in a view of `viewWidth` x `viewHeight`: below the tile, above it when it does not
    /// fit, kept inside the view. The engine's rule for the tooltip of a button (`UITooltip::Place`), applied to a tile.
    [[nodiscard]] Rect HoverPlateRect(const Rect& tile, float plateWidth, float plateHeight, float viewWidth, float viewHeight) noexcept;

}  // namespace BECore::Mahjong
