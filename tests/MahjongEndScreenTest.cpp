#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Events/SceneEvents.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/ScoreText.h>

/**
 * The screens that end a hand and a match in the shipped scene (`Components/MahjongHudComponent.h`): the score table `HandOver` is shown
 * exactly while the hand is over and says what `FormatScoreTable` says, `Next hand` settles it, the last hand leads to the standings
 * `MatchOver`, `New game` opens the seat choice again, and both panels pass the size matrix with the longest rows.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// The worst case: the longest wind names (North, South) on both seats and the mark of "you" on one of them (61 characters).
        constexpr const char* kLongestHeadline = "Seat 4 (North) You wins by robbing the kong of Seat 2 (South)";
        constexpr const char* kLongestSeatRow = "Seat 4 (North) You: hand 500, -4000, total 12000";
        constexpr const char* kLongestSummary = "Winning tile 9 Characters: 64 points x 4096 = 262144, score 500";
        constexpr const char* kLongestStanding = "4. Seat 4 You: 12000";
        constexpr const char* kLongestStandingsHeadline = "Tied for first: Seat 1, Seat 2, Seat 3";
        /// The standings are a Screen canvas with minScale: its button is a touch target in any window. The score table lies on the
        /// fixed design of the table, as `Setup` and the HUD do: from a 480 px window its button is 21 px (the owner's list).
        constexpr float kStandingsTouchMinimum = 32.0f;
        constexpr float kScoreTableTouchMinimum = 20.0f;
        constexpr float kButtonAir = 8.0f;
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
            return FormatScoreTable(settlement, *Hand().Result(), totals, _game->GetSession().Seats());
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
        EXPECT_FALSE(Shown("MatchOver"));

        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        EXPECT_TRUE(Shown("HandOver"));
        EXPECT_FALSE(Shown("MatchOver"));

        ASSERT_TRUE(_game->GetSession().NextHand().has_value());
        Draw();
        EXPECT_FALSE(Shown("HandOver"));
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
        EXPECT_EQ(Text("HandOver", "NextHand"), "Next hand");
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

        ASSERT_TRUE(ClickElement("HandOver", "NextHand"));
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

        Json action = Json::object();
        action["type"] = "next_hand";
        ASSERT_TRUE(control->PerformAction(action).has_value());
        Draw();

        EXPECT_FALSE(Shown("HandOver"));
    }

    TEST_F(MahjongEndScreenTest, TheButtonOfTheLastHandSaysFinishAndLeadsToTheStandings) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        int finishing = 0;
        for (int hand = 0; hand < kHandLimit && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
            ASSERT_NO_FATAL_FAILURE(PlayHandOver());
            const bool says = Text("HandOver", "NextHand") == "Finish match";
            ASSERT_TRUE(says || Text("HandOver", "NextHand") == "Next hand");
            ASSERT_TRUE(ClickElement("HandOver", "NextHand"));
            Draw();
            EXPECT_EQ(says, _game->GetMatch().GetPhase() == MatchPhase::Finished) << "hand " << hand << ": the button promises what the click does";
            finishing += says ? 1 : 0;
        }
        EXPECT_EQ(finishing, 1);
        ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        EXPECT_FALSE(Shown("HandOver"));
        EXPECT_TRUE(Shown("MatchOver"));
        const StandingsTable table = FormatStandings(_game->GetMatch(), _game->GetSession().Seats());
        EXPECT_EQ(Var("matchOverHeadline"), table.headline.c_str());
        EXPECT_EQ(Text("MatchOver", "Headline"), table.headline.c_str());
        for (int row = 0; row < kSeatCount; ++row) {
            EXPECT_EQ(Var(Format("standing{}", row).c_str()), table.rows[row].c_str());
            EXPECT_EQ(Text("MatchOver", Format("Standing{}", row).c_str()), table.rows[row].c_str());
        }
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
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("handOverHeadline"_intern, eastl::string_view(kLongestHeadline));
        auto canvas = Canvas("HandOver");
        ASSERT_TRUE(canvas);
        for (int i = 0; i < kMaxScoreLines; ++i) {
            ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(Format("ScoreLine{}", i)), true));
        }
        canvas->UpdateLayout(kViewWidth, kViewHeight);

        const UIElement* plateElement = Element("HandOver", "Plate");
        ASSERT_NE(plateElement, nullptr);
        const Rect& plate = plateElement->rect;
        EXPECT_TRUE(SameColor(plateElement->color, PaletteColor("surface")));
        EXPECT_GE(plate.y, kViewMargin);
        EXPECT_LE(plate.y + plate.h, kViewHeight - kViewMargin) << "the plate lies inside the view with a margin";

        const UIElement* headline = Element("HandOver", "Headline");
        ASSERT_NE(headline, nullptr);
        EXPECT_TRUE(SameColor(headline->color, PaletteColor("header")));
        EXPECT_EQ(headline->rect.x, plate.x + kModalPadding);
        EXPECT_EQ(headline->rect.y, plate.y + kModalPadding);
        EXPECT_EQ(headline->rect.w, plate.w - 2.0f * kModalPadding);

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
        for (int i = 0; i < kMaxScoreLines; ++i) {
            const UIElement* line = Element("HandOver", Format("ScoreLine{}", i).c_str());
            ASSERT_NE(line, nullptr) << i;
            EXPECT_GE(line->rect.x, lines->rect.x) << i;
            EXPECT_LE(line->rect.x + line->rect.w, lines->rect.x + lines->rect.w) << i;
            EXPECT_GE(line->rect.y, lines->rect.y) << i;
            EXPECT_LE(line->rect.y + line->rect.h, lines->rect.y + lines->rect.h) << "line " << i << " lies inside its area, all sixteen shown";
        }
        above = &lines->rect;

        const UIElement* summary = Element("HandOver", "Summary");
        ASSERT_NE(summary, nullptr);
        EXPECT_TRUE(SameColor(summary->color, PaletteColor("surfaceRaised")));
        EXPECT_TRUE(SameColor(summary->Cast<UILabel>()->textColor, PaletteColor("textMuted")));
        EXPECT_EQ(GapBelow(*above, summary->rect), kModalGap);
        above = &summary->rect;

        const UIElement* next = Element("HandOver", "NextHand");
        ASSERT_NE(next, nullptr);
        EXPECT_TRUE(SameColor(next->color, PaletteColor("primary")));
        EXPECT_EQ(next->rect.h, kMainButtonHeight);
        EXPECT_EQ(GapBelow(*above, next->rect), kModalSectionGap) << "the button is set apart from the rows by 16";
        EXPECT_EQ(next->rect.y + next->rect.h, plate.y + plate.h - kModalPadding) << "the plate holds the button and nothing more";
    }

    TEST_F(MahjongEndScreenTest, TheStandingsFollowTheModalAnatomyAndTheFirstPlaceIsGold) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("matchOverHeadline"_intern, eastl::string_view(kLongestStandingsHeadline));
        auto canvas = Canvas("MatchOver");
        ASSERT_TRUE(canvas);
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
    }

    // ----- the size matrix -----

    TEST_F(MahjongEndScreenTest, TheScoreTablePassesTheSizeMatrixWithTheLongestRowsAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayHandOver());
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("handOverHeadline"_intern, eastl::string_view(kLongestHeadline));
        vars.Set("handOverSummary"_intern, eastl::string_view(kLongestSummary));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            vars.Set(PoolString::Intern(Format("handOverSeat{}", seat)), eastl::string_view(kLongestSeatRow));
        }
        auto canvas = Canvas("HandOver");
        ASSERT_TRUE(canvas);
        for (int i = 0; i < kMaxScoreLines; ++i) {
            ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(Format("ScoreLine{}", i)), true));
        }

        // The longest line the rules can print, of every kind, in all sixteen places at once
        for (const ScoreKind kind : EnumUtils<ScoreKind>::Values()) {
            ScoreLine line;
            line.kind = kind;
            line.tile = T("9m");
            line.points = 128;
            line.doubles = 3;
            const eastl::string text = ScoreLineText(line);
            for (int i = 0; i < kMaxScoreLines; ++i) {
                vars.Set(PoolString::Intern(Format("scoreLine{}", i)), eastl::string_view(text));
            }
            SCOPED_TRACE(EnumUtils<ScoreKind>::ToString(kind).data());
            for (const Output& output : kOutputs) {
                ExpectWordsFit("HandOver", output.width, output.height, kButtonAir);
            }
        }
        ExpectPassesTheMatrix("HandOver", kScoreTableTouchMinimum);
        EXPECT_GE(Element("HandOver", "NextHand")->rect.h, kMinControlHeight);
    }

    TEST_F(MahjongEndScreenTest, TheStandingsPassTheSizeMatrixWithTheLongestRowsAndEveryWordFits) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        Draw();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("matchOverHeadline"_intern, eastl::string_view(kLongestStandingsHeadline));
        for (int row = 0; row < kSeatCount; ++row) {
            vars.Set(PoolString::Intern(Format("standing{}", row)), eastl::string_view(kLongestStanding));
        }

        ExpectPassesTheMatrix("MatchOver", kStandingsTouchMinimum);
        for (const Output& output : kOutputs) {
            ExpectWordsFit("MatchOver", output.width, output.height, kButtonAir);
        }
        EXPECT_GE(Element("MatchOver", "NewGame")->rect.h, kMinControlHeight);
    }

    TEST_F(MahjongEndScreenTest, TheButtonsOfBothPanelsCarryTheirActions) {
        ASSERT_NO_FATAL_FAILURE(StartShortMatch(kAllComputers));
        Draw();
        EXPECT_EQ(Element("HandOver", "NextHand")->Cast<UIButton>()->action, "MahjongNextHand"_intern);
        EXPECT_EQ(Element("MatchOver", "NewGame")->Cast<UIButton>()->action, "MahjongNewGame"_intern);
    }

}  // namespace BECore::Mahjong::Tests
