#include "TileFlights.h"

#include <BECore/Animation/Lerp.h>
#include <cmath>

namespace BECore::Mahjong {

    namespace {

        /// A pile entry as the snapshot knows it: the tile and the seat of its slot.
        struct PileEntry {
            Seat seat = 0;
            Tile tile;

            bool operator==(const PileEntry&) const noexcept = default;
        };

        eastl::vector<PileEntry> PileOf(const TableSnapshot& snapshot) {
            eastl::vector<PileEntry> pile(snapshot.pile.size());
            for (size_t entry = 0; entry < pile.size(); ++entry) {
                pile[entry].tile = snapshot.pile[entry];
            }
            for (const TileSlot& slot : snapshot.layout.slots) {
                if (slot.role == SlotRole::DiscardTile && slot.index >= 0 && static_cast<size_t>(slot.index) < pile.size()) {
                    pile[static_cast<size_t>(slot.index)].seat = slot.seat;
                }
            }
            return pile;
        }

        const TileSlot* FindSlot(const TableLayout& layout, const SlotKey& key) {
            for (const TileSlot& slot : layout.slots) {
                if (key.Matches(slot)) {
                    return &slot;
                }
            }
            return nullptr;
        }

        const TileSlot* LastWallStack(const TableLayout& layout) {
            const TileSlot* last = nullptr;
            for (const TileSlot& slot : layout.slots) {
                if (slot.role == SlotRole::WallStack && (last == nullptr || slot.index > last->index)) {
                    last = &slot;
                }
            }
            return last;
        }

        bool IsClaimMove(MoveKind kind) noexcept {
            return kind == MoveKind::ClaimChow || kind == MoveKind::ClaimPung || kind == MoveKind::ClaimKong;
        }

        /// One planning pass: it remembers which copies of the tiles of the face-up hands are already flying out of their hands.
        class Planner {
        public:
            Planner(const TableSnapshot& before, const TableSnapshot& after, eastl::span<const HandMove> newMoves) : _before(before), _after(after), _moves(newMoves), _beforePile(PileOf(before)) {
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    _used[seat].assign(before.openHand[seat].size(), false);
                }
                const eastl::vector<PileEntry> afterPile = PileOf(after);
                while (_shared < _beforePile.size() && _shared < afterPile.size() && _beforePile[_shared] == afterPile[_shared]) {
                    ++_shared;
                }
            }

            eastl::vector<TileFlight> Run() {
                for (const TileSlot& slot : _after.layout.slots) {
                    switch (slot.role) {
                        case SlotRole::DiscardTile:
                            PlanDiscard(slot);
                            break;
                        case SlotRole::MeldTile:
                            PlanMeldTile(slot);
                            break;
                        case SlotRole::HandTile:
                            PlanDraw(slot);
                            PlanWin(slot);
                            break;
                        default:
                            break;
                    }
                }
                return eastl::move(_flights);
            }

        private:
            /// `source` is the slot of `before` the tile leaves: its rectangle and its turn are where the flight starts.
            void Add(const TileSlot& slot, const TileSlot& source) {
                _flights.push_back(TileFlight{SlotKey::Of(slot), source.rect, source.rotation});
            }

            /// The slot of `before` a tile of `seat` leaves its hand from; null when the seat has no hand slot there.
            const TileSlot* HandSource(Seat seat, Tile tile) {
                if (IsSeat(seat)) {
                    const eastl::vector<Tile>& open = _before.openHand[seat];
                    for (size_t i = 0; i < open.size(); ++i) {
                        if (_used[seat][i] || open[i] != tile) {
                            continue;
                        }
                        if (const TileSlot* slot = FindSlot(_before.layout, SlotKey{SlotRole::HandTile, seat, static_cast<int>(i), 0})) {
                            _used[seat][i] = true;
                            return slot;
                        }
                    }
                }
                int count = 0;
                for (const TileSlot& slot : _before.layout.slots) {
                    count += slot.role == SlotRole::HandTile && slot.seat == seat ? 1 : 0;
                }
                if (count == 0) {
                    return nullptr;
                }
                return FindSlot(_before.layout, SlotKey{SlotRole::HandTile, seat, count / 2, 0});
            }

            /// A pile slot past the shared part of the two piles: the tile comes from the discarder's hand.
            void PlanDiscard(const TileSlot& slot) {
                if (slot.index < 0 || static_cast<size_t>(slot.index) < _shared || static_cast<size_t>(slot.index) >= _after.pile.size()) {
                    return;
                }
                if (const TileSlot* from = HandSource(slot.seat, _after.pile[static_cast<size_t>(slot.index)])) {
                    Add(slot, *from);
                }
            }

            void PlanMeldTile(const TileSlot& slot) {
                if (FindSlot(_before.layout, SlotKey::Of(slot)) != nullptr || slot.seat < 0 || slot.seat >= kSeatCount) {
                    return;
                }
                const eastl::vector<Meld>& melds = _after.melds[slot.seat];
                if (slot.index < 0 || static_cast<size_t>(slot.index) >= melds.size()) {
                    return;
                }
                const Meld& meld = melds[static_cast<size_t>(slot.index)];
                if (slot.claimEdge != Edge::None) {
                    PlanClaimedTile(slot, meld);
                    return;
                }
                if (const TileSlot* from = HandSource(slot.seat, MeldTileAt(meld, slot.part))) {
                    Add(slot, *from);
                }
            }

