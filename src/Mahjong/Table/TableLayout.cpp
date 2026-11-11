#include "TableLayout.h"

#include <Mahjong/Table/TableLayoutSpec.h>
#include <Mahjong/Table/TableSolver.h>
#include <Mahjong/Table/TapSelect.h>
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
            /// The upright size of a discarded tile of the seat (a side seat's block is turned, the tiles keep this size).
            float discardTileW = kDiscardTileW;
            float discardTileH = kDiscardTileH;
            /// The hand is spread over its capacity (the portrait table): tile i lies at i times `(capacity - tileW) / (tiles - 1)`.
            bool spreadHand = false;
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
        // The bonus columns start below the HUD's move line (it ends at y 172 and reaches over the right bonus column).
        constexpr float kSideBonusY = 192.0f;
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
        // The hand starts right of the hints' `WaitPanel` (top left, x 96..432) and is not centred: the larger tiles would reach under the
        // panel. The bonus tiles lie in the row of the melds, right of them, so the row ends left of the `Rules` button (x 996).
        constexpr float kTopHandX = 444.0f;
        constexpr float kTopMeldY = kEdge + kOpponentHandTileH + kZoneGap;
        constexpr float kTopMeldCapacity = StripCapacity(kOpponentMeldTileW, kMeldTilesPerKong* kMaxMelds, kMaxMelds - 1);
        constexpr float kTopMeldX = Centered(kViewWidth, kTopMeldCapacity);
        constexpr float kTopBonusX = kTopMeldX + kTopMeldCapacity + kBlockGap;
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
        // Bottom: left of the bottom discard block, above the viewer's bonus row, right of the left meld column (which ends at x 81). Right: above the right discard block, below the move
        // line of the HUD (which ends at y 172), right of the `Hint` label (x 1000) and left of the right bonus column. Top: below the top
        // seat's melds and the HUD's `Info`, left of the top discard block. Left: above the left discard block, clear of the left bonus
        // column. The side plates start on the grid unit next to their discard block.
        constexpr float kSeatPlateBottomX = 84.0f;
        constexpr float kSeatPlateBottomY = 532.0f;
        constexpr float kSeatPlateRightX = 1012.0f;
        constexpr float kSeatPlateRightY = 184.0f;
        constexpr float kSeatPlateTopX = 408.0f;
        constexpr float kSeatPlateTopY = 96.0f;
        constexpr float kSeatPlateLeftX = 112.0f;
        constexpr float kSeatPlateLeftY = 184.0f;

        // The layout above must fit the zones it names: a change to a size that breaks it fails to compile.
        // The side columns are the squeeze of the table: with the turned tiles (37 / 32 / 21 wide) and the block of 160 x 144 the right
        // discard block lies at x 1006, so the `Hint` label of `config/nodes/MahjongActions.xml` ends at x 1000 (clear of the block,
        // which the seat plates keep `kPlatePadding` away from) and the side seat plates sit right at their blocks. The score table of a
        // hand (`MahjongHandOver.xml`) lies on x 108..1172 and the HUD's move line ends at y 172, x 1188: the columns stay outside the
        // one and the bonus column starts below the other.
        static_assert(kViewerRowX + kViewerHandSpan + kViewerRowGap + kViewerMeldCapacity <= kViewWidth - kEdge);
        static_assert(kSideMeldY + kSideMeldCapacity <= kViewHeight - kEdge);
        static_assert(kSideHandY + kSideHandSpan <= kViewHeight - kEdge);
        static_assert(kTopDiscardY > kTopMeldY + kOpponentMeldTileH);
        static_assert(kTopHandX + kTopHandSpan <= 996.0f - kZoneGap && kTopBonusX + kTopBonusCapacity <= 996.0f - kZoneGap);
        static_assert(kTopDiscardY + kDiscardBlockH < kWallRingY);
        static_assert(kWallRingY + kWallRingH < kBottomDiscardY);
        static_assert(kBottomDiscardY + kDiscardBlockH < kViewerBonusY);
        static_assert(kCenterPanelX == kWallRingX + kWallStackSize && kCenterPanelY == kWallRingY + kWallStackSize);
        static_assert(kCenterPanelW == kWallRingW - 2.0f * kWallStackSize && kCenterPanelH == kWallRingH - 2.0f * kWallStackSize);
        static_assert(kLeftDiscardX + kSideDiscardBlockW < kWallRingX);
        static_assert(kRightDiscardX > kWallRingX + kWallRingW);
        static_assert(kLeftDiscardX > kLeftBonusX + kOpponentBonusTileH);
        static_assert(kSideBonusY >= kSideHandY && kSideBonusY + kSideBonusCapacity <= kSideHandY + kSideHandSpan);
        // The phone profile of the viewer's side (`TableProfile`): the hand is one row of tiles side by side from the left inside the rim and the rack, at the
        // bottom edge of the desktop hand; the tiles are as high as `handTileW` says, up to the tallest (`kPhoneHandTileMaxW`). The bottom
        // plate lies under the tallest hand tile's top, the bonus tiles and melds in a row above the hand's tallest top starting right of
        // the plate, the bottom discard block between the wall ring and that row, squeezed to the height left (its rows overlap).
        constexpr float kPhoneHandX = kEdge + kRackPadding;
        constexpr float kPhoneHandSpan = kViewWidth - 2.0f * kPhoneHandX;
        constexpr float kPhoneHandBottom = kViewHeight - kEdge;
        constexpr float kPhoneHandMaxH = kPhoneHandTileMaxW * kViewerHandTileH / kViewerHandTileW;
        constexpr float kPhoneHandTopAtMax = kPhoneHandBottom - kPhoneHandMaxH;
        constexpr float kPhoneRowBottom = kPhoneHandTopAtMax - kZoneGap;
        constexpr float kPhoneRowX = kSeatPlateBottomX + kSeatPlateW + kBlockGap;
        constexpr float kPhoneBonusY = kPhoneRowBottom - kViewerBonusTileH;
        constexpr float kPhoneMeldX = kPhoneRowX + kViewerBonusTileW * kMaxBonusTiles + kViewerRowGap;
        constexpr float kPhoneMeldY = kPhoneRowBottom - kViewerMeldTileH;
        constexpr float kPhoneDiscardY = kWallRingY + kWallRingH + kZoneGap;
        constexpr float kPhoneDiscardBlockH = 120.0f;
        constexpr float kSeatPlateBottomPhoneY = kPhoneHandTopAtMax - kSeatPlateH;

        static_assert(kPhoneHandTileMaxW >= kViewerHandTileW);
        static_assert(kPhoneHandTileMaxW * kMaxConcealedTiles <= kPhoneHandSpan);
        static_assert(kPhoneHandTopAtMax >= kSeatPlateBottomPhoneY + kSeatPlateH);
        static_assert(kSeatPlateBottomPhoneY >= kWallRingY + kWallRingH + kPlatePadding);
        static_assert(kPhoneMeldX + kViewerMeldCapacity <= kViewWidth - kEdge);
        static_assert(kPhoneMeldY + kViewerMeldTileH <= kPhoneHandTopAtMax);
        static_assert(kPhoneDiscardY >= kWallRingY + kWallRingH);
        static_assert(kPhoneDiscardY + kPhoneDiscardBlockH + kZoneGap <= kPhoneMeldY);
        static_assert(kPhoneDiscardBlockH >= kDiscardTileH);
        // the side seats' columns end above the row of melds, and the viewer's row clears them at the sides
        static_assert(kSideMeldY + kSideMeldCapacity <= kPhoneMeldY);
        static_assert(kPhoneRowX >= kLeftMeldX + kOpponentMeldTileH);
        static_assert(kPhoneMeldX + kViewerMeldCapacity < kRightDiscardX);
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

        // The portrait table, a design of kPortraitViewWidth x kPortraitViewHeight (`TableShape::Portrait`). From the top: the top seat
        // (hand, melds), under it a band of the three discard blocks (top between left and right) with the side seats' columns at the edges
        // of the view, then a row of the three plates (each under the block of its seat), the wall ring, the viewer's discards and, at the
        // foot, the viewer's rows. The height the four seats do not need is free ground between the viewer's discards and the plate of the
        // viewer, which the canvases of the table use (`config/nodes/`): the layout keeps it empty. The opponents' tiles have the sizes of the
        // landscape table except the hands, which are larger (the opponents are what a phone held upright shows smallest), and the
        // viewer's discards are one and a half times larger in five rows (the view is 720 wide: on a phone a 24 unit tile is about 13
        // points). The top seat starts a margin below the edge, where a phone's status bar or notch sits.
        constexpr float kPortraitTopY = 32.0f;
        constexpr float kPortraitTopHandTileW = 36.0f;
        constexpr float kPortraitTopHandTileH = 48.0f;
        constexpr float kPortraitSideHandTileW = 28.0f;
        constexpr float kPortraitSideHandTileH = 37.0f;
        constexpr float kPortraitTopHandSpan = kPortraitTopHandTileW * kMaxConcealedTiles;
        constexpr float kPortraitSideHandSpan = kPortraitSideHandTileW * kMaxConcealedTiles;
        constexpr float kPortraitTopMeldY = kPortraitTopY + kPortraitTopHandTileH + kZoneGap;
        constexpr float kPortraitBandY = 140.0f;
        constexpr float kPortraitDiscardTileW = kDiscardTileW * 1.5f;
        constexpr float kPortraitDiscardTileH = kDiscardTileH * 1.5f;
        constexpr int kPortraitViewerDiscardRows = kDiscardRowCapacity;
        constexpr float kPortraitViewerDiscardBlockW = kPortraitDiscardTileW * kDiscardsPerRow;
        constexpr float kPortraitViewerDiscardBlockH = kPortraitDiscardTileH * kPortraitViewerDiscardRows;
        constexpr float kPortraitViewerDiscardX = Centered(kPortraitViewWidth, kPortraitViewerDiscardBlockW);

        // The viewer's hand is one row of 14 spread over the view inside the rim and the rack, resting on the rim; the melds and the bonus
        // tiles lie in rows above it, the melds nearer, both clear of a raised (selected) tile.
        constexpr float kPortraitHandX = kEdge + kRackPadding;
        constexpr float kPortraitHandSpan = kPortraitViewWidth - 2.0f * kPortraitHandX;
        constexpr float kPortraitHandY = kPortraitViewHeight - kEdge - kViewerHandTileH;
        constexpr float kPortraitMeldY = kPortraitHandY - kSelectedTileRise - kZoneGap - kViewerMeldTileH;
        constexpr float kPortraitBonusY = kPortraitMeldY - kZoneGap - kViewerBonusTileH;
        constexpr float kPortraitViewerRowX = kEdge + kRackPadding;

        constexpr float kPortraitLeftHandX = kEdge;
        constexpr float kPortraitLeftMeldX = kPortraitLeftHandX + kPortraitSideHandTileH + kZoneGap;
        constexpr float kPortraitLeftBonusX = kPortraitLeftMeldX + kOpponentMeldTileH + kZoneGap;
        constexpr float kPortraitLeftDiscardX = kPortraitLeftBonusX + kOpponentBonusTileH + kSideBlockGap;
        constexpr float kPortraitRightHandX = kPortraitViewWidth - kEdge - kPortraitSideHandTileH;
        constexpr float kPortraitRightMeldX = kPortraitRightHandX - kZoneGap - kOpponentMeldTileH;
        constexpr float kPortraitRightBonusX = kPortraitRightMeldX - kZoneGap - kOpponentBonusTileH;
        constexpr float kPortraitRightDiscardX = kPortraitRightBonusX - kSideBlockGap - kSideDiscardBlockW;
        // The top hand and its bonus row beside it, centred together.
        constexpr float kPortraitTopHandX = Centered(kPortraitViewWidth, kPortraitTopHandSpan + kBlockGap + kTopBonusCapacity);
        constexpr float kPortraitTopDiscardX = Centered(kPortraitViewWidth, kDiscardBlockW);

        // The wall ring under the plates, and its inside, the centre panel (the ring less one stack on every side).
        constexpr float kPortraitSeatPlateRowY = 308.0f;
        constexpr float kPortraitWallRingX = Centered(kPortraitViewWidth, kWallRingW);
        constexpr float kPortraitWallRingY = 400.0f;
        constexpr float kPortraitBottomDiscardY = kPortraitWallRingY + kWallRingH + kBlockGap;

        // The plates: left and right under the blocks of their seats, the top plate under the top block, the viewer's above its rows.
        constexpr float kPortraitSeatPlateLeftX = 112.0f;
        constexpr float kPortraitSeatPlateTopX = 284.0f;
        constexpr float kPortraitSeatPlateRightX = 456.0f;
        constexpr float kPortraitSeatPlateBottomX = kPortraitViewerRowX;
        constexpr float kPortraitSeatPlateBottomY = kPortraitBonusY - kZoneGap - kSeatPlateH;

        static_assert(kViewerHandTileW * kMaxConcealedTiles <= kPortraitHandSpan);
        static_assert(kPortraitViewerRowX + kViewerMeldCapacity <= kPortraitViewWidth - kEdge);
        static_assert(kPortraitViewerRowX + kViewerBonusTileW * kMaxBonusTiles <= kPortraitViewWidth - kEdge);
        static_assert(kPortraitMeldY + kViewerMeldTileH + kSelectedTileRise < kPortraitHandY);
        static_assert(kPortraitBonusY + kViewerBonusTileH < kPortraitMeldY);
        static_assert(kPortraitSeatPlateBottomY + kSeatPlateH < kPortraitBonusY);
        static_assert(kPortraitBottomDiscardY + kPortraitViewerDiscardBlockH + kPlatePadding <= kPortraitSeatPlateBottomY);
        // the top seat's rows end above the band, the side columns and the blocks lie in the band between the edges and the top block
        static_assert(kPortraitTopMeldY + kOpponentMeldTileH < kPortraitBandY);
        static_assert(kPortraitTopHandTileH >= kOpponentHandTileH && kPortraitSideHandTileH >= kSideHandTileH);
        static_assert(kPortraitBandY + kSideMeldCapacity < kPortraitSeatPlateBottomY);
        static_assert(kPortraitBandY + kPortraitSideHandSpan < kPortraitBottomDiscardY + kPortraitViewerDiscardBlockH);
        static_assert(kPortraitLeftDiscardX + kSideDiscardBlockW < kPortraitTopDiscardX);
        static_assert(kPortraitTopDiscardX + kDiscardBlockW < kPortraitRightDiscardX);
        static_assert(kPortraitTopDiscardX + kDiscardBlockW <= kPortraitViewWidth - kEdge);
        static_assert(kPortraitTopHandX >= kEdge && kPortraitTopHandX + kPortraitTopHandSpan + kBlockGap + kTopBonusCapacity <= kPortraitViewWidth - kEdge);
        static_assert(kPortraitLeftBonusX + kOpponentBonusTileH < kPortraitSeatPlateLeftX);
        static_assert(kPortraitSeatPlateRightX + kSeatPlateW < kPortraitRightBonusX);
        // the plates keep `kPlatePadding` clear of the blocks above them (and of each other), and of the wall ring below
        static_assert(kPortraitSeatPlateRowY >= kPortraitBandY + kSideDiscardBlockH + kPlatePadding);
        static_assert(kPortraitSeatPlateRowY >= kPortraitBandY + kDiscardBlockH + kPlatePadding);
        static_assert(kPortraitSeatPlateLeftX + kSeatPlateW <= kPortraitTopDiscardX - kPlatePadding);
        static_assert(kPortraitSeatPlateTopX + kSeatPlateW <= kPortraitSeatPlateRightX - kPlatePadding);
        static_assert(kPortraitSeatPlateLeftX + kSeatPlateW <= kPortraitSeatPlateTopX - kPlatePadding);
        static_assert(kPortraitSeatPlateRowY + kSeatPlateH + kPlatePadding <= kPortraitWallRingY);
        static_assert(kPortraitWallRingY + kWallRingH < kPortraitBottomDiscardY);
        static_assert(kPortraitWallRingX > kPortraitLeftDiscardX);
        static_assert(kPortraitWallRingX + kWallRingW < kPortraitRightDiscardX + kSideDiscardBlockW);

        SeatGeometry PortraitGeometryAt(int position) {
            SeatGeometry geometry;
            switch (position) {
                case 0:
                    geometry.hand = {kPortraitHandX, kPortraitHandY, kViewerHandTileW, kViewerHandTileH, false, kPortraitHandSpan};
                    geometry.spreadHand = true;
                    geometry.melds = {kPortraitViewerRowX, kPortraitMeldY, kViewerMeldTileW, kViewerMeldTileH, false, kViewerMeldCapacity};
                    geometry.bonus = {kPortraitViewerRowX, kPortraitBonusY, kViewerBonusTileW, kViewerBonusTileH, false, kViewerBonusTileW * kMaxBonusTiles};
                    geometry.discardX = kPortraitViewerDiscardX;
                    geometry.discardY = kPortraitBottomDiscardY;
                    geometry.discardW = kPortraitViewerDiscardBlockW;
                    geometry.discardH = kPortraitViewerDiscardBlockH;
                    geometry.discardTileW = kPortraitDiscardTileW;
                    geometry.discardTileH = kPortraitDiscardTileH;
                    break;
                case 1:
                    geometry.hand = {kPortraitRightHandX, kPortraitBandY, kPortraitSideHandTileH, kPortraitSideHandTileW, true, kPortraitSideHandSpan};
                    geometry.melds = {kPortraitRightMeldX, kPortraitBandY, kOpponentMeldTileH, kOpponentMeldTileW, true, kSideMeldCapacity};
                    geometry.bonus = {kPortraitRightBonusX, kPortraitBandY, kOpponentBonusTileH, kOpponentBonusTileW, true, kSideBonusCapacity};
                    geometry.discardX = kPortraitRightDiscardX;
                    geometry.discardY = kPortraitBandY;
                    geometry.discardW = kSideDiscardBlockW;
                    geometry.discardH = kSideDiscardBlockH;
                    geometry.rotation = kRightSeatRotation;
                    break;
                case 2:
                    geometry.hand = {kPortraitTopHandX, kPortraitTopY, kPortraitTopHandTileW, kPortraitTopHandTileH, false, kPortraitTopHandSpan};
                    geometry.melds = {Centered(kPortraitViewWidth, kTopMeldCapacity), kPortraitTopMeldY, kOpponentMeldTileW, kOpponentMeldTileH, false, kTopMeldCapacity};
                    geometry.bonus = {kPortraitTopHandX + kPortraitTopHandSpan + kBlockGap,
                                      kPortraitTopY + Centered(kPortraitTopHandTileH, kOpponentBonusTileH),
                                      kOpponentBonusTileW,
                                      kOpponentBonusTileH,
                                      false,
                                      kTopBonusCapacity};
                    geometry.discardX = kPortraitTopDiscardX;
                    geometry.discardY = kPortraitBandY;
                    geometry.discardW = kDiscardBlockW;
                    geometry.discardH = kDiscardBlockH;
                    break;
                default:
                    geometry.hand = {kPortraitLeftHandX, kPortraitBandY, kPortraitSideHandTileH, kPortraitSideHandTileW, true, kPortraitSideHandSpan};
                    geometry.melds = {kPortraitLeftMeldX, kPortraitBandY, kOpponentMeldTileH, kOpponentMeldTileW, true, kSideMeldCapacity};
                    geometry.bonus = {kPortraitLeftBonusX, kPortraitBandY, kOpponentBonusTileH, kOpponentBonusTileW, true, kSideBonusCapacity};
                    geometry.discardX = kPortraitLeftDiscardX;
                    geometry.discardY = kPortraitBandY;
                    geometry.discardW = kSideDiscardBlockW;
                    geometry.discardH = kSideDiscardBlockH;
                    geometry.rotation = kLeftSeatRotation;
                    break;
            }
            return geometry;
        }

        Rect PortraitSeatPlateAt(int position) {
            switch (position) {
                case 0:
                    return Rect(kPortraitSeatPlateBottomX, kPortraitSeatPlateBottomY, kSeatPlateW, kSeatPlateH);
                case 1:
                    return Rect(kPortraitSeatPlateRightX, kPortraitSeatPlateRowY, kSeatPlateW, kSeatPlateH);
                case 2:
                    return Rect(kPortraitSeatPlateTopX, kPortraitSeatPlateRowY, kSeatPlateW, kSeatPlateH);
                default:
                    return Rect(kPortraitSeatPlateLeftX, kPortraitSeatPlateRowY, kSeatPlateW, kSeatPlateH);
            }
        }

        // The narrow table, a design of kNarrowViewWidth x kNarrowViewHeight (`TableShape::Narrow`): the landscape table with the viewer's side
        // arranged the way the phone profile does it. The middle (the three other seats' discard blocks, the wall ring, the centre panel)
        // keeps its heights and is centred in the narrower view, and the side seats' columns stay at the edges (the view is 200 units
        // narrower than the landscape one: the middle gives them up). The viewer's hand is one row of 14 tiles of the desktop size resting on the
        // rim, the bonus tiles and melds one row above it (the 48 units of height the view has more than the landscape one), clear of the
        // tile a tap raises.
        constexpr float kNarrowDiscardBlockX = Centered(kNarrowViewWidth, kDiscardBlockW);
        // The top hand starts right of the hints' `WaitPanel` (x 8..328 in this view) as in landscape: centred, the larger tiles reach under it.
        constexpr float kNarrowTopHandX = 340.0f;
        constexpr float kNarrowSideHandY = Centered(kNarrowViewHeight, kSideHandSpan);
        constexpr float kNarrowSideMeldY = Centered(kNarrowViewHeight, kSideMeldCapacity);
        constexpr float kNarrowSideDiscardY = Centered(kNarrowViewHeight, kSideDiscardBlockH);
        constexpr float kNarrowRightHandX = kNarrowViewWidth - kEdge - kSideHandTileH;
        constexpr float kNarrowRightMeldX = kNarrowRightHandX - kZoneGap - kOpponentMeldTileH;
        constexpr float kNarrowRightBonusX = kNarrowRightMeldX - kZoneGap - kOpponentBonusTileH;
        constexpr float kNarrowRightDiscardX = kNarrowRightBonusX - kSideBlockGap - kSideDiscardBlockW;
        constexpr float kNarrowWallRingX = Centered(kNarrowViewWidth, kWallRingW);

        // The viewer: the hand centred, the bonus tiles at its left edge and the melds right of them in one row, bottom aligned, above the
        // raised tile.
        constexpr float kNarrowHandX = Centered(kNarrowViewWidth, kViewerHandSpan);
        constexpr float kNarrowHandY = kNarrowViewHeight - kEdge - kViewerHandTileH;
        constexpr float kNarrowRowBottom = kNarrowHandY - kSelectedTileRise - kZoneGap;
        constexpr float kNarrowMeldY = kNarrowRowBottom - kViewerMeldTileH;
        constexpr float kNarrowBonusY = kNarrowRowBottom - kViewerBonusTileH;
        constexpr float kNarrowMeldX = kNarrowHandX + kViewerBonusTileW * kMaxBonusTiles + kViewerRowGap;

        // The plates: the bottom one and the two above the side blocks as in landscape (the same places clear the same things), the top one
        // left of the top block.
        constexpr float kNarrowSeatPlateRightX = kNarrowRightDiscardX + 2.0f;
        constexpr float kNarrowSeatPlateTopX = 308.0f;

        static_assert(kNarrowHandX + kViewerHandSpan <= kNarrowViewWidth - kEdge - kRackPadding);
        static_assert(kNarrowMeldX + kViewerMeldCapacity <= kNarrowViewWidth - kEdge);
        static_assert(kNarrowMeldY + kViewerMeldTileH + kSelectedTileRise < kNarrowHandY);
        static_assert(kNarrowBonusY + kViewerBonusTileH + kSelectedTileRise < kNarrowHandY);
        // the middle: the bottom block lies between the ring and the viewer's row, the side columns end above that row
        static_assert(kBottomDiscardY + kDiscardBlockH + kZoneGap <= kNarrowBonusY);
        static_assert(kBottomDiscardY + kDiscardBlockH + kZoneGap <= kNarrowMeldY);
        static_assert(kNarrowSideMeldY + kSideMeldCapacity < kNarrowBonusY);
        static_assert(kNarrowSideMeldY + kSideMeldCapacity < kNarrowMeldY);
        static_assert(kNarrowSideMeldY + kSideMeldCapacity <= kNarrowViewHeight - kEdge);
        static_assert(kNarrowSideHandY + kSideHandSpan <= kNarrowViewHeight - kEdge);
        static_assert(kNarrowTopHandX + kTopHandSpan + kBlockGap + kTopBonusCapacity <= kNarrowViewWidth - kEdge);
        static_assert(kNarrowRightBonusX - kSideBlockGap == kNarrowRightDiscardX + kSideDiscardBlockW);
        // the blocks of the side seats lie between the edges and the middle, which is as wide as the wall ring
        static_assert(kLeftDiscardX + kSideDiscardBlockW < kNarrowWallRingX);
        static_assert(kNarrowRightDiscardX > kNarrowWallRingX + kWallRingW);
        static_assert(kLeftDiscardX + kSideDiscardBlockW < kNarrowDiscardBlockX);
        static_assert(kNarrowDiscardBlockX + kDiscardBlockW < kNarrowRightDiscardX);
        static_assert(kNarrowSideDiscardY >= kTopDiscardY + kDiscardBlockH);
        // the plates keep `kPlatePadding` clear of the blocks, of the ring and of each other, and clear of the bonus columns and the rows
        static_assert(kSeatPlateBottomX + kSeatPlateW + kPlatePadding <= kNarrowDiscardBlockX);
        static_assert(kSeatPlateBottomX >= kLeftMeldX + kOpponentMeldTileH);
        static_assert(kSeatPlateBottomY + kSeatPlateH + kPlatePadding <= kNarrowBonusY);
        static_assert(kSeatPlateBottomY >= kNarrowSideDiscardY + kSideDiscardBlockH + kPlatePadding);
        static_assert(kSeatPlateLeftX + kSeatPlateW + kPlatePadding <= kNarrowSeatPlateTopX);
        static_assert(kNarrowSeatPlateTopX + kSeatPlateW + kPlatePadding <= kNarrowDiscardBlockX);
        static_assert(kNarrowSeatPlateTopX >= kSeatPlateLeftX);
        static_assert(kNarrowSeatPlateRightX >= kNarrowDiscardBlockX + kDiscardBlockW + kPlatePadding);
        static_assert(kNarrowSeatPlateRightX + kSeatPlateW <= kNarrowRightBonusX);
        static_assert(kSeatPlateRightY + kSeatPlateH + kPlatePadding <= kNarrowSideDiscardY);
        static_assert(kSeatPlateLeftY + kSeatPlateH + kPlatePadding <= kNarrowSideDiscardY);
        static_assert(kSeatPlateTopY + kSeatPlateH + kPlatePadding <= kWallRingY);
        static_assert(kSeatPlateTopY >= kTopMeldY + kOpponentMeldTileH);
        static_assert(kSeatPlateBottomX + kSeatPlateW + kPlatePadding <= kNarrowWallRingX);

        SeatGeometry NarrowGeometryAt(int position) {
            SeatGeometry geometry;
            switch (position) {
                case 0:
                    geometry.hand = {kNarrowHandX, kNarrowHandY, kViewerHandTileW, kViewerHandTileH, false, kViewerHandSpan};
                    geometry.melds = {kNarrowMeldX, kNarrowMeldY, kViewerMeldTileW, kViewerMeldTileH, false, kViewerMeldCapacity};
                    geometry.bonus = {kNarrowHandX, kNarrowBonusY, kViewerBonusTileW, kViewerBonusTileH, false, kViewerBonusTileW * kMaxBonusTiles};
                    geometry.discardX = kNarrowDiscardBlockX;
                    geometry.discardY = kBottomDiscardY;
                    geometry.discardW = kDiscardBlockW;
                    geometry.discardH = kDiscardBlockH;
                    break;
                case 1:
                    geometry.hand = {kNarrowRightHandX, kNarrowSideHandY, kSideHandTileH, kSideHandTileW, true, kSideHandSpan};
                    geometry.melds = {kNarrowRightMeldX, kNarrowSideMeldY, kOpponentMeldTileH, kOpponentMeldTileW, true, kSideMeldCapacity};
                    geometry.bonus = {kNarrowRightBonusX, kNarrowSideHandY, kOpponentBonusTileH, kOpponentBonusTileW, true, kSideBonusCapacity};
                    geometry.discardX = kNarrowRightDiscardX;
                    geometry.discardY = kNarrowSideDiscardY;
                    geometry.discardW = kSideDiscardBlockW;
                    geometry.discardH = kSideDiscardBlockH;
                    geometry.rotation = kRightSeatRotation;
                    break;
                case 2:
                    geometry.hand = {kNarrowTopHandX, kEdge, kOpponentHandTileW, kOpponentHandTileH, false, kTopHandSpan};
                    geometry.melds = {Centered(kNarrowViewWidth, kTopMeldCapacity), kTopMeldY, kOpponentMeldTileW, kOpponentMeldTileH, false, kTopMeldCapacity};
                    geometry.bonus = {
                        kNarrowTopHandX + kTopHandSpan + kBlockGap, kEdge + Centered(kOpponentHandTileH, kOpponentBonusTileH), kOpponentBonusTileW, kOpponentBonusTileH, false, kTopBonusCapacity};
                    geometry.discardX = kNarrowDiscardBlockX;
                    geometry.discardY = kTopDiscardY;
                    geometry.discardW = kDiscardBlockW;
                    geometry.discardH = kDiscardBlockH;
                    break;
                default:
                    geometry.hand = {kLeftHandX, kNarrowSideHandY, kSideHandTileH, kSideHandTileW, true, kSideHandSpan};
                    geometry.melds = {kLeftMeldX, kNarrowSideMeldY, kOpponentMeldTileH, kOpponentMeldTileW, true, kSideMeldCapacity};
                    geometry.bonus = {kLeftBonusX, kNarrowSideHandY, kOpponentBonusTileH, kOpponentBonusTileW, true, kSideBonusCapacity};
                    geometry.discardX = kLeftDiscardX;
                    geometry.discardY = kNarrowSideDiscardY;
                    geometry.discardW = kSideDiscardBlockW;
                    geometry.discardH = kSideDiscardBlockH;
                    geometry.rotation = kLeftSeatRotation;
                    break;
            }
            return geometry;
        }

        Rect NarrowSeatPlateAt(int position) {
            switch (position) {
                case 0:
                    return Rect(kSeatPlateBottomX, kSeatPlateBottomY, kSeatPlateW, kSeatPlateH);
                case 1:
                    return Rect(kNarrowSeatPlateRightX, kSeatPlateRightY, kSeatPlateW, kSeatPlateH);
                case 2:
                    return Rect(kNarrowSeatPlateTopX, kSeatPlateTopY, kSeatPlateW, kSeatPlateH);
                default:
                    return Rect(kSeatPlateLeftX, kSeatPlateLeftY, kSeatPlateW, kSeatPlateH);
            }
        }

        SeatGeometry GeometryAt(TableShape shape, int position, const TableProfile& profile) {
            if (shape == TableShape::Portrait) {
                return PortraitGeometryAt(position);
            }
            if (shape == TableShape::Narrow) {
                return NarrowGeometryAt(position);
            }
            SeatGeometry geometry;
            switch (position) {
                case 0:
                    if (profile.phone) {
                        const float tileH = profile.handTileW * kViewerHandTileH / kViewerHandTileW;
                        geometry.hand = {kPhoneHandX, kPhoneHandBottom - tileH, profile.handTileW, tileH, false, kPhoneHandSpan};
                        geometry.melds = {kPhoneMeldX, kPhoneMeldY, kViewerMeldTileW, kViewerMeldTileH, false, kViewerMeldCapacity};
                        geometry.bonus = {kPhoneRowX, kPhoneBonusY, kViewerBonusTileW, kViewerBonusTileH, false, kViewerBonusTileW * kMaxBonusTiles};
                        geometry.discardX = kDiscardBlockX;
                        geometry.discardY = kPhoneDiscardY;
                        geometry.discardW = kDiscardBlockW;
                        geometry.discardH = kPhoneDiscardBlockH;
                        break;
                    }
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
                    geometry.melds = {kTopMeldX, kTopMeldY, kOpponentMeldTileW, kOpponentMeldTileH, false, kTopMeldCapacity};
                    geometry.bonus = {kTopBonusX, kTopMeldY, kOpponentBonusTileW, kOpponentBonusTileH, false, kTopBonusCapacity};
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

        Rect SeatPlateAt(TableShape shape, int position, const TableProfile& profile) {
            if (shape == TableShape::Portrait) {
                return PortraitSeatPlateAt(position);
            }
            if (shape == TableShape::Narrow) {
                return NarrowSeatPlateAt(position);
            }
            switch (position) {
                case 0:
                    return Rect(kSeatPlateBottomX, profile.phone ? kSeatPlateBottomPhoneY : kSeatPlateBottomY, kSeatPlateW, kSeatPlateH);
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

        /// `wing` is how far the table lies from the left edge of the design of the view's shape (the wide design holds the landscape table in
        /// its middle, a wing from its left edge).
        ViewFit FitView(float viewWidth, float viewHeight, float wing) {
            const TableShape shape = ShapeOfView(viewWidth, viewHeight);
            ViewFit fit;
            fit.scale = LayoutScale(viewWidth, viewHeight);
            fit.offsetX = Centered(viewWidth, DesignWidthOf(shape) * fit.scale) + wing * fit.scale;
            fit.offsetY = Centered(viewHeight, DesignHeightOf(shape) * fit.scale);
            return fit;
        }

        void AddSlot(TableLayout& layout, const ViewFit& fit, const Rect& rect, SlotRole role, Seat seat, int index, float rotation, int part = 0, Edge claimEdge = Edge::None) {
            layout.slots.push_back(TileSlot{fit.Map(rect), role, seat, index, part, claimEdge, rotation});
        }

        /// The hand in input order, one plain row; a spread one (the portrait table) puts tile `i` at `i` times the pitch that makes 14
        /// tiles span the strip, a longer hand squeezes to the strip.
        void AddHand(TableLayout& layout, const ViewFit& fit, const SeatGeometry& geometry, Seat seat, int count) {
            eastl::vector<float> offsets = StripOffsets(geometry.hand, {count});
            if (geometry.spreadHand) {
                const float pitch = (geometry.hand.capacity - geometry.hand.tileW) / static_cast<float>(eastl::max(count, kMaxConcealedTiles) - 1);
                for (int index = 0; index < count; ++index) {
                    offsets[index] = static_cast<float>(index) * pitch;
                }
            }
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
            const float tileW = geometry.discardTileW;
            const float tileH = geometry.discardTileH;
            const float column = static_cast<float>(cell % kDiscardsPerRow);
            const float row = static_cast<float>(cell / kDiscardsPerRow);
            switch (position) {
                case 1:
                    return Rect(geometry.discardX + geometry.discardW - tileH - row * rowPitch, geometry.discardY + geometry.discardH - (column + 1.0f) * tileW, tileH, tileW);
                case 3:
                    return Rect(geometry.discardX + row * rowPitch, geometry.discardY + column * tileW, tileH, tileW);
                default:
                    return Rect(geometry.discardX + column * tileW, geometry.discardY + row * rowPitch, tileW, tileH);
            }
        }

        /// The distance between two rows of a pile of `rows` rows in a block whose rows run along `rowSpan` (the block's extent across its
        /// rows): a tile's thickness while the rows fit, else the rows squeezed to the span.
        float DiscardRowPitch(int rows, float rowSpan, float tileThickness) {
            return rows <= 1 ? tileThickness : eastl::min(tileThickness, (rowSpan - tileThickness) / static_cast<float>(rows - 1));
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
                const int position = SeatPosition(layout.viewer, seat);
                const SeatGeometry& geometry = geometries[position];
                rowPitchOfSeat[seat] = DiscardRowPitch(rows, position == 1 || position == 3 ? geometry.discardW : geometry.discardH, geometry.discardTileH);
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

        Rect WallRingRect(TableShape shape) {
            switch (shape) {
                case TableShape::Portrait:
                    return Rect(kPortraitWallRingX, kPortraitWallRingY, kWallRingW, kWallRingH);
                case TableShape::Narrow:
                    return Rect(kNarrowWallRingX, kWallRingY, kWallRingW, kWallRingH);
                default:
                    return Rect(kWallRingX, kWallRingY, kWallRingW, kWallRingH);
            }
        }

        /// The inside of the wall ring: where the centre panel lies.
        Rect CenterPanelRect(TableShape shape) {
            if (shape == TableShape::Landscape || shape == TableShape::Wide) {
                return Rect(kCenterPanelX, kCenterPanelY, kCenterPanelW, kCenterPanelH);
            }
            const Rect ring = WallRingRect(shape);
            return Rect(ring.x + kWallStackSize, ring.y + kWallStackSize, ring.w - 2.0f * kWallStackSize, ring.h - 2.0f * kWallStackSize);
        }

        /// The cell `n` along the ring, clockwise from the top left corner: the top side, the right, the bottom, the left. The ring is
        /// `ring`, its cells squares of side `stack`.
        Rect WallCellIn(const Rect& ring, float stack, int n) {
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
            return Rect(ring.x + static_cast<float>(column) * stack, ring.y + static_cast<float>(row) * stack, stack, stack);
        }

        Rect WallCell(TableShape shape, int n) {
            return WallCellIn(WallRingRect(shape), kWallStackSize, n);
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

    TableLayout MakeLegacyTableLayout(const TableInput& input, float viewWidth, float viewHeight) {
        TableLayout layout;
        layout.viewer = IsSeat(input.viewer) ? input.viewer : 0;
        const TableShape shape = ShapeOfView(viewWidth, viewHeight);
        layout.shape = shape;
        if (IsLandscapeLike(shape) && input.profile.phone) {
            layout.profile.phone = true;
            layout.profile.handTileW = eastl::max(kViewerHandTileW, eastl::min(input.profile.handTileW, kPhoneHandTileMaxW));
        }
        layout.viewWidth = viewWidth;
        layout.viewHeight = viewHeight;
        const ViewFit fit = FitView(viewWidth, viewHeight, shape == TableShape::Wide ? kWideWingWidth : 0.0f);

        eastl::array<SeatGeometry, kSeatCount> geometries;
        for (int position = 0; position < kSeatCount; ++position) {
            geometries[position] = GeometryAt(shape, position, layout.profile);
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const SeatGeometry& geometry = geometries[SeatPosition(layout.viewer, seat)];
            layout.zones.discards[seat] = fit.Map(Rect(geometry.discardX, geometry.discardY, geometry.discardW, geometry.discardH));
            layout.zones.seatPlates[seat] = fit.Map(SeatPlateAt(shape, SeatPosition(layout.viewer, seat), layout.profile));
            AddHand(layout, fit, geometry, seat, ClampCount(input.concealedCount[seat]));
            AddMelds(layout, fit, geometry, seat, SeatPosition(layout.viewer, seat), input.melds[seat]);
            AddBonus(layout, fit, geometry, seat, ClampCount(input.bonusCount[seat]));
        }
        AddDiscards(layout, fit, geometries, input);
        layout.zones.wall = fit.Map(WallRingRect(shape));
        layout.zones.centerPanel = fit.Map(CenterPanelRect(shape));
        const int stacks = WallStackCount(input.wallRemaining);
        for (int stack = 0; stack < stacks; ++stack) {
            AddSlot(layout, fit, WallCell(shape, stack), SlotRole::WallStack, 0, stack, 0.0f);
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

    // The layout from a spec: the zones are data, the tiles are solved (`TableSolver.h`), and the helpers above that know no size lay the
    // slots out. What follows has no constant of its own except the ones the rules give.

    namespace {

        /// The design size the zones of a spec are fractions of.
        struct ZoneDesign {
            float width = kViewWidth;
            float height = kViewHeight;
        };

        /// The shape whose design the zones of `shape` are in: its own, or the one it is `like`.
        TableShape ZoneShapeOf(const TableLayoutSpec& spec, TableShape shape) noexcept {
            const ShapeSpec& own = spec.Shape(shape);
            return own.like ? own.likeShape : shape;
        }

        bool IsSideSeat(int position) noexcept {
            return position == 1 || position == 3;
        }

        /// The solved strip of a zone; `askedTileW` (over 0) is the width of a zone that asks for it (the phone profile's hand).
        SolvedStrip SolveZoneStrip(const StripZone& zone, const ZoneDesign& design, bool vertical, int tiles, int gaps, float askedTileW) {
            const Rect rect = SnapZone(zone.rect, design.width, design.height);
            if (zone.profileTile && askedTileW > 0.0f && !vertical) {
                return PlaceStrip(rect, zone.align, askedTileW, askedTileW * kViewerHandTileH / kViewerHandTileW);
            }
            return SolveStrip(rect, zone.align, vertical, tiles, gaps);
        }

        Strip StripOfSolved(const SolvedStrip& solved) {
            return Strip{solved.x, solved.y, solved.slotW, solved.slotH, solved.vertical, solved.capacity};
        }

        /// The geometry of the seat at screen position `position` from its zones; `profileTileW` is the width of the phone profile's hand tile
        /// (0 when the seat has none).
        SeatGeometry GeometryFromZones(const SeatSpec& seat, int position, const ZoneDesign& design, float profileTileW) {
            const bool vertical = IsSideSeat(position);
            SeatGeometry geometry;
            const SolvedStrip hand = SolveZoneStrip(seat.hand, design, vertical, kMaxConcealedTiles, 0, profileTileW);
            geometry.hand = StripOfSolved(hand);
            geometry.spreadHand = seat.hand.spread;
            geometry.melds = StripOfSolved(SolveZoneStrip(seat.melds, design, vertical, kMaxMeldTiles, kMaxMelds - 1, 0.0f));
            geometry.bonus = StripOfSolved(SolveZoneStrip(seat.bonus, design, vertical, kMaxBonusTiles, 0, 0.0f));
            // The zone is the block, whatever the tile that fills it: a pile of more rows than the tile leaves squeezes inside the zone.
            const Rect block = SnapZone(seat.discards.rect, design.width, design.height);
            const SolvedBlock solvedBlock = SolveDiscardBlock(block, vertical, seat.discards.rows);
            geometry.discardX = block.x;
            geometry.discardY = block.y;
            geometry.discardW = block.w;
            geometry.discardH = block.h;
            geometry.discardTileW = solvedBlock.tileW;
            geometry.discardTileH = solvedBlock.tileH;
            geometry.rotation = position == 1 ? kRightSeatRotation : (position == 3 ? kLeftSeatRotation : 0.0f);
            return geometry;
        }

        /// The tile width of the viewer's hand zone as it is laid out for the desktop, and the widest the phone override's hand zone solves.
        float DesktopHandTileW(const ShapeSpec& zones, const ZoneDesign& design) {
            return SolveZoneStrip(zones.seats[0].hand, design, false, kMaxConcealedTiles, 0, 0.0f).tileW;
        }

        float WidestPhoneHandTileW(const ShapeSpec& zones, const ZoneDesign& design) {
            StripZone widest = zones.phone.hand;
            widest.profileTile = false;
            return SolveZoneStrip(widest, design, false, kMaxConcealedTiles, 0, 0.0f).tileW;
        }

    }  // namespace

    TableLayout MakeTableLayout(const TableInput& input, const TableLayoutSpec& spec, float viewWidth, float viewHeight) {
        TableLayout layout;
        layout.viewer = IsSeat(input.viewer) ? input.viewer : 0;
        const TableShape shape = ShapeOfView(viewWidth, viewHeight);
        layout.shape = shape;
        layout.viewWidth = viewWidth;
        layout.viewHeight = viewHeight;
        const ShapeSpec* zones = ZonesOf(spec, shape);
        if (zones == nullptr) {
            return layout;
        }
        const TableShape zoneShape = ZoneShapeOf(spec, shape);
        const ZoneDesign design{DesignWidthOf(zoneShape), DesignHeightOf(zoneShape)};
        const bool phone = IsLandscapeLike(shape) && input.profile.phone && zones->hasPhone;
        if (phone) {
            layout.profile.phone = true;
            layout.profile.handTileW = eastl::max(DesktopHandTileW(*zones, design), eastl::min(input.profile.handTileW, WidestPhoneHandTileW(*zones, design)));
        }
        const float wing = SnapZone(ZoneRect{spec.Shape(shape).inset, 0.0f, 0.0f, 0.0f}, DesignWidthOf(shape), DesignHeightOf(shape)).x;
        const ViewFit fit = FitView(viewWidth, viewHeight, wing);

        eastl::array<SeatGeometry, kSeatCount> geometries;
        eastl::array<Rect, kSeatCount> plates;
        for (int position = 0; position < kSeatCount; ++position) {
            const bool phoneSeat = phone && position == 0;
            const SeatSpec& seat = phoneSeat ? zones->phone : zones->seats[static_cast<size_t>(position)];
            geometries[position] = GeometryFromZones(seat, position, design, phoneSeat ? layout.profile.handTileW : 0.0f);
            plates[position] = SnapZone(seat.plate, design.width, design.height);
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const int position = SeatPosition(layout.viewer, seat);
            const SeatGeometry& geometry = geometries[position];
            layout.zones.discards[seat] = fit.Map(Rect(geometry.discardX, geometry.discardY, geometry.discardW, geometry.discardH));
            layout.zones.seatPlates[seat] = fit.Map(plates[position]);
            AddHand(layout, fit, geometry, seat, ClampCount(input.concealedCount[seat]));
            AddMelds(layout, fit, geometry, seat, position, input.melds[seat]);
            AddBonus(layout, fit, geometry, seat, ClampCount(input.bonusCount[seat]));
        }
        AddDiscards(layout, fit, geometries, input);
        const SolvedWall ring = SolveWall(SnapZone(zones->wall, design.width, design.height));
        layout.zones.wall = fit.Map(Rect(ring.x, ring.y, ring.w, ring.h));
        layout.zones.centerPanel = fit.Map(Rect(ring.x + ring.stack, ring.y + ring.stack, ring.w - 2.0f * ring.stack, ring.h - 2.0f * ring.stack));
        const int stacks = WallStackCount(input.wallRemaining);
        for (int stack = 0; stack < stacks; ++stack) {
            AddSlot(layout, fit, WallCellIn(Rect(ring.x, ring.y, ring.w, ring.h), ring.stack, stack), SlotRole::WallStack, 0, stack, 0.0f);
        }
        return layout;
    }

    namespace {

        const TableLayoutSpec* gActiveTableLayoutSpec = nullptr;

    }  // namespace

    void SetActiveTableLayoutSpec(const TableLayoutSpec* spec) noexcept {
        gActiveTableLayoutSpec = spec;
    }

    const TableLayoutSpec* ActiveTableLayoutSpec() noexcept {
        return gActiveTableLayoutSpec;
    }

    TableLayout MakeTableLayout(const TableInput& input, float viewWidth, float viewHeight) {
        if (gActiveTableLayoutSpec == nullptr) {
            static bool logged = false;
            if (!logged) {
                logged = true;
                LOG_ERROR("TableLayout: no layout spec is set, the table has no tiles (SetActiveTableLayoutSpec)");
            }
            TableLayout layout;
            layout.viewer = IsSeat(input.viewer) ? input.viewer : 0;
            layout.shape = ShapeOfView(viewWidth, viewHeight);
            layout.viewWidth = viewWidth;
            layout.viewHeight = viewHeight;
            return layout;
        }
        return MakeTableLayout(input, *gActiveTableLayoutSpec, viewWidth, viewHeight);
    }

    // The check of a spec: the worst table built from it, for every shape, and the properties the table must have.

    namespace {

        /// How far two rectangles may overlap (or lie short of a gap) before it counts, in design units: the float noise of a scaled layout.
        constexpr float kCheckTolerance = 0.01f;
        /// The pile the worst table holds: longer than any hand leaves (136 tiles, 52 in the hands at the least).
        constexpr int kCheckedPileSize = 100;
        constexpr size_t kPhoneWidthSteps = 3;

        eastl::string_view ShapeNameOf(TableShape shape) {
            return EnumUtils<TableShape>::ToString(shape);
        }

        /// Every zone at what the rules allow it to hold, the 144-tile wall.
        TableInput WorstCaseInput(Seat viewer, const TableProfile& profile) {
            TableInput input;
            input.viewer = viewer;
            input.profile = profile;
            input.concealedCount.fill(kMaxConcealedTiles);
            input.bonusCount.fill(kMaxBonusTiles);
            Meld kong;
            kong.kind = MeldKind::Kong;
            for (auto& melds : input.melds) {
                melds.assign(static_cast<size_t>(kMaxMelds), kong);
            }
            for (int entry = 0; entry < kCheckedPileSize; ++entry) {
                input.discardSeats.push_back(static_cast<Seat>(entry % kSeatCount));
            }
            input.wallRemaining = 2 * kWallRingCells;
            return input;
        }

        /// The messages of one check, each once (the same table is checked for every viewer).
        class CheckReport {
        public:
            void Add(const eastl::string& message) {
                for (const eastl::string& known : _messages) {
                    if (known == message) {
                        return;
                    }
                }
                _messages.push_back(message);
            }

            eastl::vector<eastl::string> Take() {
                return eastl::move(_messages);
            }

        private:
            eastl::vector<eastl::string> _messages;
        };

        bool IsSolid(const Rect& rect) noexcept {
            return rect.w > 0.0f && rect.h > 0.0f;
        }

        bool Overlaps(const Rect& a, const Rect& b) noexcept {
            return a.x + a.w - kCheckTolerance > b.x && b.x + b.w - kCheckTolerance > a.x && a.y + a.h - kCheckTolerance > b.y && b.y + b.h - kCheckTolerance > a.y;
        }

        bool Contains(const Rect& outer, const Rect& inner) noexcept {
            return inner.x >= outer.x - kCheckTolerance && inner.y >= outer.y - kCheckTolerance && inner.x + inner.w <= outer.x + outer.w + kCheckTolerance &&
                   inner.y + inner.h <= outer.y + outer.h + kCheckTolerance;
        }

        Rect Grown(const Rect& rect, float by) noexcept {
            return Rect(rect.x - by, rect.y - by, rect.w + 2.0f * by, rect.h + 2.0f * by);
        }

        /// A rectangle of the table the others must keep off, with the words for a message.
        struct NamedRect {
            eastl::string name;
            Rect rect;
        };

        /// The thing a layout names `position` for: a slot's seat for the viewer.
        int PositionOf(const TableLayout& layout, Seat seat) noexcept {
            return SeatPosition(layout.viewer, seat);
        }

        constexpr const char* kRoleNames[] = {"hand", "meld", "bonus", "discard", "wall stack"};

        void CheckSlotsInView(const TableLayout& layout, const eastl::string& tag, CheckReport& report) {
            const Rect view(0.0f, 0.0f, layout.viewWidth, layout.viewHeight);
            for (const TileSlot& slot : layout.slots) {
                if (Contains(view, slot.rect)) {
                    continue;
                }
                const char* role = kRoleNames[static_cast<size_t>(+slot.role)];
                if (slot.role == SlotRole::WallStack) {
                    report.Add(Format("{}: a wall stack lies outside the view", tag));
                } else {
                    report.Add(Format("{}: a {} tile of position {} lies outside the view", tag, role, PositionOf(layout, slot.seat)));
                }
            }
        }

        void CheckZonesClear(const TableLayout& layout, const eastl::string& tag, CheckReport& report) {
            eastl::vector<NamedRect> solids;
            for (int position = 0; position < kSeatCount; ++position) {
                const Seat seat = SeatAtPosition(layout.viewer, position);
                const SlotRole rows[] = {SlotRole::HandTile, SlotRole::MeldTile, SlotRole::BonusTile};
                for (const SlotRole role : rows) {
                    solids.push_back({Format("the {} row of position {}", kRoleNames[static_cast<size_t>(+role)], position), BoundsOf(layout, role, seat)});
                }
                solids.push_back({Format("the discard block of position {}", position), layout.zones.discards[seat]});
            }
            solids.push_back({"the wall ring", layout.zones.wall});
            for (size_t first = 0; first < solids.size(); ++first) {
                for (size_t second = first + 1; second < solids.size(); ++second) {
                    if (IsSolid(solids[first].rect) && IsSolid(solids[second].rect) && Overlaps(solids[first].rect, solids[second].rect)) {
                        report.Add(Format("{}: {} overlaps {}", tag, solids[first].name, solids[second].name));
                    }
                }
            }
            // the pile and the wall stay in the zones that are made for them
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == SlotRole::DiscardTile && !Contains(layout.zones.discards[slot.seat], slot.rect)) {
                    report.Add(Format("{}: the pile of position {} leaves its discard block", tag, PositionOf(layout, slot.seat)));
                } else if (slot.role == SlotRole::WallStack && !Contains(layout.zones.wall, slot.rect)) {
                    report.Add(Format("{}: a wall stack leaves the wall ring", tag));
                }
            }
            // the plates: over no row, no block, no other plate, and `kPlatePadding` clear of the blocks and the ring
            for (int position = 0; position < kSeatCount; ++position) {
                const Rect& plate = layout.zones.seatPlates[SeatAtPosition(layout.viewer, position)];
                for (const NamedRect& solid : solids) {
                    if (IsSolid(solid.rect) && Overlaps(plate, solid.rect)) {
                        report.Add(Format("{}: the plate of position {} overlaps {}", tag, position, solid.name));
                    } else if (IsSolid(solid.rect) && solid.name.find("row") == eastl::string::npos && Overlaps(Grown(plate, kPlatePadding - kCheckTolerance), solid.rect)) {
                        report.Add(Format("{}: the plate of position {} is nearer than {} units to {}", tag, position, kPlatePadding, solid.name));
                    }
                }
                for (int other = position + 1; other < kSeatCount; ++other) {
                    if (Overlaps(plate, layout.zones.seatPlates[SeatAtPosition(layout.viewer, other)])) {
                        report.Add(Format("{}: the plate of position {} overlaps the plate of position {}", tag, position, other));
                    }
                }
            }
        }

        /// The tiles of the zones of one seat (by screen position) as they solve: too small, or under the minimum the zone asks.
        void CheckSolvedSeat(const SeatSpec& seat, int position, const ZoneDesign& design, const eastl::string& tag, CheckReport& report) {
            const bool vertical = IsSideSeat(position);
            struct Row {
                const char* name;
                const StripZone& zone;
                int tiles;
                int gaps;
            };
            const Row rows[] = {{"hand", seat.hand, kMaxConcealedTiles, 0}, {"melds", seat.melds, kMaxMeldTiles, kMaxMelds - 1}, {"bonus", seat.bonus, kMaxBonusTiles, 0}};
            for (const Row& row : rows) {
                StripZone solved = row.zone;
                solved.profileTile = false;
                const SolvedStrip strip = SolveZoneStrip(solved, design, vertical, row.tiles, row.gaps, 0.0f);
                if (!strip.Fits()) {
                    report.Add(Format("{}: the {} zone of position {} is too small for its {} tiles", tag, row.name, position, row.tiles));
                } else if (strip.tileW < static_cast<float>(row.zone.minTileW)) {
                    report.Add(Format("{}: the {} tile of position {} is {} wide, under its minTileW {}", tag, row.name, position, strip.tileW, row.zone.minTileW));
                }
            }
            const Rect block = SnapZone(seat.discards.rect, design.width, design.height);
            if (!SolveDiscardBlock(block, vertical, seat.discards.rows).Fits()) {
                report.Add(Format("{}: the discard zone of position {} is too small for {} rows of {} tiles", tag, position, seat.discards.rows, kDiscardsPerRow));
            }
        }

        void CheckSolvedShape(const ShapeSpec& zones, const ZoneDesign& design, const eastl::string& tag, CheckReport& report) {
            for (int position = 0; position < kSeatCount; ++position) {
                CheckSolvedSeat(zones.seats[static_cast<size_t>(position)], position, design, tag, report);
            }
            if (!SolveWall(SnapZone(zones.wall, design.width, design.height)).Fits()) {
                report.Add(Format("{}: the wall zone is too small for the ring of {} x {} stacks", tag, kWallRingColumns, kWallRingRows));
            }
            const float desktopHand = DesktopHandTileW(zones, design);
            if (desktopHand < kViewerHandTileW) {
                report.Add(Format("{}: the viewer's hand tile is {} wide, under kViewerHandTileW {}", tag, desktopHand, kViewerHandTileW));
            }
            if (zones.hasPhone) {
                const eastl::string phoneTag = Format("{}, phone profile", tag);
                CheckSolvedSeat(zones.phone, 0, design, phoneTag, report);
                const float widest = WidestPhoneHandTileW(zones, design);
                if (widest != kPhoneHandTileMaxW) {
                    report.Add(Format("{}: the widest hand tile is {}, not kPhoneHandTileMaxW {}", phoneTag, widest, kPhoneHandTileMaxW));
                }
            }
        }

        void CheckTable(const TableLayoutSpec& spec, TableShape shape, const TableProfile& profile, const eastl::string& tag, bool whole, CheckReport& report) {
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeTableLayout(WorstCaseInput(viewer, profile), spec, DesignWidthOf(shape), DesignHeightOf(shape));
                CheckSlotsInView(layout, tag, report);
                if (whole) {
                    CheckZonesClear(layout, tag, report);
                }
            }
        }

    }  // namespace

    eastl::vector<eastl::string> CheckTableLayoutSpec(const TableLayoutSpec& spec) {
        CheckReport report;
        for (const TableShape shape : EnumUtils<TableShape>::Values()) {
            const eastl::string tag = Format("shape '{}'", ShapeNameOf(shape));
            const ShapeSpec* zones = ZonesOf(spec, shape);
            if (zones == nullptr) {
                report.Add(Format("{}: the spec does not describe it", tag));
                continue;
            }
            // a shape that is like another is the same zones moved: the table must still lie inside its own view, the rest is the other's
            const bool own = !spec.Shape(shape).like;
            if (own) {
                CheckSolvedShape(*zones, ZoneDesign{DesignWidthOf(shape), DesignHeightOf(shape)}, tag, report);
            }
            CheckTable(spec, shape, TableProfile{}, tag, own, report);
            if (IsLandscapeLike(shape) && zones->hasPhone) {
                const eastl::string phoneTag = Format("{}, phone profile", tag);
                for (size_t step = 0; step < kPhoneWidthSteps; ++step) {
                    const float along = static_cast<float>(step) / static_cast<float>(kPhoneWidthSteps - 1);
                    const TableProfile profile{true, kViewerHandTileW + along * (kPhoneHandTileMaxW - kViewerHandTileW)};
                    CheckTable(spec, shape, profile, phoneTag, own, report);
                }
            }
        }
        return report.Take();
    }

}  // namespace BECore::Mahjong
