#include "TileHover.h"

#include <BECore/UI/UITooltip.h>
#include <Mahjong/Table/TileFlights.h>

namespace BECore::Mahjong {

    namespace {

        [[nodiscard]] bool Contains(const Rect& rect, float x, float y) noexcept {
            return x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h;
        }

        [[nodiscard]] bool InRange(int index, size_t count) noexcept {
            return index >= 0 && static_cast<size_t>(index) < count;
        }

    }  // namespace

    const TileSlot* SlotAt(const TableLayout& layout, float x, float y) noexcept {
        for (auto it = layout.slots.rbegin(); it != layout.slots.rend(); ++it) {
            if (it->role != SlotRole::WallStack && Contains(it->rect, x, y)) {
                return &*it;
            }
        }
        return nullptr;
    }

    std::optional<Tile> TileOfSlot(const HandModel& hand, const TileSlot& slot, bool seatFaceUp) noexcept {
        if (slot.role == SlotRole::DiscardTile) {
            const eastl::vector<DiscardEntry>& pile = hand.Discards();
            return InRange(slot.index, pile.size()) ? std::optional<Tile>(pile[slot.index].tile) : std::nullopt;
        }
        if (slot.role == SlotRole::WallStack || !IsSeat(slot.seat)) {
            return std::nullopt;
        }
        const PlayerHand& held = hand.GetHand(slot.seat);
        switch (slot.role) {
            case SlotRole::HandTile:
                return seatFaceUp && InRange(slot.index, held.concealed.size()) ? std::optional<Tile>(held.concealed[slot.index]) : std::nullopt;
            case SlotRole::MeldTile: {
                if (!InRange(slot.index, held.melds.size())) {
                    return std::nullopt;
                }
                const Meld& meld = held.melds[slot.index];
                return !meld.concealed || seatFaceUp ? std::optional<Tile>(MeldTileAt(meld, slot.part)) : std::nullopt;
            }
            case SlotRole::BonusTile:
                return InRange(slot.index, held.bonus.size()) ? std::optional<Tile>(held.bonus[slot.index]) : std::nullopt;
            default:
                return std::nullopt;
        }
    }

    Rect HoverPlateRect(const Rect& tile, float plateWidth, float plateHeight, float viewWidth, float viewHeight) noexcept {
        return UITooltip::Place(tile, Vec2{plateWidth, plateHeight}, Rect{0.0f, 0.0f, viewWidth, viewHeight});
    }

}  // namespace BECore::Mahjong
