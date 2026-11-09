#pragma once

#include <BECore/Animation/Easing.h>
#include <EASTL/array.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    /**
     * The tiles that arrive somewhere new fly there: a discard to its place in the pile, a claimed tile into the new set, the tiles of a
     * kong, a draw from the wall into the hand. Plain C++, no scene, no renderer and no model object: the table component keeps a
     * `TableSnapshot` of the frame it drew last, makes one of the frame it draws now and `PlanFlights` says which slots of the new layout
     * did not exist and where the tile came from. A flight carries no tile identity of its own: the component draws the slot's own face
     * at an interpolated rectangle (`FlightRect`), so a flight shows nothing the slot itself does not.
     */

    /// How long one flight lasts, in seconds. Well inside the pace of the computers (`kComputerStepSeconds` in the table component, which
    /// `MahjongTableFlightTest` holds it to), so the pace does not change.
    inline constexpr float kTileFlightSeconds = 0.3f;
    /// The curve of a flight: fast at the start, settling into the place.
    inline constexpr EaseType kTileFlightEase = EaseType::CubicOut;

    /// What a slot of a layout is, without its rectangle: the same key in two layouts names the same thing on the table.
    struct SlotKey {
        SlotRole role = SlotRole::HandTile;
        Seat seat = 0;
        int index = 0;
        int part = 0;

        constexpr bool operator==(const SlotKey&) const noexcept = default;

        [[nodiscard]] static constexpr SlotKey Of(const TileSlot& slot) noexcept {
            return SlotKey{slot.role, slot.seat, slot.index, slot.part};
        }

        [[nodiscard]] constexpr bool Matches(const TileSlot& slot) const noexcept {
            return *this == Of(slot);
        }
    };

    /// What the table showed in one frame, as far as flights need it: the layout and the public facts (plus the viewer's own tiles, to
    /// find the slot a tile of the viewer leaves).
    struct TableSnapshot {
        TableLayout layout;
        /// The viewer's concealed tiles in order (slot `HandTile` `i` is tile `i`); empty when that hand is not face up.
        eastl::vector<Tile> viewerHand;
        /// `Discards()` by entry (the seat of an entry is that of its `DiscardTile` slot).
        eastl::vector<Tile> pile;
        eastl::array<eastl::vector<Meld>, kSeatCount> melds;
        int wallRemaining = 0;
        /// The seat that has just drawn and holds the tile, `kNoSeat` otherwise.
        Seat drawSeat = kNoSeat;
        /// The index of the drawn tile in the concealed tiles of `drawSeat`: the viewer's drawn tile, an opponent's last slot.
        int drawIndex = -1;
    };

    /// One slot of the new layout that flies, and the rectangle it flies from (in the view units of the old layout, which is the new
    /// one's: a plan is made only for the same viewer and view size).
    struct TileFlight {
        SlotKey key;
        Rect from;
    };

    /**
     * @brief The slots of `after` that are new since `before` and where each comes from.
     *
     * - A pile slot past the part of the pile both frames share (compared by seat and tile) flies from the discarder's hand.
     * - A meld tile whose (seat, meld, part) was not in `before` flies. The claimed tile (the slot with a `claimEdge`; its discarder is
     *   in `newMoves`, else in the meld) comes from the old pile slot when that entry left the pile, else, when the discard and the
     *   claim fall into one batch, from the discarder's hand. The other new tiles come from the owner's hand (a concealed kong: all four
     *   tiles; an upgraded kong: its one new tile).
     * - A draw (`after.drawSeat` is a seat, the wall lost tiles, and `before` shows the wall) flies the hand slot `drawIndex` from the
     *   last wall stack of `before`.
     * - A tile that leaves a hand comes from the slot of the first unused copy of it in the viewer's face-up hand, else from the middle
     *   hand slot of its seat in `before` (no identity: an opponent's hand is backs); no hand slot in `before`, no flight for that tile.
     *
     * Empty when `before` has no slots, the viewer or the view size differ, or nothing is new. In the order of the slots of `after`.
     */
    [[nodiscard]] eastl::vector<TileFlight> PlanFlights(const TableSnapshot& before, const TableSnapshot& after, eastl::span<const HandMove> newMoves);

    /// The rectangle of a flight at `progress` (0 = `from`, 1 = `to`, clamped; NaN is 0): x, y, width and height interpolated.
    [[nodiscard]] Rect FlightRect(const TileFlight& flight, const Rect& to, float progress);

}  // namespace BECore::Mahjong
