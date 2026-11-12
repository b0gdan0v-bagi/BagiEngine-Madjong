#include "TableLayout.h"

#include <Mahjong/Table/TableLayoutSpec.h>
#include <Mahjong/Table/TableSolver.h>
#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/UiPalette.h>
#include <cmath>

namespace BECore::Mahjong {

    namespace {

        /// The margin under the viewer's hand tiles that still belongs to the hand strip (`HitHandTile`).
        constexpr float kEdge = 8.0f;
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
            float discardTileW = 0.0f;
            float discardTileH = 0.0f;
            /// The hand is spread over its capacity (the portrait table): tile i lies at i times `(capacity - tileW) / (tiles - 1)`.
            bool spreadHand = false;
        };

        /// The offsets of one strip: as many as the most a strip holds (four kongs) without the heap, more only for an input past the rules
        using StripOffsetList = eastl::fixed_vector<float, kMaxMeldTiles, true>;

        /// The offsets along the strip's axis of `groups` of tiles (`kMeldGap` between two groups), squeezed to the capacity.
        StripOffsetList StripOffsets(const Strip& strip, eastl::span<const int> groups) {
            const float length = strip.vertical ? strip.tileH : strip.tileW;
            StripOffsetList offsets;
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
            const int groups[] = {count};
            StripOffsetList offsets = StripOffsets(geometry.hand, groups);
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
            eastl::fixed_vector<int, kMaxMelds, true> groups;
            for (const Meld& meld : melds) {
                groups.push_back(MeldTileCount(meld));
            }
            const StripOffsetList offsets = StripOffsets(geometry.melds, groups);
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
            const int groups[] = {count};
            const StripOffsetList offsets = StripOffsets(geometry.bonus, groups);
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
            // A row is as thick as a tile is high, upright or turned: the thickness is `discardTileH` of the seat's geometry on the
            // screen's vertical for the bottom and top seats and on its horizontal for the side seats.
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

        /// The zones of a shape with the design they are fractions of and the fit of that design into a view: what every zone and region is
        /// built with. `zones` is null for a shape the spec does not describe.
        struct ShapeFrame {
            const ShapeSpec* zones = nullptr;
            ZoneDesign design;
            ViewFit fit;
        };

        ShapeFrame FrameOf(const TableLayoutSpec& spec, TableShape shape, float viewWidth, float viewHeight) {
            ShapeFrame frame;
            frame.zones = ZonesOf(spec, shape);
            if (frame.zones == nullptr) {
                return frame;
            }
            const TableShape zoneShape = ZoneShapeOf(spec, shape);
            frame.design = ZoneDesign{DesignWidthOf(zoneShape), DesignHeightOf(zoneShape)};
            const float wing = SnapZone(ZoneRect{spec.Shape(shape).inset, 0.0f, 0.0f, 0.0f}, DesignWidthOf(shape), DesignHeightOf(shape)).x;
            frame.fit = FitView(viewWidth, viewHeight, wing);
            return frame;
        }

        /// The free regions of the frame's shape fitted like its zones (snapped to a quarter unit by edges, then mapped into the view), into
        /// `out`, which keeps its capacity.
        void FitFreeZones(const ShapeFrame& frame, eastl::vector<FreeZone>& out) {
            out.clear();
            if (frame.zones == nullptr) {
                return;
            }
            for (const FreeRegion& region : frame.zones->freeRegions) {
                out.push_back(FreeZone{region.name, frame.fit.Map(SnapZone(region.rect, frame.design.width, frame.design.height))});
            }
        }

    }  // namespace

    TableLayout MakeTableLayout(const TableInput& input, const TableLayoutSpec& spec, float viewWidth, float viewHeight) {
        TableLayout layout;
        RebuildTableLayout(layout, input, spec, viewWidth, viewHeight);
        return layout;
    }

