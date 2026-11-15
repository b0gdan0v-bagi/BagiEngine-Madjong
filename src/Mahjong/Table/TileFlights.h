#pragma once

#include <BECore/Animation/Easing.h>
#include <EASTL/array.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    /**
     * The tiles that arrive somewhere new fly there: a discard to the pile, a claimed tile into the new set, the tiles of a kong, a draw
     * from the wall into the hand. The table component keeps a `TableSnapshot` of the frame it drew last and makes one of the frame it
     * draws now; `PlanFlights` says which slots are new and where the tile came from. A flight carries no tile identity: the component
     * draws the slot's own face at an interpolated rectangle (`FlightRect`) and turn (`FlightPlacement`), so a flight shows nothing the
     * slot itself does not (no peeking).
     */

    /// Well inside the pace of the computers (`kComputerStepSeconds` in the table component), so a flight never changes the pace.
    inline constexpr float kTileFlightSeconds = 0.3f;
    inline constexpr EaseType kTileFlightEase = EaseType::CubicOut;

    /// The tile at `part` of a meld as laid. The one definition: the component draws it and the planner names the tile that flies by it.
    [[nodiscard]] Tile MeldTileAt(const Meld& meld, int part) noexcept;

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

    /// What the table showed in one frame, as far as flights need it: the layout, the public facts and the concealed tiles of the hands
    /// that were face up (to find the slot a tile leaves).
    struct TableSnapshot {
        TableLayout layout;
        /// Per seat its concealed tiles in order; empty when that hand was not face up (a row of backs).
        Array<Vector<Tile>, kSeatCount> openHand;
        /// `Discards()` by entry (the seat of an entry is that of its `DiscardTile` slot).
        Vector<Tile> pile;
        Array<Vector<Meld>, kSeatCount> melds;
        int wallRemaining = 0;
        /// The seat that has just drawn and holds the tile, `kNoSeat` otherwise.
        Seat drawSeat = kNoSeat;
        /// The index of the drawn tile in the concealed tiles of `drawSeat`: the viewer's drawn tile, an opponent's last slot.
        int drawIndex = -1;
        /// The seat that won on a discard and holds the winning tile, `kNoSeat` otherwise (a self-draw or a robbed kong has no pile slot
        /// to fly from). Never set together with `drawSeat`.
        Seat winSeat = kNoSeat;
        /// The index of the winning tile in the sorted concealed tiles of `winSeat`: the last copy of that kind.
        int winIndex = -1;
    };

    /// One slot of the new layout that flies, and the rectangle it flies from; a plan is made only for the same viewer and view size, so
    /// the two layouts share view units.
    struct TileFlight {
        SlotKey key;
        Rect from;
        /// The turn of the slot the tile leaves.
        float fromRotation = 0.0f;
    };

    /**
     * @brief The slots of `after` that are new since `before` and where each comes from.
     *
     * - A pile slot past the part of the pile both frames share flies from the discarder's hand.
     * - A new meld tile flies. The claimed tile (the slot with a `claimEdge`) comes from the old pile slot when that entry left the pile,
     *   else, when the discard and the claim fall into one batch, from the discarder's hand (`newMoves` names the discarder). The others
     *   come from the owner's hand.
     * - A draw flies the hand slot `drawIndex` from the last wall stack of `before`.
     * - A mahjong on a discard flies the winner's hand slot `winIndex` from the last pile slot of `before` when that entry is gone from
     *   `after`'s pile; with the discard and the win in one batch there is no flight.
     * - A tile that leaves a hand comes from the slot of the first unused copy of it in the face-up hand (`openHand`), else from the middle
     *   hand slot of its seat (a hand of backs has no identity); no hand slot in `before`, no flight.
     *
     * Empty when `before` has no slots, the viewer or the view size differ, or nothing is new. In the order of the slots of `after`.
     */
    [[nodiscard]] Vector<TileFlight> PlanFlights(const TableSnapshot& before, const TableSnapshot& after, Span<const HandMove> newMoves);

    /// How a tile is handed to a draw: the unturned rectangle, the turn in degrees clockwise and the pivot (`DrawTransform`).
    struct TilePlacement {
        Rect upright;
        float rotation = 0.0f;
        float pivotX = 0.0f;
        float pivotY = 0.0f;
    };

    /// The placement of a tile that lies in `screenRect` turned by `rotation` (a slot as laid).
    [[nodiscard]] TilePlacement PlacementOf(const Rect& screenRect, float rotation) noexcept;

    /**
     * @brief How a flying tile is drawn at `progress` (0 = the source slot, 1 = `to`, clamped; NaN is 0).
     *
     * The centre follows `FlightRect`; the size runs from the unturned size of the source to that of `to`; the turn runs from
     * `fromRotation` to `to.rotation` the shorter way round (0 and 270 meet through 315). At 0 the placement is that of the source slot,
     * at 1 exactly that of `to`: no jump at the landing.
     */
    [[nodiscard]] TilePlacement FlightPlacement(const TileFlight& flight, const TileSlot& to, float progress);

    /// The rectangle of a flight at `progress` (0 = `from`, 1 = `to`, clamped; NaN is 0): x, y, width and height interpolated.
    [[nodiscard]] Rect FlightRect(const TileFlight& flight, const Rect& to, float progress);

}  // namespace BECore::Mahjong
