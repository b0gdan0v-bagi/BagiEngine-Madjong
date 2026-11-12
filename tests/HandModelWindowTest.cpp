#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * `HandModel::LastClaimWindow()`: how the last claim window was settled, with the claims that lost. Fixed walls say what every seat
 * holds and draws. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // Seat 3 discards 4p: seat 0 can chow it (3p 5p), seat 1 pung it (4p 4p), seat 2 has declared a calling hand that waits on it.
        constexpr const char* kPriorityTile = "4p";
        constexpr const char* kPriorityChow = "3p";

        HandModel PriorityWindow() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "3p 5p 1s 4s 7s 1m 4m 7m 2s 8s Sw Ww Gd";
            spec.hands[kSeat1] = "4p 4p 7p 1s 4s 7s 2m 5m 8m Sw Ww Rd 9p";
            spec.hands[kSeat2] = "3p 5p 4s 5s 6s 7m 8m 9m Ew Ew Ew 1m 1m";
            spec.hands[kSeat3] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Rd Wd Nw 9s";
            spec.live = std::string("Wd Gd Nw ") + kPriorityTile;
            HandModel model = MakeModel(spec);
            Lap(model, "Wd Gd Nw", {kSeat2});
            EXPECT_TRUE(model.GetHand(kSeat2).callingDeclared);
            EXPECT_EQ(model.CurrentSeat(), kSeat3);
            EXPECT_TRUE(model.Discard(kSeat3, T(kPriorityTile)).has_value());
            return model;
        }

        // Seat 0 draws 5p and discards it: seat 1 can chow it (4p 6p), seat 2 can claim the kong (5p 5p 5p).
        HandModel KongWindow() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = "4p 6p 2s 5s 8s 2m 5m 8m Sw Ww Nw Rd Wd";
            spec.hands[kSeat2] = "5p 5p 5p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd";
            spec.hands[kSeat3] = "3p 8p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd Wd";
            spec.live = "5p";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        // Seat 0 discards 5p and seat 1 (the only one in the window) claims the pung; seat 1 discards Wd and seat 2 (the only one in the
        // window again) can pung it.
        FixedWallSpec TwoWindowsSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = "5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            spec.hands[kSeat2] = "Wd Wd 3s 6s 9s 3m 6m 9m 4p 6p Sw Ww Nw";
            spec.hands[kSeat3] = "3p 8p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd Gd";
            spec.live = "5p";
            return spec;
        }

        HandModel TwoWindowsModel() {
            HandModel model = MakeModel(TwoWindowsSpec());
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            EXPECT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
            EXPECT_TRUE(model.Discard(kSeat1, T("Wd")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        // Seat 1 holds an open pung of 5p and draws the fourth; seats 2 and 3 wait on the 5p and have declared.
        HandModel RobbingModel() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = "5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            spec.hands[kSeat2] = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Ew Ew";
            spec.hands[kSeat3] = "4p 6p 1m 2m 3m 4s 5s 6s 7m 8m 9m Ww Ww";
            spec.live = "5p Rd Gd Wd 5p";
            spec.loose = "9m 9s";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            EXPECT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
            Lap(model, "Wd Rd Gd Wd", {kSeat2, kSeat3});
            EXPECT_EQ(model.GetPhase(), Phase::Discard);
            EXPECT_EQ(model.CurrentSeat(), kSeat1);
            return model;
        }

    }  // namespace

    TEST(HandModelWindowTest, NoWindowSettledYetMeansNoRecord) {
        HandModel model = PriorityWindow();
        EXPECT_FALSE(model.LastClaimWindow().has_value());  // the window is open, not settled

        HandModel fresh = MakeModel(TwoWindowsSpec());
        EXPECT_FALSE(fresh.LastClaimWindow().has_value());
        EXPECT_FALSE(HandModel{}.LastClaimWindow().has_value());
    }

    TEST(HandModelWindowTest, APungBeatsAChowAndTheChowIsRecordedAsBeaten) {
        HandModel model = PriorityWindow();

        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow)).has_value());
        ASSERT_TRUE(model.Pass(kSeat2).has_value());
        EXPECT_FALSE(model.LastClaimWindow().has_value());
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());

        ASSERT_TRUE(model.LastClaimWindow().has_value());
        const ClaimWindowResult& window = *model.LastClaimWindow();
        EXPECT_EQ(window.serial, 1);
        EXPECT_EQ(window.from, kSeat3);
        EXPECT_EQ(window.tile, T(kPriorityTile));
        EXPECT_EQ(window.winner, kSeat1);
        EXPECT_EQ(window.winningKind, ClaimKind::Pung);
        ASSERT_EQ(window.beaten.size(), 1u);  // the pass of seat 2 is no claim
        EXPECT_EQ(window.beaten[0].seat, kSeat0);
        EXPECT_EQ(window.beaten[0].kind, ClaimKind::Chow);
        EXPECT_EQ(window.beaten[0].chowFirst, T(kPriorityChow));
    }

    TEST(HandModelWindowTest, AMahjongBeatsAPungAndAChowAndTheRecordSurvivesTheEndOfTheHand) {
        HandModel model = PriorityWindow();

        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow)).has_value());
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.LastClaimWindow().has_value());
        const ClaimWindowResult& window = *model.LastClaimWindow();
        EXPECT_EQ(window.serial, 1);
        EXPECT_EQ(window.from, kSeat3);
        EXPECT_EQ(window.tile, T(kPriorityTile));
        EXPECT_EQ(window.winner, kSeat2);
        EXPECT_EQ(window.winningKind, ClaimKind::Mahjong);
        ASSERT_EQ(window.beaten.size(), 2u);  // in seat order, whatever the order of the decisions
        EXPECT_EQ(window.beaten[0].seat, kSeat0);
        EXPECT_EQ(window.beaten[0].kind, ClaimKind::Chow);
        EXPECT_EQ(window.beaten[0].chowFirst, T(kPriorityChow));
        EXPECT_EQ(window.beaten[1].seat, kSeat1);
        EXPECT_EQ(window.beaten[1].kind, ClaimKind::Pung);
        EXPECT_FALSE(window.beaten[1].chowFirst.IsValid());
    }

    TEST(HandModelWindowTest, TheNearerMahjongClaimIsBeatenByTheFartherOne) {
        // Seats 0 and 1 have declared and wait on the red dragon; seat 3 discards it: seat 1 is farther counter-clockwise.
        FixedWallSpec spec;
        spec.hands[kSeat0] = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";
        spec.hands[kSeat1] = "2p 3p 4p 5s 6s 7s 7m 8m 9m Sw Sw Sw Rd";
        spec.hands[kSeat2] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ww Nw Wd Gd";
        spec.hands[kSeat3] = "2s 5s 8s 2m 5m 8m 4p 7p 1p Ww Nw Wd Gd";
        spec.live = "9p 9s Gd Rd";
        HandModel model = MakeModel(spec);
        Lap(model, "9p 9s Gd", {kSeat0, kSeat1});
        ASSERT_TRUE(model.Discard(kSeat3, T("Rd")).has_value());

        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Mahjong).has_value());
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Mahjong).has_value());

        ASSERT_TRUE(model.LastClaimWindow().has_value());
        const ClaimWindowResult& window = *model.LastClaimWindow();
        EXPECT_EQ(window.winner, kSeat1);
        EXPECT_EQ(window.winningKind, ClaimKind::Mahjong);
        ASSERT_EQ(window.beaten.size(), 1u);
        EXPECT_EQ(window.beaten[0].seat, kSeat0);
        EXPECT_EQ(window.beaten[0].kind, ClaimKind::Mahjong);
    }

    TEST(HandModelWindowTest, AKongBeatsAChow) {
        HandModel model = KongWindow();
        ASSERT_EQ(model.LegalClaims(kSeat2).size(), 2u);  // pung and kong

        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Chow, T("4p")).has_value());
        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Kong).has_value());

        ASSERT_TRUE(model.LastClaimWindow().has_value());
        const ClaimWindowResult& window = *model.LastClaimWindow();
        EXPECT_EQ(window.winner, kSeat2);
        EXPECT_EQ(window.winningKind, ClaimKind::Kong);
        EXPECT_EQ(window.from, kSeat0);
        ASSERT_EQ(window.beaten.size(), 1u);
        EXPECT_EQ(window.beaten[0].seat, kSeat1);
        EXPECT_EQ(window.beaten[0].kind, ClaimKind::Chow);
        EXPECT_EQ(window.beaten[0].chowFirst, T("4p"));
    }

    TEST(HandModelWindowTest, WhenEveryonePassesThereIsNoWinnerAndNoBeatenClaim) {
        HandModel model = PriorityWindow();

        for (const Seat seat : model.PendingClaimSeats()) {
            ASSERT_TRUE(model.Pass(seat).has_value());
        }

        ASSERT_TRUE(model.LastClaimWindow().has_value());
        const ClaimWindowResult& window = *model.LastClaimWindow();
        EXPECT_EQ(window.serial, 1);
        EXPECT_EQ(window.from, kSeat3);
        EXPECT_EQ(window.tile, T(kPriorityTile));
        EXPECT_EQ(window.winner, kNoSeat);
        EXPECT_TRUE(window.beaten.empty());
    }

    TEST(HandModelWindowTest, AwaitsClaimIsEveryPendingSeatAndNoOtherAndNobodyOutsideAWindow) {
        HandModel model = PriorityWindow();

        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const eastl::vector<Seat> pending = model.PendingClaimSeats();
            EXPECT_EQ(model.AwaitsClaim(seat), eastl::find(pending.begin(), pending.end(), seat) != pending.end()) << seat;
        }
        EXPECT_FALSE(model.AwaitsClaim(kNoSeat));

        for (const Seat seat : model.PendingClaimSeats()) {
            ASSERT_TRUE(model.Pass(seat).has_value());
            EXPECT_FALSE(model.AwaitsClaim(seat)) << "a seat that decided waits no more";
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_FALSE(model.AwaitsClaim(seat)) << "the window is closed";
        }
    }

    TEST(HandModelWindowTest, ALoneClaimerBeatsNobody) {
        HandModel model = PriorityWindow();

        ASSERT_TRUE(model.Pass(kSeat2).has_value());
        ASSERT_TRUE(model.Pass(kSeat1).has_value());
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow)).has_value());

        ASSERT_TRUE(model.LastClaimWindow().has_value());
        EXPECT_EQ(model.LastClaimWindow()->winner, kSeat0);
        EXPECT_EQ(model.LastClaimWindow()->winningKind, ClaimKind::Chow);
        EXPECT_TRUE(model.LastClaimWindow()->beaten.empty());
    }

    TEST(HandModelWindowTest, TheSerialGrowsByOnePerWindowAndBeginClearsTheRecord) {
        HandModel model = TwoWindowsModel();
        ASSERT_TRUE(model.LastClaimWindow().has_value());
        EXPECT_EQ(model.LastClaimWindow()->serial, 1);
        EXPECT_EQ(model.LastClaimWindow()->from, kSeat0);
        EXPECT_EQ(model.LastClaimWindow()->winner, kSeat1);
        const std::string open = Snapshot(model);

        // An action that is refused (seat 3 is not in the window, seat 1 is the discarder) changes nothing, the record included.
        EXPECT_EQ(Error(model.Claim(kSeat3, ClaimKind::Pung)), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Pass(kSeat1)), HandError::NotYourTurn);
        EXPECT_EQ(Snapshot(model), open);
        EXPECT_EQ(model.LastClaimWindow()->serial, 1);

        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Pung).has_value());
        ASSERT_TRUE(model.LastClaimWindow().has_value());
        EXPECT_EQ(model.LastClaimWindow()->serial, 2);
        EXPECT_EQ(model.LastClaimWindow()->from, kSeat1);
        EXPECT_EQ(model.LastClaimWindow()->tile, T("Wd"));
        EXPECT_EQ(model.LastClaimWindow()->winner, kSeat2);

        // A new hand on the same model starts over.
        const MahjongSettings settings;
        ASSERT_TRUE(model.Begin(settings, MakeFixedWall(TwoWindowsSpec(), settings.flowers), MakeSeatWinds(0), Wind::East).has_value());
        EXPECT_FALSE(model.LastClaimWindow().has_value());
    }

    TEST(HandModelWindowTest, ARefusedClaimLeavesTheWindowAndTheRecordAlone) {
        HandModel model = PriorityWindow();
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow)).has_value());
        const std::string decided = Snapshot(model);

        EXPECT_EQ(Error(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow))), HandError::AlreadyDecided);
        EXPECT_EQ(Error(model.Pass(kSeat0)), HandError::AlreadyDecided);
        EXPECT_EQ(Error(model.Claim(kSeat3, ClaimKind::Pung)), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Mahjong)), HandError::NotCalling);
        EXPECT_EQ(Snapshot(model), decided);
        EXPECT_FALSE(model.LastClaimWindow().has_value());
    }

    TEST(HandModelWindowTest, TheRobbingWindowLeavesTheRecordAsItWas) {
        HandModel model = RobbingModel();
        ASSERT_TRUE(model.LastClaimWindow().has_value());
        const int serial = model.LastClaimWindow()->serial;
        EXPECT_EQ(serial, 1);  // the window of seat 0's 5p, claimed as a pung by seat 1

        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);
        EXPECT_EQ(model.LastClaimWindow()->serial, serial);
        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());
        ASSERT_TRUE(model.Claim(kSeat3, ClaimKind::Mahjong).has_value());

        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->source, WinSource::RobbedKong);
        ASSERT_TRUE(model.LastClaimWindow().has_value());
        EXPECT_EQ(model.LastClaimWindow()->serial, serial);
        EXPECT_EQ(model.LastClaimWindow()->from, kSeat0);
        EXPECT_EQ(model.LastClaimWindow()->winner, kSeat1);
        EXPECT_EQ(model.LastClaimWindow()->winningKind, ClaimKind::Pung);
        EXPECT_TRUE(model.LastClaimWindow()->beaten.empty());
    }

    TEST(HandModelWindowTest, ARobbingWindowNobodyUsesLeavesTheRecordToo) {
        HandModel model = RobbingModel();
        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);

        ASSERT_TRUE(model.Pass(kSeat2).has_value());
        ASSERT_TRUE(model.Pass(kSeat3).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        ASSERT_TRUE(model.LastClaimWindow().has_value());
        EXPECT_EQ(model.LastClaimWindow()->serial, 1);
    }

}  // namespace BECore::Mahjong::Tests
