#include "TableLayout.h"

#include <Mahjong/Table/UiPalette.h>
#include <cmath>

namespace BECore::Mahjong {

    namespace {

        constexpr float kEdge = 8.0f;
        /// The space between neighbouring zones of one seat, and between the zones of the side seats.
        constexpr float kZoneGap = 4.0f;
        constexpr float kBlockGap = 16.0f;
        /// The space between the bonus column of a side seat and its discard block.
        constexpr float kSideBlockGap = 8.0f;
        constexpr int kMeldTilesPerKong = 4;
        constexpr int kMeldTilesPerSet = 3;

        constexpr float Centered(float total, float span) noexcept {
            return (total - span) / 2.0f;
        }

        /// Tiles of one zone laid in a line (a row for the bottom and top seats, a column for the side seats). `tileW` x `tileH` is the
        /// size of the slot on the screen: a side seat's tiles are turned, so it is the upright size swapped.
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
            /// The turn of every tile of the seat (degrees clockwise), and the size of the discard block as it is laid.
            float rotation = 0.0f;
            float discardW = 0.0f;
            float discardH = 0.0f;
        };

        constexpr float StripCapacity(float tileLength, int tiles, int gaps) noexcept {
            return tileLength * static_cast<float>(tiles) + kMeldGap * static_cast<float>(gaps);
        }

        constexpr float kDiscardBlockW = kDiscardTileW * kDiscardsPerRow;
        constexpr float kDiscardBlockH = kDiscardTileH * kDiscardRowCapacity;
        // A side seat's block is the same block turned: a row of six runs along the screen's vertical, so the block is as high as six
        // tiles are wide and as wide as five tiles are high.
        constexpr float kSideDiscardBlockW = kDiscardBlockH;
        constexpr float kSideDiscardBlockH = kDiscardBlockW;

        // The bottom row: the hand, then the melds; the bonus tiles above the hand. Starts clear of the left column of melds.
        constexpr float kViewerRowX = 72.0f;
        constexpr float kViewerRowGap = 20.0f;
        constexpr float kViewerHandY = kViewHeight - kEdge - kViewerHandTileH;
        constexpr float kViewerBonusY = kViewerHandY - kViewerBonusTileH;
        constexpr float kViewerHandSpan = kViewerHandTileW * kMaxConcealedTiles;
        constexpr float kViewerMeldCapacity = StripCapacity(kViewerMeldTileW, kMeldTilesPerKong* kMaxMelds, kMaxMelds - 1);
        constexpr float kBottomDiscardY = 452.0f;

        // The side columns, from the edge of the view inwards: the hand, the melds, the bonus tiles, the discard block. The tiles are
        // turned, so a column is as wide as its tiles are high (kOpponent*TileH) and its tiles pitch by their width (kOpponent*TileW).
        constexpr float kSideHandSpan = kSideHandTileW * kMaxConcealedTiles;
        constexpr float kSideMeldCapacity = StripCapacity(kOpponentMeldTileW, kMeldTilesPerKong* kMaxMelds, kMaxMelds - 1);
        constexpr float kSideBonusCapacity = kOpponentBonusTileW * kMaxBonusTiles;
        // The top seat's bonus tiles lie in a row as long as the side seats' columns.
        constexpr float kTopBonusCapacity = kSideBonusCapacity;
        constexpr float kSideHandY = Centered(kViewHeight, kSideHandSpan);
        constexpr float kSideMeldY = Centered(kViewHeight, kSideMeldCapacity);
        constexpr float kSideBonusY = kSideHandY;
        constexpr float kSideDiscardY = Centered(kViewHeight, kSideDiscardBlockH);
        constexpr float kLeftHandX = kEdge;
        constexpr float kLeftMeldX = kLeftHandX + kSideHandTileH + kZoneGap;
        constexpr float kLeftBonusX = kLeftMeldX + kOpponentMeldTileH + kZoneGap;
        constexpr float kLeftDiscardX = kLeftBonusX + kOpponentBonusTileH + kSideBlockGap;
        constexpr float kRightHandX = kViewWidth - kEdge - kSideHandTileH;
        constexpr float kRightMeldX = kRightHandX - kZoneGap - kOpponentMeldTileH;
        constexpr float kRightBonusX = kRightMeldX - kZoneGap - kOpponentBonusTileH;
        constexpr float kRightDiscardX = kRightBonusX - kSideBlockGap - kSideDiscardBlockW;

