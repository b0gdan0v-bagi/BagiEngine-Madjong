#include "TableLayout.h"

namespace BECore::Mahjong {

    namespace {

        constexpr float kEdge = 8.0f;
        /// The space between neighbouring zones of one seat, and between the zones of the side seats.
        constexpr float kZoneGap = 4.0f;
        constexpr float kBlockGap = 16.0f;
        constexpr int kMeldTilesPerKong = 4;
        constexpr int kMeldTilesPerSet = 3;

        constexpr float Centered(float total, float span) noexcept {
            return (total - span) / 2.0f;
        }

        /// Tiles of one zone laid in a line (a row for the bottom and top seats, a column for the side seats).
        struct Strip {
            float x = 0.0f;
            float y = 0.0f;
            float tileW = 0.0f;
            float tileH = 0.0f;
            bool vertical = false;
            /// The most the line may span along its axis; a longer line is squeezed (tiles overlap).
            float capacity = 0.0f;
        };

        struct SeatGeometry {
            Strip hand;
            Strip melds;
            Strip bonus;
            /// The top-left corner of the discard block.
            float discardX = 0.0f;
            float discardY = 0.0f;
        };

        constexpr float StripCapacity(float tileLength, int tiles, int gaps) noexcept {
            return tileLength * static_cast<float>(tiles) + kMeldGap * static_cast<float>(gaps);
        }

        constexpr float kDiscardBlockW = kDiscardTileW * kDiscardsPerRow;
        constexpr float kDiscardBlockH = kDiscardTileH * kDiscardRowCapacity;

        // The bottom row: the hand, then the melds; the bonus tiles above the hand. Starts clear of the left column of melds.
        constexpr float kViewerRowX = 72.0f;
        constexpr float kViewerRowGap = 20.0f;
        constexpr float kViewerHandY = kViewHeight - kEdge - kViewerHandTileH;
        constexpr float kViewerBonusY = kViewerHandY - kViewerBonusTileH;
        constexpr float kViewerHandSpan = kViewerHandTileW * kMaxConcealedTiles;
        constexpr float kViewerMeldCapacity = StripCapacity(kViewerMeldTileW, kMeldTilesPerKong* kMaxMelds, kMaxMelds - 1);
        constexpr float kBottomDiscardY = 452.0f;

        // The side columns, from the edge of the view inwards: the hand, the melds, the bonus tiles, the discard block.
        constexpr float kSideHandSpan = kOpponentHandTileH * kMaxConcealedTiles;
        constexpr float kSideMeldCapacity = StripCapacity(kOpponentMeldTileH, kMeldTilesPerKong* kMaxMelds, kMaxMelds - 1);
        constexpr float kSideBonusCapacity = kOpponentBonusTileH * kMaxBonusTiles;
        // The top seat's bonus tiles lie in a row, so they span the tile width, not the height.
        constexpr float kTopBonusCapacity = kOpponentBonusTileW * kMaxBonusTiles;
        constexpr float kSideHandY = Centered(kViewHeight, kSideHandSpan);
        constexpr float kSideMeldY = Centered(kViewHeight, kSideMeldCapacity);
        constexpr float kSideBonusY = kSideHandY;
        constexpr float kSideDiscardY = Centered(kViewHeight, kDiscardBlockH);
        constexpr float kLeftHandX = kEdge;
        constexpr float kLeftMeldX = kLeftHandX + kOpponentHandTileW + kZoneGap;
        constexpr float kLeftBonusX = kLeftMeldX + kOpponentMeldTileW + kZoneGap;
        constexpr float kLeftDiscardX = kLeftBonusX + kOpponentBonusTileW + kBlockGap;
        constexpr float kRightHandX = kViewWidth - kEdge - kOpponentHandTileW;
        constexpr float kRightMeldX = kRightHandX - kZoneGap - kOpponentMeldTileW;
        constexpr float kRightBonusX = kRightMeldX - kZoneGap - kOpponentBonusTileW;
        constexpr float kRightDiscardX = kRightBonusX - kBlockGap - kDiscardBlockW;

        // The top row: the hand with the bonus tiles beside it, the melds below, then the discard block.
        constexpr float kTopHandSpan = kOpponentHandTileW * kMaxConcealedTiles;
        constexpr float kTopHandX = Centered(kViewWidth, kTopHandSpan);
        constexpr float kTopMeldY = kEdge + kOpponentHandTileH + kZoneGap;
        constexpr float kTopMeldCapacity = StripCapacity(kOpponentMeldTileW, kMeldTilesPerKong* kMaxMelds, kMaxMelds - 1);
        constexpr float kTopDiscardY = 92.0f;

