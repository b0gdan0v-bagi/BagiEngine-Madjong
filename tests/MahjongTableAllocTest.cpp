#include "ComputerTestSupport.h"
#include "MahjongOnlineTableTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"

#include <BECore/Clipboard/FakeClipboard.h>
#include <BECore/Profiler/Alloc/AllocTracker.h>
#include <Mahjong/Components/MahjongHintsComponent.h>
#include <Mahjong/Components/MahjongOnlineComponent.h>
#include <Mahjong/Table/Coach.h>
#include <Mahjong/Table/ScoreText.h>

// The table drawn again with nothing changed allocates nothing (the entry steady-frame-allocs): the layout, its input and the snapshots
// keep the capacity of their lists between draws. Only a build that counts allocations can see it (`macos-profile`).
#if BE_PROFILER_ALLOC

namespace BECore::Mahjong::Tests {

    namespace {

        /// Computer steps played before the draws: the pile and the melds have tiles, so their lists are not empty
        constexpr int kStepsIntoTheHand = 24;

    }  // namespace

    class MahjongTableAllocTest : public MahjongTableTestBase {};

    TEST_F(MahjongTableAllocTest, ATableDrawnAgainUnchangedAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        for (int step = 0; step < kStepsIntoTheHand && _game->GetSession().GetPhase() == SessionPhase::Playing; ++step) {
            ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
        }
        ASSERT_FALSE(_game->GetMatch().Hand().Discards().empty());
        Draw();
        Draw();  // both snapshots have been filled once: from here on their lists have the capacity they need

        _renderer->ClearCalls();
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        _table->OnDraw();
        const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;

        EXPECT_EQ(allocations, 0u);
        EXPECT_FALSE(_renderer->calls.empty());
    }

