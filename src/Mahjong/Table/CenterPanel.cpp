#include "CenterPanel.h"

#include <Mahjong/Table/SeatPlates.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    int HandNumberInRound(const Match& match) {
        const int handsPerRound = eastl::max(1, match.HandsTotal() / kRoundsPerMatch);
        // The hand shown: the one in progress, the last one once the match is over (`HandsPlayed()` is then the total)
        const int shown = eastl::clamp(match.HandsPlayed(), 0, eastl::max(0, match.HandsTotal() - 1));
        return shown % handsPerRound + 1;
    }

    CenterPanelText MakeCenterPanel(const UIStrings& strings, const TableSession& session, const Match& match) {
        CenterPanelText panel;
        const HandModel& hand = match.Hand();
        panel.handInRound = HandNumberInRound(match);
        const eastl::string number = Format("{}", panel.handInRound);
        const eastl::string roundWind = WindName(strings, match.RoundWind());
        panel.round = Tr(strings, "panel.round", {{"wind", roundWind}, {"n", number}});
        panel.wall = static_cast<int>(hand.GetWall().LiveCount());
        const eastl::string wall = Format("{}", panel.wall);
        panel.wallText = Tr(strings, "panel.wall", {{"wall", wall}});
        const Seat bottom = BottomSeat(session);
        const Seat turn = TurnSeat(session, hand);
        for (int position = 0; position < kSeatCount; ++position) {
            const Seat seat = SeatAtPosition(bottom, position);
            CenterPanelSeat& side = panel.seats[position];
            side.seat = seat;
            side.wind = WindLetter(strings, hand.SeatWind(seat));
            side.score = match.Scores()[seat];
            side.dealer = hand.SeatWind(seat) == Wind::East;
            side.turn = turn == seat;
        }
        return panel;
    }

    Rect PanelTurnLight(const Rect& panel, int position, float width) {
        switch (position) {
            case 1:
                return Rect(panel.x + panel.w - width, panel.y, width, panel.h);
            case 2:
                return Rect(panel.x, panel.y, panel.w, width);
            case 3:
                return Rect(panel.x, panel.y, width, panel.h);
            default:
                return Rect(panel.x, panel.y + panel.h - width, panel.w, width);
        }
    }

    Rect PanelDealerMark(const Rect& panel, int position, float thickness) {
        const float row = panel.h * kPanelRowShare;
        const float side = panel.w * kPanelSideShare;
        const float sideHeight = panel.h - 2.0f * row;
        switch (position) {
            case 1:
                return Rect(panel.x + panel.w - side - thickness, panel.y + row, thickness, sideHeight);
            case 2:
                return Rect(panel.x, panel.y + row, panel.w, thickness);
            case 3:
                return Rect(panel.x + side, panel.y + row, thickness, sideHeight);
            default:
                return Rect(panel.x, panel.y + panel.h - row - thickness, panel.w, thickness);
        }
    }

}  // namespace BECore::Mahjong
