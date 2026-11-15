#include <Mahjong/Arena/ArenaChartsClasses.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <tests/ComputerTestSupport.h>

namespace BECore::Mahjong::Arena {

    namespace {

        using namespace Tests;

        constexpr Seat kClaimer = 1;
        constexpr int kEnoughLiveTiles = 60;

        /// A claim window of seat 1 on the discard `pending` of East: the hand, the winds, the live wall and the melds are the test's own.
        TableView WindowOf(const char* concealed, const char* pending, Wind seatWind = Wind::South, int liveCount = kEnoughLiveTiles) {
            TableView view;
            view.seat = kClaimer;
            view.phase = Phase::Claim;
            view.currentSeat = kSeat0;
            view.pendingTile = T(pending);
            view.seatWind = seatWind;
            view.roundWind = Wind::East;
            view.own.concealed = ParseTiles(concealed).value();
            view.liveCount = liveCount;
            view.legal.push_back(HandAction::MakePass());
            return view;
        }

        ClaimClass Classify(const TableView& view, ClaimKind kind, const char* chowFirst = "") {
            const HandAction claim = HandAction::MakeClaim(kind, kind == ClaimKind::Chow ? T(chowFirst) : Tile{});
            return ClassifyClaim(view, claim);
        }

        Meld OpenPung(const char* tile) {
            Meld meld;
            meld.kind = MeldKind::Pung;
            meld.first = T(tile);
            return meld;
        }

        Meld ConcealedKong(const char* tile) {
            Meld meld;
            meld.kind = MeldKind::Kong;
            meld.concealed = true;
            meld.first = T(tile);
            return meld;
        }

        // Seat 1 holds 4p 5p 5p 5p 6p and four short runs that fit nothing else: two from complete.
        constexpr const char* kSeat1Runs = "4p 6p 5p 5p 5p 1s 2s 7s 8s 3m 4m 8m 9m";
        constexpr const char* kOtherSeat2 = "1p 9p 1s 9s 1m 9m Ew Sw Nw Gd Wd Rd Ww";
        constexpr const char* kOtherSeat3 = "2p 3p 8p 2s 8s 2m 8m Ww Nw Ew Sw Gd Rd";

        FixedWallSpec RunsSpec(const char* seat2, const char* seat3, const char* live) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = kSeat1Runs;
            spec.hands[kSeat2] = seat2;
            spec.hands[kSeat3] = seat3;
            spec.live = live;
            spec.loose = "9m 9s";
            return spec;
        }

