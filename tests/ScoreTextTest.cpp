#include "ScoreTestSupport.h"
#include "StringsTestSupport.h"

#include <Mahjong/Game/Match.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/SeatName.h>
#include <set>

/**
 * The words of the end-of-hand screen and of the end-of-match screen (`Table/ScoreText.h`). Pure logic: no engine services. The worked
 * hand is the self-draw of `ScoreHandTest` (36 points, 3 doubles, 290), written out here as the rows the screen shows.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// The shipped table with the given language current; one per language, loaded once (the tests only read it).
        const UIStrings& Words(std::string_view language) {
            static UIStrings english;
            static UIStrings russian;
            static const bool loaded = [] {
                EXPECT_TRUE(LoadShippedStrings(english).empty());
                EXPECT_TRUE(LoadShippedStrings(russian).empty());
                EXPECT_TRUE(english.SetLanguage("en"_intern));
                EXPECT_TRUE(russian.SetLanguage("ru"_intern));
                return true;
            }();
            (void)loaded;
            return language == "ru" ? russian : english;
        }

        const UIStrings& En() {
            return Words("en");
        }

        const UIStrings& Ru() {
            return Words("ru");
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.data(), text.size());
        }

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
            const eastl::string name = ScoreKindName(En(), kind);
            EXPECT_FALSE(name.empty()) << EnumUtils<ScoreKind>::ToString(kind).data();
            EXPECT_EQ(eastl::find(seen.begin(), seen.end(), name), seen.end()) << "the name '" << name.c_str() << "' is used twice";
            seen.push_back(name);
        }
        EXPECT_EQ(seen.size(), EnumUtils<ScoreKind>::Count());
    }

    TEST(ScoreTextTest, EveryTileHasADistinctNameAndTheSamplesReadRight) {
        eastl::vector<eastl::string> seen;
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            const eastl::string name = TileName(En(), Tile(id));
            EXPECT_FALSE(name.empty()) << int{id};
            EXPECT_EQ(eastl::find(seen.begin(), seen.end(), name), seen.end()) << name.c_str();
            seen.push_back(name);
        }
        EXPECT_STREQ(TileName(En(), T("5p")).c_str(), "5 Dots");
        EXPECT_STREQ(TileName(En(), T("9s")).c_str(), "9 Bamboo");
        EXPECT_STREQ(TileName(En(), T("1m")).c_str(), "1 Characters");
        EXPECT_STREQ(TileName(En(), T("Ew")).c_str(), "East Wind");
        EXPECT_STREQ(TileName(En(), T("Nw")).c_str(), "North Wind");
        EXPECT_STREQ(TileName(En(), T("Rd")).c_str(), "Red Dragon");
        EXPECT_STREQ(TileName(En(), T("Wd")).c_str(), "White Dragon");
        EXPECT_STREQ(TileName(En(), T("Gd")).c_str(), "Green Dragon");
        EXPECT_STREQ(TileName(En(), T("F1")).c_str(), "Flower 1");
        EXPECT_STREQ(TileName(En(), T("T4")).c_str(), "Season 4");
        EXPECT_STREQ(TileName(En(), Tile{}).c_str(), "No tile");
    }

    TEST(ScoreTextTest, ALineReadsAsItsNameItsTileAndItsValue) {
        EXPECT_STREQ(ScoreLineText(En(), MakeLine(ScoreKind::Win, "", 20, 0)).c_str(), "Win: 20");
        EXPECT_STREQ(ScoreLineText(En(), MakeLine(ScoreKind::Pung, "5p", 8, 0)).c_str(), "Pung of 5 Dots: 8");
        EXPECT_STREQ(ScoreLineText(En(), MakeLine(ScoreKind::Pair, "Rd", 2, 0)).c_str(), "Pair of Red Dragon: 2");
        EXPECT_STREQ(ScoreLineText(En(), MakeLine(ScoreKind::Bonus, "F1", 4, 0)).c_str(), "Bonus tile (Flower 1): 4");
        EXPECT_STREQ(ScoreLineText(En(), MakeLine(ScoreKind::DragonSet, "Gd", 0, 1)).c_str(), "Dragon set (Green Dragon): x2");
        EXPECT_STREQ(ScoreLineText(En(), MakeLine(ScoreKind::OneSuitNoHonours, "", 0, 3)).c_str(), "One suit, no honours: x8");
        EXPECT_STREQ(ScoreLineText(En(), MakeLine(ScoreKind::LittleFourWinds, "", 0, 2)).c_str(), "Little four winds: x4");
        EXPECT_STREQ(ScoreLineText(En(), MakeLine(ScoreKind::ThirteenOrphans, "", 0, 0)).c_str(), "Limit: Thirteen orphans");
    }

    TEST(ScoreTextTest, TheSeatsAreNamedInEveryLanguage) {
        EXPECT_STREQ(SeatName(En(), kSeat1, Wind::South).c_str(), "Seat 2 (South)");
        EXPECT_STREQ(SeatName(Ru(), kSeat1, Wind::South).c_str(), "Место 2 (Юг)");
        EXPECT_STREQ(SeatName(Ru(), kSeat3, Wind::North).c_str(), "Место 4 (Север)");
        EXPECT_STREQ(SeatLabel(En(), kSeat2, kNoPeople).c_str(), "Seat 3");
        EXPECT_STREQ(SeatLabel(Ru(), kSeat2, kNoPeople).c_str(), "Место 3");
        EXPECT_STREQ(WindName(Ru(), Wind::East).c_str(), "Восток");
        EXPECT_STREQ(WindName(Ru(), Wind::West).c_str(), "Запад");
    }

    TEST(ScoreTextTest, YouIsMarkedOnlyAtATableWithOnePersonInEveryLanguage) {
        // kDefaultSeatSetup: seat 0 is the only person
        EXPECT_STREQ(SeatLabel(En(), kSeat0, Wind::East, kDefaultSeatSetup).c_str(), "Seat 1 (East) You");
        EXPECT_STREQ(SeatLabel(Ru(), kSeat0, Wind::East, kDefaultSeatSetup).c_str(), "Место 1 (Восток) Вы");
        EXPECT_STREQ(SeatLabel(Ru(), kSeat0, kDefaultSeatSetup).c_str(), "Место 1 Вы");
        for (const UIStrings* strings : {&En(), &Ru()}) {
            EXPECT_EQ(SeatLabel(*strings, kSeat1, Wind::South, kDefaultSeatSetup), SeatName(*strings, kSeat1, Wind::South)) << "a computer is nobody";
            EXPECT_EQ(SeatLabel(*strings, kSeat0, Wind::East, kTwoSeatsOfPeople), SeatName(*strings, kSeat0, Wind::East)) << "two people: nobody is you";
            EXPECT_EQ(SeatLabel(*strings, kSeat0, Wind::East, kNoPeople), SeatName(*strings, kSeat0, Wind::East));
        }
    }

    TEST(ScoreTextTest, EveryNameInEveryLanguageIsNonEmptyDistinctAndNotItsKey) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ForEachLanguage(strings, [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            std::set<std::string> kinds;
            for (const ScoreKind kind : EnumUtils<ScoreKind>::Values()) {
                const std::string name = Std(ScoreKindName(strings, kind));
                EXPECT_FALSE(name.empty());
                EXPECT_EQ(name.find("score.kind."), std::string::npos) << "the key stands in for a missing text: " << name;
                EXPECT_TRUE(kinds.insert(name).second) << "the name '" << name << "' is used twice";
            }
            EXPECT_EQ(kinds.size(), EnumUtils<ScoreKind>::Count());

            std::set<std::string> tiles;
            for (uint8_t id = 0; id < kTileIdCount; ++id) {
                const std::string name = Std(TileName(strings, Tile(id)));
                EXPECT_FALSE(name.empty()) << int{id};
                EXPECT_EQ(name.find("tile."), std::string::npos) << name;
                EXPECT_EQ(name.find("suit."), std::string::npos) << name;
                EXPECT_EQ(name.find("wind."), std::string::npos) << name;
                EXPECT_EQ(name.find('{'), std::string::npos) << "a placeholder was left: " << name;
                EXPECT_TRUE(tiles.insert(name).second) << "the name '" << name << "' is used twice";
            }
            EXPECT_EQ(tiles.size(), static_cast<size_t>(kTileIdCount));
            EXPECT_EQ(Std(TileName(strings, Tile{})).find("tile.none"), std::string::npos);
        });
    }

    TEST(ScoreTextTest, TheRussianNamesAndLinesReadRight) {
        EXPECT_STREQ(TileName(Ru(), T("5p")).c_str(), "Круги 5");
        EXPECT_STREQ(TileName(Ru(), T("9s")).c_str(), "Бамбук 9");
        EXPECT_STREQ(TileName(Ru(), T("1m")).c_str(), "Знаки 1");
        EXPECT_STREQ(TileName(Ru(), T("Ew")).c_str(), "Ветер: Восток");
        EXPECT_STREQ(TileName(Ru(), T("Rd")).c_str(), "Красный дракон");
        EXPECT_STREQ(TileName(Ru(), T("Wd")).c_str(), "Белый дракон");
        EXPECT_STREQ(TileName(Ru(), T("Gd")).c_str(), "Зелёный дракон");
        EXPECT_STREQ(TileName(Ru(), T("F1")).c_str(), "Цветок 1");
        EXPECT_STREQ(TileName(Ru(), T("T4")).c_str(), "Сезон 4");
        EXPECT_STREQ(TileName(Ru(), Tile{}).c_str(), "Нет кости");

        EXPECT_STREQ(ScoreLineText(Ru(), MakeLine(ScoreKind::Win, "", 20, 0)).c_str(), "Победа: 20");
        EXPECT_STREQ(ScoreLineText(Ru(), MakeLine(ScoreKind::Pung, "5p", 8, 0)).c_str(), "Пун (Круги 5): 8");
        EXPECT_STREQ(ScoreLineText(Ru(), MakeLine(ScoreKind::Bonus, "F1", 4, 0)).c_str(), "Бонусная кость (Цветок 1): 4");
        EXPECT_STREQ(ScoreLineText(Ru(), MakeLine(ScoreKind::DragonSet, "Gd", 0, 1)).c_str(), "Набор драконов (Зелёный дракон): x2");
        EXPECT_STREQ(ScoreLineText(Ru(), MakeLine(ScoreKind::LittleFourWinds, "", 0, 2)).c_str(), "Малые четыре ветра: x4");
        EXPECT_STREQ(ScoreLineText(Ru(), MakeLine(ScoreKind::ThirteenOrphans, "", 0, 0)).c_str(), "Лимит: Тринадцать сирот");
    }

    // ----- the end of a hand -----

    TEST(ScoreTextTest, TheWorkedSelfDrawFormatsToTheExpectedRows) {
        const HandModel model = PlayToSelfDrawWin();
        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        const HandSettlement settlement = SettleHand(model);

        const ScoreTable table = FormatScoreTable(En(), settlement, *model.Result(), TotalsAfter(settlement), kDefaultSeatSetup);

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

        const ScoreTable none = FormatScoreTable(En(), settlement, *model.Result(), TotalsAfter(settlement), kNoPeople);
        const ScoreTable two = FormatScoreTable(En(), settlement, *model.Result(), TotalsAfter(settlement), kTwoSeatsOfPeople);

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
        EXPECT_STREQ(FormatScoreTable(En(), settlement, result, TotalsAfter(settlement), kNoPeople).headline.c_str(), "Seat 3 (West) wins on the discard of Seat 4 (North)");

        result.source = WinSource::RobbedKong;
        result.from = kSeat1;
        EXPECT_STREQ(FormatScoreTable(En(), settlement, result, TotalsAfter(settlement), kNoPeople).headline.c_str(), "Seat 3 (West) wins by robbing the kong of Seat 2 (South)");
    }

    TEST(ScoreTextTest, ADrawnHandHasNoPaymentsAndNoLines) {
        HandSettlement settlement;
        settlement.kind = ResultKind::Drawn;
        settlement.east = kSeat1;

        const ScoreTable table = FormatScoreTable(En(), settlement, HandResult{}, SeatScores{2000, 2000, 2000, 2000}, kDefaultSeatSetup);

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

        const ScoreTable table = FormatScoreTable(En(), settlement, result, TotalsAfter(settlement), kNoPeople);

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

        const ScoreTable table = FormatScoreTable(En(), settlement, HandResult{ResultKind::Win, kSeat2, T("5p")}, TotalsAfter(settlement), kNoPeople);

        ASSERT_EQ(table.lines.size(), static_cast<size_t>(kMaxScoreLines));
        EXPECT_STREQ(table.lines[kMaxScoreLines - 2].c_str(), "Bonus tile (Flower 1): 4");
        EXPECT_STREQ(table.lines[kMaxScoreLines - 1].c_str(), "+5 more") << "20 lines: 15 shown, 5 summed up";

        lines.resize(kMaxScoreLines);
        const ScoreTable exact = FormatScoreTable(En(), MadeUpWin(lines), HandResult{ResultKind::Win, kSeat2, T("5p")}, TotalsAfter(settlement), kNoPeople);
        EXPECT_EQ(exact.lines.size(), static_cast<size_t>(kMaxScoreLines)) << "exactly sixteen lines are all shown";
        EXPECT_STREQ(exact.lines.back().c_str(), "Bonus tile (Flower 1): 4");
    }

    TEST(ScoreTextTest, ALargeNumberOfDoublesNeverOverflowsTheMultiplier) {
        const HandSettlement settlement = MadeUpWin({MakeLine(ScoreKind::OnlyPungs, "", 0, 100)});

        const eastl::string text = ScoreLineText(En(), settlement.breakdowns[kSeat2].lines[0]);

        EXPECT_FALSE(text.empty());
    }

    TEST(ScoreTextTest, TheWorkedSelfDrawFormatsToTheExpectedRowsInRussian) {
        const HandModel model = PlayToSelfDrawWin();
        const HandSettlement settlement = SettleHand(model);

        const ScoreTable table = FormatScoreTable(Ru(), settlement, *model.Result(), TotalsAfter(settlement), kDefaultSeatSetup);

        EXPECT_STREQ(table.headline.c_str(), "Место 2 (Юг) побеждает, взяв кость из стены");
        EXPECT_STREQ(table.summary.c_str(), "Выигрышная кость Круги 4: 36 очк. x 8 = 288, счёт 290");
        EXPECT_STREQ(table.seats[kSeat0].c_str(), "Место 1 (Восток) Вы: рука 0, -600, итого 1400");
        EXPECT_STREQ(table.seats[kSeat1].c_str(), "Место 2 (Юг): рука 290, +1160, итого 3160");
        EXPECT_STREQ(table.seats[kSeat2].c_str(), "Место 3 (Запад): рука 10, -260, итого 1740");
        EXPECT_STREQ(table.seats[kSeat3].c_str(), "Место 4 (Север): рука 0, -300, итого 1700");
        const char* const expected[] = {"Победа: 20",
                                        "Кость из стены: 2",
                                        "Пун (Бамбук 6): 4",
                                        "Пун (Красный дракон): 8",
                                        "Пара (Ветер: Юг): 2",
                                        "Набор драконов (Красный дракон): x2",
                                        "Полностью закрытая: x2",
                                        "Заявка с первого хода: x2"};
        ASSERT_EQ(table.lines.size(), std::size(expected));
        for (size_t i = 0; i < table.lines.size(); ++i) {
            EXPECT_STREQ(table.lines[i].c_str(), expected[i]) << "line " << i;
        }
    }

    TEST(ScoreTextTest, EverySentenceOfTheHandScreenIsBuiltInEveryLanguage) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ForEachLanguage(strings, [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const bool russian = language.ToStringView() == "ru";
            HandSettlement settlement = MadeUpWin({MakeLine(ScoreKind::Win, "", 20, 0), MakeLine(ScoreKind::OnlyPungs, "", 0, 1)});
            settlement.breakdowns[kSeat2].points = 20;
            settlement.breakdowns[kSeat2].doubles = 1;
            settlement.breakdowns[kSeat2].raw = 40;
            HandResult result;
            result.kind = ResultKind::Win;
            result.winner = kSeat2;
            result.winningTile = T("5p");
            const std::string winner = Std(SeatName(strings, kSeat2, Wind::West));
            const std::string from = Std(SeatName(strings, kSeat3, Wind::North));

            std::set<std::string> headlines;
            for (const WinSource source : {WinSource::SelfDraw, WinSource::Discard, WinSource::RobbedKong}) {
                for (const Seat origin : {kSeat3, kNoSeat}) {
                    result.source = source;
                    result.from = origin;
                    const std::string headline = Std(FormatScoreTable(strings, settlement, result, TotalsAfter(settlement), kNoPeople).headline);
                    EXPECT_NE(headline.find(winner), std::string::npos) << headline;
                    EXPECT_EQ(headline.find("handover."), std::string::npos) << "a key stands in for a missing text: " << headline;
                    EXPECT_EQ(headline.find('{'), std::string::npos) << "a placeholder was left: " << headline;
                    EXPECT_EQ(headline.find(from) != std::string::npos, source != WinSource::SelfDraw && origin == kSeat3) << headline;
                    headlines.insert(headline);
                }
            }
            EXPECT_EQ(headlines.size(), 5u) << "self-draw, discard (known / unknown), robbed kong (known / unknown)";

            const ScoreTable won = FormatScoreTable(strings, settlement, result, TotalsAfter(settlement), kNoPeople);
            EXPECT_EQ(Std(won.summary).find("handover."), std::string::npos);
            EXPECT_EQ(Std(won.summary).find('{'), std::string::npos);
            EXPECT_NE(Std(won.summary).find("2"), std::string::npos);
            for (const eastl::string& row : won.seats) {
                EXPECT_EQ(Std(row).find("handover."), std::string::npos) << Std(row);
                EXPECT_EQ(Std(row).find('{'), std::string::npos) << Std(row);
            }
            EXPECT_NE(Std(won.seats[kSeat2]).find("+300"), std::string::npos) << "the sign and the number stay in the row";
            EXPECT_NE(Std(won.seats[kSeat0]).find("-100"), std::string::npos);

            HandSettlement drawn;
            drawn.kind = ResultKind::Drawn;
            const ScoreTable none = FormatScoreTable(strings, drawn, HandResult{}, SeatScores{2000, 2000, 2000, 2000}, kNoPeople);
            EXPECT_STREQ(none.headline.c_str(), russian ? "Ничья" : "Drawn hand");
            EXPECT_STREQ(none.summary.c_str(), russian ? "Без выплат" : "No payments");
            EXPECT_TRUE(none.lines.empty());
            EXPECT_STREQ(none.seats[kSeat1].c_str(), russian ? "Место 2 (Юг): без выплат, итого 2000" : "Seat 2 (South): no payment, total 2000");
        });
    }

    TEST(ScoreTextTest, TheOtherSummariesAndTheMoreLineInRussian) {
        HandSettlement settlement = MadeUpWin({MakeLine(ScoreKind::ThirteenOrphans, "", 0, 0)});
        settlement.breakdowns[kSeat2].limitHand = ScoreKind::ThirteenOrphans;
        settlement.breakdowns[kSeat2].score = kLimit;
        HandResult result;
        result.kind = ResultKind::Win;
        result.winner = kSeat2;
        result.winningTile = T("Rd");
        const ScoreTable limit = FormatScoreTable(Ru(), settlement, result, TotalsAfter(settlement), kNoPeople);
        ASSERT_EQ(limit.lines.size(), 1u);
        EXPECT_STREQ(limit.lines[0].c_str(), "Лимит: Тринадцать сирот");
        EXPECT_STREQ(limit.summary.c_str(), "Выигрышная кость Красный дракон: счёт 500");

        settlement.breakdowns[kSeat2].limitHand.reset();
        settlement.breakdowns[kSeat2].points = 20;
        settlement.breakdowns[kSeat2].score = 20;
        EXPECT_STREQ(FormatScoreTable(Ru(), settlement, result, TotalsAfter(settlement), kNoPeople).summary.c_str(), "Выигрышная кость Красный дракон: 20 очк., счёт 20");

        eastl::vector<ScoreLine> lines;
        for (int i = 0; i < kMaxScoreLines + 4; ++i) {
            lines.push_back(MakeLine(ScoreKind::Bonus, "F1", 4, 0));
        }
        const ScoreTable many = FormatScoreTable(Ru(), MadeUpWin(lines), HandResult{ResultKind::Win, kSeat2, T("5p")}, TotalsAfter(settlement), kNoPeople);
        ASSERT_EQ(many.lines.size(), static_cast<size_t>(kMaxScoreLines));
        EXPECT_STREQ(many.lines.back().c_str(), "ещё +5");
    }

    // ----- the end of a match -----

    TEST(ScoreTextTest, TheStandingsListTheSeatsBestFirstAndTiedSeatsShareAPlaceInSeatOrder) {
        Match match;
        ASSERT_TRUE(match.Start(MahjongSettings{}, 1).has_value());
        HandSettlement settlement;
        settlement.kind = ResultKind::Drawn;
        settlement.deltas = {100, -100, 100, -100};
        ASSERT_TRUE(match.ApplySettlement(settlement).has_value());

        const StandingsTable table = FormatStandings(En(), match, kNoPeople);

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

        const StandingsTable table = FormatStandings(En(), match, kDefaultSeatSetup);

        EXPECT_STREQ(table.headline.c_str(), "Seat 4 wins the match");
        EXPECT_STREQ(table.rows[0].c_str(), "1. Seat 4: 2200");
        EXPECT_STREQ(table.rows[1].c_str(), "2. Seat 3: 2100");
        EXPECT_STREQ(table.rows[2].c_str(), "3. Seat 2: 2000");
        EXPECT_STREQ(table.rows[3].c_str(), "4. Seat 1 You: 1700");
    }

    TEST(ScoreTextTest, TheStandingsAreWordedInEveryLanguage) {
        Match match;
        ASSERT_TRUE(match.Start(MahjongSettings{}, 1).has_value());
        HandSettlement settlement;
        settlement.kind = ResultKind::Drawn;
        settlement.deltas = {100, -100, 100, -100};
        ASSERT_TRUE(match.ApplySettlement(settlement).has_value());

        const StandingsTable tied = FormatStandings(Ru(), match, kNoPeople);
        EXPECT_STREQ(tied.headline.c_str(), "Делят первое место: Место 1, Место 3");
        EXPECT_STREQ(tied.rows[0].c_str(), "1. Место 1: 2100");
        EXPECT_STREQ(tied.rows[3].c_str(), "3. Место 4: 1900");

        HandSettlement second;
        second.kind = ResultKind::Drawn;
        second.deltas = {-300, 0, 100, 200};
        Match other;
        ASSERT_TRUE(other.Start(MahjongSettings{}, 1).has_value());
        ASSERT_TRUE(other.ApplySettlement(second).has_value());
        const StandingsTable one = FormatStandings(Ru(), other, kDefaultSeatSetup);
        EXPECT_STREQ(one.headline.c_str(), "Место 4 выигрывает матч");
        EXPECT_STREQ(one.rows[0].c_str(), "1. Место 4: 2200");
        EXPECT_STREQ(one.rows[3].c_str(), "4. Место 1 Вы: 1700");
    }

}  // namespace BECore::Mahjong::Tests
