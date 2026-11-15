#include <Mahjong/Arena/ArenaChartsDiscards.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>

namespace BECore::Mahjong::Arena {

    namespace {

        using namespace Tests;

        constexpr int kEnoughLiveTiles = 60;

        /// The own discard turn of seat 0 holding `concealed` (14 tiles less three a meld): every distinct tile is listed as a plain discard
        /// and the seat sees its own tiles only. A view of a real model is used where the legal list or the seen tiles matter.
        TableView TurnOf(const char* concealed, Wind seatWind = Wind::South, int liveCount = kEnoughLiveTiles) {
            TableView view;
            view.seat = kSeat0;
            view.phase = Phase::Discard;
            view.currentSeat = kSeat0;
            view.seatWind = seatWind;
            view.roundWind = Wind::East;
            view.own.concealed = ParseTiles(concealed).value();
            view.liveCount = liveCount;
            view.visible = CountTiles(view.own.concealed);
            Tile previous;
            for (const Tile tile : view.own.concealed) {
                if (!(tile == previous)) {
                    view.legal.push_back(HandAction::MakeDiscard(tile));
                }
                previous = tile;
            }
            return view;
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

        // The hands below are first turns of dealt walls, found by search; each comment is what the two readings do with the hand (seat wind
        // South, round wind East, so the dragons and Ew are value tiles).
        // Normal throws the 1s, the points reading the 4s, which costs a shanten.
        constexpr const char* kHigherHand = "1p 2p 4p 5p 8p 8p 9p 1s 4s 4s 5s 7s Ew Wd";
        // Normal throws the Wd, the points reading the 8p; the shanten stays.
        constexpr const char* kSameTwoHand = "8p 9p 9p 2s 3s 4s 5s 5s 6s 6s 7s 8s Rd Wd";
        // Normal throws the 9p, the points reading the Wd; the shanten stays.
        constexpr const char* kSameOneHand = "1p 2p 3p 3p 4p 5p 6p 7p 7p 9p 2s 3s 4s Wd";
        // Normal throws the Wd, the points reading the 7p; the shanten stays.
        constexpr const char* kSameThreeHand = "2p 3p 3p 4p 7p 8p 8p 2s 6s 7s 8s 8s Ew Wd";
        // The valued tile is one of Normal's ties here.
        constexpr const char* kTiedHand = "3p 4p 4p 5p 8p 2s 3s 6s 8s 3m 8m Ew Nw Gd";

        DiscardSelection Select(const TableView& view) {
            return SelectDiscardPair(view);
        }

        DiscardClass Measured(const TableView& view) {
            const DiscardSelection selection = Select(view);
            EXPECT_EQ(selection.outcome, DiscardOutcome::Measured);
            return ClassifyDiscard(view, selection.pair);
        }

        // East holds `east` (13 tiles) and draws the first tile of `live`; the others hold the quiet hands unless the test names theirs.
        HandModel TurnModel(const char* east, const String& live, const char* seat2 = kQuiet2, const char* seat3 = kQuiet3) {
            FixedWallSpec spec = SpecOf(east, live);
            spec.hands[kSeat2] = seat2;
            spec.hands[kSeat3] = seat3;
            return MakeModel(spec);
        }

    }  // namespace

    TEST(ArenaChartsDiscardsTest, ShantenBinsAtTheirEdges) {
        EXPECT_EQ(DiscardShantenBin(0), DiscardShanten::One);
        EXPECT_EQ(DiscardShantenBin(1), DiscardShanten::One);
        EXPECT_EQ(DiscardShantenBin(2), DiscardShanten::Two);
        EXPECT_EQ(DiscardShantenBin(3), DiscardShanten::ThreeOrMore);
        EXPECT_EQ(DiscardShantenBin(4), DiscardShanten::ThreeOrMore);
    }

    TEST(ArenaChartsDiscardsTest, CostIsHigherOnlyWhenTheValuedDiscardLeavesAHigherShanten) {
        EXPECT_EQ(ValueCostOf(2, 2), ValueCost::Same);
        EXPECT_EQ(ValueCostOf(2, 3), ValueCost::Higher);
        EXPECT_EQ(ValueCostOf(1, 2), ValueCost::Higher);
    }

    TEST(ArenaChartsDiscardsTest, ValueHeldBinsAtTheirEdges) {
        EXPECT_EQ(ValueHeldBin(0), ValueHeld::None);
        EXPECT_EQ(ValueHeldBin(1), ValueHeld::Single);
        EXPECT_EQ(ValueHeldBin(2), ValueHeld::PairOrSet);
        EXPECT_EQ(ValueHeldBin(3), ValueHeld::PairOrSet);
        EXPECT_EQ(ValueHeldBin(4), ValueHeld::PairOrSet);
    }