    /// The regions reach every canvas of the table; once they are there, a frame in each shape, the phone profile among them, allocates
    /// nothing (`Set` of an equal rectangle, and the comparison with the published list, keep the heap out).
    TEST_F(MahjongTableAllocTest, TheTablesRegionsOnTheCanvasesAllocateNothingInASteadyFrameInEveryShape) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        const MatrixWindow windows[] = {DesignWindowOf(TableShape::Landscape), DesignWindowOf(TableShape::Portrait), DesignWindowOf(TableShape::Narrow), DesignWindowOf(TableShape::Wide),
                                        MakePhoneWindow(915.0f, 330.0f, 2.6f)};
        for (const MatrixWindow& window : windows) {
            SetWindow(*_renderer, window);
            Draw();
            Draw();
            Draw();
            ASSERT_FALSE(Canvas("Hud")->GetRegions().IsEmpty()) << window.label.c_str();

            const uint64_t before = AllocTracker::GetThreadCounters().count;
            Draw();
            const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;
            EXPECT_EQ(allocations, 0u) << window.label.c_str();
        }
    }

    TEST_F(MahjongTableAllocTest, ThePointerAtRestOnATileAllocatesNothing) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        for (int step = 0; step < kStepsIntoTheHand && _game->GetSession().GetPhase() == SessionPhase::Playing; ++step) {
            ASSERT_TRUE(_game->GetSession().StepComputer().has_value());
        }
        Draw();
        const auto discard = std::find_if(_table->GetLayout().slots.begin(), _table->GetLayout().slots.end(), [](const TileSlot& slot) { return slot.role == SlotRole::DiscardTile; });
        ASSERT_NE(discard, _table->GetLayout().slots.end());
        HoverAt(discard->rect.x + discard->rect.w * 0.5f, discard->rect.y + discard->rect.h * 0.5f);
        Draw();
        Draw();  // the name is worded and the plate placed; from here on nothing changes
        ASSERT_TRUE(OnScreen("TileHover", "TileHoverPlate"));

        _renderer->ClearCalls();
        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        _table->OnDraw();
        const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;

        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
    }

    /// The whole frame (every node of the scene, not only the table) on a person's turn, with the card of the advice up: the buttons
    /// on offer are made again only when the session moved (`LegalActions` allocates)
    TEST_F(MahjongTableAllocTest, APersonsTurnWithTheCardOfTheAdviceUpAllocatesNothingInTheWholeFrame) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        for (int step = 0; step < kStepsIntoTheHand && _game->GetSession().GetPhase() == SessionPhase::Playing; ++step) {
            if (!_game->GetSession().StepComputer().has_value()) {
                break;  // the person is to act
            }
        }
        ASSERT_EQ(_game->GetSeats()[static_cast<size_t>(_game->GetSession().Actor())], SeatKind::Person);
        _game->SetHintsMode(HintsMode::Coach);  // the switch on in Coach puts the advice on the card
        Draw();
        EmitAction("MahjongHintAdvice");
        Draw();
        Draw();
        ASSERT_TRUE(OnScreen("HintsWhy", "CoachCard"));

        const uint64_t before = AllocTracker::GetThreadCounters().count;
        Draw();
        const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;

        EXPECT_EQ(allocations, 0u);
    }

    /// The coach's note up on the person's next turn (the computers moved after the person's differing discard): the words are published
    /// once, and the whole frame of every node after that allocates nothing and moves no variable.
    TEST_F(MahjongTableAllocTest, APersonsTurnWithTheCoachsNoteUpAllocatesNothingInTheWholeFrame) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        // 123s 456s 789m 5p 5p Rd Ew with a drawn north wind: a discard of 5p leaves a worse hand than the computer's choice
        Lay(MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew", "Nw")));
        Draw();
        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeDiscard(T("5p"), false)).has_value());
        ASSERT_TRUE(StepUntil([&] { return _game->GetSession().ActorIsPerson(); }));
        Draw();
        Draw();
        ASSERT_TRUE(_game->GetCoach().Note().has_value());
        ASSERT_TRUE(OnScreen("HintsWhy", "CoachCard"));

        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        Draw();
        const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;

        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
        EXPECT_TRUE(OnScreen("HintsWhy", "CoachCard"));
    }

    /// The score table with the tally row up: after the words are published, a frame allocates nothing.
    TEST_F(MahjongTableAllocTest, TheScoreTableWithTheTallyRowUpAllocatesNothingInTheWholeFrame) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        TableSession& session = _game->GetSession();
        for (int guard = 0; guard < kActionLimit && session.GetPhase() == SessionPhase::Playing; ++guard) {
            session.RunComputers();
            if (session.GetPhase() != SessionPhase::Playing) {
                break;
            }
            const TableView view = MakeTableView(Hand(), session.Actor());
            ASSERT_FALSE(view.legal.empty());
            ASSERT_TRUE(session.PerformPerson(view.legal.front()).has_value());
        }
        ASSERT_EQ(session.GetPhase(), SessionPhase::HandOver);
        ASSERT_GT(_game->GetCoach().MatchTally().moves, 0);
        Draw();
        Draw();
        ASSERT_TRUE(OnScreen("HandOver", "HandCoachRow"));
        ASSERT_FALSE(Var("handOverCoach").empty());

        const uint64_t version = CoreManager::GetUIVars().GetVersion();
        const uint64_t before = AllocTracker::GetThreadCounters().count;
        Draw();
        const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;

        EXPECT_EQ(allocations, 0u);
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
        EXPECT_TRUE(OnScreen("HandOver", "HandCoachRow"));
    }

    /// The table of a room: the display pair is made again only when the client's version moved (`SyncOnlineTable`), the buttons and the
    /// keeps-waiting bars only when the display session moved, the words only when the session, the language or the client moved. A
    /// frame is the draw of every node; the update of the scene (the client's own work) is not part of it.
    class MahjongOnlineTableAllocTest : public MahjongOnlineTableTestBase {
    protected:
        /// Settles with three draws, then counts the fourth, in each language: no allocation and no `UIVars` change.
        void ExpectSteadyFrameInBothLanguages() {
            for (const char* language : {"en", "ru"}) {
                ASSERT_TRUE(_game->SetLanguage(PoolString::Intern(language)));
                Draw();
                Draw();
                Draw();  // the words of the language are published; from here on nothing changes
                const uint64_t version = CoreManager::GetUIVars().GetVersion();
                const uint64_t before = AllocTracker::GetThreadCounters().count;
                Draw();
                const uint64_t allocations = AllocTracker::GetThreadCounters().count - before;
                EXPECT_EQ(allocations, 0u) << language;
                EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version) << language;
            }
            ASSERT_TRUE(_game->SetLanguage("en"_intern));
        }
    };

    TEST_F(MahjongOnlineTableAllocTest, APersonsTurnInARoomAllocatesNothingInTheWholeFrame) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Discard) && Hand().GetPhase() == Phase::Discard && _game->GetSession().Actor() == Viewer(); }, false));
        ASSERT_TRUE(_game->IsOnlineTable());
        ExpectSteadyFrameInBothLanguages();
    }

    // The room that allows hints (the host's choice): the player's mode decides what is up at his turn.
    TEST_F(MahjongOnlineTableAllocTest, APersonsTurnInARoomWithHintsAndTheLineOfTheRoomAllocatesNothingInTheWholeFrame) {
        _game->SetHintsMode(HintsMode::Hints);
        ASSERT_NO_FATAL_FAILURE(StartRoom(kShortMatchHands, 3, true));
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Discard) && Hand().GetPhase() == Phase::Discard && _game->GetSession().Actor() == Viewer(); }));
        Draw();
        ASSERT_TRUE(_game->HintsActive());
        ASSERT_TRUE(OnScreen("Hints", "RoomHintsText"));  // the waiting row, the marks and the line
        ExpectSteadyFrameInBothLanguages();

        EmitAction("MahjongHintAdvice");  // the sentence takes the place of the line
        Draw();
        ASSERT_TRUE(OnScreen("Hints", "AdviceText"));
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineTableAllocTest, TheCoachsAdviceCardInARoomAllocatesNothingInTheWholeFrame) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(StartRoom(kShortMatchHands, 3, true));
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Discard) && Hand().GetPhase() == Phase::Discard && _game->GetSession().Actor() == Viewer(); }));
        ASSERT_TRUE(_game->CoachActive());
        EmitAction("MahjongHintAdvice");
        Draw();
        ASSERT_TRUE(OnScreen("HintsWhy", "CoachCard"));
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineTableAllocTest, TheCoachsNoteCardInARoomAllocatesNothingInTheWholeFrame) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(StartRoom(kShortMatchHands, 3, true));
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Discard) && Hand().GetPhase() == Phase::Discard && _game->GetSession().Actor() == Viewer(); }));
        ASSERT_TRUE(_game->CoachActive());
        // A move that differs from the computer's choice, judged as the table component judges the player's own
        CoachLedger& coach = _game->GetCoach();
        const TableView view = MahjongHintsComponent::ViewOf(*_game, Viewer());
        for (const HandAction& action : view.legal) {
            coach.OnPersonMove(view, action, _game->CoachContext());
            if (coach.Note().has_value()) {
                break;
            }
        }
        ASSERT_TRUE(coach.Note().has_value());
        Draw();
        ASSERT_TRUE(OnScreen("HintsWhy", "CoachCard"));
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineTableAllocTest, AClaimWindowInARoomAllocatesNothingInTheWholeFrame) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Pass) && Hand().GetPhase() == Phase::Claim; }, false));
        Draw();
        ASSERT_TRUE(_table->GetButtons()[ButtonKind::Pass].shown);
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineTableAllocTest, TheHandOverPlateAndTheReadyCheckOfARoomAllocateNothingInTheWholeFrame) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return _game->GetSession().GetPhase() == SessionPhase::HandOver; }));
        Frame();
        ASSERT_TRUE(OnScreen("HandOverAction", "NextHand"));
        ExpectSteadyFrameInBothLanguages();

        // The own flag set: the plate says how many have read it
        ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
        const Seat viewer = Viewer();
        ASSERT_TRUE(PumpUntil([&] { return Table().ready[static_cast<size_t>(viewer)]; }));
        Frame();
        ASSERT_TRUE(OnScreen("HandOver", "HandCoachRow"));
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineTableAllocTest, TheFrozenTableWhileTheConnectionIsDownAllocatesNothingInTheWholeFrame) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Hand().Discards().size() >= 4 && _game->GetSession().GetPhase() == SessionPhase::Playing; }));
        _world.SetRefusing(true);
        _world.Sever(_links.Last());
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Reconnecting; }));
        Frame();
        ASSERT_TRUE(_game->IsOnlineTable());
        ASSERT_TRUE(OnScreen("Hud", "MoveLine"));
        ExpectSteadyFrameInBothLanguages();
    }

    /// The online screens (`Components/MahjongOnlineComponent`: the pages `Menu`, `Host`, `Join`, `Entry` and `Lobby` of the panel over `Setup`): a page drawn again with nothing
    /// changed makes no string and no list, in either language. The pages are reached as a player reaches them; the lobbies are those of a room on the in-memory hub.
    class MahjongOnlineScreensAllocTest : public MahjongOnlineTableAllocTest {
    protected:
        /// `Setup` over a match, then the panel opened by its button.
        void OpenPanel() {
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
            _game->OpenSetup();
            Draw();
            ASSERT_TRUE(ClickElement("Setup", "SetupOnline"));
            Draw();
            ASSERT_TRUE(_game->IsOnlineOpen());
        }

        /// The panel shows the lobby of the room the game's client is in.
        void ExpectTheLobby(const char* element) {
            Frame();
            Frame();
            ASSERT_EQ(MahjongOnlineComponent::GetActive()->Screens().Page(), OnlinePage::Lobby);
            ASSERT_TRUE(OnScreen("Online", element)) << element;
        }

        /// Another person hosts a room on the hub; the code of it.
        eastl::string HostElsewhere() {
            _others.push_back(std::make_unique<HubPlayer>(_world));
            HubPlayer& host = *_others.back();
            EXPECT_TRUE(host.Client().Host(kUrl, "Hanna", OnlineRoomOptions{kShortMatchHands, kMinLevel}));
            EXPECT_TRUE(PumpUntil([&] { return host.Client().Stage() == OnlineStage::Lobby; }));
            return host.Client().Room();
        }
    };

    TEST_F(MahjongOnlineScreensAllocTest, TheMenuTheHostFormAndTheJoinFormAllocateNothingInTheWholeFrame) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ASSERT_TRUE(OnScreen("Online", "OnlineMenuHost"));
        ExpectSteadyFrameInBothLanguages();

        EmitAction("MahjongOnlineHost");
        Draw();
        ASSERT_TRUE(OnScreen("Online", "OnlineCreate"));
        ExpectSteadyFrameInBothLanguages();

        EmitAction("MahjongOnlineClose");
        Draw();
        ASSERT_TRUE(ClickElement("Setup", "SetupOnline"));
        Draw();
        EmitAction("MahjongOnlineJoin");
        Draw();
        ASSERT_TRUE(OnScreen("Online", "OnlineEnter"));
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineScreensAllocTest, TheNamePadAndTheCodePadAllocateNothingInTheWholeFrame) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        EmitAction("MahjongOnlineJoin");
        Draw();
        ASSERT_TRUE(ClickElement("Online", "OnlineName"));
        Draw();
        ASSERT_EQ(MahjongOnlineComponent::GetActive()->Screens().Page(), OnlinePage::Entry);
        ASSERT_TRUE(OnScreen("OnlineEntry", "PadKey0"));
        ExpectSteadyFrameInBothLanguages();

        EmitAction("MahjongOnlineDone");
        Draw();
        ASSERT_TRUE(ClickElement("Online", "OnlineCode"));
        Draw();
        ASSERT_EQ(MahjongOnlineComponent::GetActive()->Screens().Page(), OnlinePage::Entry);
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineScreensAllocTest, TheWaitingRoomOfTheHostAndOfAGuestAllocateNothingInTheWholeFrame) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        EmitAction("MahjongOnlineHost");
        Draw();
        ASSERT_TRUE(ClickElement("Online", "OnlineCreate"));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Lobby; }));
        ASSERT_NO_FATAL_FAILURE(ExpectTheLobby("OnlineStart"));
        ExpectSteadyFrameInBothLanguages();

        // A guest: the client leaves and joins the room of another person
        ASSERT_TRUE(_game->GetOnline().Leave());
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Idle; }));
        const eastl::string code = HostElsewhere();
        ASSERT_TRUE(_game->GetOnline().Join(kUrl, code, "Cy"));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Lobby; }));
        ASSERT_NO_FATAL_FAILURE(ExpectTheLobby("OnlineWaiting"));
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineScreensAllocTest, TheLobbyWhileTheConnectionIsDownAllocatesNothingInTheWholeFrame) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        EmitAction("MahjongOnlineHost");
        Draw();
        ASSERT_TRUE(ClickElement("Online", "OnlineCreate"));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Lobby; }));
        ASSERT_NO_FATAL_FAILURE(ExpectTheLobby("OnlineStart"));
        _world.SetRefusing(true);
        _world.Sever(_links.Last());
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Reconnecting; }));
        Frame();
        ASSERT_NO_FATAL_FAILURE(ExpectTheLobby("OnlineLeave"));
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineScreensAllocTest, TheLobbyWithTheCopyButtonSayingCopiedAllocatesNothingInTheWholeFrame) {
        const IntrusivePtr<FakeClipboard> clipboard = New<FakeClipboard>();
        const ScopedClipboardBackend scope(clipboard);
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        EmitAction("MahjongOnlineHost");
        Draw();
        ASSERT_TRUE(ClickElement("Online", "OnlineCreate"));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Lobby; }));
        ASSERT_NO_FATAL_FAILURE(ExpectTheLobby("OnlineCopy"));
        ExpectSteadyFrameInBothLanguages();

        ASSERT_TRUE(ClickElement("Online", "OnlineCopy"));
        Frame();
        ASSERT_EQ(MahjongOnlineComponent::GetActive()->Screens().CopyState(), OnlineCopyState::Copied);
        ExpectSteadyFrameInBothLanguages();

        clipboard->SetRefuse(true);
        ASSERT_TRUE(ClickElement("Online", "OnlineCopy"));
        Frame();
        ASSERT_EQ(MahjongOnlineComponent::GetActive()->Screens().CopyState(), OnlineCopyState::Failed);
        ExpectSteadyFrameInBothLanguages();
    }

    TEST_F(MahjongOnlineScreensAllocTest, TheCodePadWithAPasteNoteUpAllocatesNothingInTheWholeFrame) {
        const IntrusivePtr<FakeClipboard> clipboard = New<FakeClipboard>("no code in here");
        const ScopedClipboardBackend scope(clipboard);
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        EmitAction("MahjongOnlineJoin");
        Draw();
        ASSERT_TRUE(ClickElement("Online", "OnlineCode"));
        Draw();
        ASSERT_EQ(MahjongOnlineComponent::GetActive()->Screens().Page(), OnlinePage::Entry);
        ASSERT_TRUE(OnScreen("OnlineEntry", "EntryPaste"));
        ExpectSteadyFrameInBothLanguages();

        // A text with no code from the paste key: the note stands under the value
        ASSERT_TRUE(ClickElement("OnlineEntry", "EntryPaste"));
        CoreManager::GetClipboard().Update();
        Frame();
        ASSERT_EQ(MahjongOnlineComponent::GetActive()->Screens().PasteNote(), OnlinePasteNote::NotACode);
        ASSERT_TRUE(OnScreen("OnlineEntry", "EntryNote"));
        ExpectSteadyFrameInBothLanguages();

        // The other two notes
        clipboard->SetContent("");
        ASSERT_TRUE(ClickElement("OnlineEntry", "EntryPaste"));
        CoreManager::GetClipboard().Update();
        Frame();
        ASSERT_EQ(MahjongOnlineComponent::GetActive()->Screens().PasteNote(), OnlinePasteNote::Empty);
        ExpectSteadyFrameInBothLanguages();
        clipboard->SetRefuse(true);
        ASSERT_TRUE(ClickElement("OnlineEntry", "EntryPaste"));
        CoreManager::GetClipboard().Update();
        Frame();
        ASSERT_EQ(MahjongOnlineComponent::GetActive()->Screens().PasteNote(), OnlinePasteNote::Refused);
        ExpectSteadyFrameInBothLanguages();
    }

}  // namespace BECore::Mahjong::Tests

#endif
