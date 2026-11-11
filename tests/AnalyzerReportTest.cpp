#include <BECore/Control/Json.h>
#include <Mahjong/Analyzer/AnalyzerReport.h>
#include <Mahjong/MahjongGameControl.h>
#include <gtest/gtest.h>

/**
 * The report of one analysis (Analyzer/AnalyzerReport.h): `ActionNotation` for every action kind, the fixed-column text and the JSON
 * of a hand-made `PositionAnalysis` against golden strings (no rollouts are played here), the order and the marks of the rows, blank
 * cells for what has no interval, the sentences of the header that state the approximations, and the JSON read back by the engine's
 * parser. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Analyzer {

    namespace {

        constexpr int kHandWorlds = 100;
        constexpr uint32_t kSeed = 7;

        Tile T(std::string_view text) {
            return TryParse(text).value();
        }

        Arena::MeanInterval Interval(double mean, double half, size_t n = kHandWorlds) {
            Arena::MeanInterval interval;
            interval.n = n;
            interval.mean = mean;
            interval.half = half;
            interval.hasInterval = n >= Arena::kMinSeedsForInterval;
            return interval;
        }

        ActionDifference Difference(double mean, double half, bool significant, size_t n = kHandWorlds) {
            ActionDifference difference;
            difference.interval = Interval(mean, half, n);
            difference.significant = significant;
            return difference;
        }

        ActionRow Row(const HandAction& action, const Arena::MeanInterval& ev, const ActionDifference& vsBest, const ActionDifference& vsNormal, bool isBest = false, bool isNormal = false) {
            ActionRow row;
            row.action = action;
            row.ev = ev;
            row.vsBest = vsBest;
            row.vsNormal = vsNormal;
            row.isBest = isBest;
            row.isNormal = isNormal;
            return row;
        }

        // Seat 2 answers the 5p that seat 1 discarded; it holds an open pung of dragons.
        TableView ClaimView() {
            TableView view;
            view.seat = 2;
            view.phase = Phase::Claim;
            view.currentSeat = 1;
            view.pendingTile = T("5p");
            view.seatWind = Wind::West;
            view.roundWind = Wind::South;
            view.own.concealed = ParseTiles("3s 4s 5p 5p 7s 8s 2m 3m 4m Nw").value();
            Meld pung;
            pung.kind = MeldKind::Pung;
            pung.first = T("Rd");
            pung.from = 0;
            pung.fromDiscard = true;
            pung.claimed = T("Rd");
            view.own.melds.push_back(pung);
            view.seats[0].discardCount = 5;
            view.seats[1].discardCount = 6;
            view.seats[1].callingDeclared = true;
            view.seats[3].discardCount = 4;
            view.seats[3].concealedKongs = 1;
            view.seats[3].bonus = ParseTiles("F1").value();
            view.liveCount = 70;
            return view;
        }

        const HandAction kPass = HandAction::MakePass();
        const HandAction kPung = HandAction::MakeClaim(ClaimKind::Pung);
        const HandAction kChow = HandAction::MakeClaim(ClaimKind::Chow, Tile::Suited(TileSuit::Bamboo, 3));
        const HandAction kKong = HandAction::MakeClaim(ClaimKind::Kong);
        const HandAction kMahjong = HandAction::MakeClaim(ClaimKind::Mahjong);

        // The rows in the order of `legal`: pass (Normal's choice), pung and pass tie on the mean, the chow is the best, the kong was
        // played in one world only and the mahjong claim is worse than both.
        PositionAnalysis ClaimAnalysis() {
            PositionAnalysis analysis;
            analysis.config.worlds = kHandWorlds;
            analysis.config.seed = kSeed;
            analysis.seat = 2;
            analysis.phase = Phase::Claim;
            analysis.pendingTile = T("5p");
            analysis.worldsAsked = kHandWorlds;
            analysis.worldsPlayed = kHandWorlds - 2;
            analysis.worldsDropped = 2;
            analysis.normalChoice = kPass;
            analysis.rows.push_back(Row(kPass, Interval(1.234, 0.5), Difference(-2.266, 0.8, true), ActionDifference{}, false, true));
            analysis.rows.push_back(Row(kPung, Interval(1.234, 0.61), Difference(-2.266, 1.5, true), Difference(0.001, 0.4, false)));
            analysis.rows.push_back(Row(kChow, Interval(3.5, 0.7), ActionDifference{}, Difference(2.266, 0.9, true), true));
            analysis.rows.push_back(Row(kKong, Interval(0.5, 0.0, 1), Difference(-3.0, 0.0, false, 1), Difference(-0.734, 0.0, false, 1)));
            analysis.rows.push_back(Row(kMahjong, Interval(-1.0, 0.3), Difference(-4.5, 0.6, true), Difference(-2.234, 0.5, true)));
            return analysis;
        }

        PositionLabel LogLabel() {
            PositionLabel label;
            label.source = "logs/matches/a.jsonl";
            label.hand = 3;
            label.actionIndex = 17;
            label.hasLogged = true;
            label.logged = kPung;
            return label;
        }

        std::string Str(const eastl::string& text) {
            return std::string(text.c_str(), text.size());
        }

        bool Has(const eastl::string& text, std::string_view part) {
            return std::string_view(text.data(), text.size()).find(part) != std::string_view::npos;
        }

        /// The lines of `text` that start with `prefix` (the table rows are found by their action).
        std::string LineOf(const eastl::string& text, std::string_view prefix) {
            const std::string_view all(text.data(), text.size());
            size_t start = 0;
            while (start < all.size()) {
                const size_t end = all.find('\n', start);
                const std::string_view line = all.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
                if (line.starts_with(prefix)) {
                    return std::string(line);
                }
                if (end == std::string_view::npos) {
                    break;
                }
                start = end + 1;
            }
            return {};
        }

    }  // namespace

    TEST(AnalyzerReportTest, EveryKindOfActionHasOneNotation) {
        EXPECT_EQ(Str(ActionNotation(HandAction::MakeDiscard(T("5p")))), "discard 5p");
        EXPECT_EQ(Str(ActionNotation(HandAction::MakeDiscard(T("Rd"), true))), "discard Rd declare");
        EXPECT_EQ(Str(ActionNotation(HandAction::MakeDeclareWin())), "win");
        EXPECT_EQ(Str(ActionNotation(HandAction::MakeConcealedKong(T("5p")))), "kong 5p concealed");
        EXPECT_EQ(Str(ActionNotation(HandAction::MakeUpgradeKong(T("2m")))), "upgrade 2m");
        EXPECT_EQ(Str(ActionNotation(kChow)), "claim chow 3s-4s-5s");
        EXPECT_EQ(Str(ActionNotation(HandAction::MakeClaim(ClaimKind::Chow, T("7m")))), "claim chow 7m-8m-9m");
        EXPECT_EQ(Str(ActionNotation(kPung)), "claim pung");
        EXPECT_EQ(Str(ActionNotation(kKong)), "claim kong");
        EXPECT_EQ(Str(ActionNotation(kMahjong)), "claim mahjong");
        EXPECT_EQ(Str(ActionNotation(kPass)), "pass");
    }

    TEST(AnalyzerReportTest, TheTextOfAClaimPositionIsTheGoldenText) {
        const eastl::string text = FormatAnalysisReport(ClaimView(), ClaimAnalysis(), LogLabel());
        const std::string golden = "EV of every legal action, in points at the end of the hand\n"
                                   "The opponents are Normal players (level 5) and the seat itself plays Normal after the chosen action.\n"
                                   "Hidden tiles are sampled uniformly from the tiles the seat cannot see (the approximations of ComputerWorld.h).\n"
                                   "The points are those of this hand only (a drawn hand pays 0), against Normal bots: like a poker chart against a known pool, not an equilibrium.\n"
                                   "worlds: asked 100, played 98, dropped 2; seed 7; Normal's choice seed 1\n"
                                   "+- is 1.96 standard errors over the worlds (a 95 percent interval); a world is one sample of the hidden tiles with its rollout seed.\n"
                                   "vs best and vs Normal are paired by world (the same worlds and rollout seeds), so their intervals are tighter than those of two means;\n"
                                   "vs best has a small bias: the best row was picked by the same worlds.\n"
                                   "\n"
                                   "position: logs/matches/a.jsonl, hand 3, action 17\n"
                                   "seat 2 (West wind), round South, phase Claim, live tiles 70\n"
                                   "hand: 3s 4s 5p 5p 7s 8s 2m 3m 4m Nw\n"
                                   "melds: pung Rd from seat 0\n"
                                   "pending: 5p from seat 1\n"
                                   "seat 0: discards 5\n"
                                   "seat 1: discards 6, calling\n"
                                   "seat 3: discards 4, concealed kongs 1, bonus F1\n"
                                   "\n"
                                   "action                         EV        +-    vs best        +-  vs Normal        +-  note\n"
                                   "claim chow 3s-4s-5s          3.50      0.70                           +2.27      0.90  best, better than Normal\n"
                                   "pass                         1.23      0.50      -2.27      0.80                       Normal, worse than best\n"
                                   "claim pung                   1.23      0.61      -2.27      1.50      +0.00      0.40  logged, worse than best\n"
                                   "claim kong                   0.50                -3.00                -0.73\n"
                                   "claim mahjong               -1.00      0.30      -4.50      0.60      -2.23      0.50  worse than best, worse than Normal\n";
        EXPECT_EQ(Str(text), golden);
    }

    TEST(AnalyzerReportTest, TheSameInputGivesTheSameBytes) {
        EXPECT_EQ(Str(FormatAnalysisReport(ClaimView(), ClaimAnalysis(), LogLabel())), Str(FormatAnalysisReport(ClaimView(), ClaimAnalysis(), LogLabel())));
        EXPECT_EQ(Str(FormatAnalysisJson(ClaimView(), ClaimAnalysis(), LogLabel())), Str(FormatAnalysisJson(ClaimView(), ClaimAnalysis(), LogLabel())));
    }

    TEST(AnalyzerReportTest, RowsAreSortedByTheMeanAndTheOrderOfLegalBreaksATie) {
        PositionAnalysis analysis = ClaimAnalysis();
        const eastl::string text = FormatAnalysisReport(ClaimView(), analysis, PositionLabel{});
        EXPECT_LT(text.find("claim chow"), text.find("pass  "));
        EXPECT_LT(text.find("pass  "), text.find("claim pung"));
        EXPECT_LT(text.find("claim pung"), text.find("claim kong"));
        EXPECT_LT(text.find("claim kong"), text.find("claim mahjong"));

        // The two rows of the same mean swap places when their order in `legal` does.
        eastl::swap(analysis.rows[0], analysis.rows[1]);
        const eastl::string swapped = FormatAnalysisReport(ClaimView(), analysis, PositionLabel{});
        EXPECT_LT(swapped.find("claim pung"), swapped.find("pass  "));
    }

    TEST(AnalyzerReportTest, TheMarksFollowTheRowsAndTheLabel) {
        const eastl::string text = FormatAnalysisReport(ClaimView(), ClaimAnalysis(), LogLabel());
        EXPECT_TRUE(Has(text, "best, better than Normal"));
        EXPECT_TRUE(Has(LineOf(text, "pass").c_str(), "Normal, worse than best"));
        EXPECT_TRUE(Has(LineOf(text, "claim pung").c_str(), "logged"));
        EXPECT_FALSE(Has(LineOf(text, "claim chow").c_str(), "logged"));

        // No log: nothing is `logged`, no source line and no hand number.
        const eastl::string plain = FormatAnalysisReport(ClaimView(), ClaimAnalysis(), PositionLabel{});
        EXPECT_FALSE(Has(plain, "logged"));
        EXPECT_FALSE(Has(plain, "position:"));
        // A named hand-written text prints its name only.
        PositionLabel named;
        named.source = "tests/pung.txt";
        const eastl::string byName = FormatAnalysisReport(ClaimView(), ClaimAnalysis(), named);
        EXPECT_TRUE(Has(byName, "position: tests/pung.txt\n"));
        EXPECT_FALSE(Has(byName, "hand 3"));
    }

    TEST(AnalyzerReportTest, ACellWithoutANumberOrAnIntervalIsBlankNeverNan) {
        const eastl::string text = FormatAnalysisReport(ClaimView(), ClaimAnalysis(), LogLabel());
        EXPECT_FALSE(Has(text, "nan"));
        EXPECT_FALSE(Has(text, "inf"));
        // The best row has no difference to itself, the kong has no interval at all.
        EXPECT_EQ(LineOf(text, "claim kong"), "claim kong                   0.50                -3.00                -0.73");
        EXPECT_FALSE(Has(LineOf(text, "claim chow").c_str(), "-0"));

        // An analysis of nothing prints a table of the header only.
        const eastl::string empty = FormatAnalysisReport(TableView{}, PositionAnalysis{}, PositionLabel{});
        EXPECT_FALSE(Has(empty, "nan"));
    }

    TEST(AnalyzerReportTest, ANegativeZeroIsNeverPrinted) {
        PositionAnalysis analysis = ClaimAnalysis();
        analysis.rows[1].vsNormal = Difference(-0.001, 0.4, false);
        EXPECT_TRUE(Has(LineOf(FormatAnalysisReport(ClaimView(), analysis, PositionLabel{}), "claim pung").c_str(), "+0.00"));
        EXPECT_FALSE(Has(FormatAnalysisJson(ClaimView(), analysis, PositionLabel{}), "-0.0,"));
    }

    TEST(AnalyzerReportTest, TheHeaderStatesEveryApproximation) {
        const eastl::string text = FormatAnalysisReport(ClaimView(), ClaimAnalysis(), PositionLabel{});
        EXPECT_TRUE(Has(text, "opponents are Normal players (level 5)"));
        EXPECT_TRUE(Has(text, "the seat itself plays Normal after the chosen action"));
        EXPECT_TRUE(Has(text, "sampled uniformly"));
        EXPECT_TRUE(Has(text, "this hand only (a drawn hand pays 0)"));
        EXPECT_TRUE(Has(text, "not an equilibrium"));
        EXPECT_TRUE(Has(text, "asked 100, played 98, dropped 2"));
        EXPECT_TRUE(Has(text, "seed 7"));
        EXPECT_TRUE(Has(text, "1.96 standard errors over the worlds"));
        EXPECT_TRUE(Has(text, "paired by world"));
        EXPECT_TRUE(Has(text, "small bias"));
    }

    TEST(AnalyzerReportTest, TheJsonIsTheGoldenLine) {
        const eastl::string line = FormatAnalysisJson(ClaimView(), ClaimAnalysis(), LogLabel());
        EXPECT_EQ(std::count(line.begin(), line.end(), '\n'), 0);
        const std::string golden =
            R"({"actionIndex":17,"actions":[{"action":{"claim":"chow","first":"3s","type":"claim"},"best":true,"ev":3.5,"half":0.7,"logged":false,"normal":false,"text":"claim chow 3s-4s-5s","vsBest":null,"vsNormal":{"half":0.9,"mean":2.27,"significant":true}},{"action":{"type":"pass"},"best":false,"ev":1.23,"half":0.5,"logged":false,"normal":true,"text":"pass","vsBest":{"half":0.8,"mean":-2.27,"significant":true},"vsNormal":null},{"action":{"claim":"pung","type":"claim"},"best":false,"ev":1.23,"half":0.61,"logged":true,"normal":false,"text":"claim pung","vsBest":{"half":1.5,"mean":-2.27,"significant":true},"vsNormal":{"half":0.4,"mean":0.0,"significant":false}},{"action":{"claim":"kong","type":"claim"},"best":false,"ev":0.5,"half":null,"logged":false,"normal":false,"text":"claim kong","vsBest":{"half":null,"mean":-3.0,"significant":false},"vsNormal":{"half":null,"mean":-0.73,"significant":false}},{"action":{"claim":"mahjong","type":"claim"},"best":false,"ev":-1.0,"half":0.3,"logged":false,"normal":false,"text":"claim mahjong","vsBest":{"half":0.6,"mean":-4.5,"significant":true},"vsNormal":{"half":0.5,"mean":-2.23,"significant":true}}],"hand":3,"hiddenTiles":"uniform","intervalZ":1.96,"liveTiles":70,"logged":{"claim":"pung","type":"claim"},"normalSeed":1,"opponentLevel":5,"opponents":"normal","own":"3s 4s 5p 5p 7s 8s 2m 3m 4m Nw","pending":"5p","phase":"Claim","points":"this hand","roundWind":"South","seat":2,"seatAfterAction":"normal","seatWind":"West","seed":7,"source":"logs/matches/a.jsonl","version":1,"worldsAsked":100,"worldsDropped":2,"worldsPlayed":98})";
        EXPECT_EQ(Str(line), golden);
    }

    TEST(AnalyzerReportTest, TheJsonReadsBackAndItsActionsAreAcceptedByTheControlApi) {
        const eastl::string line = FormatAnalysisJson(ClaimView(), ClaimAnalysis(), LogLabel());
        const std::expected<Json, ControlError> parsed = ParseJson(eastl::string_view(line.c_str(), line.size()));
        ASSERT_TRUE(parsed.has_value());
        const Json& json = *parsed;
        EXPECT_EQ(GetJsonInt(json, "version"), kAnalysisJsonVersion);
        EXPECT_EQ(GetJsonInt(json, "seat"), 2);
        EXPECT_EQ(GetJsonInt(json, "worldsPlayed"), kHandWorlds - 2);
        EXPECT_EQ(GetJsonString(json, "phase")->c_str(), std::string("Claim"));
        ASSERT_NE(FindJsonMember(json, "actions"), nullptr);
        const Json& actions = *FindJsonMember(json, "actions");
        ASSERT_EQ(actions.size(), ClaimAnalysis().rows.size());
        const eastl::vector<HandAction> sorted = {kChow, kPass, kPung, kKong, kMahjong};
        for (size_t index = 0; index < actions.size(); ++index) {
            const std::expected<ControlAction, ControlError> action = ActionFromJson(*FindJsonMember(actions[index], "action"));
            ASSERT_TRUE(action.has_value()) << index;
            EXPECT_EQ(action->hand, sorted[index]) << index;
            EXPECT_EQ(GetJsonString(actions[index], "text")->c_str(), Str(ActionNotation(sorted[index])));
        }
        EXPECT_EQ(GetJsonBool(actions[0], "best"), true);
        EXPECT_EQ(GetJsonBool(actions[1], "normal"), true);
        EXPECT_EQ(GetJsonBool(actions[2], "logged"), true);
        // The best row has no difference to itself; the kong has a mean and no half-width.
        EXPECT_TRUE(FindJsonMember(actions[0], "vsBest")->is_null());
        EXPECT_TRUE(FindJsonMember(actions[1], "vsNormal")->is_null());
        EXPECT_TRUE(FindJsonMember(actions[3], "half")->is_null());
        EXPECT_TRUE(FindJsonMember(*FindJsonMember(actions[3], "vsBest"), "half")->is_null());
    }

}  // namespace BECore::Mahjong::Analyzer