        constexpr float kDiscardBlockX = Centered(kViewWidth, kDiscardBlockW);

        // The wall: a rectangle ring of cells in the middle, centred between the top and the bottom discard blocks (so the plates that
        // the table paints under the three keep the same gap).
        constexpr float kWallRingW = kWallStackSize * kWallRingColumns;
        constexpr float kWallRingH = kWallStackSize * kWallRingRows;
        constexpr float kWallRingX = Centered(kViewWidth, kWallRingW);
        constexpr float kWallRingY = (kTopDiscardY + kDiscardBlockH + kBottomDiscardY - kWallRingH) / 2.0f;

        // The layout above must fit the zones it names: a change to a size that breaks it fails to compile.
        static_assert(kViewerRowX + kViewerHandSpan + kViewerRowGap + kViewerMeldCapacity <= kViewWidth - kEdge);
        // The drawn tile's gap is in the hand's strip: 14 tiles and the gap end before the melds start.
        static_assert(kViewerRowX + kViewerHandSpan + kDrawnTileGap <= kViewerRowX + kViewerHandSpan + kViewerRowGap);
        static_assert(kSideMeldY + kSideMeldCapacity <= kViewHeight - kEdge);
        static_assert(kTopDiscardY > kTopMeldY + kOpponentMeldTileH);
        static_assert(kTopDiscardY + kDiscardBlockH < kWallRingY);
        static_assert(kWallRingY + kWallRingH < kBottomDiscardY);
        static_assert(kBottomDiscardY + kDiscardBlockH < kViewerBonusY);
        static_assert(kLeftDiscardX + kDiscardBlockW < kWallRingX);
        static_assert(kRightDiscardX > kWallRingX + kWallRingW);
        static_assert(kLeftDiscardX > kLeftBonusX + kOpponentBonusTileW);
        static_assert(kSideBonusCapacity <= kSideHandSpan);

        SeatGeometry GeometryAt(int position) {
            SeatGeometry geometry;
            switch (position) {
                case 0:
                    geometry.hand = {kViewerRowX, kViewerHandY, kViewerHandTileW, kViewerHandTileH, false, kViewerHandSpan + kDrawnTileGap};
                    geometry.melds = {kViewerRowX + kViewerHandSpan + kViewerRowGap, kViewHeight - kEdge - kViewerMeldTileH, kViewerMeldTileW, kViewerMeldTileH, false, kViewerMeldCapacity};
                    geometry.bonus = {kViewerRowX, kViewerBonusY, kViewerBonusTileW, kViewerBonusTileH, false, kViewerBonusTileW * kMaxBonusTiles};
                    geometry.discardX = kDiscardBlockX;
                    geometry.discardY = kBottomDiscardY;
                    break;
                case 1:
                    geometry.hand = {kRightHandX, kSideHandY, kOpponentHandTileW, kOpponentHandTileH, true, kSideHandSpan};
                    geometry.melds = {kRightMeldX, kSideMeldY, kOpponentMeldTileW, kOpponentMeldTileH, true, kSideMeldCapacity};
                    geometry.bonus = {kRightBonusX, kSideBonusY, kOpponentBonusTileW, kOpponentBonusTileH, true, kSideBonusCapacity};
                    geometry.discardX = kRightDiscardX;
                    geometry.discardY = kSideDiscardY;
                    break;
                case 2:
                    geometry.hand = {kTopHandX, kEdge, kOpponentHandTileW, kOpponentHandTileH, false, kTopHandSpan};
                    geometry.melds = {Centered(kViewWidth, kTopMeldCapacity), kTopMeldY, kOpponentMeldTileW, kOpponentMeldTileH, false, kTopMeldCapacity};
                    geometry.bonus = {
                        kTopHandX + kTopHandSpan + kBlockGap, kEdge + Centered(kOpponentHandTileH, kOpponentBonusTileH), kOpponentBonusTileW, kOpponentBonusTileH, false, kTopBonusCapacity};
                    geometry.discardX = kDiscardBlockX;
                    geometry.discardY = kTopDiscardY;
                    break;
                default:
                    geometry.hand = {kLeftHandX, kSideHandY, kOpponentHandTileW, kOpponentHandTileH, true, kSideHandSpan};
                    geometry.melds = {kLeftMeldX, kSideMeldY, kOpponentMeldTileW, kOpponentMeldTileH, true, kSideMeldCapacity};
                    geometry.bonus = {kLeftBonusX, kSideBonusY, kOpponentBonusTileW, kOpponentBonusTileH, true, kSideBonusCapacity};
                    geometry.discardX = kLeftDiscardX;
                    geometry.discardY = kSideDiscardY;
                    break;
            }
            return geometry;
        }