            /// The discarder of the claim that made meld `slot.index` of `slot.seat`: from the moves, else from the meld.
            Seat DiscarderOf(const TileSlot& slot, const Meld& meld) const {
                for (const HandMove& move : _moves) {
                    if (IsClaimMove(move.kind) && move.seat == slot.seat && move.meld == slot.index && IsSeat(move.from)) {
                        return move.from;
                    }
                }
                return meld.from;
            }

            /// The claimed tile: from the pile slot it left, else (the discard and the claim in one batch) from the discarder's hand.
            void PlanClaimedTile(const TileSlot& slot, const Meld& meld) {
                const Seat discarder = DiscarderOf(slot, meld);
                const size_t last = _beforePile.size();
                if (last > 0 && _shared < last && _beforePile[last - 1].seat == discarder && _beforePile[last - 1].tile == meld.claimed) {
                    if (const TileSlot* pileSlot = FindSlot(_before.layout, SlotKey{SlotRole::DiscardTile, discarder, static_cast<int>(last) - 1, 0})) {
                        Add(slot, *pileSlot);
                        return;
                    }
                }
                if (const TileSlot* from = HandSource(discarder, meld.claimed)) {
                    Add(slot, *from);
                }
            }

            /// The drawn tile: the hand slot of the drawn index flies from the last stack of the wall.
            void PlanDraw(const TileSlot& slot) {
                if (!IsSeat(_after.drawSeat) || slot.seat != _after.drawSeat || slot.index != _after.drawIndex || _after.wallRemaining >= _before.wallRemaining) {
                    return;
                }
                if (const TileSlot* stack = LastWallStack(_before.layout)) {
                    Add(slot, *stack);
                }
            }

            /// The winning tile of a mahjong on a discard: the hand slot of the winner flies from the pile slot the discard left.
            void PlanWin(const TileSlot& slot) {
                if (!IsSeat(_after.winSeat) || IsSeat(_before.winSeat) || slot.seat != _after.winSeat || slot.index != _after.winIndex) {
                    return;
                }
                const size_t last = _beforePile.size();
                if (last == 0 || _after.pile.size() >= last) {
                    return;
                }
                if (const TileSlot* pileSlot = FindSlot(_before.layout, SlotKey{SlotRole::DiscardTile, _beforePile[last - 1].seat, static_cast<int>(last) - 1, 0})) {
                    Add(slot, *pileSlot);
                }
            }

            const TableSnapshot& _before;
            const TableSnapshot& _after;
            eastl::span<const HandMove> _moves;
            eastl::array<eastl::vector<bool>, kSeatCount> _used;
            eastl::vector<PileEntry> _beforePile;
            size_t _shared = 0;
            eastl::vector<TileFlight> _flights;
        };

    }  // namespace

    Tile MeldTileAt(const Meld& meld, int part) noexcept {
        if (meld.kind == MeldKind::Chow && meld.first.IsSuit()) {
            return Tile::Suited(meld.first.Suit(), meld.first.Rank() + part);
        }
        return meld.first;
    }

    eastl::vector<TileFlight> PlanFlights(const TableSnapshot& before, const TableSnapshot& after, eastl::span<const HandMove> newMoves) {
        if (before.layout.slots.empty() || before.layout.viewer != after.layout.viewer || before.layout.viewWidth != after.layout.viewWidth || before.layout.viewHeight != after.layout.viewHeight) {
            return {};
        }
        return Planner(before, after, newMoves).Run();
    }

    TilePlacement PlacementOf(const Rect& screenRect, float rotation) noexcept {
        return TilePlacement{UprightRect(screenRect, rotation), rotation, RectCentreX(screenRect), RectCentreY(screenRect)};
    }

    TilePlacement FlightPlacement(const TileFlight& flight, const TileSlot& to, float progress) {
        const float t = progress > 0.0f ? (progress < 1.0f ? progress : 1.0f) : 0.0f;
        if (t <= 0.0f) {
            return PlacementOf(flight.from, flight.fromRotation);
        }
        if (t >= 1.0f) {
            return PlacementOf(to.rect, to.rotation);
        }
        // The centre follows the curve of FlightRect, the size is that of the unturned tile, the turn takes the shorter way round
        const Rect screen = FlightRect(flight, to.rect, t);
        const Rect fromUpright = UprightRect(flight.from, flight.fromRotation);
        const Rect toUpright = UprightRect(to.rect, to.rotation);
        const float width = Lerp(fromUpright.w, toUpright.w, t);
        const float height = Lerp(fromUpright.h, toUpright.h, t);
        constexpr float kFullTurn = 360.0f;
        constexpr float kHalfTurn = 180.0f;
        float delta = std::fmod(to.rotation - flight.fromRotation, kFullTurn);
        if (delta > kHalfTurn) {
            delta -= kFullTurn;
        } else if (delta <= -kHalfTurn) {
            delta += kFullTurn;
        }
        float rotation = std::fmod(flight.fromRotation + delta * t, kFullTurn);
        if (rotation < 0.0f) {
            rotation += kFullTurn;
        }
        const float centreX = RectCentreX(screen);
        const float centreY = RectCentreY(screen);
        return TilePlacement{Rect(centreX - width / 2.0f, centreY - height / 2.0f, width, height), rotation, centreX, centreY};
    }

    Rect FlightRect(const TileFlight& flight, const Rect& to, float progress) {
        const float t = progress > 0.0f ? (progress < 1.0f ? progress : 1.0f) : 0.0f;
        return Rect(Lerp(flight.from.x, to.x, t), Lerp(flight.from.y, to.y, t), Lerp(flight.from.w, to.w, t), Lerp(flight.from.h, to.h, t));
    }

}  // namespace BECore::Mahjong
