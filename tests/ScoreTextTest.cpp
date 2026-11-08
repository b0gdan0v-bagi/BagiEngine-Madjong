#include "ScoreTestSupport.h"

#include <Mahjong/Game/Match.h>
#include <Mahjong/Table/ScoreText.h>

/**
 * The words of the end-of-hand screen and of the end-of-match screen (`Table/ScoreText.h`). Pure logic: no engine services. The worked
 * hand is the self-draw of `ScoreHandTest` (36 points, 3 doubles, 290), written out here as the rows the screen shows.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr SeatSetup kNoPeople{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        constexpr SeatSetup kTwoSeatsOfPeople{SeatKind::Person, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};

        // The self-draw of ScoreHandTest: South (seat 1) draws the 4p after declaring with its first discard.
        FixedWallSpec SelfDrawSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Ew Wd Gd 5p";
            spec.hands[kSeat1] = "3p 6s 6s 6s 7m 8m 9m Rd Rd Rd Sw Sw Nw";
            spec.hands[kSeat2] = "2p 5p 8p 2s 5s 8s 2m 5m 8m 3s 3s 3s Ww";
            spec.hands[kSeat3] = "3p 6p 9p 3m 6m 9m 1s 4s 7s Gd Nw Wd Ew";
            spec.live = "9m 2p 6p 7p 9s 4p";
            return spec;
        }

        HandModel PlayToSelfDrawWin() {
            HandModel model = MakeModel(SelfDrawSpec());
            Lap(model, "Ew Nw Ww Gd Wd", {kSeat1});
            EXPECT_TRUE(model.DeclareWin(kSeat1).has_value());
            return model;
        }

        SeatScores TotalsAfter(const HandSettlement& settlement) {
            SeatScores totals{};
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                totals[seat] = kStartingScore + settlement.deltas[seat];
            }
            return totals;
        }

        ScoreLine MakeLine(ScoreKind kind, const char* tile, int points, int doubles) {
            ScoreLine line;
            line.kind = kind;
            line.tile = tile[0] != '\0' ? T(tile) : Tile{};
            line.points = points;
            line.doubles = doubles;
            return line;
        }

        /// A settlement that East wins by a self-draw with the given lines; the payments are made up and add up to zero.
        HandSettlement MadeUpWin(const eastl::vector<ScoreLine>& lines) {
            HandSettlement settlement;
            settlement.kind = ResultKind::Win;
            settlement.winner = kSeat2;
            settlement.east = kSeat0;
            settlement.scores = {0, 0, 100, 0};
            settlement.deltas = {-100, 0, 300, -200};
            settlement.breakdowns[kSeat2].won = true;
            settlement.breakdowns[kSeat2].lines = lines;
            settlement.breakdowns[kSeat2].score = 100;
            return settlement;
        }

    }  // namespace

    // ----- the names -----

    TEST(ScoreTextTest, EveryScoreKindHasANonEmptyDistinctName) {
        eastl::vector<eastl::string> seen;
        for (const ScoreKind kind : EnumUtils<ScoreKind>::Values()) {
            const eastl::string name(ScoreKindName(kind));
            EXPECT_FALSE(name.empty()) << EnumUtils<ScoreKind>::ToString(kind).data();
            EXPECT_EQ(eastl::find(seen.begin(), seen.end(), name), seen.end()) << "the name '" << name.c_str() << "' is used twice";
            seen.push_back(name);
        }
        EXPECT_EQ(seen.size(), EnumUtils<ScoreKind>::Count());
    }

    TEST(ScoreTextTest, EveryTileHasADistinctNameAndTheSamplesReadRight) {
        eastl::vector<eastl::string> seen;
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const eastl::string name = TileName(Tile(id));
            EXPECT_FALSE(name.empty()) << int{id};
            EXPECT_EQ(eastl::find(seen.begin(), seen.end(), name), seen.end()) << name.c_str();
            seen.push_back(name);
        }
        EXPECT_STREQ(TileName(T("5p")).c_str(), "5 Dots");
        EXPECT_STREQ(TileName(T("9s")).c_str(), "9 Bamboo");
        EXPECT_STREQ(TileName(T("1m")).c_str(), "1 Characters");
        EXPECT_STREQ(TileName(T("Ew")).c_str(), "East Wind");
        EXPECT_STREQ(TileName(T("Nw")).c_str(), "North Wind");
        EXPECT_STREQ(TileName(T("Rd")).c_str(), "Red Dragon");
        EXPECT_STREQ(TileName(T("Wd")).c_str(), "White Dragon");
        EXPECT_STREQ(TileName(T("Gd")).c_str(), "Green Dragon");
        EXPECT_STREQ(TileName(T("F1")).c_str(), "Flower 1");
        EXPECT_STREQ(TileName(T("T4")).c_str(), "Season 4");
        EXPECT_STREQ(TileName(Tile{}).c_str(), "No tile");
    }

    TEST(ScoreTextTest, ALineReadsAsItsNameItsTileAndItsValue) {
        EXPECT_STREQ(ScoreLineText(MakeLine(ScoreKind::Win, "", 20, 0)).c_str(), "Win: 20");
        EXPECT_STREQ(ScoreLineText(MakeLine(ScoreKind::Pung, "5p", 8, 0)).c_str(), "Pung of 5 Dots: 8");
        EXPECT_STREQ(ScoreLineText(MakeLine(ScoreKind::Pair, "Rd", 2, 0)).c_str(), "Pair of Red Dragon: 2");
        EXPECT_STREQ(ScoreLineText(MakeLine(ScoreKind::Bonus, "F1", 4, 0)).c_str(), "Bonus tile (Flower 1): 4");
        EXPECT_STREQ(ScoreLineText(MakeLine(ScoreKind::DragonSet, "Gd", 0, 1)).c_str(), "Dragon set (Green Dragon): x2");
        EXPECT_STREQ(ScoreLineText(MakeLine(ScoreKind::OneSuitNoHonours, "", 0, 3)).c_str(), "One suit, no honours: x8");
        EXPECT_STREQ(ScoreLineText(MakeLine(ScoreKind::LittleFourWinds, "", 0, 2)).c_str(), "Little four winds: x4");
        EXPECT_STREQ(ScoreLineText(MakeLine(ScoreKind::ThirteenOrphans, "", 0, 0)).c_str(), "Limit: Thirteen orphans");
    }

    // ----- the end of a hand -----

    TEST(ScoreTextTest, TheWorkedSelfDrawFormatsToTheExpectedRows) {
        const HandModel model = PlayToSelfDrawWin();
        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        const HandSettlement settlement = SettleHand(model);

        const ScoreTable table = FormatScoreTable(settlement, *model.Result(), TotalsAfter(settlement), kDefaultSeatSetup);

        EXPECT_STREQ(table.headline.c_str(), "Seat 2 (South) wins by self-draw");
        EXPECT_STREQ(table.summary.c_str(), "Winning tile 4 Dots: 36 points x 8 = 288, score 290");
        // -600 / +1160 / -260 / -300: the worked payments of ScoreHandTest
        EXPECT_STREQ(table.seats[kSeat0].c_str(), "Seat 1 (East) You: hand 0, -600, total 1400");
        EXPECT_STREQ(table.seats[kSeat1].c_str(), "Seat 2 (South): hand 290, +1160, total 3160");
        EXPECT_STREQ(table.seats[kSeat2].c_str(), "Seat 3 (West): hand 10, -260, total 1740");
        EXPECT_STREQ(table.seats[kSeat3].c_str(), "Seat 4 (North): hand 0, -300, total 1700");
        const char* const expected[] = {"Win: 20",
                                        "Tile from the wall: 2",
                                        "Pung of 6 Bamboo: 4",
                                        "Pung of Red Dragon: 8",
                                        "Pair of South Wind: 2",
                                        "Dragon set (Red Dragon): x2",
                                        "Fully concealed: x2",
                                        "Calling hand, first hand: x2"};
        ASSERT_EQ(table.lines.size(), std::size(expected)) << "the lines of the breakdown, as the rules print them";
        for (size_t i = 0; i < table.lines.size(); ++i) {
            EXPECT_STREQ(table.lines[i].c_str(), expected[i]) << "line " << i;
        }
    }

    TEST(ScoreTextTest, OnlyTheTableWithOnePersonMarksHimAsYou) {
        const HandModel model = PlayToSelfDrawWin();
        const HandSettlement settlement = SettleHand(model);

        const ScoreTable none = FormatScoreTable(settlement, *model.Result(), TotalsAfter(settlement), kNoPeople);
        const ScoreTable two = FormatScoreTable(settlement, *model.Result(), TotalsAfter(settlement), kTwoSeatsOfPeople);

        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(std::string(none.seats[seat].c_str()).find("You"), std::string::npos) << seat;
            EXPECT_EQ(std::string(two.seats[seat].c_str()).find("You"), std::string::npos) << seat;
        }
    }

    TEST(ScoreTextTest, AWinOnADiscardAndARobbedKongNameTheSeatThatPaidForIt) {
        HandSettlement settlement = MadeUpWin({MakeLine(ScoreKind::Win, "", 20, 0)});
        HandResult result;
        result.kind = ResultKind::Win;
        result.winner = kSeat2;
        result.winningTile = T("5p");

        result.source = WinSource::Discard;
        result.from = kSeat3;
        EXPECT_STREQ(FormatScoreTable(settlement, result, TotalsAfter(settlement), kNoPeople).headline.c_str(), "Seat 3 (West) wins on the discard of Seat 4 (North)");

        result.source = WinSource::RobbedKong;
        result.from = kSeat1;
        EXPECT_STREQ(FormatScoreTable(settlement, result, TotalsAfter(settlement), kNoPeople).headline.c_str(), "Seat 3 (West) wins by robbing the kong of Seat 2 (South)");
    }

    TEST(ScoreTextTest, ADrawnHandHasNoPaymentsAndNoLines) {
        HandSettlement settlement;
        settlement.kind = ResultKind::Drawn;
        settlement.east = kSeat1;

        const ScoreTable table = FormatScoreTable(settlement, HandResult{}, SeatScores{2000, 2000, 2000, 2000}, kDefaultSeatSetup);

        EXPECT_STREQ(table.headline.c_str(), "Drawn hand");
        EXPECT_STREQ(table.summary.c_str(), "No payments");
        EXPECT_TRUE(table.lines.empty());
        EXPECT_STREQ(table.seats[kSeat0].c_str(), "Seat 1 (North) You: no payment, total 2000");
        EXPECT_STREQ(table.seats[kSeat1].c_str(), "Seat 2 (East): no payment, total 2000");
    }

    TEST(ScoreTextTest, ALimitHandIsOneLineAndTheSummaryHasNoMultiplication) {
        HandSettlement settlement = MadeUpWin({MakeLine(ScoreKind::ThirteenOrphans, "", 0, 0)});
        settlement.breakdowns[kSeat2].limitHand = ScoreKind::ThirteenOrphans;
        settlement.breakdowns[kSeat2].score = kLimit;
        HandResult result;
        result.kind = ResultKind::Win;
        result.winner = kSeat2;
        result.winningTile = T("Rd");

        const ScoreTable table = FormatScoreTable(settlement, result, TotalsAfter(settlement), kNoPeople);

        ASSERT_EQ(table.lines.size(), 1u);
        EXPECT_STREQ(table.lines[0].c_str(), "Limit: Thirteen orphans");
        EXPECT_STREQ(table.summary.c_str(), "Winning tile Red Dragon: score 500");
    }

    TEST(ScoreTextTest, AWinnerWithMoreThanSixteenLinesGetsASummaryInTheLastOne) {
        eastl::vector<ScoreLine> lines;
        for (int i = 0; i < kMaxScoreLines + 4; ++i) {
            lines.push_back(MakeLine(ScoreKind::Bonus, "F1", 4, 0));
        }
        const HandSettlement settlement = MadeUpWin(lines);

        const ScoreTable table = FormatScoreTable(settlement, HandResult{ResultKind::Win, kSeat2, T("5p")}, TotalsAfter(settlement), kNoPeople);

        ASSERT_EQ(table.lines.size(), static_cast<size_t>(kMaxScoreLines));
        EXPECT_STREQ(table.lines[kMaxScoreLines - 2].c_str(), "Bonus tile (Flower 1): 4");
        EXPECT_STREQ(table.lines[kMaxScoreLines - 1].c_str(), "+5 more") << "20 lines: 15 shown, 5 summed up";

        lines.resize(kMaxScoreLines);
        const ScoreTable exact = FormatScoreTable(MadeUpWin(lines), HandResult{ResultKind::Win, kSeat2, T("5p")}, TotalsAfter(settlement), kNoPeople);
        EXPECT_EQ(exact.lines.size(), static_cast<size_t>(kMaxScoreLines)) << "exactly sixteen lines are all shown";
        EXPECT_STREQ(exact.lines.back().c_str(), "Bonus tile (Flower 1): 4");
    }

    TEST(ScoreTextTest, ALargeNumberOfDoublesNeverOverflowsTheMultiplier) {
        const HandSettlement settlement = MadeUpWin({MakeLine(ScoreKind::OnlyPungs, "", 0, 100)});

        const eastl::string text = ScoreLineText(settlement.breakdowns[kSeat2].lines[0]);

        EXPECT_FALSE(text.empty());
    }

    // ----- the end of a match -----

    TEST(ScoreTextTest, TheStandingsListTheSeatsBestFirstAndTiedSeatsShareAPlaceInSeatOrder) {
        Match match;
        ASSERT_TRUE(match.Start(MahjongSettings{}, 1).has_value());
        HandSettlement settlement;
        settlement.kind = ResultKind::Drawn;
        settlement.deltas = {100, -100, 100, -100};
        ASSERT_TRUE(match.ApplySettlement(settlement).has_value());

        const StandingsTable table = FormatStandings(match, kNoPeople);

        EXPECT_STREQ(table.headline.c_str(), "Tied for first: Seat 1, Seat 3");
        EXPECT_STREQ(table.rows[0].c_str(), "1. Seat 1: 2100");
        EXPECT_STREQ(table.rows[1].c_str(), "1. Seat 3: 2100");
        EXPECT_STREQ(table.rows[2].c_str(), "3. Seat 2: 1900");
        EXPECT_STREQ(table.rows[3].c_str(), "3. Seat 4: 1900");
    }

    TEST(ScoreTextTest, ASingleWinnerOfTheMatchIsNamedAndYouIsMarkedForTheOnePerson) {
        Match match;
        ASSERT_TRUE(match.Start(MahjongSettings{}, 1).has_value());
        HandSettlement settlement;
        settlement.kind = ResultKind::Drawn;
        settlement.deltas = {-300, 0, 100, 200};
        ASSERT_TRUE(match.ApplySettlement(settlement).has_value());

        const StandingsTable table = FormatStandings(match, kDefaultSeatSetup);

        EXPECT_STREQ(table.headline.c_str(), "Seat 4 wins the match");
        EXPECT_STREQ(table.rows[0].c_str(), "1. Seat 4: 2200");
        EXPECT_STREQ(table.rows[1].c_str(), "2. Seat 3: 2100");
        EXPECT_STREQ(table.rows[2].c_str(), "3. Seat 2: 2000");
        EXPECT_STREQ(table.rows[3].c_str(), "4. Seat 1 You: 1700");
    }

}  // namespace BECore::Mahjong::Tests