        /// The offsets along the strip's axis of `groups` of tiles (the gap `gap` between two groups), squeezed to the capacity.
        eastl::vector<float> StripOffsets(const Strip& strip, const eastl::vector<int>& groups, float gap = kMeldGap) {
            const float length = strip.vertical ? strip.tileH : strip.tileW;
            eastl::vector<float> offsets;
            float position = 0.0f;
            for (size_t group = 0; group < groups.size(); ++group) {
                if (group > 0 && !offsets.empty()) {
                    position += gap;
                }
                for (int tile = 0; tile < groups[group]; ++tile) {
                    offsets.push_back(position);
                    position += length;
                }
            }
            if (position > strip.capacity && position > length) {
                const float squeeze = (strip.capacity - length) / (position - length);
                for (float& offset : offsets) {
                    offset *= squeeze;
                }
            }
            return offsets;
        }

        Rect StripRect(const Strip& strip, float offset) {
            return Rect(strip.x + (strip.vertical ? 0.0f : offset), strip.y + (strip.vertical ? offset : 0.0f), strip.tileW, strip.tileH);
        }

        int ClampCount(int count) noexcept {
            return count < 0 ? 0 : count;
        }

        int MeldTileCount(const Meld& meld) noexcept {
            return meld.kind == MeldKind::Kong ? kMeldTilesPerKong : kMeldTilesPerSet;
        }

        /// Where the layout is built (design units) and how it is mapped into the view.
        struct ViewFit {
            float scale = 1.0f;
            float offsetX = 0.0f;
            float offsetY = 0.0f;

            [[nodiscard]] Rect Map(const Rect& rect) const {
                return Rect(offsetX + rect.x * scale, offsetY + rect.y * scale, rect.w * scale, rect.h * scale);
            }
        };

        ViewFit FitView(float viewWidth, float viewHeight) {
            ViewFit fit;
            fit.scale = eastl::max(0.0f, eastl::min(viewWidth / kViewWidth, viewHeight / kViewHeight));
            fit.offsetX = Centered(viewWidth, kViewWidth * fit.scale);
            fit.offsetY = Centered(viewHeight, kViewHeight * fit.scale);
            return fit;
        }

        void AddSlot(TableLayout& layout, const ViewFit& fit, const Rect& rect, SlotRole role, Seat seat, int index, int part = 0, Edge claimEdge = Edge::None) {
            layout.slots.push_back(TileSlot{fit.Map(rect), role, seat, index, part, claimEdge});
        }

        /// The hand in input order; with `drawnIndex` in range that tile lies at the right end after a gap, the others closing up.
        void AddHand(TableLayout& layout, const ViewFit& fit, const SeatGeometry& geometry, Seat seat, int count, int drawnIndex) {
            const bool apart = drawnIndex >= 0 && drawnIndex < count;
            const eastl::vector<float> offsets = apart ? StripOffsets(geometry.hand, {count - 1, 1}, kDrawnTileGap) : StripOffsets(geometry.hand, {count});
            for (int index = 0; index < count; ++index) {
                // the tiles before the drawn one keep their place, the ones after it move up one; the drawn tile is the last offset
                const int place = !apart || index < drawnIndex ? index : (index == drawnIndex ? count - 1 : index - 1);
                AddSlot(layout, fit, StripRect(geometry.hand, offsets[place]), SlotRole::HandTile, seat, index);
            }
        }

        /// The edge of a meld tile that faces the seat the tile came from, for an owner at screen position `position`; `offset` is
        /// `Offset(owner, from)`: 1 the next seat, 2 across, 3 the previous seat.
        Edge FacingEdge(int position, int offset) noexcept {
            // rows: the owner at the bottom, right, top, left; columns: the source is the next seat, across, the previous seat
            constexpr Edge kEdges[kSeatCount][3] = {
                {Edge::Right, Edge::Top, Edge::Left},
                {Edge::Top, Edge::Left, Edge::Bottom},
                {Edge::Left, Edge::Bottom, Edge::Right},
                {Edge::Bottom, Edge::Right, Edge::Top},
            };
            return position >= 0 && position < kSeatCount && offset >= 1 && offset < kSeatCount ? kEdges[position][offset - 1] : Edge::None;
        }

