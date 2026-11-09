#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Text/Utf8.h>
#include <Events/SceneEvents.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/SeatName.h>

/**
 * The screens that end a hand and a match in the shipped scene (`Components/MahjongHudComponent.h`): the score table `HandOver` is shown
 * exactly while the hand is over and says what `FormatScoreTable` says, `Next hand` settles it, the last hand leads to the standings
 * `MatchOver`, `New game` opens the seat choice again, and both panels pass the size matrix with the longest rows.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// The worst numbers a screen can show: a limit-sized score, a big change and total, a long chain of doubles.
        constexpr int kWorstScore = 500;
        constexpr int kWorstDelta = -4000;
        constexpr int kWorstTotal = 12000;
        constexpr int kWorstPoints = 64;
        constexpr int kWorstDoubles = 12;
        constexpr int kWorstLinePoints = 32;  // the most one line can give: a concealed kong of terminals (2 x 2 x 2 x 4)
        constexpr float kButtonAir = 8.0f;
        /// The shape of the score table (see `config/nodes/MahjongHandOver.xml`): the plate lies in the ground the hands leave free, so all
        /// four hands stay readable under it. The edges are those of the hands, melds and bonus tiles of the worst-case table.
        constexpr float kScoreTableLeft = 96.0f;
        constexpr float kScoreTableRight = 1184.0f;
        constexpr float kScoreTableTop = 96.0f;
        constexpr float kScoreTableBottom = 612.0f;
        constexpr float kScoreTableHeight = 492.0f;
        constexpr float kScoreHeadlineHeight = 48.0f;
        constexpr float kScoreNextHeight = 56.0f;
        constexpr float kScoreNextWidth = 176.0f;
        constexpr float kMinControlHeight = 48.0f;
        /// The standings lie on a Screen canvas that a 480 px window at the user scale 1.5 makes 320 units high (296 with a notch), so
        /// the plate keeps its rows 4 apart; the button is still 16 below them (a spacer of 8 between two gaps of 4).
        constexpr float kStandingsGap = 4.0f;
        constexpr int kHandLimit = 60;  // hands of a 4-hand match with replayed draws
        /// Output sizes the words are asked in: the design size, a small window, the narrowest windows and a wide short one.
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}, {1707.0f, 480.0f}};

        /// The longest texts of the screens in the current language of `strings`: the real formatters over every winner, East, kind of win, seat of the
        /// discarder, seat of the one person (the mark of "you") and winning tile, the longest found by code points.
        /// The tiles a line of this kind can name: a set or a pair any tile of the hand, a bonus line a flower or a season, a dragon set a dragon, a wind set a wind.
        bool CanNameTile(ScoreKind kind, Tile tile) {
            switch (kind) {
                case ScoreKind::Pung:
                case ScoreKind::Kong:
                case ScoreKind::Pair:
                    return !tile.IsBonus();
                case ScoreKind::Bonus:
                case ScoreKind::OwnBonus:
                case ScoreKind::RoundBonus:
                    return tile.IsBonus();
                case ScoreKind::DragonSet:
                    return tile.IsDragon();
                case ScoreKind::OwnWindSet:
                case ScoreKind::RoundWindSet:
                    return tile.IsWind();
                default:
                    return false;
            }
        }

        /// The tile of the longest name among those `candidate` allows (by code points); `Tile{}` when none.
        Tile LongestTile(const UIStrings& strings, const std::function<bool(Tile)>& candidate) {
            Tile longest;
            size_t most = 0;
            for (uint8_t id = 0; id < kTileIdCount; ++id) {
                const size_t length = candidate(Tile(id)) ? Utf8::CountCodePoints(TileName(strings, Tile(id))) : 0;
                if (length > most) {
                    most = length;
                    longest = Tile(id);
                }
            }
            return longest;
        }

        struct WorstTexts {
            eastl::string headline;
            eastl::string seatRow;
            eastl::string summary;
            eastl::string standing;
            eastl::string standingsHeadline;
            Tile tile;  // the winning tile with the longest name (not a flower)
        };

        WorstTexts FindWorstTexts(const UIStrings& strings) {
            WorstTexts worst;
            worst.tile = LongestTile(strings, [](Tile tile) { return !tile.IsBonus(); });  // a winning tile is never a flower

            std::vector<eastl::string> headlines;
            std::vector<eastl::string> rows;
            std::vector<eastl::string> summaries;
            for (Seat winner = 0; winner < kSeatCount; ++winner) {
                for (Seat east = 0; east < kSeatCount; ++east) {
                    HandSettlement settlement;
                    settlement.kind = ResultKind::Win;
                    settlement.winner = winner;
                    settlement.east = east;
                    settlement.scores.fill(kWorstScore);
                    settlement.deltas.fill(kWorstDelta);
                    ScoreBreakdown& breakdown = settlement.breakdowns[winner];
                    breakdown.won = true;
                    breakdown.points = kWorstPoints;
                    breakdown.doubles = kWorstDoubles;
                    breakdown.raw = kWorstPoints << kWorstDoubles;
                    breakdown.score = kWorstScore;
                    SeatScores totals;
                    totals.fill(kWorstTotal);
                    for (const WinSource source : {WinSource::SelfDraw, WinSource::Discard, WinSource::RobbedKong}) {
                        for (Seat from = 0; from < kSeatCount; ++from) {
                            for (Seat person = 0; person < kSeatCount; ++person) {
                                SeatSetup seats{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
                                seats[person] = SeatKind::Person;
                                HandResult result;
                                result.kind = ResultKind::Win;
                                result.winner = winner;
                                result.winningTile = worst.tile;
                                result.source = source;
                                result.from = from;
                                const ScoreTable table = FormatScoreTable(strings, settlement, result, totals, seats);
                                headlines.push_back(table.headline);
                                summaries.push_back(table.summary);
                                rows.insert(rows.end(), table.seats.begin(), table.seats.end());
                            }
                        }
                    }
                }
            }
            worst.headline = LongestText(headlines);
            worst.seatRow = LongestText(rows);
            worst.summary = LongestText(summaries);

            // The standings: a row with the last place and the biggest score, the headline of three tied seats and of a single winner
            std::vector<eastl::string> standings;
            std::vector<eastl::string> standingsHeadlines;
            for (Seat person = 0; person < kSeatCount; ++person) {
                SeatSetup seats{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
                seats[person] = SeatKind::Person;
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    standings.push_back(Tr(strings, "matchover.row", {{"place", "4"}, {"name", SeatLabel(strings, seat, seats)}, {"score", Format("{}", kWorstTotal)}}));
                }
                for (const SeatScores& deltas : {SeatScores{-300, 100, 100, 100}, SeatScores{100, -300, 100, 100}, SeatScores{100, 100, -300, 100}, SeatScores{100, 100, 100, -300},
                                                 SeatScores{200, 100, 0, -300}, SeatScores{-300, 0, 100, 200}}) {
                    Match match;
                    EXPECT_TRUE(match.Start(MahjongSettings{}, 1).has_value());
                    HandSettlement settlement;
                    settlement.kind = ResultKind::Drawn;
                    settlement.deltas = deltas;
                    EXPECT_TRUE(match.ApplySettlement(settlement).has_value());
                    standingsHeadlines.push_back(FormatStandings(strings, match, seats).headline);
                }
            }
            worst.standing = LongestText(standings);
            worst.standingsHeadline = LongestText(standingsHeadlines);
            return worst;
        }

    }  // namespace

    class MahjongEndScreenTest : public MahjongTableTestBase {
    protected:
        void StartShortMatch(const SeatSetup& seats, uint32_t seed = kSeed) {
            MahjongSettings settings = _game->GetSettings();
            settings.handsPerMatch = kRoundsPerMatch;
            _game->SetSettings(settings);
            ASSERT_NO_FATAL_FAILURE(Start(seats, seed));
        }

        /// Plays the hand to its end between computers and draws.
        void PlayHandOver() {
            _game->GetSession().RunComputers();
            ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
            Draw();
        }

        std::string Text(const char* node, const char* element) const {
            const UIElement* found = Element(node, element);
            const UILabel* label = found != nullptr ? found->Cast<UILabel>() : nullptr;
            if (label == nullptr && found != nullptr) {
                label = found->Cast<UIButton>();  // a button is a label by inheritance, but `Cast` is exact
            }
            return label != nullptr ? std::string(label->GetDisplayText().data(), label->GetDisplayText().size()) : std::string("<no label>");
        }

        bool Shown(const char* node) const {
            const auto canvas = Canvas(node);
            return canvas && canvas->TakesInput();
        }

        /// The settlement the screen of the hand in progress must show, worked out from the model here.
        ScoreTable ExpectedTable() const {
            const HandSettlement settlement = SettleHand(Hand());
            SeatScores totals = _game->GetMatch().Scores();
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                totals[seat] += settlement.deltas[seat];
            }
            return FormatScoreTable(CoreManager::GetUIStrings(), settlement, *Hand().Result(), totals, _game->GetSession().Seats());
        }

        /// Plays whole hands (the person clicking `Next hand` each time) until the match is over.
        void PlayMatchToTheEnd() {
            for (int hand = 0; hand < kHandLimit && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
                _game->GetSession().RunComputers();
                ASSERT_TRUE(_game->GetSession().NextHand().has_value());
            }
            ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        }
    };

    // ----- the score table of a hand -----

    TEST_F(MahjongEndScreenTest, TheScoreTableIsShownExactlyWhileTheHandIsOverAndHiddenAfterNextHand) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        Draw();
        EXPECT_FALSE(Shown("HandOver")) << "a hand in play";
        EXPECT_FALSE(Shown("HandOverAction")) << "a hand in play";
        EXPECT_FALSE(Shown("MatchOver"));

        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        EXPECT_TRUE(Shown("HandOver"));
        EXPECT_TRUE(Shown("HandOverAction"));
        EXPECT_FALSE(Shown("MatchOver"));

        ASSERT_TRUE(_game->GetSession().NextHand().has_value());
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
        EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::Playing);
    }

    TEST_F(MahjongEndScreenTest, TheVariablesAndTheLabelsSayWhatTheScoreTableSays) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers, 3));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());

        const ScoreTable expected = ExpectedTable();
        EXPECT_EQ(Var("handOverHeadline"), expected.headline.c_str());
        EXPECT_EQ(Var("handOverSummary"), expected.summary.c_str());
        EXPECT_EQ(Text("HandOver", "Headline"), expected.headline.c_str());
        EXPECT_EQ(Text("HandOver", "Summary"), expected.summary.c_str());
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(Var(Format("handOverSeat{}", seat).c_str()), expected.seats[seat].c_str());
            EXPECT_EQ(Text("HandOver", Format("SeatRow{}", seat).c_str()), expected.seats[seat].c_str());
        }
        for (int i = 0; i < kMaxScoreLines; ++i) {
            const bool has = i < static_cast<int>(expected.lines.size());
            EXPECT_EQ(Var(Format("scoreLine{}", i).c_str()), has ? expected.lines[i].c_str() : "") << "line " << i;
            EXPECT_EQ(OnScreen("HandOver", Format("ScoreLine{}", i).c_str()), has) << "line " << i << " is visible exactly when it has text";
        }
        EXPECT_EQ(Text("HandOverAction", "NextHand"), "Next hand");
    }

    TEST_F(MahjongEndScreenTest, ALanguageSwitchWhileTheScoreTableIsUpChangesItsTextAtTheNextDraw) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers, 3));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        const ScoreTable english = ExpectedTable();
        ASSERT_EQ(Text("HandOver", "Headline"), english.headline.c_str());
        const uint64_t version = _game->GetSession().Version();

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Draw();

        const ScoreTable russian = ExpectedTable();
        EXPECT_NE(russian.headline, english.headline);
        EXPECT_EQ(Text("HandOver", "Headline"), russian.headline.c_str());
        EXPECT_EQ(Text("HandOver", "Summary"), russian.summary.c_str());
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(Text("HandOver", Format("SeatRow{}", seat).c_str()), russian.seats[seat].c_str());
        }
        for (int i = 0; i < kMaxScoreLines; ++i) {
            const bool has = i < static_cast<int>(russian.lines.size());
            EXPECT_EQ(Var(Format("scoreLine{}", i).c_str()), has ? russian.lines[i].c_str() : "") << "line " << i;
            EXPECT_EQ(OnScreen("HandOver", Format("ScoreLine{}", i).c_str()), has) << "the same lines are visible in the new language";
        }
        EXPECT_NE(Text("HandOverAction", "NextHand"), "Next hand");
        EXPECT_EQ(Text("HandOverAction", "NextHand").size(), Tr(CoreManager::GetUIStrings(), "handover.next").size());
        EXPECT_EQ(_game->GetSession().Version(), version) << "the game did not move";
        EXPECT_TRUE(Shown("HandOver"));

        ASSERT_TRUE(_game->SetLanguage("en"_intern));
        Draw();
        EXPECT_EQ(Text("HandOver", "Headline"), english.headline.c_str());
        EXPECT_EQ(Text("HandOverAction", "NextHand"), "Next hand");
    }

    TEST_F(MahjongEndScreenTest, AWonHandShowsTheWinnersLinesAndADrawnHandShowsNone) {
        bool sawWin = false;
        bool sawDraw = false;
        for (uint32_t seed = 1; seed <= 40 && !(sawWin && sawDraw); ++seed) {
            ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers, seed));
            ASSERT_NO_FATAL_FAILURE(PlayHandOver());
            const bool win = Hand().Result()->kind == ResultKind::Win;
            (win ? sawWin : sawDraw) = true;
            const ScoreTable expected = ExpectedTable();
            if (win) {
                EXPECT_FALSE(expected.lines.empty());
                EXPECT_TRUE(OnScreen("HandOver", "ScoreLine0")) << "seed " << seed;
                EXPECT_EQ(Text("HandOver", "ScoreLine0"), expected.lines[0].c_str());
            } else {
                EXPECT_EQ(Text("HandOver", "Headline"), "Drawn hand");
                EXPECT_FALSE(OnScreen("HandOver", "ScoreLine0")) << "seed " << seed;
            }
        }
        EXPECT_TRUE(sawWin);
        // A drawn hand is rare among computers; the pure test of the text covers it, this one only looks when a seed gives it
    }

    TEST_F(MahjongEndScreenTest, ClickingNextHandSettlesTheHandAndDealsTheNext) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        const SeatScores before = _game->GetMatch().Scores();
        const SeatScores deltas = SettleHand(Hand()).deltas;
        const size_t history = _game->GetMatch().History().size();

        ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
        Draw();

        EXPECT_EQ(_game->GetMatch().History().size(), history + 1);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(_game->GetMatch().Scores()[seat], before[seat] + deltas[seat]);
        }
        EXPECT_FALSE(Shown("HandOver"));
    }

    TEST_F(MahjongEndScreenTest, ANextHandThatIsOutOfDateChangesNothing) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        ASSERT_TRUE(_game->GetSession().NextHand().has_value());  // a control client settled it before the click arrived
        const uint64_t version = _game->GetSession().Version();
        const size_t history = _game->GetMatch().History().size();

        EmitAction("MahjongNextHand");

        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(_game->GetMatch().History().size(), history);
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
    }

    TEST_F(MahjongEndScreenTest, ANextHandFromAControlClientClosesTheScoreTable) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kDefaultSeatSetup));
        // The person plays the hand to its end through the control API (the computers answer at once)
        _game->GetSession().RunComputers();
        IControllableGame* control = _game->AsControllable();
        for (int step = 0; step < kActionLimit && _game->GetSession().GetPhase() == SessionPhase::Playing; ++step) {
            const auto actions = control->GetActions();
            ASSERT_TRUE(actions.has_value());
            ASSERT_FALSE(actions->empty());
            ASSERT_TRUE(control->PerformAction(actions->back()).has_value());
        }
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        Draw();
        ASSERT_TRUE(Shown("HandOver"));
        ASSERT_TRUE(Shown("HandOverAction"));

        Json action = Json::object();
        action["type"] = "next_hand";
        ASSERT_TRUE(control->PerformAction(action).has_value());
        Draw();

        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
    }

    TEST_F(MahjongEndScreenTest, TheButtonOfTheLastHandSaysFinishAndLeadsToTheStandings) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        int finishing = 0;
        for (int hand = 0; hand < kHandLimit && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
            ASSERT_NO_FATAL_FAILURE(PlayHandOver());
            const bool says = Text("HandOverAction", "NextHand") == "Finish match";
            ASSERT_TRUE(says || Text("HandOverAction", "NextHand") == "Next hand");
            ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
            Draw();
            EXPECT_EQ(says, _game->GetMatch().GetPhase() == MatchPhase::Finished) << "hand " << hand << ": the button promises what the click does";
            finishing += says ? 1 : 0;
        }
        EXPECT_EQ(finishing, 1);
        ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_FALSE(Shown("HandOverAction"));
        EXPECT_TRUE(Shown("MatchOver"));
        const StandingsTable table = FormatStandings(CoreManager::GetUIStrings(), _game->GetMatch(), _game->GetSession().Seats());
        EXPECT_EQ(Var("matchOverHeadline"), table.headline.c_str());
        EXPECT_EQ(Text("MatchOver", "Headline"), table.headline.c_str());
        for (int row = 0; row < kSeatCount; ++row) {
            EXPECT_EQ(Var(Format("standing{}", row).c_str()), table.rows[row].c_str());
            EXPECT_EQ(Text("MatchOver", Format("Standing{}", row).c_str()), table.rows[row].c_str());
        }
    }

    TEST_F(MahjongEndScreenTest, TheButtonOfTheLastHandSaysFinishInEveryLanguageAndTheStandingsFollowALanguageSwitch) {
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const UIStrings& strings = CoreManager::GetUIStrings();
            ASSERT_TRUE(_game->SetLanguage(language));  // a start applies the language of the game, not the one the walk set on the table
            ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
            int finishing = 0;
            for (int hand = 0; hand < kHandLimit && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
                ASSERT_NO_FATAL_FAILURE(PlayHandOver());
                const std::string label = Text("HandOverAction", "NextHand");
                const bool says = label == Tr(strings, "handover.finish").c_str();
                ASSERT_TRUE(says || label == Tr(strings, "handover.next").c_str()) << label;
                ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
                Draw();
                EXPECT_EQ(says, _game->GetMatch().GetPhase() == MatchPhase::Finished) << "hand " << hand << ": the button promises what the click does";
                finishing += says ? 1 : 0;
            }
            EXPECT_EQ(finishing, 1);
            ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);

            // The standings are in this language, and in the other after a switch while they are up
            const StandingsTable table = FormatStandings(strings, _game->GetMatch(), _game->GetSession().Seats());
            EXPECT_EQ(Text("MatchOver", "Headline"), table.headline.c_str());
            EXPECT_EQ(Text("MatchOver", "NewGame"), Tr(strings, "matchover.newGame").c_str());
            const PoolString other = language == "en"_intern ? PoolString("ru"_intern) : PoolString("en"_intern);
            ASSERT_TRUE(_game->SetLanguage(other));
            Draw();
            const StandingsTable switched = FormatStandings(CoreManager::GetUIStrings(), _game->GetMatch(), _game->GetSession().Seats());
            EXPECT_NE(switched.headline, table.headline);
            EXPECT_EQ(Text("MatchOver", "Headline"), switched.headline.c_str());
            for (int row = 0; row < kSeatCount; ++row) {
                EXPECT_EQ(Text("MatchOver", Format("Standing{}", row).c_str()), switched.rows[row].c_str());
            }
            ASSERT_TRUE(_game->SetLanguage(language));  // the walk goes on from this language
        });
    }

    TEST_F(MahjongEndScreenTest, NewGameOnTheStandingsOpensTheSeatChoiceAndAStartHidesThem) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        ASSERT_TRUE(Shown("MatchOver"));

        ASSERT_TRUE(ClickElement("MatchOver", "NewGame"));
        Draw();
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(Shown("Setup"));

        ASSERT_TRUE(ClickElement("Setup", "Start"));
        Draw();
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(Shown("MatchOver")) << "a new match is in play";
        EXPECT_EQ(_game->GetMatch().GetPhase(), MatchPhase::InHand);
    }

    TEST_F(MahjongEndScreenTest, NothingMovesBehindTheScoreTableUntilNextHand) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        const uint64_t version = _game->GetSession().Version();

        for (int i = 0; i < 4; ++i) {
            SceneEvents::SceneUpdateEvent::Emit(kComputerStepSeconds * 2.0f);
        }

        EXPECT_EQ(_game->GetSession().Version(), version);
        EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
    }

    // ----- the look of the two screens -----

    TEST_F(MahjongEndScreenTest, TheScoreTableFollowsTheModalAnatomyAndTheGridGaps) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        ASSERT_TRUE(canvas);
        for (int i = 0; i < kMaxScoreLines; ++i) {
            ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(Format("ScoreLine{}", i)), true));
        }
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            CoreManager::GetUIVars().Set("handOverHeadline"_intern, eastl::string_view(FindWorstTexts(CoreManager::GetUIStrings()).headline));
            canvas->UpdateLayout(kViewWidth, kViewHeight);

            const UIElement* plateElement = Element("HandOver", "Plate");
            ASSERT_NE(plateElement, nullptr);
            const Rect& plate = plateElement->rect;
            EXPECT_TRUE(SameColor(plateElement->color, PaletteColor("surface")));
            EXPECT_GE(plate.y, kScoreTableTop) << "the plate keeps clear of the top seat's sets";
            EXPECT_LE(plate.y + plate.h, kScoreTableBottom) << "the plate keeps clear of the viewer's bonus tiles";
            EXPECT_GE(plate.x, kScoreTableLeft) << "the plate keeps clear of the left seat's columns";
            EXPECT_LE(plate.x + plate.w, kScoreTableRight) << "the plate keeps clear of the right seat's columns";
            EXPECT_EQ(plate.h, kScoreTableHeight);

            const UIElement* headline = Element("HandOver", "Headline");
            ASSERT_NE(headline, nullptr);
            EXPECT_TRUE(SameColor(headline->color, PaletteColor("header")));
            EXPECT_EQ(headline->rect.x, plate.x + kModalPadding);
            EXPECT_EQ(headline->rect.y, plate.y + kModalPadding);
            EXPECT_EQ(headline->rect.w, plate.w - 2.0f * kModalPadding);
            EXPECT_EQ(headline->rect.h, kScoreHeadlineHeight) << "one line of 28";

            const Rect* above = &headline->rect;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const UIElement* row = Element("HandOver", Format("SeatRow{}", seat).c_str());
                ASSERT_NE(row, nullptr);
                EXPECT_TRUE(SameColor(row->color, PaletteColor("surfaceRaised"))) << seat;
                EXPECT_EQ(GapBelow(*above, row->rect), kModalGap) << "row " << seat;
                EXPECT_EQ(row->rect.x, headline->rect.x) << seat;
                EXPECT_EQ(row->rect.w, headline->rect.w) << seat;
                above = &row->rect;
            }

            const UIElement* lines = Element("HandOver", "Lines");
            ASSERT_NE(lines, nullptr);
            EXPECT_EQ(GapBelow(*above, lines->rect), kModalGap);
            std::vector<float> columns;  // the distinct left edges of the lines: three columns
            for (int i = 0; i < kMaxScoreLines; ++i) {
                const UIElement* line = Element("HandOver", Format("ScoreLine{}", i).c_str());
                ASSERT_NE(line, nullptr) << i;
                if (std::find(columns.begin(), columns.end(), line->rect.x) == columns.end()) {
                    columns.push_back(line->rect.x);
                }
                EXPECT_GE(line->rect.x, lines->rect.x) << i;
                EXPECT_LE(line->rect.x + line->rect.w, lines->rect.x + lines->rect.w) << i;
                EXPECT_GE(line->rect.y, lines->rect.y) << i;
                EXPECT_LE(line->rect.y + line->rect.h, lines->rect.y + lines->rect.h) << "line " << i << " lies inside its area, all sixteen shown";
            }
            EXPECT_EQ(columns.size(), 3u) << "the sixteen lines lie in three columns";
            above = &lines->rect;

            const UIElement* summary = Element("HandOver", "Summary");
            ASSERT_NE(summary, nullptr);
            EXPECT_TRUE(SameColor(summary->color, PaletteColor("surfaceRaised")));
            EXPECT_TRUE(SameColor(summary->Cast<UILabel>()->textColor, PaletteColor("textMuted")));
            EXPECT_EQ(GapBelow(*above, summary->rect), kModalGap);
            above = &summary->rect;

            EXPECT_EQ(summary->rect.y + summary->rect.h, plate.y + plate.h - kModalPadding) << "the plate ends with the summary band: the button is in the corner";

            // The score table leaves the hands face up under it: no dim, and the plate is the only thing that blocks the clicks
            EXPECT_EQ(Element("HandOver", "Dim"), nullptr) << "a dim would cover the hands";
            EXPECT_TRUE(plateElement->blocksInput);
            EXPECT_TRUE(canvas->_modal) << "the canvas is still modal: it takes every click";

            // The main button is not in the plate: it is in the corner of New game (HudMenu is hidden meanwhile), in a Screen canvas
            // whose floor keeps it a touch target. 56 high at 22, because a bar or a button of 64 / 28 would lie on the viewer's hand.
            auto corner = Canvas("HandOverAction");
            ASSERT_TRUE(corner);
            corner->UpdateLayout(kViewWidth, kViewHeight);
            EXPECT_EQ(Element("HandOverAction", "Bar"), nullptr) << "no bar: it would cover the viewer's hand";
            const UIElement* next = Element("HandOverAction", "NextHand");
            ASSERT_NE(next, nullptr);
            EXPECT_TRUE(SameColor(next->color, PaletteColor("primary")));
            EXPECT_EQ(next->rect.h, kScoreNextHeight);
            EXPECT_EQ(next->rect.w, kScoreNextWidth);
            EXPECT_EQ(next->rect.x + next->rect.w, kViewWidth - kViewMargin) << "in the top right corner";
            EXPECT_EQ(next->rect.y, kViewMargin);
            EXPECT_GE(GapBelow(next->rect, plate), 0.0f) << "above the plate";
            EXPECT_FALSE(Intersects(next->rect, plate));
        });
    }

    namespace {

        /// Whether a slot is part of a hand: its concealed tiles, its sets and its bonus tiles. The discard blocks and the wall are the piles and
        /// the ring in the middle, which the score table covers on purpose (the owner's call is about the hands).
        bool IsHandSlot(const TileSlot& slot) {
            return slot.role == SlotRole::HandTile || slot.role == SlotRole::MeldTile || slot.role == SlotRole::BonusTile;
        }

        /// Expects that no hand, meld or bonus slot of the layout (view units) intersects `rect`, and that the layout has such slots of all four
        /// seats, so the check is not an empty one.
        void ExpectNoHandUnder(const Rect& rect, const TableLayout& layout, const char* label) {
            std::array<size_t, kSeatCount> slotsOfSeat{};
            for (const TileSlot& slot : layout.slots) {
                if (!IsHandSlot(slot)) {
                    continue;
                }
                ++slotsOfSeat[slot.seat];
                EXPECT_FALSE(Intersects(rect, slot.rect)) << label << ": covers the slot of role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index << " part "
                                                          << slot.part << " at (" << slot.rect.x << ", " << slot.rect.y << ", " << slot.rect.w << " x " << slot.rect.h << ")";
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_GT(slotsOfSeat[seat], 0u) << label << ": seat " << seat;
            }
        }

    }  // namespace

    TEST_F(MahjongEndScreenTest, TheScoreTableCoversNoHandMeldOrBonusTileAtTheDesignSize) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        auto corner = Canvas("HandOverAction");
        ASSERT_TRUE(canvas && corner);
        ASSERT_TRUE(Shown("HandOver") && Shown("HandOverAction"));
        for (int i = 0; i < kMaxScoreLines; ++i) {
            ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(Format("ScoreLine{}", i)), true));
        }

        // The score table promises that the four hands stay readable under it: there is no dim, it is the plate that is modal
        EXPECT_EQ(Element("HandOver", "Dim"), nullptr);
        EXPECT_TRUE(canvas->_modal);
        EXPECT_EQ(Element("HandOverAction", "Bar"), nullptr);

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const WorstTexts worst = FindWorstTexts(CoreManager::GetUIStrings());
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("handOverHeadline"_intern, eastl::string_view(worst.headline));
            vars.Set("handOverSummary"_intern, eastl::string_view(worst.summary));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                vars.Set(PoolString::Intern(Format("handOverSeat{}", seat)), eastl::string_view(worst.seatRow));
            }
            canvas->UpdateLayout(kViewWidth, kViewHeight);
            corner->UpdateLayout(kViewWidth, kViewHeight);
            const UIElement* plate = Element("HandOver", "Plate");
            const UIElement* next = Element("HandOverAction", "NextHand");
            ASSERT_TRUE(plate != nullptr && next != nullptr);

            // The worst-case table, seen from each seat: 14 tiles, four kongs and 8 bonus tiles a seat
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                const TableLayout layout = MakeWorstCaseTableLayout(viewer);
                ExpectNoHandUnder(plate->rect, layout, Format("plate, viewer {}", viewer).c_str());
                ExpectNoHandUnder(next->rect, layout, Format("Next hand, viewer {}", viewer).c_str());
            }
        });

        // The table of the finished hand itself: all four hands are face up and no slot of them is under the plate or the button
        const TableLayout& layout = _table->GetLayout();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(FaceUpHandTiles(seat), static_cast<int>(Hand().GetHand(seat).concealed.size())) << "seat " << seat << ": the hand is face up under the score table";
        }
        ExpectNoHandUnder(Element("HandOver", "Plate")->rect, layout, "plate, the table of the hand");
        ExpectNoHandUnder(Element("HandOverAction", "NextHand")->rect, layout, "Next hand, the table of the hand");
    }

    TEST_F(MahjongEndScreenTest, ThePlateOfTheScoreTableCoversNoHandMeldOrBonusTileInAnyWindowOfTheMatrix) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        ASSERT_TRUE(canvas);
        for (int i = 0; i < kMaxScoreLines; ++i) {
            ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(Format("ScoreLine{}", i)), true));
        }
        std::vector<TableLayout> layouts;  // the worst case seen from each seat
        for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
            layouts.push_back(MakeWorstCaseTableLayout(viewer));
        }

        // The plate is a SceneView canvas and follows the table (it has ignoreSafeArea like the table), so no window brings it onto a hand,
        // not even one with insets
        size_t windows = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            LayOutInWindow(*canvas, window);
            const UIElement* plate = Element("HandOver", "Plate");
            ASSERT_NE(plate, nullptr);
            ASSERT_FALSE(plate->rect.IsEmpty()) << window.label.c_str();
            const Rect plateRect = ToView(plate->rect, *canvas, window);
            EXPECT_GE(plateRect.x, kScoreTableLeft - 0.01f) << window.label.c_str();
            EXPECT_LE(plateRect.x + plateRect.w, kScoreTableRight + 0.01f) << window.label.c_str();
            EXPECT_GE(plateRect.y, kScoreTableTop - 0.01f) << window.label.c_str();
            EXPECT_LE(plateRect.y + plateRect.h, kScoreTableBottom + 0.01f) << window.label.c_str();
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                ExpectNoHandUnder(plateRect, layouts[viewer], Format("{} viewer {}", window.label.c_str(), viewer).c_str());
            }
            ++windows;
        });
        EXPECT_GT(windows, 0u);
    }

    TEST_F(MahjongEndScreenTest, TheStandingsFollowTheModalAnatomyAndTheFirstPlaceIsGold) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        auto canvas = Canvas("MatchOver");
        ASSERT_TRUE(canvas);
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            CoreManager::GetUIVars().Set("matchOverHeadline"_intern, eastl::string_view(FindWorstTexts(CoreManager::GetUIStrings()).standingsHeadline));
            canvas->UpdateLayout(kViewWidth, kViewHeight);

            const UIElement* plateElement = Element("MatchOver", "Plate");
            ASSERT_NE(plateElement, nullptr);
            const Rect& plate = plateElement->rect;
            EXPECT_TRUE(SameColor(plateElement->color, PaletteColor("surface")));

            const UIElement* headline = Element("MatchOver", "Headline");
            ASSERT_NE(headline, nullptr);
            EXPECT_TRUE(SameColor(headline->color, PaletteColor("header")));
            EXPECT_EQ(headline->rect.x, plate.x + kModalPadding);
            EXPECT_EQ(headline->rect.y, plate.y + kModalPadding);

            const Rect* above = &headline->rect;
            for (int row = 0; row < kSeatCount; ++row) {
                const UIElement* standing = Element("MatchOver", Format("Standing{}", row).c_str());
                ASSERT_NE(standing, nullptr);
                EXPECT_TRUE(SameColor(standing->color, PaletteColor("surfaceRaised"))) << row;
                EXPECT_EQ(GapBelow(*above, standing->rect), kStandingsGap) << "row " << row;
                above = &standing->rect;
                // The first place is the accent (gold), the others the plain text colour
                const Color& text = standing->Cast<UILabel>()->textColor;
                EXPECT_EQ(SameColor(text, PaletteColor("accent")), row == 0) << "row " << row;
                if (row != 0) {
                    EXPECT_TRUE(SameColor(text, PaletteColor("text"))) << "row " << row;
                }
            }

            const UIElement* button = Element("MatchOver", "NewGame");
            ASSERT_NE(button, nullptr);
            EXPECT_TRUE(SameColor(button->color, PaletteColor("primary")));
            EXPECT_EQ(button->rect.h, kMainButtonHeight);
            EXPECT_EQ(GapBelow(*above, button->rect), kModalSectionGap) << "the button is set apart from the rows by 16";
            EXPECT_EQ(button->rect.y + button->rect.h, plate.y + plate.h - kModalPadding) << "the plate holds the button and nothing more";
        });
    }

    // ----- the size matrix -----

    TEST_F(MahjongEndScreenTest, TheScoreTablePassesTheSizeMatrixInEveryLanguageWithTheLongestRowsAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        auto canvas = Canvas("HandOver");
        ASSERT_TRUE(canvas);
        for (int i = 0; i < kMaxScoreLines; ++i) {
            ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(Format("ScoreLine{}", i)), true));
        }

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const UIStrings& strings = CoreManager::GetUIStrings();
            const WorstTexts worst = FindWorstTexts(strings);
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("handOverHeadline"_intern, eastl::string_view(worst.headline));
            vars.Set("handOverSummary"_intern, eastl::string_view(worst.summary));
            vars.Set("handOverButton"_intern, eastl::string_view(LongestText({Tr(strings, "handover.next"), Tr(strings, "handover.finish")})));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                vars.Set(PoolString::Intern(Format("handOverSeat{}", seat)), eastl::string_view(worst.seatRow));
            }

            // The longest line the rules can print, of every kind and in both forms (points and doubles), in all sixteen places at once
            for (const ScoreKind kind : EnumUtils<ScoreKind>::Values()) {
                for (const int doubles : {0, 3}) {
                    ScoreLine line;
                    line.kind = kind;
                    line.tile = LongestTile(strings, [&](Tile tile) { return CanNameTile(kind, tile); });  // the longest tile this kind can name
                    line.points = kWorstLinePoints;
                    line.doubles = doubles;
                    const eastl::string text = ScoreLineText(strings, line);
                    for (int i = 0; i < kMaxScoreLines; ++i) {
                        vars.Set(PoolString::Intern(Format("scoreLine{}", i)), eastl::string_view(text));
                    }
                    SCOPED_TRACE(EnumUtils<ScoreKind>::ToString(kind).data());
                    for (const Output& output : kOutputs) {
                        ExpectWordsFit("HandOver", output.width, output.height, kButtonAir);
                    }
                }
            }
            // The plate is text only; the button is in the corner canvas, and the longer of its two words fits there
            for (const Output& output : kOutputs) {
                ExpectWordsFit("HandOverAction", output.width, output.height, kButtonAir);
            }
            ExpectPassesTheMatrix("HandOver", EngineTouchMinimum());
            ExpectPassesTheMatrix("HandOverAction", EngineTouchMinimum());
            EXPECT_GE(Element("HandOverAction", "NextHand")->rect.h, kMinControlHeight);
        });
    }

    TEST_F(MahjongEndScreenTest, TheStandingsPassTheSizeMatrixInEveryLanguageWithTheLongestRowsAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const WorstTexts worst = FindWorstTexts(CoreManager::GetUIStrings());
            UIVars& vars = CoreManager::GetUIVars();
            vars.Set("matchOverHeadline"_intern, eastl::string_view(worst.standingsHeadline));
            for (int row = 0; row < kSeatCount; ++row) {
                vars.Set(PoolString::Intern(Format("standing{}", row)), eastl::string_view(worst.standing));
            }

            ExpectPassesTheMatrix("MatchOver", EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("MatchOver", output.width, output.height, kButtonAir);
            }
            EXPECT_GE(Element("MatchOver", "NewGame")->rect.h, kMinControlHeight);
        });
    }

    TEST_F(MahjongEndScreenTest, TheButtonsOfBothPanelsCarryTheirActions) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        Draw();
        EXPECT_EQ(Element("HandOverAction", "NextHand")->Cast<UIButton>()->action, "MahjongNextHand"_intern);
        EXPECT_EQ(Element("MatchOver", "NewGame")->Cast<UIButton>()->action, "MahjongNewGame"_intern);
    }

    TEST_F(MahjongEndScreenTest, TheScoreTableHasNoControlAndTheNewGameButtonOfTheHudIsHiddenWhileItIsShown) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        Draw();
        EXPECT_TRUE(Shown("HudMenu")) << "a hand in play";

        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        // The Next hand button takes the corner of the New game button of the HUD, so the HUD buttons are hidden
        EXPECT_FALSE(Shown("HudMenu"));
        auto plate = Canvas("HandOver");
        ASSERT_TRUE(plate);
        std::vector<const UIElement*> controls;
        CollectControls(plate->GetRoot(), controls);
        for (const UIElement* control : controls) {
            EXPECT_FALSE(control->interactive) << control->name.ToStringView().data() << ": the plate is text, the button is in HandOverAction";
        }

        ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
        Draw();
        EXPECT_TRUE(Shown("HudMenu"));
    }

    TEST_F(MahjongEndScreenTest, TheStandingsCoverTheWholeWindowSoTheNewGameButtonOfTheHudStaysAsItIs) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        ASSERT_TRUE(Shown("MatchOver"));
        EXPECT_TRUE(Shown("HudMenu"));
        const UIElement* dim = Element("MatchOver", "Dim");
        ASSERT_NE(dim, nullptr);
        EXPECT_TRUE(dim->blocksInput);
        EXPECT_EQ(Canvas("MatchOver")->GetLayout().params.space, CanvasSpace::Screen) << "a Screen canvas: its dim covers the bars too";
        // The button of the standings is the one that answers, and the click opens the seat choice
        ASSERT_TRUE(ClickElement("MatchOver", "NewGame"));
        EXPECT_TRUE(_game->IsSetupOpen());
    }

}  // namespace BECore::Mahjong::Tests
