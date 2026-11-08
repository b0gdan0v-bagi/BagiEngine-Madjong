#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * What the model keeps for a front end besides the hands: `DrawnTile(seat)` (the tile just drawn, a regular or a loose one, nothing
 * after a claim) and the append-only log `Moves()` (discards, carried-out claims, the two kongs of the own turn). Fixed walls say what
 * every seat holds and draws. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kEastQuiet = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ww Nw Gd";

        // Seat 0 draws the 5p; seat 1 holds 4p 6p and can chow it (seat 2 and 3 cannot).
        FixedWallSpec ChowSpec() {
            FixedWallSpec spec = SpecOf(kEastQuiet, "5p 9m");
            spec.hands[kSeat1] = "4p 6p 2s 5s 8s 2m 5m 8m Sw Ww Nw Rd Wd";
            spec.hands[kSeat2] = "4p 6p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd Wd";
            spec.hands[kSeat3] = "3p 8p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd Wd";
            return spec;
        }

        // Seat 0 draws and discards the red dragon; seat 2 holds three of them and claims the kong.
        FixedWallSpec KongClaimSpec() {
            FixedWallSpec spec = SpecOf(kEastQuiet, "Rd");
            spec.hands[kSeat2] = "Rd Rd Rd 3p 6p 9p 3s 6s 9s 3m 6m 9m Sw";
            spec.loose = "8p 4s";
            return spec;
        }

        // East holds three east winds, draws the fourth and declares a concealed kong; the loose tile is 8p.
        FixedWallSpec ConcealedKongSpec() {
            FixedWallSpec spec = SpecOf("Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd", "Ew");
            spec.loose = "8p 4s";
            return spec;
        }

        // East waits on the red dragon (a pair wait) and declares with its first discard, the 9p it draws.
        FixedWallSpec CallingSpec() {
            return SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p");
        }

        /// The one move of the log at `index`, after checking that it exists.
        const HandMove& MoveAt(const HandModel& model, size_t index) {
            EXPECT_LT(index, model.Moves().size());
            static const HandMove none;
            return index < model.Moves().size() ? model.Moves()[index] : none;
        }

    }  // namespace

    // ----- the drawn tile -----

    TEST(HandModelMovesTest, TheSeatThatHasJustDrawnKnowsItsTileAndNoOtherSeatDoes) {
        const HandModel model = MakeModel(SpecOf(kEastQuiet, "5p"));

        EXPECT_EQ(model.DrawnTile(kSeat0), T("5p"));
        EXPECT_FALSE(model.DrawnTile(kSeat1).IsValid());
        EXPECT_FALSE(model.DrawnTile(kSeat2).IsValid());
        EXPECT_FALSE(model.DrawnTile(kSeat3).IsValid());
        EXPECT_FALSE(model.DrawnTile(kNoSeat).IsValid());
    }

    TEST(HandModelMovesTest, TheDrawnTileIsTheNextSeatsAfterADiscardWithoutAWindow) {
        HandModel model = MakeModel(SpecOf(kEastQuiet, "5p Rd"));

        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());

        ASSERT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_FALSE(model.DrawnTile(kSeat0).IsValid());
        EXPECT_EQ(model.DrawnTile(kSeat1), T("Rd"));
    }

    TEST(HandModelMovesTest, NoSeatHasADrawnTileInAClaimWindow) {
        HandModel model = MakeModel(ChowSpec());

        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());

        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_FALSE(model.DrawnTile(seat).IsValid()) << "seat " << seat;
        }
    }

    TEST(HandModelMovesTest, AfterTheClaimOfAChowOrAPungTheClaimerHoldsAnExtraTileButDrewNothing) {
        HandModel chow = MakeModel(ChowSpec());
        ASSERT_TRUE(chow.Discard(kSeat0, T("5p")).has_value());
        ASSERT_TRUE(chow.Claim(kSeat1, ClaimKind::Chow, T("4p")).has_value());
        ASSERT_EQ(chow.GetPhase(), Phase::Discard);
        ASSERT_EQ(chow.CurrentSeat(), kSeat1);
        EXPECT_FALSE(chow.DrawnTile(kSeat1).IsValid());

        HandModel pung = OpenPungModel(kSeat1Hand);  // seat 1 has pung-claimed the 5p and, after a lap, drawn the fifth tile of the wall
        EXPECT_EQ(pung.DrawnTile(kSeat1), T("5p"));
        HandModel claimed = MakeModel(OpenPungSpec(kSeat1Hand));
        ASSERT_TRUE(claimed.Discard(kSeat0, T("5p")).has_value());
        ASSERT_TRUE(claimed.Claim(kSeat1, ClaimKind::Pung).has_value());
        ASSERT_EQ(claimed.GetPhase(), Phase::Discard);
        EXPECT_FALSE(claimed.DrawnTile(kSeat1).IsValid());
    }

    TEST(HandModelMovesTest, AfterAKongTheDrawnTileIsTheLooseTile) {
        HandModel claimed = MakeModel(KongClaimSpec());
        ASSERT_TRUE(claimed.Discard(kSeat0, T("Rd")).has_value());
        ASSERT_TRUE(claimed.Claim(kSeat2, ClaimKind::Kong).has_value());
        ASSERT_EQ(claimed.GetPhase(), Phase::Discard);
        ASSERT_EQ(claimed.CurrentSeat(), kSeat2);
        EXPECT_EQ(claimed.DrawnTile(kSeat2), T("8p"));

        HandModel concealed = MakeModel(ConcealedKongSpec());
        ASSERT_TRUE(concealed.DeclareConcealedKong(kSeat0, T("Ew")).has_value());
        EXPECT_EQ(concealed.DrawnTile(kSeat0), T("8p"));
    }

    TEST(HandModelMovesTest, NoDrawnTileInARobbingWindowOrAfterTheEnd) {
        HandModel model = UpgradeModel({kSeat2});
        ASSERT_EQ(model.DrawnTile(kSeat1), T("5p"));

        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_FALSE(model.DrawnTile(seat).IsValid()) << "seat " << seat;
        }

        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_FALSE(model.DrawnTile(seat).IsValid()) << "seat " << seat;
        }
    }

    TEST(HandModelMovesTest, ADrawnTileThatIsReplacedGivesTheReplacement) {
        FixedWallSpec spec = SpecOf(kEastQuiet, "F1");
        spec.loose = "8p 4s";
        const HandModel model = MakeModel(spec, 0, Wind::East, true);

        EXPECT_EQ(model.DrawnTile(kSeat0), T("8p"));
        EXPECT_EQ(model.GetHand(kSeat0).bonus.size(), 1u);
    }

    // ----- the log -----

    TEST(HandModelMovesTest, ANewHandHasNoMoves) {
        EXPECT_TRUE(HandModel{}.Moves().empty());
        EXPECT_TRUE(MakeModel(SpecOf(kEastQuiet, "5p")).Moves().empty());
    }

    TEST(HandModelMovesTest, ADiscardIsLoggedWithAndWithoutTheDeclaration) {
        HandModel plain = MakeModel(SpecOf(kEastQuiet, "5p"));
        ASSERT_TRUE(plain.Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(plain.Moves().size(), 1u);
        const HandMove& move = plain.Moves()[0];
        EXPECT_EQ(move.kind, MoveKind::Discard);
        EXPECT_EQ(move.seat, kSeat0);
        EXPECT_EQ(move.tile, T("5p"));
        EXPECT_EQ(move.from, kNoSeat);
        EXPECT_EQ(move.meld, -1);
        EXPECT_FALSE(move.declaredCalling);

        HandModel calling = MakeModel(CallingSpec());
        ASSERT_TRUE(calling.Discard(kSeat0, T("9p"), true).has_value());
        ASSERT_EQ(calling.Moves().size(), 1u);
        EXPECT_EQ(calling.Moves()[0].kind, MoveKind::Discard);
        EXPECT_EQ(calling.Moves()[0].tile, T("9p"));
        EXPECT_TRUE(calling.Moves()[0].declaredCalling);
    }

    TEST(HandModelMovesTest, AClaimedChowIsLoggedAfterTheDiscardAndBeforeTheClaimersDiscard) {
        HandModel model = MakeModel(ChowSpec());
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(model.Moves().size(), 1u);  // a claim that has not been carried out is no move

        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Chow, T("4p")).has_value());
        ASSERT_EQ(model.Moves().size(), 2u);
        const HandMove& chow = model.Moves()[1];
        EXPECT_EQ(chow.kind, MoveKind::ClaimChow);
        EXPECT_EQ(chow.seat, kSeat1);
        EXPECT_EQ(chow.tile, T("5p"));
        EXPECT_EQ(chow.from, kSeat0);
        EXPECT_EQ(chow.meld, 0);
        EXPECT_FALSE(chow.declaredCalling);

        ASSERT_TRUE(model.Discard(kSeat1, T("2s")).has_value());
        ASSERT_EQ(model.Moves().size(), 3u);
        EXPECT_EQ(MoveAt(model, 0).kind, MoveKind::Discard);
        EXPECT_EQ(MoveAt(model, 1).kind, MoveKind::ClaimChow);
        EXPECT_EQ(MoveAt(model, 2).kind, MoveKind::Discard);
        EXPECT_EQ(MoveAt(model, 2).seat, kSeat1);
        EXPECT_EQ(MoveAt(model, 2).tile, T("2s"));
    }

    TEST(HandModelMovesTest, AClaimedPungAndAClaimedKongAreLogged) {
        HandModel pung = MakeModel(OpenPungSpec(kSeat1Hand));
        ASSERT_TRUE(pung.Discard(kSeat0, T("5p")).has_value());
        ASSERT_TRUE(pung.Claim(kSeat1, ClaimKind::Pung).has_value());
        ASSERT_EQ(pung.Moves().size(), 2u);
        EXPECT_EQ(MoveAt(pung, 1).kind, MoveKind::ClaimPung);
        EXPECT_EQ(MoveAt(pung, 1).seat, kSeat1);
        EXPECT_EQ(MoveAt(pung, 1).tile, T("5p"));
        EXPECT_EQ(MoveAt(pung, 1).from, kSeat0);
        EXPECT_EQ(MoveAt(pung, 1).meld, 0);

        HandModel kong = MakeModel(KongClaimSpec());
        ASSERT_TRUE(kong.Discard(kSeat0, T("Rd")).has_value());
        ASSERT_TRUE(kong.Claim(kSeat2, ClaimKind::Kong).has_value());
        ASSERT_EQ(kong.Moves().size(), 2u);
        EXPECT_EQ(MoveAt(kong, 1).kind, MoveKind::ClaimKong);
        EXPECT_EQ(MoveAt(kong, 1).seat, kSeat2);
        EXPECT_EQ(MoveAt(kong, 1).tile, T("Rd"));
        EXPECT_EQ(MoveAt(kong, 1).from, kSeat0);
        EXPECT_EQ(MoveAt(kong, 1).meld, 0);
    }

    TEST(HandModelMovesTest, TheIndexOfTheMeldCountsTheSetsAlreadyLaid) {
        // Seat 1 claims a pung (meld 0); after a lap East discards the 2s and seat 1 chows it with its 3s 4s (meld 1).
        FixedWallSpec spec = OpenPungSpec("5p 5p 3s 4s 8s 2m 5m 8m Ww Nw Gd Wd 1m");
        spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd 4s";
        spec.live = "5p Rd Gd 2s";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
        ASSERT_EQ(model.Moves().back().meld, 0);
        Lap(model, "Wd Rd Gd");
        ASSERT_EQ(model.CurrentSeat(), kSeat0);

        ASSERT_TRUE(model.Discard(kSeat0, T("2s")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Chow, T("2s")).has_value());

        EXPECT_EQ(model.Moves().back().kind, MoveKind::ClaimChow);
        EXPECT_EQ(model.Moves().back().meld, 1);
        EXPECT_EQ(model.GetHand(kSeat1).melds.size(), 2u);
    }

    TEST(HandModelMovesTest, AConcealedKongIsLoggedWithoutItsTile) {
        HandModel model = MakeModel(ConcealedKongSpec());

        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T("Ew")).has_value());

        ASSERT_EQ(model.Moves().size(), 1u);
        const HandMove& move = model.Moves()[0];
        EXPECT_EQ(move.kind, MoveKind::ConcealedKong);
        EXPECT_EQ(move.seat, kSeat0);
        EXPECT_FALSE(move.tile.IsValid());  // the tile of a concealed kong is not public
        EXPECT_EQ(move.from, kNoSeat);
        EXPECT_EQ(move.meld, 0);
    }

    TEST(HandModelMovesTest, AnUpgradedKongIsLoggedWithThePungItGrowsFrom) {
        HandModel model = UpgradeModel();
        const size_t before = model.Moves().size();

        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());

        ASSERT_EQ(model.Moves().size(), before + 1);
        const HandMove& move = model.Moves().back();
        EXPECT_EQ(move.kind, MoveKind::UpgradeKong);
        EXPECT_EQ(move.seat, kSeat1);
        EXPECT_EQ(move.tile, T("5p"));
        EXPECT_EQ(move.from, kNoSeat);
        EXPECT_EQ(move.meld, 0);
        EXPECT_EQ(model.GetHand(kSeat1).melds[0].kind, MeldKind::Kong);
    }

    TEST(HandModelMovesTest, AnUpgradedKongThatIsRobbedIsLoggedToo) {
        HandModel model = UpgradeModel({kSeat2});
        const size_t before = model.Moves().size();

        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);
        ASSERT_EQ(model.Moves().size(), before + 1);
        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());

        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_EQ(model.Moves().size(), before + 1);  // the robbery is the result, not a move
        EXPECT_EQ(model.Moves().back().kind, MoveKind::UpgradeKong);
        EXPECT_EQ(model.Moves().back().meld, 0);
    }

    TEST(HandModelMovesTest, APassIsNotAMove) {
        HandModel model = MakeModel(ChowSpec());
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        ASSERT_TRUE(model.Pass(kSeat1).has_value());

        EXPECT_EQ(model.Moves().size(), 1u);  // only the discard
        EXPECT_EQ(model.GetPhase(), Phase::Discard);

        HandModel upgrade = UpgradeModel({kSeat2});
        ASSERT_TRUE(upgrade.UpgradeKong(kSeat1, T("5p")).has_value());
        const size_t logged = upgrade.Moves().size();
        ASSERT_TRUE(upgrade.Pass(kSeat2).has_value());  // the robber lets the kong stand
        EXPECT_EQ(upgrade.Moves().size(), logged);
    }

    TEST(HandModelMovesTest, ARefusedActionLogsNothing) {
        HandModel model = MakeModel(SpecOf(kEastQuiet, "5p"));
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.Discard(kSeat1, T("5p"))), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Discard(kSeat0, T("9p"))), HandError::TileNotInHand);
        EXPECT_EQ(Error(model.Discard(kSeat0, T("5p"), true)), HandError::NotWaiting);
        EXPECT_EQ(Error(model.DeclareConcealedKong(kSeat0, T("5p"))), HandError::IllegalKong);
        EXPECT_EQ(Error(model.UpgradeKong(kSeat0, T("5p"))), HandError::IllegalKong);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Pung)), HandError::WrongPhase);

        EXPECT_TRUE(model.Moves().empty());
        EXPECT_EQ(Snapshot(model), before);
    }

    TEST(HandModelMovesTest, ABeginClearsTheLog) {
        HandModel model = MakeModel(SpecOf(kEastQuiet, "5p"));
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_FALSE(model.Moves().empty());

        const HandStatus begun = model.Begin(MahjongSettings{}, MakeFixedWall(SpecOf(kEastQuiet, "5p"), false, 0), MakeSeatWinds(0), Wind::East);

        ASSERT_TRUE(begun.has_value());
        EXPECT_TRUE(model.Moves().empty());
        EXPECT_EQ(model.DrawnTile(kSeat0), T("5p"));
    }

    TEST(HandModelMovesTest, ABeginThatIsRefusedKeepsTheLog) {
        HandModel model = MakeModel(SpecOf(kEastQuiet, "5p"));
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        SeatWinds noEast = MakeSeatWinds(0);
        noEast[0] = Wind::West;

        const HandStatus begun = model.Begin(MahjongSettings{}, MakeFixedWall(SpecOf(kEastQuiet, "5p"), false, 0), noEast, Wind::East);

        EXPECT_EQ(Error(begun), HandError::BadSeatWinds);
        EXPECT_EQ(model.Moves().size(), 1u);
    }

}  // namespace BECore::Mahjong::Tests