        /// The part of an open meld that is the claimed tile, -1 for a meld that has none.
        int ClaimedPart(const Meld& meld, Seat owner) noexcept {
            if (meld.concealed || !meld.fromDiscard || !IsSeat(meld.from) || meld.from == owner) {
                return -1;
            }
            if (meld.kind == MeldKind::Chow) {
                const int part = meld.claimed.Rank() - meld.first.Rank();
                return meld.claimed.IsSuit() && part >= 0 && part < kMeldTilesPerSet ? part : -1;
            }
            // a pung or kong: the claimed tile lies on the side of the seat it came from: the previous seat left, across middle, next right
            return kSeatCount - Offset(owner, meld.from) - 1;
        }

        void AddMelds(TableLayout& layout, const ViewFit& fit, const SeatGeometry& geometry, Seat seat, int position, const eastl::vector<Meld>& melds) {
            eastl::vector<int> groups;
            for (const Meld& meld : melds) {
                groups.push_back(MeldTileCount(meld));
            }
            const eastl::vector<float> offsets = StripOffsets(geometry.melds, groups);
            size_t next = 0;
            for (size_t meld = 0; meld < groups.size(); ++meld) {
                const int claimedPart = ClaimedPart(melds[meld], seat);
                const Edge claimEdge = claimedPart >= 0 ? FacingEdge(position, Offset(seat, melds[meld].from)) : Edge::None;
                for (int part = 0; part < groups[meld]; ++part) {
                    AddSlot(layout, fit, StripRect(geometry.melds, offsets[next++]), SlotRole::MeldTile, seat, static_cast<int>(meld), part, part == claimedPart ? claimEdge : Edge::None);
                }
            }
        }

        void AddBonus(TableLayout& layout, const ViewFit& fit, const SeatGeometry& geometry, Seat seat, int count) {
            const eastl::vector<float> offsets = StripOffsets(geometry.bonus, {count});
            for (int index = 0; index < count; ++index) {
                AddSlot(layout, fit, StripRect(geometry.bonus, offsets[index]), SlotRole::BonusTile, seat, index);
            }
        }

        /// The discards of the seats, in the pile's order; each seat counts its own tiles to find the cell in its block.
        void AddDiscards(TableLayout& layout, const ViewFit& fit, const eastl::array<SeatGeometry, kSeatCount>& geometries, const TableInput& input) {
            eastl::array<int, kSeatCount> pileOfSeat{};
            for (const Seat seat : input.discardSeats) {
                if (IsSeat(seat)) {
                    ++pileOfSeat[seat];
                }
            }
            // A pile of more rows than the block holds squeezes its rows together; the pitch is the same for the whole pile of a seat.
            eastl::array<float, kSeatCount> rowPitchOfSeat{};
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const int rows = (pileOfSeat[seat] + kDiscardsPerRow - 1) / kDiscardsPerRow;
                rowPitchOfSeat[seat] = rows <= kDiscardRowCapacity ? kDiscardTileH : kDiscardTileH * (kDiscardRowCapacity - 1) / static_cast<float>(rows - 1);
            }
            eastl::array<int, kSeatCount> placed{};
            for (size_t entry = 0; entry < input.discardSeats.size(); ++entry) {
                const Seat seat = input.discardSeats[entry];
                if (!IsSeat(seat)) {
                    continue;
                }
                const SeatGeometry& geometry = geometries[SeatPosition(layout.viewer, seat)];
                const int cell = placed[seat]++;
                const Rect rect(geometry.discardX + static_cast<float>(cell % kDiscardsPerRow) * kDiscardTileW, geometry.discardY + static_cast<float>(cell / kDiscardsPerRow) * rowPitchOfSeat[seat],
                                kDiscardTileW, kDiscardTileH);
                AddSlot(layout, fit, rect, SlotRole::DiscardTile, seat, static_cast<int>(entry));
            }
        }