    void RebuildTableLayout(TableLayout& layout, const TableInput& input, const TableLayoutSpec& spec, float viewWidth, float viewHeight) {
        BE_PROFILE_SCOPE("RebuildTableLayout");
        eastl::vector<TileSlot> slots = eastl::move(layout.slots);
        slots.clear();  // the capacity stays: a table drawn every frame builds its slots without the heap
        layout = TableLayout{};
        layout.slots = eastl::move(slots);
        layout.viewer = IsSeat(input.viewer) ? input.viewer : 0;
        const TableShape shape = ShapeOfView(viewWidth, viewHeight);
        layout.shape = shape;
        layout.viewWidth = viewWidth;
        layout.viewHeight = viewHeight;
        const ShapeFrame frame = FrameOf(spec, shape, viewWidth, viewHeight);
        const ShapeSpec* zones = frame.zones;
        if (zones == nullptr) {
            return;
        }
        const ZoneDesign& design = frame.design;
        const ViewFit& fit = frame.fit;
        const bool phone = IsLandscapeLike(shape) && input.profile.phone && zones->hasPhone;
        if (phone) {
            layout.profile.phone = true;
            layout.profile.handTileW = eastl::max(DesktopHandTileW(*zones, design), eastl::min(input.profile.handTileW, WidestPhoneHandTileW(*zones, design)));
        }

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
        TableLayout layout;
        RebuildTableLayout(layout, input, viewWidth, viewHeight);
        return layout;
    }

    void RebuildTableLayout(TableLayout& layout, const TableInput& input, float viewWidth, float viewHeight) {
        if (gActiveTableLayoutSpec != nullptr) {
            RebuildTableLayout(layout, input, *gActiveTableLayoutSpec, viewWidth, viewHeight);
            return;
        }
        static bool logged = false;
        if (!logged) {
            logged = true;
            LOG_ERROR("TableLayout: no layout spec is set, the table has no tiles (SetActiveTableLayoutSpec)");
        }
        eastl::vector<TileSlot> slots = eastl::move(layout.slots);
        slots.clear();
        layout = TableLayout{};
        layout.slots = eastl::move(slots);
        layout.viewer = IsSeat(input.viewer) ? input.viewer : 0;
        layout.shape = ShapeOfView(viewWidth, viewHeight);
        layout.viewWidth = viewWidth;
        layout.viewHeight = viewHeight;
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

        /// The free regions of the shape of `layout` (built from `spec`): inside the view, and for a shape with zones of its own clear of
        /// every tile, discard block, wall ring and plate of the worst-case table (a `like` shape is the same table moved).
        void CheckFreeRegions(const TableLayout& layout, const TableLayoutSpec& spec, const eastl::string& tag, bool clearOfTable, CheckReport& report) {
            eastl::vector<FreeZone> regions;
            FitFreeZones(FrameOf(spec, layout.shape, layout.viewWidth, layout.viewHeight), regions);
            const Rect view(0.0f, 0.0f, layout.viewWidth, layout.viewHeight);
            for (const FreeZone& region : regions) {
                if (!Contains(view, region.rect)) {
                    report.Add(Format("{}: the free region '{}' lies outside the view", tag, region.name.c_str()));
                }
                if (!clearOfTable) {
                    continue;
                }
                for (const TileSlot& slot : layout.slots) {
                    if (!Overlaps(region.rect, slot.rect)) {
                        continue;
                    }
                    if (slot.role == SlotRole::WallStack) {
                        report.Add(Format("{}: the free region '{}' overlaps a wall stack", tag, region.name.c_str()));
                    } else {
                        report.Add(
                            Format("{}: the free region '{}' overlaps a {} tile of position {}", tag, region.name.c_str(), kRoleNames[static_cast<size_t>(+slot.role)], PositionOf(layout, slot.seat)));
                    }
                }
                for (int position = 0; position < kSeatCount; ++position) {
                    const Seat seat = SeatAtPosition(layout.viewer, position);
                    if (Overlaps(region.rect, layout.zones.discards[seat])) {
                        report.Add(Format("{}: the free region '{}' overlaps the discard block of position {}", tag, region.name.c_str(), position));
                    }
                    if (Overlaps(region.rect, layout.zones.seatPlates[seat])) {
                        report.Add(Format("{}: the free region '{}' overlaps the plate of position {}", tag, region.name.c_str(), position));
                    }
                }
                if (Overlaps(region.rect, layout.zones.wall)) {
                    report.Add(Format("{}: the free region '{}' overlaps the wall ring", tag, region.name.c_str()));
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
                CheckFreeRegions(layout, spec, tag, whole, report);
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
