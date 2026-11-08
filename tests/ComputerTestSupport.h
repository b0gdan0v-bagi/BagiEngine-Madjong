#pragma once

#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/HandModel.h>
#include <initializer_list>
#include <string>

/**
 * What the tests of the computer player share (`ComputerViewTest`, `ComputerTurnTest`, `ComputerClaimTest`): the quiet hands of the
 * other seats, the spec of a hand where East holds a given hand and the model of an open pung with the fourth tile drawn. Header only.
 */
namespace BECore::Mahjong::Tests {

    // The hands of the other three seats. They hold no pair and no two suit tiles near each other, so no discard of a quiet seat (or of
    // a draw listed in a test) opens a claim window; honours are single in each hand.
    inline constexpr const char* kQuiet1 = "2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
    inline constexpr const char* kQuiet2 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd";
    inline constexpr const char* kQuiet3 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd";

    /// East holds `east` (13 tiles), the others the quiet hands; `live` is the start of the live wall.
    inline FixedWallSpec SpecOf(const char* east, const std::string& live) {
        FixedWallSpec spec;
        spec.hands[kSeat0] = east;
        spec.hands[kSeat1] = kQuiet1;
        spec.hands[kSeat2] = kQuiet2;
        spec.hands[kSeat3] = kQuiet3;
        spec.live = live;
        return spec;
    }

    /**
     * Seat 1 (`seat1`, two 5p among its tiles) is to act with an open pung of 5p and the fourth 5p drawn: East discards a 5p, seat 1 claims
     * the pung, a lap of discards goes round and seat 1 draws the fifth tile of the wall. The seats in `declaring` declare the calling hand
     * in that lap (the hand has to be waiting). Seats 2 and 3 hold `seat2` and `seat3`.
     */
    inline FixedWallSpec OpenPungSpec(const char* seat1, const char* seat2 = kQuiet2, const char* seat3 = kQuiet3) {
        FixedWallSpec spec;
        spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        spec.hands[kSeat1] = seat1;
        spec.hands[kSeat2] = seat2;
        spec.hands[kSeat3] = seat3;
        spec.live = "5p Rd Gd Wd 5p";
        spec.loose = "9m 9s";
        return spec;
    }

    inline HandModel OpenPungModel(const char* seat1, const char* seat2 = kQuiet2, const char* seat3 = kQuiet3, std::initializer_list<Seat> declaring = {}) {
        HandModel model = MakeModel(OpenPungSpec(seat1, seat2, seat3));
        EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Claim);
        EXPECT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
        Lap(model, "Wd Rd Gd Wd", declaring);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        return model;
    }

}  // namespace BECore::Mahjong::Tests