        /// The cell `n` along the ring, clockwise from the top left corner: the top side, the right, the bottom, the left.
        Rect WallCell(int n) {
            constexpr int kRight = kWallRingColumns - 1;
            constexpr int kBottom = kWallRingRows - 1;
            int column = 0;
            int row = 0;
            if (n < kRight) {
                column = n;
            } else if (n < kRight + kBottom) {
                column = kRight;
                row = n - kRight;
            } else if (n < 2 * kRight + kBottom) {
                column = kRight - (n - kRight - kBottom);
                row = kBottom;
            } else {
                row = kBottom - (n - 2 * kRight - kBottom);
            }
            return Rect(kWallRingX + static_cast<float>(column) * kWallStackSize, kWallRingY + static_cast<float>(row) * kWallStackSize, kWallStackSize, kWallStackSize);
        }

    }  // namespace

    int WallStackCount(int wallRemaining) noexcept {
        const int stacks = (ClampCount(wallRemaining) + 1) / 2;
        return stacks < kWallRingCells ? stacks : kWallRingCells;
    }

    TableLayout MakeTableLayout(const TableInput& input, float viewWidth, float viewHeight) {
        TableLayout layout;
        layout.viewer = IsSeat(input.viewer) ? input.viewer : 0;
        layout.viewWidth = viewWidth;
        layout.viewHeight = viewHeight;
        const ViewFit fit = FitView(viewWidth, viewHeight);

        eastl::array<SeatGeometry, kSeatCount> geometries;
        for (int position = 0; position < kSeatCount; ++position) {
            geometries[position] = GeometryAt(position);
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const SeatGeometry& geometry = geometries[SeatPosition(layout.viewer, seat)];
            layout.zones.discards[seat] = fit.Map(Rect(geometry.discardX, geometry.discardY, kDiscardBlockW, kDiscardBlockH));
            AddHand(layout, fit, geometry, seat, ClampCount(input.concealedCount[seat]), seat == layout.viewer ? input.drawnIndex : -1);
            AddMelds(layout, fit, geometry, seat, SeatPosition(layout.viewer, seat), input.melds[seat]);
            AddBonus(layout, fit, geometry, seat, ClampCount(input.bonusCount[seat]));
        }
        AddDiscards(layout, fit, geometries, input);
        layout.zones.wall = fit.Map(Rect(kWallRingX, kWallRingY, kWallRingW, kWallRingH));
        const int stacks = WallStackCount(input.wallRemaining);
        for (int stack = 0; stack < stacks; ++stack) {
            AddSlot(layout, fit, WallCell(stack), SlotRole::WallStack, 0, stack);
        }
        return layout;
    }

    Rect BoundsOf(const TableLayout& layout, SlotRole role, Seat seat) {
        bool any = false;
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        for (const TileSlot& slot : layout.slots) {
            if (slot.role != role || (role != SlotRole::WallStack && slot.seat != seat)) {
                continue;
            }
            const float slotRight = slot.rect.x + slot.rect.w;
            const float slotBottom = slot.rect.y + slot.rect.h;
            left = any ? eastl::min(left, slot.rect.x) : slot.rect.x;
            top = any ? eastl::min(top, slot.rect.y) : slot.rect.y;
            right = any ? eastl::max(right, slotRight) : slotRight;
            bottom = any ? eastl::max(bottom, slotBottom) : slotBottom;
            any = true;
        }
        return any ? Rect(left, top, right - left, bottom - top) : Rect();
    }

    Rect BoundsOfMeld(const TableLayout& layout, Seat seat, int meldIndex) {
        bool any = false;
        float left = 0.0f;
        float top = 0.0f;
        float right = 0.0f;
        float bottom = 0.0f;
        for (const TileSlot& slot : layout.slots) {
            if (slot.role != SlotRole::MeldTile || slot.seat != seat || slot.index != meldIndex) {
                continue;
            }
            left = any ? eastl::min(left, slot.rect.x) : slot.rect.x;
            top = any ? eastl::min(top, slot.rect.y) : slot.rect.y;
            right = any ? eastl::max(right, slot.rect.x + slot.rect.w) : slot.rect.x + slot.rect.w;
            bottom = any ? eastl::max(bottom, slot.rect.y + slot.rect.h) : slot.rect.y + slot.rect.h;
            any = true;
        }
        return any ? Rect(left, top, right - left, bottom - top) : Rect();
    }

    std::optional<int> HitHandTile(const TableLayout& layout, float x, float y) {
        for (auto slot = layout.slots.rbegin(); slot != layout.slots.rend(); ++slot) {
            if (slot->role != SlotRole::HandTile || slot->seat != layout.viewer) {
                continue;
            }
            const Rect& rect = slot->rect;
            if (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) {
                return slot->index;
            }
        }
        return std::nullopt;
    }

}  // namespace BECore::Mahjong
