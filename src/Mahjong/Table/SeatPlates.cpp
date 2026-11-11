#include "SeatPlates.h"

#include <Mahjong/Table/SeatName.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    Seat BottomSeat(const TableSession& session) {
        if (IsSeat(session.Viewer())) {
            return session.Viewer();
        }
        if (session.GetPhase() == SessionPhase::Playing && session.ActorIsPerson()) {
            return session.Actor();
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (session.KindOf(seat) == SeatKind::Person) {
                return seat;
            }
        }
        return 0;
    }

    Seat TurnSeat(const TableSession& session, const HandModel& hand) {
        return session.GetPhase() == SessionPhase::Playing ? hand.CurrentSeat() : kNoSeat;
    }

    Rect EmblemBox(const Rect& plate, float unit) noexcept {
        const float size = kEmblemBoxSize * unit;
        return Rect(plate.x + (plate.w - size) * 0.5f, plate.y + kEmblemBoxTop * unit, size, size);
    }

    Rect NameBox(const Rect& plate, float unit) noexcept {
        const float width = kNameBoxWidth * unit;
        return Rect(plate.x + (plate.w - width) * 0.5f, plate.y + kNameBoxTop * unit, width, kNameBoxHeight * unit);
    }

    eastl::array<SeatPlateText, kSeatCount> MakeSeatPlates(const UIStrings& strings, const TableSession& session, const Match& match) {
        eastl::array<SeatPlateText, kSeatCount> plates;
        const HandModel& hand = match.Hand();
        const Seat bottom = BottomSeat(session);
        const Seat turn = TurnSeat(session, hand);
        for (int position = 0; position < kSeatCount; ++position) {
            const Seat seat = SeatAtPosition(bottom, position);
            SeatPlateText& plate = plates[position];
            plate.seat = seat;
            plate.name = SeatLabel(strings, seat, session.Seats());
            plate.kind = session.KindOf(seat);
            plate.level = session.LevelOf(seat);
            plate.turn = turn == seat;
            plate.calling = hand.GetHand(seat).callingDeclared;
        }
        return plates;
    }

}  // namespace BECore::Mahjong
