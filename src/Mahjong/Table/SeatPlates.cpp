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
            plate.turn = turn == seat;
        }
        return plates;
    }

}  // namespace BECore::Mahjong