    TEST(ArenaChartsDiscardsTest, TheMostCopiesOfAValueTileCountTheMeldsAndNoOtherTile) {
        // Seat wind South, round wind East: the dragons, Sw and Ew are value tiles, Ww and Nw are not.
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Ww Nw Nw 5p")), 0);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Ww Nw Nw Gd")), 1);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Ww Nw Sw Sw")), 2);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Nw Rd")), 3);
        // The dragon, the own wind and the round wind each count; another seat's wind does not.
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Wd Wd Nw Nw 5p")), 2);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Nw Nw 5p")), 2);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Ww Nw Nw 5p", Wind::West)), 2);

        TableView meld = TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Nw 5p");
        meld.own.melds.push_back(OpenPung("Rd"));
        EXPECT_EQ(MostValueCopies(meld), 3);
        // A meld and a copy in the hand are one holding.
        meld.own.concealed = ParseTiles("1p 2p 3p 4s 5s 6s 7m 8m Rd Ww Nw").value();
        EXPECT_EQ(MostValueCopies(meld), 4);
        // A chow holds no value tile; a pung of a plain tile is no value.
        TableView plain = TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Nw 5p");
        plain.own.melds.push_back(OpenPung("4p"));
        EXPECT_EQ(MostValueCopies(plain), 0);
    }

    TEST(ArenaChartsDiscardsTest, TheValuedDiscardThatCostsAShantenIsHigher) {
        const TableView view = TurnOf(kHigherHand, Wind::East);
        const DiscardSelection selection = Select(view);
        ASSERT_EQ(selection.outcome, DiscardOutcome::Measured);
        EXPECT_EQ(selection.pair.fast, T("1s"));
        EXPECT_EQ(selection.pair.valued, T("4s"));

        const DiscardClass cls = ClassifyDiscard(view, selection.pair);
        EXPECT_EQ(cls.cost, ValueCost::Higher);
        EXPECT_EQ(cls.shanten, DiscardShanten::ThreeOrMore);
        EXPECT_EQ(cls.valueHeld, ValueHeld::Single);
        EXPECT_EQ(cls.stage, WallStage::Early);
        EXPECT_EQ(cls.openSets, OpenSets::None);
        EXPECT_TRUE(cls.east);
        EXPECT_FALSE(ClassifyDiscard(TurnOf(kHigherHand, Wind::South), selection.pair).east);
    }

    TEST(ArenaChartsDiscardsTest, TheValuedDiscardThatKeepsTheShantenIsSameAtEveryShanten) {
        struct Case {
            const char* hand;
            const char* fast;
            const char* valued;
            DiscardShanten shanten;
        };
        const Case cases[] = {
            {kSameOneHand, "9p", "Wd", DiscardShanten::One},
            {kSameTwoHand, "Wd", "8p", DiscardShanten::Two},
            {kSameThreeHand, "Wd", "7p", DiscardShanten::ThreeOrMore},
        };
        for (const Case& test : cases) {
            const TableView view = TurnOf(test.hand);
            const DiscardSelection selection = Select(view);
            ASSERT_EQ(selection.outcome, DiscardOutcome::Measured) << test.hand;
            EXPECT_EQ(selection.pair.fast, T(test.fast)) << test.hand;
            EXPECT_EQ(selection.pair.valued, T(test.valued)) << test.hand;
            const DiscardClass cls = ClassifyDiscard(view, selection.pair);
            EXPECT_EQ(cls.cost, ValueCost::Same) << test.hand;
            EXPECT_EQ(cls.shanten, test.shanten) << test.hand;
            EXPECT_EQ(cls.valueHeld, ValueHeld::Single) << test.hand;
            EXPECT_FALSE(cls.east);
        }
    }

    TEST(ArenaChartsDiscardsTest, TheClassFollowsTheStageAndTheOpenMelds) {
        const DiscardPair pair{T("1s"), T("4s")};
        ASSERT_EQ(Select(TurnOf(kHigherHand)).pair, pair);

        // The cut values of the wall are those of the claim charts.
        const auto stageAt = [&pair](int liveCount) { return ClassifyDiscard(TurnOf(kHigherHand, Wind::South, liveCount), pair).stage; };
        EXPECT_EQ(stageAt(kLiveWallAtDeal), WallStage::Early);
        EXPECT_EQ(stageAt(2 * kLiveStageThird + 1), WallStage::Early);
        EXPECT_EQ(stageAt(2 * kLiveStageThird), WallStage::Middle);
        EXPECT_EQ(stageAt(kLiveStageThird + 1), WallStage::Middle);
        EXPECT_EQ(stageAt(kLiveStageThird), WallStage::Late);
        EXPECT_EQ(stageAt(0), WallStage::Late);

        // A hand with melds holds fewer concealed tiles; a concealed kong is not an open set.
        const char* const concealedOf[] = {kHigherHand, "4p 5p 8p 8p 9p 1s 4s 4s 5s 7s Wd", "8p 8p 1s 4s 4s 5s 7s Wd", "1s 4s 4s 7s Wd"};
        const char* const pungs[] = {"1p", "9p", "1m"};
        const auto openSets = [&](int open, bool concealedKong) {
            TableView view = TurnOf(concealedOf[open + (concealedKong ? 1 : 0)]);
            for (int i = 0; i < open; ++i) {
                view.own.melds.push_back(OpenPung(pungs[i]));
            }
            if (concealedKong) {
                view.own.melds.push_back(ConcealedKong("Rd"));
            }
            return ClassifyDiscard(view, pair).openSets;
        };
        EXPECT_EQ(openSets(0, false), OpenSets::None);
        EXPECT_EQ(openSets(0, true), OpenSets::None);
        EXPECT_EQ(openSets(1, false), OpenSets::One);
        EXPECT_EQ(openSets(2, false), OpenSets::TwoOrMore);
        EXPECT_EQ(openSets(1, true), OpenSets::One);
        EXPECT_EQ(openSets(2, true), OpenSets::TwoOrMore);
    }

    TEST(ArenaChartsDiscardsTest, AValuedTileThatIsOneOfNormalsTiesIsNotADisagreement) {
        const TableView view = TurnOf(kTiedHand, Wind::East);
        EXPECT_EQ(Select(view).outcome, DiscardOutcome::SameAsNormal);
    }

    TEST(ArenaChartsDiscardsTest, ATurnWithNoRatedDiscardIsNoValue) {
        TableView view = TurnOf(kHigherHand, Wind::East);
        ASSERT_EQ(Select(view).outcome, DiscardOutcome::Measured);
        view.legal.clear();
        EXPECT_EQ(Select(view).outcome, DiscardOutcome::NoValue);
    }

    TEST(ArenaChartsDiscardsTest, ATurnThatIsNotNormalsStepFiveIsOtherStep) {
        // Step 1: the win is listed.
        HandModel winning = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Rd"));
        Lap(winning, "9p Wd Gd Nw", {kSeat0});
        ASSERT_TRUE(Has(MakeTableView(winning, kSeat0).legal, HandAction::MakeDeclareWin()));
        EXPECT_EQ(Select(MakeTableView(winning, kSeat0)).outcome, DiscardOutcome::OtherStep);

        // Step 2: a discard that declares the calling hand is listed.
        const HandModel calling = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew"));
        ASSERT_TRUE(Has(MakeTableView(calling, kSeat0).legal, HandAction::MakeDiscard(T("6p"), true)));
        EXPECT_EQ(Select(MakeTableView(calling, kSeat0)).outcome, DiscardOutcome::OtherStep);

        // Step 4: a declared hand keeps its wait.
        HandModel declared = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "Gd Wd Gd Nw 9p"));
        Lap(declared, "Gd Wd Gd Nw", {kSeat0});
        const TableView keeping = MakeTableView(declared, kSeat0);
        ASSERT_TRUE(keeping.own.callingDeclared);
        EXPECT_EQ(Select(keeping).outcome, DiscardOutcome::OtherStep);
    }

    TEST(ArenaChartsDiscardsTest, AViewThatIsNotTheOwnDiscardTurnIsOtherStep) {
        EXPECT_EQ(Select(TableView{}).outcome, DiscardOutcome::OtherStep);
        TableView window = TurnOf(kHigherHand, Wind::East);
        window.phase = Phase::Claim;
        EXPECT_EQ(Select(window).outcome, DiscardOutcome::OtherStep);
        TableView other = TurnOf(kHigherHand, Wind::East);
        other.currentSeat = kSeat1;
        EXPECT_EQ(Select(other).outcome, DiscardOutcome::OtherStep);
    }

    TEST(ArenaChartsDiscardsTest, TwoModelsThatDifferOnlyInHiddenTilesGiveOneSelectionAndOneClass) {
        const char* const east = "1p 2p 4p 5p 8p 8p 9p 1s 4s 4s 5s 7s Ew";
        const HandModel a = TurnModel(east, "Wd Rd Wd 1p");
        const HandModel b = TurnModel(east, "Wd 1p 5m Wd", kQuiet3, kQuiet2);
        const TableView viewA = MakeTableView(a, kSeat0);
        const TableView viewB = MakeTableView(b, kSeat0);
        ASSERT_EQ(viewA.visible, viewB.visible);

        const DiscardSelection first = Select(viewA);
        const DiscardSelection second = Select(viewB);
        ASSERT_EQ(first.outcome, DiscardOutcome::Measured);
        EXPECT_EQ(first.outcome, second.outcome);
        EXPECT_EQ(first.pair, second.pair);
        EXPECT_EQ(ClassifyDiscard(viewA, first.pair), ClassifyDiscard(viewB, second.pair));
    }

}  // namespace BECore::Mahjong::Arena