        // The top row: the hand with the bonus tiles beside it, the melds below, then the discard block.
        constexpr float kTopHandSpan = kOpponentHandTileW * kMaxConcealedTiles;
        constexpr float kTopHandX = Centered(kViewWidth, kTopHandSpan);
        constexpr float kTopMeldY = kEdge + kOpponentHandTileH + kZoneGap;
        constexpr float kTopMeldCapacity = StripCapacity(kOpponentMeldTileW, kMeldTilesPerKong* kMaxMelds, kMaxMelds - 1);
        constexpr float kTopDiscardY = 92.0f;

        constexpr float kDiscardBlockX = Centered(kViewWidth, kDiscardBlockW);

        // The wall: a rectangle ring of cells in the middle, centred between the top and the bottom discard blocks (so the gap to the
        // top and to the bottom discard block is the same).
        constexpr float kWallRingW = kWallStackSize * kWallRingColumns;
        constexpr float kWallRingH = kWallStackSize * kWallRingRows;
        constexpr float kWallRingX = Centered(kViewWidth, kWallRingW);
        constexpr float kWallRingY = (kTopDiscardY + kDiscardBlockH + kBottomDiscardY - kWallRingH) / 2.0f;

        // The centre panel: the inside of the wall ring (the ring inset by one stack), where `MahjongCenterPanel.xml` puts the round, the wall
        // and the seats' scores. Written out, so that the static_assert below says what it is.
        constexpr float kCenterPanelX = 472.0f;
        constexpr float kCenterPanelY = 282.0f;
        constexpr float kCenterPanelW = 336.0f;
        constexpr float kCenterPanelH = 140.0f;

        // The seat plates, by screen position (0 bottom, 1 right, 2 top, 3 left), each in the free ground next to its seat's discard block.
        // Bottom: left of the bottom discard block, above the viewer's bonus row, right of the left meld column (which ends at x 76). Right: above the right discard block, below the move
        // line of the HUD (which ends at y 172), right of the `Hint` label (x 1000) and left of the right bonus column. Top: below the top
        // seat's melds and the HUD's `Info`, left of the top discard block. Left: above the left discard block, clear of the left bonus
        // column. The side plates start on the grid unit next to their discard block.
        constexpr float kSeatPlateBottomX = 76.0f;
        constexpr float kSeatPlateBottomY = 532.0f;
        constexpr float kSeatPlateRightX = 1012.0f;
        constexpr float kSeatPlateRightY = 184.0f;
        constexpr float kSeatPlateTopX = 408.0f;
        constexpr float kSeatPlateTopY = 96.0f;
        constexpr float kSeatPlateLeftX = 112.0f;
        constexpr float kSeatPlateLeftY = 184.0f;