        TableView RunsWindow(const FixedWallSpec& spec) {
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return MakeTableView(model, kClaimer);
        }

    }  // namespace

    TEST(ArenaChartsClassesTest, TheLiveWallAtTheDealIsTheOneTheRulesLeave) {
        const MahjongSettings settings;
        MahjongRng rng(7);
        HandModel hand;
        ASSERT_TRUE(hand.Begin(settings, MakeWall(rng, settings, 0), MakeSeatWinds(0), Wind::East).has_value());
        // East has drawn its first tile already.
        EXPECT_EQ(static_cast<int>(hand.GetWall().LiveCount()) + 1, kLiveWallAtDeal);
    }

    TEST(ArenaChartsClassesTest, StagesAreThirdsOfTheLiveWallAtTheDeal) {
        EXPECT_EQ(StageOfLiveCount(kLiveWallAtDeal), WallStage::Early);
        EXPECT_EQ(StageOfLiveCount(2 * kLiveStageThird + 1), WallStage::Early);
        EXPECT_EQ(StageOfLiveCount(2 * kLiveStageThird), WallStage::Middle);
        EXPECT_EQ(StageOfLiveCount(kLiveStageThird + 1), WallStage::Middle);
        EXPECT_EQ(StageOfLiveCount(kLiveStageThird), WallStage::Late);
        EXPECT_EQ(StageOfLiveCount(0), WallStage::Late);
        EXPECT_GE(kLiveWallAtDeal, 3 * kLiveStageThird);
    }

    TEST(ArenaChartsClassesTest, ShantenBinsAtTheirEdges) {
        EXPECT_EQ(ShantenBin(-1), ShantenBefore::Zero);
        EXPECT_EQ(ShantenBin(0), ShantenBefore::Zero);
        EXPECT_EQ(ShantenBin(1), ShantenBefore::One);
        EXPECT_EQ(ShantenBin(2), ShantenBefore::Two);
        EXPECT_EQ(ShantenBin(3), ShantenBefore::ThreeOrMore);
        EXPECT_EQ(ShantenBin(4), ShantenBefore::ThreeOrMore);
    }

    TEST(ArenaChartsClassesTest, ShantenChangeIsTheSignOfTheDifference) {
        EXPECT_EQ(ShantenChangeOf(2, 1), ShantenChange::Lower);
        EXPECT_EQ(ShantenChangeOf(2, 2), ShantenChange::Equal);
        EXPECT_EQ(ShantenChangeOf(2, 3), ShantenChange::Higher);
    }

    TEST(ArenaChartsClassesTest, OpenSetsBinsAtTheirEdges) {
        EXPECT_EQ(OpenSetsBin(0), OpenSets::None);
        EXPECT_EQ(OpenSetsBin(1), OpenSets::One);
        EXPECT_EQ(OpenSetsBin(2), OpenSets::TwoOrMore);
        EXPECT_EQ(OpenSetsBin(3), OpenSets::TwoOrMore);
    }

    TEST(ArenaChartsClassesTest, TileGroupsKnowTheSeatsWindFromAnotherWind) {
        // Seat wind South, round wind East.
        const auto group = [](const char* tile) { return TileGroupOf(T(tile), Wind::South, Wind::East); };
        EXPECT_EQ(group("2p"), TileGroup::Simple);
        EXPECT_EQ(group("8m"), TileGroup::Simple);
        EXPECT_EQ(group("1p"), TileGroup::Terminal);
        EXPECT_EQ(group("9s"), TileGroup::Terminal);
        EXPECT_EQ(group("Rd"), TileGroup::ValueHonour);
        EXPECT_EQ(group("Gd"), TileGroup::ValueHonour);
        EXPECT_EQ(group("Wd"), TileGroup::ValueHonour);
        EXPECT_EQ(group("Sw"), TileGroup::ValueHonour);
        EXPECT_EQ(group("Ew"), TileGroup::ValueHonour);
        EXPECT_EQ(group("Ww"), TileGroup::OtherHonour);
        EXPECT_EQ(group("Nw"), TileGroup::OtherHonour);
        // The same wind is another seat's, so no value.
        EXPECT_EQ(TileGroupOf(T("Sw"), Wind::West, Wind::East), TileGroup::OtherHonour);
    }

    TEST(ArenaChartsClassesTest, OnlyAChowAPungAndAKongAreCharted) {
        EXPECT_TRUE(IsChartedClaim(HandAction::MakeClaim(ClaimKind::Chow, T("4p"))));
        EXPECT_TRUE(IsChartedClaim(HandAction::MakeClaim(ClaimKind::Pung)));
        EXPECT_TRUE(IsChartedClaim(HandAction::MakeClaim(ClaimKind::Kong)));
        EXPECT_FALSE(IsChartedClaim(HandAction::MakeClaim(ClaimKind::Mahjong)));
        EXPECT_FALSE(IsChartedClaim(HandAction::MakePass()));
        EXPECT_FALSE(IsChartedClaim(HandAction::MakeDiscard(T("5p"))));
    }

    TEST(ArenaChartsClassesTest, AChowAPungAndAKongOfOneDiscardAreThreeClasses) {
        const TableView view = RunsWindow(RunsSpec(kQuiet2, kQuiet3, "5p Rd Gd Wd"));
        ASSERT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Chow, T("4p"))));
        ASSERT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Pung)));
        ASSERT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Kong)));

        const ClaimClass chow = ClassifyClaim(view, HandAction::MakeClaim(ClaimKind::Chow, T("4p")));
        const ClaimClass pung = ClassifyClaim(view, HandAction::MakeClaim(ClaimKind::Pung));
        const ClaimClass kong = ClassifyClaim(view, HandAction::MakeClaim(ClaimKind::Kong));
        EXPECT_EQ(chow.kind, ClaimChartKind::Chow);
        EXPECT_EQ(pung.kind, ClaimChartKind::Pung);
        EXPECT_EQ(kong.kind, ClaimChartKind::Kong);
        for (const ClaimClass& cls : {chow, pung, kong}) {
            EXPECT_EQ(cls.tile, TileGroup::Simple);
            EXPECT_EQ(cls.shantenBefore, ShantenBefore::Two);
            EXPECT_EQ(cls.stage, WallStage::Early);
            EXPECT_FALSE(cls.east);
            EXPECT_EQ(cls.openSets, OpenSets::None);
        }
        // A kong takes three tiles out of the hand and gives no discard back, so it is the one that leaves the hand further away.
        EXPECT_EQ(chow.shantenChange, ShantenChange::Equal);
        EXPECT_EQ(pung.shantenChange, ShantenChange::Equal);
        EXPECT_EQ(kong.shantenChange, ShantenChange::Higher);
    }

    TEST(ArenaChartsClassesTest, APairMadeASetLowersTheShanten) {
        // A pair of 5p and four lone tiles: the pung makes a set and the pair was worth less than the set.
        const TableView view = WindowOf("5p 5p 1s 2s 7s 8s 2m 5m 8m Ww Nw Gd Wd", "5p");
        const ClaimClass pung = Classify(view, ClaimKind::Pung);
        EXPECT_EQ(pung.shantenBefore, ShantenBefore::ThreeOrMore);
        EXPECT_EQ(pung.shantenChange, ShantenChange::Lower);
    }

    TEST(ArenaChartsClassesTest, TheClassReadsTheSeatsWindTheWallAndTheMelds) {
        TableView view = WindowOf("5p 5p 1s 2s 7s 8s 2m 5m", "Rd", Wind::East, kLiveStageThird);
        view.own.concealed = ParseTiles("Rd Rd 1s 2s 7s 8s 2m 5m").value();
        view.own.melds = {OpenPung("2p"), OpenPung("3p"), ConcealedKong("9m")};
        const ClaimClass pung = Classify(view, ClaimKind::Pung);
        EXPECT_EQ(pung.tile, TileGroup::ValueHonour);
        EXPECT_TRUE(pung.east);
        EXPECT_EQ(pung.stage, WallStage::Late);
        // Two open pungs, and the concealed kong is not open.
        EXPECT_EQ(pung.openSets, OpenSets::TwoOrMore);

        view.own.melds = {OpenPung("2p"), ConcealedKong("9m")};
        EXPECT_EQ(Classify(view, ClaimKind::Pung).openSets, OpenSets::One);
        view.own.melds = {ConcealedKong("9m")};
        EXPECT_EQ(Classify(view, ClaimKind::Pung).openSets, OpenSets::None);
    }

    TEST(ArenaChartsClassesTest, TwoModelsThatDifferOnlyInHiddenTilesGiveOneClass) {
        const TableView first = RunsWindow(RunsSpec(kQuiet2, kQuiet3, "5p Rd Gd Wd"));
        const TableView second = RunsWindow(RunsSpec(kOtherSeat2, kOtherSeat3, "5p Wd Wd Rd"));
        ASSERT_EQ(first.legal, second.legal);
        for (const HandAction& action : first.legal) {
            if (IsChartedClaim(action)) {
                EXPECT_EQ(ClassifyClaim(first, action), ClassifyClaim(second, action));
            }
        }
    }

    TEST(ArenaChartsClassesTest, NamesAreTheWordsOfTheReport) {
        EXPECT_EQ(KindName(ClaimChartKind::Kong), "kong");
        EXPECT_EQ(TileGroupName(TileGroup::ValueHonour), "value honour");
        EXPECT_EQ(ShantenBeforeName(ShantenBefore::ThreeOrMore), "3+");
        EXPECT_EQ(ShantenChangeName(ShantenChange::Higher), "higher");
        EXPECT_EQ(WallStageName(WallStage::Middle), "middle");
        EXPECT_EQ(OpenSetsName(OpenSets::TwoOrMore), "2+");
        EXPECT_EQ(EastName(true), "east");
        EXPECT_EQ(EastName(false), "not east");
    }

}  // namespace BECore::Mahjong::Arena
