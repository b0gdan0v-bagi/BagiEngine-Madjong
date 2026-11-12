#include "CoachCardTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"
#include "MahjongOnlineTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Mahjong/Components/MahjongHintsComponent.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/Coach.h>
#include <Mahjong/Table/Hints.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The hints at the table of a room (`Components/MahjongHintsComponent.h`, `MahjongGame::HintsActive()`): the host decides whether they exist, the
 * player's own mode (`Options`) decides whether he uses them, and every hint is worked out from the viewer's view of the display copy with the server's
 * `actions` as `legal`. The server's own position is laid by a test (`LayOnServer`: the match of the room's game is replaced and the client is
 * cut and comes back, which sends it a fresh view), so the oracle is `MakeTableView` of the server's hand for the viewer's seat.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // Seat 0 (East, the viewer) waits on the Ew with 123s 456s 789m 234p and draws the Rd (so the Ew and the Rd declare); seat 1 declared with its
        // discard of the Wd in the second lap, after which the 9m (seat 2) and the Ew (seat 3) were discarded (as in `MahjongHintsDangerTest`).
        constexpr const char* kViewerHand = "1s 2s 3s 4s 5s 6s 7m 8m 9m 2p 3p 4p Ew";
        constexpr const char* kWaitingHand = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Sw Sw";
        constexpr const char* kSeat2Hand = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd";
        constexpr const char* kSeat3Hand = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd";
        // The same position with other tiles in the hands the viewer cannot see
        constexpr const char* kOtherSeat2Hand = "1m 1m 2m 2m 5m 5m 8s 8s 9s 9s 7p 7p 8p";
        constexpr const char* kOtherSeat3Hand = "2s 2s 7s 7s 1m 5m 6m 7p 8p 8p 9p 9p 1s";
        constexpr const char* kLive = "Ew Rd Nw Ww Gd Wd 9m Ew Rd 1p 2p";
        constexpr const char* kViewersDiscard = "Rd";
        constexpr int kHostSeat = 0;
        constexpr int kIdleSeat = 1;
        constexpr int kLayFrames = 3;

        /// Each seat in turn discards the next tile of @p tiles; the state after every discard is kept (the room is shown one move at a time, as a client
        /// that stays connected sees it: the log of a hand is inferred from the views, and a gap would coalesce the moves).
        void Play(std::vector<HandModel>& states, const std::string& tiles, std::initializer_list<Seat> declaring = {}) {
            HandModel model = states.back();
            const eastl::vector<Tile> parsed = ParseTiles(tiles).value();
            for (const Tile tile : parsed) {
                const Seat seat = model.CurrentSeat();
                const bool declare = eastl::find(declaring.begin(), declaring.end(), seat) != declaring.end();
                EXPECT_TRUE(model.Discard(seat, tile, declare).has_value()) << "seat " << seat << " " << ToString(tile);
                EXPECT_EQ(model.GetPhase(), Phase::Discard) << "the discard of " << ToString(tile) << " opened a window";
                states.push_back(model);
            }
        }

        /// The lap of the notes above on a fixed wall, state by state; the last is the viewer to act with 14 tiles, or, when @p viewerHasDiscarded, seat 1 (a
        /// person who waits) to act after the viewer's declaring discard.
        std::vector<HandModel> PlayedStates(const char* seat2, const char* seat3, bool viewerHasDiscarded) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kViewerHand;
            spec.hands[kSeat1] = kWaitingHand;
            spec.hands[kSeat2] = seat2;
            spec.hands[kSeat3] = seat3;
            spec.live = kLive;
            std::vector<HandModel> states{MakeModel(spec)};
            Play(states, "Ew Rd Nw Ww");
            Play(states, "Gd Wd 9m Ew", {kSeat1});
            EXPECT_EQ(states.back().CurrentSeat(), kSeat0);
            if (viewerHasDiscarded) {
                Play(states, kViewersDiscard, {kSeat0});
                EXPECT_EQ(states.back().CurrentSeat(), kSeat1);
            }
            return states;
        }

        int Count(const eastl::array<bool, kTileIdCount>& flags, const eastl::vector<Tile>& hand) {
            int count = 0;
            for (const Tile tile : hand) {
                count += flags[tile.Id()] ? 1 : 0;
            }
            return count;
        }

    }  // namespace

    class MahjongOnlineHintsTest : public MahjongOnlineTableTestBase {
    protected:
        /// A room whose host (the viewer, seat 0) has @p mode; the other three seats are people who never act, so no computer plays and the laid position stays where it is.
        void StartHintsRoom(bool allowed, HintsMode mode = HintsMode::Hints) {
            _game->SetHintsMode(mode);
            ASSERT_NO_FATAL_FAILURE(StartRoom(kShortMatchHands, 3, allowed));
            ASSERT_EQ(_game->GetOnline().Seat(), kHostSeat);
            ASSERT_EQ(_game->GetOnline().Lobby().seats[kIdleSeat].kind, BECore::SeatKind::Human);
        }

        /// The room's game is in the position of @p model, announced as a move would (the hub sends the new view at its next tick).
        void LayOnServer(const HandModel& model) {
            ASSERT_NE(_world.Game(), nullptr);
            _world.Game()->GetMatch().Hand() = model;
            _world.Game()->AnnounceChange();
            const uint64_t version = _world.Game()->GetStateVersion();
            ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Game && Table().version == version; }));
            for (int frame = 0; frame < kLayFrames; ++frame) {
                Frame();
            }
            ASSERT_TRUE(_game->IsOnlineTable());
        }

        /// The room's game plays through @p states, the client seeing each in turn.
        void LayStates(const std::vector<HandModel>& states) {
            for (const HandModel& state : states) {
                ASSERT_NO_FATAL_FAILURE(LayOnServer(state));
            }
        }

        /// What the server tells the viewer's seat, as its own view: the oracle of every hint.
        TableView ServerView() const {
            return MakeTableView(_world.Game()->GetMatch().Hand(), kHostSeat);
        }

        std::vector<Rect> RectsOf(const Color& color) const {
            std::vector<Rect> found;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Rect && SameColor(call.color, color)) {
                    found.push_back(call.rect);
                }
            }
            return found;
        }

        size_t DangerBarCount(DangerMark grade) const {
            return RectsOf(DangerMarkColor(grade)).size();
        }

        size_t AllDangerBars() const {
            return DangerBarCount(DangerMark::Safe) + DangerBarCount(DangerMark::Risky) + DangerBarCount(DangerMark::Dangerous);
        }

        bool ButtonShown() const {
            const auto canvas = Canvas("HintsButton");
            return canvas && canvas->TakesInput() && OnScreen("HintsButton", "HintButton");
        }

        bool AdviceShown() const {
            return OnScreen("Hints", "AdviceText");
        }

        bool CardShown() const {
            return OnScreen("HintsWhy", "CoachCard");
        }

        bool WaitingRowShown() const {
            return OnScreen("Hints", "WaitPanel");
        }

        /// Whether anything of the hints is on screen: the row, the two kinds of marks, the button, the advice, the card.
        bool AnyHintShown() const {
            return WaitingRowShown() || !RectsOf(kDeclareMark).empty() || AllDangerBars() > 0 || ButtonShown() || AdviceShown() || CardShown();
        }

        void PressHint() {
            EmitAction("MahjongHintAdvice");
            Frame();
        }

        /// The player discards @p tile through the table (a click on the tile of his hand), after the Declare button when @p declare; true when the server's next view came.
        bool DiscardThroughTheTable(const char* tile, bool declare = false) {
            const uint64_t version = Table().version;
            if (declare) {
                EmitAction(SpecOf(ButtonKind::Declare).action, SpecOf(ButtonKind::Declare).value);
            }
            const int index = IndexOf(Viewer(), T(tile));
            if (index < 0) {
                ADD_FAILURE() << tile << " is not in the hand";
                return false;
            }
            ClickHandTile(index);
            return PumpUntil([&] { return Table().version > version; });
        }

        /// The number of tiles of the waiting row on screen.
        int ShownWaitCells() const {
            int shown = 0;
            for (int i = 0; i < kMaxWaitingTiles; ++i) {
                shown += OnScreen("Hints", ("WaitCell" + std::to_string(i)).c_str()) ? 1 : 0;
            }
            return shown;
        }
    };

    TEST_F(MahjongOnlineHintsTest, ARoomThatAllowsHintsShowsTheMarksAndTheAdviceOfTheViewersView) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true));
        ASSERT_TRUE(Table().hintsAllowed);
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
        ASSERT_TRUE(Lists(ActionKind::Discard));
        const TableView server = ServerView();
        const auto declaring = DeclaringDiscards(server);
        const auto dangers = DangerMarks(server);
        const eastl::vector<Tile>& held = Hand().GetHand(kHostSeat).concealed;

        EXPECT_TRUE(_game->HintsActive());
        EXPECT_TRUE(ButtonShown());
        EXPECT_FALSE(AdviceShown()) << "the advice waits for the press";

        // The marks are exactly those of the server's own view of the seat
        const int declaringTiles = Count(declaring, held);
        ASSERT_GT(declaringTiles, 0) << "the position declares";
        EXPECT_EQ(RectsOf(kDeclareMark).size(), static_cast<size_t>(declaringTiles));
        size_t grades[3] = {};
        for (const Tile tile : held) {
            for (const DangerMark grade : {DangerMark::Safe, DangerMark::Risky, DangerMark::Dangerous}) {
                grades[static_cast<size_t>(grade) - 1] += dangers[tile.Id()] == grade ? 1 : 0;
            }
        }
        EXPECT_GT(grades[0] + grades[1] + grades[2], 0u) << "seat 1 has declared";
        for (const DangerMark grade : {DangerMark::Safe, DangerMark::Risky, DangerMark::Dangerous}) {
            EXPECT_EQ(DangerBarCount(grade), grades[static_cast<size_t>(grade) - 1]) << static_cast<int>(grade);
        }

        // The advice is the one of the server's own seat view
        const std::optional<HandAction> advice = AdviceFor(server);
        ASSERT_TRUE(advice.has_value());
        const uint64_t version = _game->GetSession().Version();
        PressHint();
        ASSERT_TRUE(AdviceShown());
        const eastl::string expected = AdviceText(CoreManager::GetUIStrings(), server, *advice);
        EXPECT_EQ(Var("hintAdvice"), std::string(expected.c_str(), expected.size()));
        EXPECT_EQ(_game->GetSession().Version(), version) << "a hint never moves the table";
        EXPECT_TRUE(Lists(ActionKind::Discard)) << "and sends nothing";
    }

    TEST_F(MahjongOnlineHintsTest, TheViewOfTheDisplayCopyAgreesWithTheViewOfTheServersOwnSeat) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true));
        for (const bool viewerHasDiscarded : {false, true}) {
            ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, viewerHasDiscarded)));
            const TableView server = ServerView();
            const TableView mine = MahjongHintsComponent::ViewOf(*_game, Viewer());
            EXPECT_EQ(mine.seat, server.seat);
            EXPECT_EQ(mine.phase, server.phase);
            EXPECT_EQ(mine.currentSeat, server.currentSeat);
            EXPECT_EQ(mine.pendingTile, server.pendingTile);
            EXPECT_EQ(mine.own.concealed, server.own.concealed);
            EXPECT_EQ(mine.own.callingDeclared, server.own.callingDeclared);
            EXPECT_EQ(mine.visible, server.visible);
            EXPECT_EQ(mine.liveCount, server.liveCount);
            EXPECT_EQ(mine.legal, server.legal) << "the server's list of actions is the legal list";
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(mine.seats[seat].discardCount, server.seats[seat].discardCount) << seat;
                EXPECT_EQ(mine.seats[seat].callingDeclared, server.seats[seat].callingDeclared) << seat;
            }
            EXPECT_EQ(mine.legal.empty(), viewerHasDiscarded);
        }
    }

    TEST_F(MahjongOnlineHintsTest, TheWaitingRowShowsWhileAnotherSeatActsAndIsTheServersView) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true));
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, true)));
        ASSERT_EQ(_game->GetSession().Actor(), kIdleSeat);
        const std::vector<WaitEntry> expected = [&] {
            const eastl::vector<WaitEntry> row = WaitingTiles(ServerView());
            return std::vector<WaitEntry>(row.begin(), row.end());
        }();
        ASSERT_FALSE(expected.empty()) << "the viewer waits";
        EXPECT_EQ(ShownWaitCells(), static_cast<int>(expected.size()));
        EXPECT_TRUE(WaitingRowShown());
        for (size_t i = 0; i < expected.size(); ++i) {
            EXPECT_EQ(Var(("hintCount" + std::to_string(i)).c_str()), std::to_string(expected[i].unseen)) << i;
        }
        EXPECT_FALSE(ButtonShown()) << "it is not the viewer's turn";
        EXPECT_TRUE(RectsOf(kDeclareMark).empty());
    }

    TEST_F(MahjongOnlineHintsTest, ARoomThatDoesNotAllowHintsShowsNoneOfThemEvenInTheModeCoach) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(false, HintsMode::Coach));
        EXPECT_FALSE(Table().hintsAllowed);
        for (const bool viewerHasDiscarded : {false, true}) {
            ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, viewerHasDiscarded)));
            EXPECT_EQ(_game->GetHintsMode(), HintsMode::Coach) << "the player's choice is kept";
            EXPECT_EQ(_game->EffectiveHintsMode(), HintsMode::Off);
            EXPECT_FALSE(_game->HintsActive());
            EXPECT_FALSE(_game->CoachActive());
            EXPECT_FALSE(AnyHintShown()) << (viewerHasDiscarded ? "seat 1 acts" : "the viewer acts");
            EXPECT_EQ(ShownWaitCells(), 0);
            EmitAction("MahjongHintAdvice");
            Frame();
            EXPECT_FALSE(AnyHintShown()) << "a press of a button that is not there changes nothing";
        }
    }

    TEST_F(MahjongOnlineHintsTest, TheModeOffShowsNoneInARoomThatAllowsThemAndTheKeyFollowsTheMode) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true, HintsMode::Off));
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
        EXPECT_TRUE(Table().hintsAllowed);
        EXPECT_FALSE(_game->HintsActive());
        EXPECT_FALSE(AnyHintShown());

        _game->SetHintsMode(HintsMode::Hints);
        Frame();
        EXPECT_TRUE(_game->HintsActive());
        EXPECT_TRUE(ButtonShown());
        EXPECT_FALSE(RectsOf(kDeclareMark).empty());

        _game->SetHintsMode(HintsMode::Off);
        Frame();
        EXPECT_FALSE(AnyHintShown()) << "the marks and the button are dropped with the mode";
    }

    TEST_F(MahjongOnlineHintsTest, ThePlayerCannotTurnTheHintsOnAgainstTheRoom) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(false, HintsMode::Off));
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
        _game->SetHintsMode(HintsMode::Coach);
        EXPECT_FALSE(_game->HintsActive());
        EXPECT_FALSE(_game->CoachActive());

        _game->OpenOptions();
        EmitAction("MahjongSetHints", "coach");
        Frame();
        EXPECT_EQ(_game->GetHintsMode(), HintsMode::Coach) << "the page keeps the choice";
        EXPECT_FALSE(_game->HintsActive());
        _game->CloseOptions();
        Frame();
        EXPECT_FALSE(AnyHintShown());
        EXPECT_FALSE(_game->GetCoach().Enabled() && _game->CoachActive());
    }

    TEST_F(MahjongOnlineHintsTest, WhenTheTableEndsTheLocalGamesHintsAreAsTheyWere) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(false, HintsMode::Hints));
        ASSERT_TRUE(_game->IsOnlineTable());
        EXPECT_FALSE(_game->HintsActive());
        EXPECT_TRUE(_game->HintsOn()) << "the own choice";

        ASSERT_TRUE(_game->GetOnline().Leave());
        ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
        EXPECT_TRUE(_game->HintsActive());
        EXPECT_EQ(_game->EffectiveHintsMode(), HintsMode::Hints);
        _game->SetHintsMode(HintsMode::Coach);
        EXPECT_TRUE(_game->CoachActive());
        EXPECT_EQ(_game->CoachActive(), _game->CoachOn());
    }

    TEST_F(MahjongOnlineHintsTest, NothingTheViewerCannotSeeReachesTheHints) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true));
        struct Shown {
            size_t declare = 0;
            size_t dangers = 0;
            std::string advice;
            std::vector<uint8_t> visible;
        };
        Shown shown[2];
        eastl::vector<Tile> hidden[2];
        const char* const seat2[] = {kSeat2Hand, kOtherSeat2Hand};
        const char* const seat3[] = {kSeat3Hand, kOtherSeat3Hand};
        for (int variant = 0; variant < 2; ++variant) {
            ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(seat2[variant], seat3[variant], false)));
            hidden[variant] = _world.Game()->GetMatch().Hand().GetHand(kSeat2).concealed;
            if (variant == 0) {
                PressHint();  // a switch that stays on for the next table
            } else {
                Frame();
            }
            const TableView mine = MahjongHintsComponent::ViewOf(*_game, Viewer());
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(mine.seats[seat].openMelds.size(), ServerView().seats[seat].openMelds.size());
            }
            shown[variant].declare = RectsOf(kDeclareMark).size();
            shown[variant].dangers = AllDangerBars();
            shown[variant].advice = Var("hintAdvice");
            shown[variant].visible.assign(mine.visible.begin(), mine.visible.end());
            EXPECT_EQ(mine.own.concealed, ServerView().own.concealed);
            EXPECT_EQ(mine.visible, ServerView().visible);
            // The display copy holds no real tile of another hand: they are counts
            for (Seat seat = 1; seat < kSeatCount; ++seat) {
                for (const Tile tile : Hand().GetHand(seat).concealed) {
                    EXPECT_FALSE(tile.IsPlayable()) << "seat " << seat << " of the display hand";
                }
            }
        }
        EXPECT_NE(hidden[0], hidden[1]) << "the hidden hands of the two positions differ";
        EXPECT_EQ(shown[0].declare, shown[1].declare);
        EXPECT_EQ(shown[0].dangers, shown[1].dangers);
        EXPECT_EQ(shown[0].advice, shown[1].advice);
        EXPECT_EQ(shown[0].visible, shown[1].visible) << "the other hands are not in what the viewer sees";
        EXPECT_FALSE(shown[0].advice.empty());
    }

    // ----- the line of the room -----

    TEST_F(MahjongOnlineHintsTest, TheTableOfARoomSaysInBothLanguagesWhetherHintsAreAllowed) {
        struct Case {
            bool allowed;
            HintsMode mode;
            const char* key;
        };
        const Case cases[] = {{true, HintsMode::Hints, "online.table.hintsAllowed"}, {true, HintsMode::Off, "online.table.hintsAllowedOff"}, {false, HintsMode::Coach, "online.table.hintsOff"}};
        for (const Case& test : cases) {
            ASSERT_NO_FATAL_FAILURE(StartHintsRoom(test.allowed, test.mode));
            ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
            for (const char* language : {"en", "ru"}) {
                ASSERT_TRUE(_game->SetLanguage(PoolString::Intern(language)));
                Frame();
                const eastl::string expected = Tr(CoreManager::GetUIStrings(), test.key);
                EXPECT_TRUE(OnScreen("Hints", "RoomHintsText")) << test.key << " " << language;
                EXPECT_EQ(Var("roomHintsText"), std::string(expected.c_str(), expected.size())) << test.key << " " << language;
                EXPECT_FALSE(expected.empty());
            }
            ASSERT_TRUE(_game->SetLanguage("en"_intern));
            ASSERT_TRUE(_game->GetOnline().Leave());
            ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
            _others.clear();
        }
    }

    TEST_F(MahjongOnlineHintsTest, TheLineOfARoomFollowsThePlayersModeAndGivesWayToTheAdviceAndTheCard) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true, HintsMode::Off));
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
        const auto say = [&](const char* key) {
            const eastl::string expected = Tr(CoreManager::GetUIStrings(), key);
            return OnScreen("Hints", "RoomHintsText") && Var("roomHintsText") == std::string(expected.c_str(), expected.size());
        };
        EXPECT_TRUE(say("online.table.hintsAllowedOff")) << "it says where to switch them on";

        _game->SetHintsMode(HintsMode::Hints);
        Frame();
        EXPECT_TRUE(say("online.table.hintsAllowed"));
        PressHint();
        ASSERT_TRUE(AdviceShown());
        EXPECT_FALSE(OnScreen("Hints", "RoomHintsText")) << "the sentence takes the place";
        EXPECT_EQ(Var("roomHintsText"), "") << "nothing of the word stays in the variable";
        PressHint();
        EXPECT_FALSE(AdviceShown());
        EXPECT_TRUE(say("online.table.hintsAllowed"));

        _game->SetHintsMode(HintsMode::Coach);
        PressHint();
        ASSERT_TRUE(CardShown());
        EXPECT_FALSE(OnScreen("Hints", "RoomHintsText")) << "the card takes the place";
        PressHint();
        EXPECT_FALSE(CardShown());
        EXPECT_TRUE(say("online.table.hintsAllowed"));

        _game->OpenOptions();
        Frame();
        EXPECT_FALSE(OnScreen("Hints", "RoomHintsText")) << "no line under a page";
        _game->CloseOptions();
        Frame();
        EXPECT_TRUE(say("online.table.hintsAllowed"));

        ASSERT_TRUE(_game->GetOnline().Leave());
        ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
        Frame();
        EXPECT_FALSE(OnScreen("Hints", "RoomHintsText")) << "the line is the room's";
        EXPECT_EQ(Var("roomHintsText"), "");
    }

    TEST_F(MahjongOnlineHintsTest, TheLineOfARoomFitsItsPlaceInEveryLanguageOnTheWholeMatrix) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true, HintsMode::Hints));
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const UIStrings& strings = CoreManager::GetUIStrings();
            std::vector<eastl::string> lines;
            for (const char* key : {"online.table.hintsAllowed", "online.table.hintsAllowedOff", "online.table.hintsOff"}) {
                lines.push_back(Tr(strings, key));
            }
            CoreManager::GetUIVars().Set("roomHintsText"_intern, eastl::string_view(LongestText(lines)));
            Draw();
            ASSERT_TRUE(OnScreen("Hints", "RoomHintsText"));
            ExpectPassesTheMatrix("Hints", EngineTouchMinimum());
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("Hints"), 0.0f);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
    }

    TEST_F(MahjongOnlineHintsTest, TheLocalGameHasNoLineOfARoom) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->SetHintsMode(HintsMode::Hints);
        _game->CloseSetup();
        _game->GetSession().RunComputers();
        Draw();
        Draw();
        EXPECT_FALSE(OnScreen("Hints", "RoomHintsText"));
        EXPECT_EQ(Var("roomHintsText"), "");
    }

    // ----- the coach of the room -----

    TEST_F(MahjongOnlineHintsTest, ADifferingDiscardInARoomLeavesTheNoteOfTheViewersViewAndTheCardAndTheTally) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true, HintsMode::Coach));
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
        ASSERT_TRUE(_game->CoachActive());
        const CoachLedger& coach = _game->GetCoach();
        ASSERT_TRUE(coach.Enabled());
        const TableView before = ServerView();
        const std::optional<HandAction> advice = AdviceFor(before);
        ASSERT_TRUE(advice.has_value());
        const HandAction taken = HandAction::MakeDiscard(T("1s"));
        ASSERT_NE(*advice, taken) << "the move differs from the computer's choice";
        const std::optional<CoachNote> expected = JudgeMove(before, taken);
        ASSERT_TRUE(expected.has_value());

        ASSERT_TRUE(DiscardThroughTheTable("1s"));
        Frame();

        ASSERT_TRUE(coach.Note().has_value());
        EXPECT_EQ(*coach.Note(), *expected) << "judged on the viewer's view, as the server's own seat view judges it";
        EXPECT_EQ(coach.Note()->advised, *advice);
        EXPECT_EQ(coach.Note()->taken, taken);
        EXPECT_EQ(coach.HandTally(), (CoachTally{0, 1}));
        EXPECT_EQ(coach.MatchTally(), (CoachTally{0, 1}));
        ASSERT_TRUE(CardShown());
        const UIStrings& strings = CoreManager::GetUIStrings();
        const eastl::string tally = CardTallyText(strings, coach.HandTally(), coach.MatchTally());
        EXPECT_EQ(CardVars(), CardTexts(MakeNoteCard(strings, *expected, tally)));
    }

    TEST_F(MahjongOnlineHintsTest, AnAgreeingDiscardInARoomOnlyCounts) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true, HintsMode::Coach));
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
        const CoachLedger& coach = _game->GetCoach();
        const std::optional<HandAction> advice = AdviceFor(ServerView());
        ASSERT_EQ(advice, std::optional<HandAction>(HandAction::MakeDiscard(T("Ew"), true)));

        ASSERT_TRUE(DiscardThroughTheTable("Ew", true));
        Frame();

        EXPECT_FALSE(coach.Note().has_value());
        EXPECT_EQ(coach.HandTally(), (CoachTally{1, 1}));
        EXPECT_EQ(coach.MatchTally(), (CoachTally{1, 1}));
        EXPECT_FALSE(CardShown());
    }

    TEST_F(MahjongOnlineHintsTest, NothingIsJudgedWithTheHintsNotAllowedOrTheModeHints) {
        struct Case {
            bool allowed;
            HintsMode mode;
        };
        for (const Case& test : {Case{false, HintsMode::Coach}, Case{true, HintsMode::Hints}, Case{true, HintsMode::Off}}) {
            ASSERT_NO_FATAL_FAILURE(StartHintsRoom(test.allowed, test.mode));
            ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
            const CoachLedger& coach = _game->GetCoach();
            EXPECT_FALSE(coach.Enabled());
            ASSERT_TRUE(DiscardThroughTheTable("1s")) << test.allowed << " " << static_cast<int>(test.mode);
            Frame();
            EXPECT_FALSE(coach.Note().has_value());
            EXPECT_EQ(coach.HandTally(), CoachTally{});
            EXPECT_EQ(coach.MatchTally(), CoachTally{});
            EXPECT_FALSE(CardShown());
            ASSERT_TRUE(_game->GetOnline().Leave());
            ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
            _others.clear();
        }
    }

    TEST_F(MahjongOnlineHintsTest, AClickWhileTheServerListsNothingForTheViewerRecordsNothing) {
        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true, HintsMode::Coach));
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, true)));
        ASSERT_FALSE(Lists(ActionKind::Discard));
        const uint64_t version = Table().version;
        ClickHandTile(IndexOf(Viewer(), T("1s")));
        Frame();
        EXPECT_EQ(Table().version, version);
        EXPECT_EQ(_game->GetCoach().MatchTally(), CoachTally{});
        EXPECT_FALSE(_game->GetCoach().Note().has_value());
    }

    TEST_F(MahjongOnlineHintsTest, TheLedgerOfTheRoomAndTheLedgerOfTheLocalGameNeverMix) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->SetHintsMode(HintsMode::Coach);
        _game->CloseSetup();
        _game->GetSession().RunComputers();
        ASSERT_TRUE(_game->GetSession().ActorIsPerson());
        ASSERT_TRUE(_game->GetSession().PerformPerson(Hand().LegalActions(_game->GetSession().Actor()).front()).has_value());
        const CoachTally local = _game->GetCoach().MatchTally();
        ASSERT_EQ(local.moves, 1);

        ASSERT_NO_FATAL_FAILURE(StartHintsRoom(true, HintsMode::Coach));
        ASSERT_TRUE(_game->IsOnlineTable());
        EXPECT_EQ(_game->GetCoach().MatchTally(), CoachTally{}) << "the room starts with a ledger of its own";
        ASSERT_NO_FATAL_FAILURE(LayStates(PlayedStates(kSeat2Hand, kSeat3Hand, false)));
        ASSERT_TRUE(DiscardThroughTheTable("1s"));
        Frame();
        EXPECT_EQ(_game->GetCoach().MatchTally(), (CoachTally{0, 1}));

        ASSERT_TRUE(_game->GetOnline().Leave());
        ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
        EXPECT_EQ(_game->GetCoach().MatchTally(), local) << "the local game's tally is as it was";
        EXPECT_TRUE(_game->GetCoach().Enabled());
    }

    TEST_F(MahjongOnlineHintsTest, TheMatchOverPlateOfARoomShowsTheTallyOfTheRoomsLedger) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(StartRoom(kShortMatchHands, 0, true));  // computers play the other seats
        CoachLedger& coach = _game->GetCoach();
        ASSERT_TRUE(coach.Enabled());
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Discard); }, false));
        // A move judged as the table component does it (the matches of this size are played by the first listed action, which feeds nothing)
        const TableView view = MahjongHintsComponent::ViewOf(*_game, Viewer());
        ASSERT_FALSE(view.legal.empty());
        coach.OnPersonMove(view, view.legal.front(), _game->CoachContext());
        const CoachTally tally = coach.MatchTally();
        ASSERT_EQ(tally.moves, 1);

        constexpr int kMostHands = 40;
        constexpr int kMatchFrames = 40000;
        int hands = 0;
        while (_game->GetSession().GetPhase() != SessionPhase::MatchOver && hands < kMostHands) {
            ASSERT_TRUE(PlayUntil([&] { return _game->GetSession().GetPhase() != SessionPhase::Playing; }, true, kMatchFrames)) << "hand " << hands;
            if (_game->GetSession().GetPhase() == SessionPhase::HandOver) {
                ++hands;
                _game->GetOnline().Act(0);
                for (const std::unique_ptr<HubPlayer>& other : _others) {
                    other->ActFirst();
                }
                ASSERT_TRUE(PlayUntil([&] { return _game->GetSession().GetPhase() != SessionPhase::HandOver; }, false, kMatchFrames));
            }
        }
        ASSERT_EQ(_game->GetSession().GetPhase(), SessionPhase::MatchOver) << "after " << hands << " hands";
        Frame();
        Frame();
        EXPECT_EQ(coach.MatchTally(), tally) << "the tally survives the hands of the match";
        ASSERT_TRUE(OnScreen("MatchOver", "MatchCoachRow"));
        const eastl::string expected = FormatCoachTally(CoreManager::GetUIStrings(), CoachTally{}, tally);
        EXPECT_STREQ(Var("matchOverCoach").c_str(), expected.c_str());
    }

}  // namespace BECore::Mahjong::Tests