        // The layout above must fit the zones it names: a change to a size that breaks it fails to compile.
        // The side columns are the squeeze of the table: with the turned tiles (32 / 32 / 21 wide) and the block of 160 x 144 the right
        // discard block lies at x 1011, so the `Hint` label of `config/nodes/MahjongActions.xml` ends at x 1000 (a grid unit clear of the
        // block, which the seat plates keep `kPlatePadding` away from) and the side seat plates sit right at their blocks. The score
        // table of a hand (`MahjongHandOver.xml`) lies on x 104..1176 and the HUD's move line ends at y 172, x 1188: the columns stay
        // outside the one and the bonus column starts below the other.
        static_assert(kViewerRowX + kViewerHandSpan + kViewerRowGap + kViewerMeldCapacity <= kViewWidth - kEdge);
        static_assert(kSideMeldY + kSideMeldCapacity <= kViewHeight - kEdge);
        static_assert(kSideHandY + kSideHandSpan <= kViewHeight - kEdge);
        static_assert(kTopDiscardY > kTopMeldY + kOpponentMeldTileH);
        static_assert(kTopDiscardY + kDiscardBlockH < kWallRingY);
        static_assert(kWallRingY + kWallRingH < kBottomDiscardY);
        static_assert(kBottomDiscardY + kDiscardBlockH < kViewerBonusY);
        static_assert(kCenterPanelX == kWallRingX + kWallStackSize && kCenterPanelY == kWallRingY + kWallStackSize);
        static_assert(kCenterPanelW == kWallRingW - 2.0f * kWallStackSize && kCenterPanelH == kWallRingH - 2.0f * kWallStackSize);
        static_assert(kLeftDiscardX + kSideDiscardBlockW < kWallRingX);
        static_assert(kRightDiscardX > kWallRingX + kWallRingW);
        static_assert(kLeftDiscardX > kLeftBonusX + kOpponentBonusTileH);
        static_assert(kSideBonusCapacity <= kSideHandSpan);
        // The seat plates keep `kPlatePadding` clear of the discard blocks and clear of the tiles round them
        static_assert(kSeatPlateBottomX + kSeatPlateW <= kDiscardBlockX - kPlatePadding);
        static_assert(kSeatPlateBottomY + kSeatPlateH <= kViewerBonusY);
        static_assert(kSeatPlateBottomX >= kLeftMeldX + kOpponentMeldTileH);
        static_assert(kSeatPlateRightY + kSeatPlateH <= kSideDiscardY - kPlatePadding);
        static_assert(kSeatPlateRightX + kSeatPlateW <= kRightBonusX);
        static_assert(kSeatPlateLeftY + kSeatPlateH <= kSideDiscardY - kPlatePadding);
        static_assert(kSeatPlateLeftX >= kLeftBonusX + kOpponentBonusTileH);
        static_assert(kSeatPlateTopX + kSeatPlateW <= kDiscardBlockX - kPlatePadding);
        static_assert(kSeatPlateTopY >= kTopMeldY + kOpponentMeldTileH);

        SeatGeometry GeometryAt(int position) {
            SeatGeometry geometry;
            switch (position) {
                case 0:
                    geometry.hand = {kViewerRowX, kViewerHandY, kViewerHandTileW, kViewerHandTileH, false, kViewerHandSpan};
                    geometry.melds = {kViewerRowX + kViewerHandSpan + kViewerRowGap, kViewHeight - kEdge - kViewerMeldTileH, kViewerMeldTileW, kViewerMeldTileH, false, kViewerMeldCapacity};
                    geometry.bonus = {kViewerRowX, kViewerBonusY, kViewerBonusTileW, kViewerBonusTileH, false, kViewerBonusTileW * kMaxBonusTiles};
                    geometry.discardX = kDiscardBlockX;
                    geometry.discardY = kBottomDiscardY;
                    geometry.discardW = kDiscardBlockW;
                    geometry.discardH = kDiscardBlockH;
                    break;
                case 1:
                    geometry.hand = {kRightHandX, kSideHandY, kSideHandTileH, kSideHandTileW, true, kSideHandSpan};
                    geometry.melds = {kRightMeldX, kSideMeldY, kOpponentMeldTileH, kOpponentMeldTileW, true, kSideMeldCapacity};
                    geometry.bonus = {kRightBonusX, kSideBonusY, kOpponentBonusTileH, kOpponentBonusTileW, true, kSideBonusCapacity};
                    geometry.discardX = kRightDiscardX;
                    geometry.discardY = kSideDiscardY;
                    geometry.discardW = kSideDiscardBlockW;
                    geometry.discardH = kSideDiscardBlockH;
                    geometry.rotation = kRightSeatRotation;
                    break;
                case 2:
                    geometry.hand = {kTopHandX, kEdge, kOpponentHandTileW, kOpponentHandTileH, false, kTopHandSpan};
                    geometry.melds = {Centered(kViewWidth, kTopMeldCapacity), kTopMeldY, kOpponentMeldTileW, kOpponentMeldTileH, false, kTopMeldCapacity};
                    geometry.bonus = {
                        kTopHandX + kTopHandSpan + kBlockGap, kEdge + Centered(kOpponentHandTileH, kOpponentBonusTileH), kOpponentBonusTileW, kOpponentBonusTileH, false, kTopBonusCapacity};
                    geometry.discardX = kDiscardBlockX;
                    geometry.discardY = kTopDiscardY;
                    geometry.discardW = kDiscardBlockW;
                    geometry.discardH = kDiscardBlockH;
                    break;
                default:
                    geometry.hand = {kLeftHandX, kSideHandY, kSideHandTileH, kSideHandTileW, true, kSideHandSpan};
                    geometry.melds = {kLeftMeldX, kSideMeldY, kOpponentMeldTileH, kOpponentMeldTileW, true, kSideMeldCapacity};
                    geometry.bonus = {kLeftBonusX, kSideBonusY, kOpponentBonusTileH, kOpponentBonusTileW, true, kSideBonusCapacity};
                    geometry.discardX = kLeftDiscardX;
                    geometry.discardY = kSideDiscardY;
                    geometry.discardW = kSideDiscardBlockW;
                    geometry.discardH = kSideDiscardBlockH;
                    geometry.rotation = kLeftSeatRotation;
                    break;
            }
            return geometry;
        }

        Rect SeatPlateAt(int position) {
            switch (position) {
                case 0:
                    return Rect(kSeatPlateBottomX, kSeatPlateBottomY, kSeatPlateW, kSeatPlateH);
                case 1:
                    return Rect(kSeatPlateRightX, kSeatPlateRightY, kSeatPlateW, kSeatPlateH);
                case 2:
                    return Rect(kSeatPlateTopX, kSeatPlateTopY, kSeatPlateW, kSeatPlateH);
                default:
                    return Rect(kSeatPlateLeftX, kSeatPlateLeftY, kSeatPlateW, kSeatPlateH);
            }
        }

        /// The offsets along the strip's axis of `groups` of tiles (`kMeldGap` between two groups), squeezed to the capacity.
        eastl::vector<float> StripOffsets(const Strip& strip, const eastl::vector<int>& groups) {
            const float length = strip.vertical ? strip.tileH : strip.tileW;
            eastl::vector<float> offsets;
            float position = 0.0f;
            for (size_t group = 0; group < groups.size(); ++group) {
                if (group > 0 && !offsets.empty()) {
                    position += kMeldGap;
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

        void AddSlot(TableLayout& layout, const ViewFit& fit, const Rect& rect, SlotRole role, Seat seat, int index, float rotation, int part = 0, Edge claimEdge = Edge::None) {
            layout.slots.push_back(TileSlot{fit.Map(rect), role, seat, index, part, claimEdge, rotation});
        }

        /// The hand in input order, one plain row.
        void AddHand(TableLayout& layout, const ViewFit& fit, const SeatGeometry& geometry, Seat seat, int count) {
            const eastl::vector<float> offsets = StripOffsets(geometry.hand, {count});
            for (int index = 0; index < count; ++index) {
                AddSlot(layout, fit, StripRect(geometry.hand, offsets[index]), SlotRole::HandTile, seat, index, geometry.rotation);
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
                    AddSlot(layout, fit, StripRect(geometry.melds, offsets[next++]), SlotRole::MeldTile, seat, static_cast<int>(meld), geometry.rotation, part,
                            part == claimedPart ? claimEdge : Edge::None);
                }
            }
        }

        void AddBonus(TableLayout& layout, const ViewFit& fit, const SeatGeometry& geometry, Seat seat, int count) {
            const eastl::vector<float> offsets = StripOffsets(geometry.bonus, {count});
            for (int index = 0; index < count; ++index) {
                AddSlot(layout, fit, StripRect(geometry.bonus, offsets[index]), SlotRole::BonusTile, seat, index, geometry.rotation);
            }
        }

        /// The tile `cell` of a seat's pile, `rows` rows long, as laid in its block. The bottom and top seats lie in rows from left to right,
        /// the next row below. A side seat's block is turned: a row runs along the screen's vertical the way that seat reads it (the left
        /// seat top to bottom, the right seat bottom to top) and the next row lies toward the middle of the table, row 0 at the outer side.
        /// `rowPitch` is the distance between two rows (along the horizontal for a side seat), squeezed when the pile is longer than the
        /// block holds.
        Rect DiscardRect(const SeatGeometry& geometry, int position, int cell, float rowPitch) {
            const float column = static_cast<float>(cell % kDiscardsPerRow);
            const float row = static_cast<float>(cell / kDiscardsPerRow);
            switch (position) {
                case 1:
                    return Rect(geometry.discardX + geometry.discardW - kDiscardTileH - row * rowPitch, geometry.discardY + geometry.discardH - (column + 1.0f) * kDiscardTileW, kDiscardTileH,
                                kDiscardTileW);
                case 3:
                    return Rect(geometry.discardX + row * rowPitch, geometry.discardY + column * kDiscardTileW, kDiscardTileH, kDiscardTileW);
                default:
                    return Rect(geometry.discardX + column * kDiscardTileW, geometry.discardY + row * rowPitch, kDiscardTileW, kDiscardTileH);
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
            // A row is as thick as a tile is high, upright or turned: the thickness is `kDiscardTileH` on the screen's vertical for the
            // bottom and top seats and on its horizontal for the side seats.
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
                const int position = SeatPosition(layout.viewer, seat);
                const SeatGeometry& geometry = geometries[position];
                const int cell = placed[seat]++;
                AddSlot(layout, fit, DiscardRect(geometry, position, cell, rowPitchOfSeat[seat]), SlotRole::DiscardTile, seat, static_cast<int>(entry), geometry.rotation);
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

    Rect UprightRect(const Rect& screenRect, float rotation) noexcept {
        constexpr float kFullTurn = 360.0f;
        constexpr float kQuarterTurn = 90.0f;
        constexpr float kTurnTolerance = 0.5f;
        float turn = std::fmod(rotation, kFullTurn);
        if (turn < 0.0f) {
            turn += kFullTurn;
        }
        const float quarters = std::fmod(turn, 2.0f * kQuarterTurn);
        if (!(std::fabs(quarters - kQuarterTurn) <= kTurnTolerance)) {  // also a turn that is no number
            return screenRect;
        }
        const float centreX = RectCentreX(screenRect);
        const float centreY = RectCentreY(screenRect);
        return Rect(centreX - screenRect.h / 2.0f, centreY - screenRect.w / 2.0f, screenRect.h, screenRect.w);
    }

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
            layout.zones.discards[seat] = fit.Map(Rect(geometry.discardX, geometry.discardY, geometry.discardW, geometry.discardH));
            layout.zones.seatPlates[seat] = fit.Map(SeatPlateAt(SeatPosition(layout.viewer, seat)));
            AddHand(layout, fit, geometry, seat, ClampCount(input.concealedCount[seat]));
            AddMelds(layout, fit, geometry, seat, SeatPosition(layout.viewer, seat), input.melds[seat]);
            AddBonus(layout, fit, geometry, seat, ClampCount(input.bonusCount[seat]));
        }
        AddDiscards(layout, fit, geometries, input);
        layout.zones.wall = fit.Map(Rect(kWallRingX, kWallRingY, kWallRingW, kWallRingH));
        layout.zones.centerPanel = fit.Map(Rect(kCenterPanelX, kCenterPanelY, kCenterPanelW, kCenterPanelH));
        const int stacks = WallStackCount(input.wallRemaining);
        for (int stack = 0; stack < stacks; ++stack) {
            AddSlot(layout, fit, WallCell(stack), SlotRole::WallStack, 0, stack, 0.0f);
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
        bool any = false;
        float left = 0.0f;
        float right = 0.0f;
        float top = 0.0f;
        float bottom = 0.0f;
        std::optional<int> nearest;
        float nearestDistance = 0.0f;
        for (const TileSlot& slot : layout.slots) {
            if (slot.role != SlotRole::HandTile || slot.seat != layout.viewer) {
                continue;
            }
            const Rect& rect = slot.rect;
            // the margin under the tiles belongs to the strip: kEdge design units, at the scale the layout was fitted to
            const float tileBottom = rect.y + rect.h + kEdge * (rect.h / kViewerHandTileH);
            left = any ? eastl::min(left, rect.x) : rect.x;
            right = any ? eastl::max(right, rect.x + rect.w) : rect.x + rect.w;
            top = any ? eastl::min(top, rect.y) : rect.y;
            bottom = any ? eastl::max(bottom, tileBottom) : tileBottom;
            any = true;
            // the tile whose span is nearest in x; on a tie the later tile (slots are in index order)
            const float distance = eastl::max(eastl::max(rect.x - x, x - (rect.x + rect.w)), 0.0f);
            if (!nearest || distance <= nearestDistance) {
                nearest = slot.index;
                nearestDistance = distance;
            }
        }
        if (!any || x < left || x > right || y < top || y >= bottom) {
            return std::nullopt;
        }
        return nearest;
    }

}  // namespace BECore::Mahjong
