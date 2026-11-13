#include "MahjongButtonSupport.h"
#include "MahjongOnlineTableTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/UI/UIInputClicker.h>
#include <Common/TempPath.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Mahjong/Save/MatchSave.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/TileAtlas.h>
#include <Mahjong/Table/TileHover.h>
#include <Rooms/RoomProtocol.h>
#include <memory>
#include <thread>

/**
 * The table of a started online game in the shipped scene: the game's client over the in-memory hub with three other people
 * (`HubPlayer`), the display pair that `MahjongGame` shows while the client has a table (`IsOnlineTable()`, `GetMatch()`), and what
 * must not notice it (the local match, the save slot, the control API). The unit under test is the scene's glue; the translation of a
 * view is `MahjongOnlineMirror`.
 */
namespace BECore::Mahjong::Tests {

    class MahjongOnlineTableSceneTest : public MahjongOnlineTableTestBase {
    protected:
        std::string Text(const char* node, const char* element) const {
            return DisplayText(Element(node, element));
        }

        /// Whether the button of the score table is disabled (the room's ready check).
        bool NextHandDisabled() const {
            const UIElement* button = Element("HandOverAction", "NextHand");
            const UIInputClicker* clicker = button != nullptr ? button->GetComponent<UIInputClicker>() : nullptr;
            return clicker != nullptr && clicker->disabled;
        }

        /// The room's name of the player at @p seat, as the lobby has it.
        std::string LobbyName(Seat seat) const {
            const eastl::string& name = _game->GetOnline().Lobby().seats[static_cast<size_t>(seat)].name;
            return std::string(name.c_str(), name.size());
        }

        /// Frames until the hand is over for the game's client: the others and the client itself play on (the first action of each list) and stop short of the first `next_hand`.
        bool PlayToHandOver() {
            return PlayUntil([&] { return _game->GetSession().GetPhase() == SessionPhase::HandOver; });
        }

        /// The tile an atlas cell shows face up; none for the back, the wall stack and anything else.
        static std::optional<Tile> FaceOfCell(const Rect& source) {
            const int cell = static_cast<int>(source.y) / kCellHeight * kAtlasColumns + static_cast<int>(source.x) / kCellWidth;
            return cell < kTileIdCount ? std::optional<Tile>(Tile(static_cast<uint8_t>(cell))) : std::nullopt;
        }

        /// **No peeking in the scene:** the faces of the hand slots of this frame's draw, by seat, set against what the server told this client and nothing else: while a hand is played
        /// its own tiles face up and no other seat's, once it is over the four hands of `result`.
        void ExpectNoPeeking() {
            const OnlineTable& table = Table();
            const std::vector<DrawCall> calls = TileCalls();
            const TableLayout& layout = _table->GetLayout();
            ASSERT_EQ(calls.size(), layout.slots.size());
            std::array<std::vector<uint8_t>, kSeatCount> faces;
            for (size_t i = 0; i < calls.size(); ++i) {
                const TileSlot& slot = layout.slots[i];
                if (slot.role != SlotRole::HandTile || !calls[i].source.has_value()) {
                    continue;
                }
                if (const std::optional<Tile> face = FaceOfCell(*calls[i].source)) {
                    faces[static_cast<size_t>(slot.seat)].push_back(face->Id());
                }
            }
            const auto idsOf = [](const eastl::vector<Tile>& tiles) {
                std::vector<uint8_t> ids;
                for (const Tile tile : tiles) {
                    ids.push_back(tile.Id());
                }
                std::sort(ids.begin(), ids.end());
                return ids;
            };
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                std::vector<uint8_t>& drawn = faces[static_cast<size_t>(seat)];
                std::sort(drawn.begin(), drawn.end());
                std::vector<uint8_t> allowed;
                if (table.phase != SessionPhase::Playing && table.result.has_value()) {
                    allowed = idsOf(table.result->seats[static_cast<size_t>(seat)].concealed);
                } else if (seat == table.seat) {
                    allowed = idsOf(table.hand);
                }
                EXPECT_EQ(drawn, allowed) << "seat " << seat << " phase " << static_cast<int>(table.phase) << " version " << table.version;
                (seat == table.seat ? _ownFacesSeen : _otherFacesSeen) += static_cast<int>(drawn.size());
            }
        }

        /// The game's client acts through the scene: the first action the server lists is clicked on its tile or its button, a `next_hand` on the button of the score table.
        /// Once per view.
        void ViewerActsThroughTheScene() {
            OnlineClient& client = _game->GetOnline();
            if (client.Stage() != OnlineStage::Game || Table().actions.empty() || Table().version == _viewerActed) {
                return;
            }
            _viewerActed = Table().version;
            const ControlAction& first = Table().actions.front();
            if (first.nextHand) {
                ClickElement("HandOverAction", "NextHand");
                return;
            }
            const HandAction& action = first.hand;
            if (action.kind == ActionKind::Discard) {
                const auto& held = Hand().GetHand(Viewer()).concealed;
                const auto found = eastl::find(held.begin(), held.end(), action.tile);
                ASSERT_NE(found, held.end());
                if (action.declareCalling) {
                    EmitAction(SpecOf(ButtonKind::Declare).action, SpecOf(ButtonKind::Declare).value);
                }
                ClickHandTile(static_cast<int>(found - held.begin()));
                return;
            }
            for (const ButtonKind kind : EnumUtils<ButtonKind>::Values()) {
                const ButtonState& button = _table->GetButtons()[kind];
                if (button.shown && button.action == action) {
                    EmitAction(SpecOf(kind).action, SpecOf(kind).value);
                    return;
                }
            }
            ADD_FAILURE() << "no button for the listed action of kind " << static_cast<int>(action.kind);
        }

        /// Frames in which everybody plays by the first action of their list (the game's client through the scene), with the no-peeking check of every frame, until @p done.
        template <typename Done>
        bool PlayThroughTheScene(Done done, int limit = kMatchFrames) {
            for (int count = 0; count < limit; ++count) {
                if (_game->GetOnline().Stage() == OnlineStage::Reconnecting) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));  // the client reconnects on the real clock
                    _viewerActed = 0;                                           // an action sent into the cut is lost: the same version is played again, as a person would
                }
                Frame();
                if (_game->IsOnlineTable()) {
                    ExpectNoPeeking();
                    if (::testing::Test::HasFatalFailure()) {
                        return false;
                    }
                }
                if (done()) {
                    return true;
                }
                for (const std::unique_ptr<HubPlayer>& other : _others) {
                    other->ActFirst();
                }
                ViewerActsThroughTheScene();
            }
            return false;
        }

        /// The files of the save folder that hold the match, by name.
        std::vector<std::pair<std::string, std::string>> MatchSlot() const {
            std::vector<std::pair<std::string, std::string>> files;
            for (const auto& entry : std::filesystem::directory_iterator(_folder)) {
                const std::string name = entry.path().filename().string();
                if (name.find(std::string(kMatchSlot.data(), kMatchSlot.size())) == std::string::npos) {
                    continue;
                }
                std::ifstream in(entry.path(), std::ios::binary);
                std::ostringstream content;
                content << in.rdbuf();
                files.emplace_back(name, content.str());
            }
            return files;
        }

        static constexpr int kMatchFrames = 40000;

        uint64_t _viewerActed = std::numeric_limits<uint64_t>::max();
        int _ownFacesSeen = 0;    // what `ExpectNoPeeking` has looked at: the check is no empty one
        int _otherFacesSeen = 0;  // the faces of other seats drawn once a hand was over
    };

    TEST_F(MahjongOnlineTableSceneTest, TheGameShowsTheDisplayPairWhileTheRoomHasATableAndGivesTheLocalGameBack) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Frame();
        Frame();
        const std::vector<std::pair<std::string, std::string>> savedBefore = MatchSlot();
        ASSERT_FALSE(savedBefore.empty()) << "the local match is saved";
        const bool canContinue = _game->CanContinue();
        const Match* const local = &_game->GetLocalMatch();
        const uint64_t localVersion = _game->GetLocalSession().Version();
        const uint64_t localStarts = _game->GetLocalSession().StartCount();
        const eastl::array<int, kSeatCount> localScores = _game->GetLocalMatch().Scores();
        const size_t localMoves = _game->GetLocalMatch().Hand().Moves().size();
        EXPECT_EQ(&_game->GetMatch(), local);
        EXPECT_FALSE(_game->IsOnlineTable());

        ASSERT_NO_FATAL_FAILURE(StartRoom());

        // The table of the room is shown; the local pair is untouched
        EXPECT_NE(&_game->GetMatch(), local);
        EXPECT_EQ(&_game->GetLocalMatch(), local);
        EXPECT_TRUE(_game->GetSession().IsRemote());
        EXPECT_FALSE(_game->GetLocalSession().IsRemote());
        EXPECT_EQ(_game->GetSession().Viewer(), _game->GetOnline().Seat());
        EXPECT_EQ(_game->GetLocalSession().Version(), localVersion);
        EXPECT_EQ(_game->GetLocalSession().StartCount(), localStarts);
        EXPECT_EQ(_game->GetLocalMatch().Scores(), localScores);
        EXPECT_EQ(_game->GetLocalMatch().Hand().Moves().size(), localMoves);

        // The panels are closed and the table is played (not paused)
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(_game->IsOnlineOpen());
        EXPECT_FALSE(_game->IsTablePaused());
        EXPECT_EQ(_game->CanContinue(), canContinue) << "the offer of Continue survives the room";

        // The save slot is the local match's, the frames of the room did not touch it; the match log and the upload saw nothing
        for (int i = 0; i < 5; ++i) {
            Frame();
        }
        EXPECT_EQ(MatchSlot(), savedBefore);
        EXPECT_EQ(_world.Judge().Offences().size(), 0u);

        // The control API does not see the room
        IControllableGame* control = _game->AsControllable();
        ASSERT_NE(control, nullptr);
        EXPECT_FALSE(control->GetState().has_value());
        EXPECT_FALSE(control->GetActions().has_value());
        EXPECT_FALSE(control->PerformAction(Json::object()).has_value());
        EXPECT_FALSE(control->Reset(Json::object()).has_value());
        EXPECT_EQ(_game->GetLocalSession().Version(), localVersion) << "a refused reset started nothing";

        // Leave: the local game is as it was, behind Setup
        ASSERT_TRUE(_game->GetOnline().Leave());
        ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
        EXPECT_EQ(&_game->GetMatch(), local);
        EXPECT_FALSE(_game->GetSession().IsRemote());
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_EQ(_game->GetLocalSession().Version(), localVersion);
        EXPECT_EQ(_game->GetLocalMatch().Scores(), localScores);
        EXPECT_EQ(MatchSlot(), savedBefore);
        EXPECT_TRUE(control->GetState().has_value()) << "the control API plays the local game again";
    }

    TEST_F(MahjongOnlineTableSceneTest, TheTableDrawsTheOwnTilesFaceUpAndEveryoneElsesAsBacks) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Hand().Discards().size() >= 6 && _game->GetSession().GetPhase() == SessionPhase::Playing; }));
        const Seat viewer = Viewer();
        ASSERT_TRUE(IsSeat(viewer));
        ExpectTableDrawn(false, viewer);
        EXPECT_EQ(FaceUpHandTiles(viewer), static_cast<int>(Hand().GetHand(viewer).concealed.size()));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (seat == viewer) {
                continue;
            }
            EXPECT_EQ(FaceUpHandTiles(seat), 0) << "seat " << seat;
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.seat == seat && slot.role == SlotRole::HandTile) {
                    EXPECT_FALSE(TileOfSlot(Hand(), slot, false).has_value());
                }
            }
        }
        EXPECT_EQ(_world.Judge().Offences().size(), 0u);
    }

    TEST_F(MahjongOnlineTableSceneTest, AClickOnAHandTileSendsTheListedDiscardAndTheNextViewMovesThePile) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Discard) && Hand().GetPhase() == Phase::Discard && _game->GetSession().Actor() == Viewer(); }));
        const Seat viewer = Viewer();
        const size_t piled = Hand().Discards().size();
        constexpr int kIndex = 3;
        const Tile clicked = Hand().GetHand(viewer).concealed[kIndex];
        const uint64_t version = Table().version;

        ASSERT_FALSE(ClickHandTile(kIndex)) << "no canvas took the click";
        ASSERT_TRUE(PlayUntil([&] { return Table().version != version; }, false));
        ASSERT_EQ(Hand().Discards().size(), piled + 1);
        EXPECT_EQ(Hand().Discards().back().tile, clicked);
        EXPECT_EQ(Hand().Discards().back().seat, viewer);
        EXPECT_EQ(_game->GetOnline().Notice().serial, 0u) << "nothing to say";
    }

    TEST_F(MahjongOnlineTableSceneTest, AClaimWindowShowsTheClaimButtonsAndPassSendsPass) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Pass) && Hand().GetPhase() == Phase::Claim; }));
        Draw();
        EXPECT_TRUE(_table->GetButtons()[ButtonKind::Pass].shown);
        int claims = 0;
        for (const ButtonKind kind : {ButtonKind::ClaimMahjong, ButtonKind::ClaimPung, ButtonKind::ClaimKong, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2}) {
            claims += _table->GetButtons()[kind].shown ? 1 : 0;
        }
        EXPECT_GT(claims, 0) << "a window that lists a pass lists a claim";
        EXPECT_FALSE(_table->GetButtons()[ButtonKind::Declare].shown);
        EXPECT_EQ(Hand().PendingClaimSeats(), eastl::vector<Seat>{Viewer()});

        const uint64_t version = _world.Game()->GetStateVersion();
        EmitAction(SpecOf(ButtonKind::Pass).action, SpecOf(ButtonKind::Pass).value);
        EXPECT_GT(_world.Game()->GetStateVersion(), version) << "the server took the pass";
        ASSERT_TRUE(PlayUntil([&] { return !Lists(ActionKind::Pass) || Hand().GetPhase() != Phase::Claim; }, false));
        EXPECT_FALSE(_table->GetButtons()[ButtonKind::Pass].shown);
    }

    TEST_F(MahjongOnlineTableSceneTest, AClickWhileTheServerListsNoDiscardDoesNothing) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Table().actions.empty() && _game->GetSession().GetPhase() == SessionPhase::Playing; }));
        const uint64_t version = _world.Game()->GetStateVersion();
        const size_t piled = Hand().Discards().size();
        EXPECT_FALSE(_table->GetButtons()[ButtonKind::Pass].shown);
        ClickHandTile(0);
        EmitAction(SpecOf(ButtonKind::Pass).action, SpecOf(ButtonKind::Pass).value);
        for (int i = 0; i < 5; ++i) {
            Frame();
        }
        EXPECT_EQ(_world.Game()->GetStateVersion(), version) << "nothing was sent";
        EXPECT_EQ(Hand().Discards().size(), piled);
    }

    TEST_F(MahjongOnlineTableSceneTest, AnActionThatTheServerRefusedAsStaleSaysNothingAndTheNextViewFixesTheTable) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        // A window in which the game's client and another person may both decide
        const auto otherDecides = [&] {
            return eastl::any_of(_others.begin(), _others.end(),
                                 [](const std::unique_ptr<HubPlayer>& other) { return !other->Table().actions.empty() && other->Client().Stage() == OnlineStage::Game; });
        };
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Pass) && Hand().GetPhase() == Phase::Claim && otherDecides(); }));
        Draw();
        for (const std::unique_ptr<HubPlayer>& other : _others) {
            other->ActFirst();  // the other decides: the view of the game's client is old before it clicks
        }
        const uint64_t shown = Table().version;
        ASSERT_GT(_world.Game()->GetStateVersion(), shown);
        EmitAction(SpecOf(ButtonKind::Pass).action, SpecOf(ButtonKind::Pass).value);
        EXPECT_EQ(_world.Game()->GetStateVersion(), shown + 1) << "the stale pass changed nothing";
        ASSERT_TRUE(PlayUntil([&] { return Table().version == _world.Game()->GetStateVersion(); }, false));
        EXPECT_EQ(_game->GetOnline().Notice().serial, 0u) << "a stale action is silent";
        EXPECT_TRUE(CoreManager::GetUIVars().Get("onlineNotice"_intern).empty());
        EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::Playing);
        EXPECT_EQ(Hand().Discards().size(), Table().discards.size()) << "the table is the view's";
    }

    TEST_F(MahjongOnlineTableSceneTest, NoHintIsShownInARoomWhateverTheSwitchSays) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(_game->HintsOn()) << "the default switch";
        ASSERT_TRUE(PlayUntil([&] { return Lists(ActionKind::Discard) && Hand().GetPhase() == Phase::Discard && _game->GetSession().Actor() == Viewer(); }));
        Frame();
        const auto hintsCanvas = Canvas("HintsButton");
        EXPECT_FALSE(hintsCanvas && hintsCanvas->TakesInput() && OnScreen("HintsButton", "HintButton"));
        const auto compactCanvas = Canvas("ActionsCompact");
        EXPECT_FALSE(compactCanvas && compactCanvas->TakesInput() && OnScreen("ActionsCompact", "HintButton"));
        EXPECT_FALSE(OnScreen("Hints", "WaitPanel"));
        EXPECT_FALSE(OnScreen("Hints", "AdviceText"));
    }

    // ----- the HUD speaks the room -----

    TEST_F(MahjongOnlineTableSceneTest, ThePlatesNameThePlayersAndOnlyTheOwnSeatIsYou) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        Frame();
        const Seat viewer = Viewer();
        ASSERT_TRUE(IsSeat(viewer));
        for (int position = 0; position < kSeatCount; ++position) {
            const Seat seat = SeatAtPosition(viewer, position);
            const std::string expected = LobbyName(seat) + (seat == viewer ? " You" : "");
            EXPECT_EQ(Var(Format("plateName{}", position).c_str()), expected) << "position " << position;
            EXPECT_EQ(Text("SeatPlates", Format("PlateName{}", position).c_str()), expected);
        }
        EXPECT_EQ(Var("plateName0"), std::string(kHostName) + " You") << "the host sits at seat 0 and at the bottom";
        EXPECT_EQ(Var("panelRound"), "East 1") << "the centre panel follows the display match";
        EXPECT_EQ(Var("panelWall"), "Wall " + std::to_string(Hand().GetWall().LiveCount()));

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Frame();
        EXPECT_EQ(Var("plateName0"), std::string(kHostName) + " Вы");
        EXPECT_EQ(Var("panelRound"), "Восток 1");
    }

    TEST_F(MahjongOnlineTableSceneTest, TheMenuButtonLeavesTheRoomAndTheEndOfTheTableOpensSetupWithoutAWord) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        Frame();
        EXPECT_EQ(ShownTooltip(Element("HudMenu", "NewGame")), "Leave room");
        EXPECT_TRUE(OnScreen("HudMenu", "Options")) << "the options stay";
        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Frame();
        Frame();
        EXPECT_EQ(ShownTooltip(Element("HudMenu", "NewGame")), "Выйти из комнаты");
        ASSERT_TRUE(_game->SetLanguage("en"_intern));
        Frame();
        Frame();

        ASSERT_TRUE(ClickElement("HudMenu", "NewGame"));
        ASSERT_TRUE(ClickElement("HudMenu", "NewGame")) << "a second press before the server answered";
        ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
        EXPECT_EQ(_game->GetOnline().Stage(), OnlineStage::Idle);
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_FALSE(_game->IsOnlineOpen()) << "the player asked to leave: no word";
        EXPECT_EQ(_game->GetOnline().Notice().serial, 0u);
        Frame();
        Frame();
        EXPECT_EQ(ShownTooltip(Element("HudMenu", "NewGame")), "New game") << "the local game's button again";
        EXPECT_FALSE(_game->GetSession().IsRemote());
        EXPECT_EQ(Var("plateName0"), "Seat 1 You") << "the plates are the local game's again";
    }

    TEST_F(MahjongOnlineTableSceneTest, TheConnectionLostShowsAStatusLineOverTheFrozenTableAndTheTableGoesOnWhenItIsBack) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Hand().Discards().size() >= 4 && _game->GetSession().GetPhase() == SessionPhase::Playing; }));
        Frame();
        const uint64_t starts = _game->GetSession().StartCount();
        const size_t piled = Hand().Discards().size();

        _world.SetRefusing(true);
        _world.Sever(_links.Last());
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Reconnecting; }));
        Frame();
        EXPECT_TRUE(_game->IsOnlineTable()) << "the last view stays";
        const std::string status = std::string(OnlineNoticeText(CoreManager::GetUIStrings(), kOnlineReconnectingKey).c_str());
        EXPECT_EQ(Var("lastMove"), status);
        EXPECT_TRUE(OnScreen("Hud", "MoveLine"));
        EXPECT_EQ(Text("Hud", "MoveText"), status);
        EXPECT_EQ(Hand().Discards().size(), piled);
        ExpectTableDrawn(false, Viewer());
        EXPECT_EQ(ShownTooltip(Element("HudMenu", "NewGame")), "Leave room") << "a room can be left while the link is down";

        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Frame();
        EXPECT_EQ(Var("lastMove"), std::string(OnlineNoticeText(CoreManager::GetUIStrings(), kOnlineReconnectingKey).c_str()));
        ASSERT_TRUE(_game->SetLanguage("en"_intern));

        _world.SetRefusing(false);
        ASSERT_TRUE(PlayUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Game && Var("lastMove") != status; }));
        EXPECT_EQ(_game->GetSession().StartCount(), starts) << "the same table";
    }

    TEST_F(MahjongOnlineTableSceneTest, AWordOfTheClientWhileTheTableIsUpIsTheLineUnderTheDiscardsUntilTheTableMoves) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Hand().Discards().size() >= 3 && _game->GetSession().GetPhase() == SessionPhase::Playing; }));
        Frame();
        const uint32_t before = _game->GetOnline().Notice().serial;
        EXPECT_FALSE(_game->GetOnline().Host(kUrl, kHostName));  // not possible in a room: a word, and the table stays
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Notice().serial != before; }));
        Frame();
        const std::string word = std::string(OnlineNoticeText(CoreManager::GetUIStrings(), _game->GetOnline().Notice().key, _game->GetOnline().Notice().arg).c_str());
        ASSERT_FALSE(word.empty());
        EXPECT_TRUE(_game->IsOnlineTable());
        EXPECT_EQ(Var("lastMove"), word);
        EXPECT_TRUE(OnScreen("Hud", "MoveLine"));

        // The next view moves the table: the line is the move again
        const uint64_t version = Table().version;
        ASSERT_TRUE(PlayUntil([&] { return Table().version != version; }));
        Frame();
        EXPECT_NE(Var("lastMove"), word);
    }

    /// A client that gives up on a lost connection after a third of a second.
    class MahjongOnlineTableGaveUpTest : public MahjongOnlineTableSceneTest {
    protected:
        RoomClientSettings ClientSettings() override {
            RoomClientSettings settings = MahjongOnlineTableSceneTest::ClientSettings();
            settings.reconnectGiveUp = std::chrono::milliseconds(300);
            return settings;
        }
    };

    TEST_F(MahjongOnlineTableGaveUpTest, ARoomTheClientGaveUpOnBringsThePlayerBackToTheOnlinePanelWithTheWord) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        Frame();
        _world.SetRefusing(true);
        _world.Sever(_links.Last());
        constexpr auto kWait = std::chrono::seconds(5);
        const auto until = std::chrono::steady_clock::now() + kWait;
        while (_game->IsOnlineTable() && std::chrono::steady_clock::now() < until) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            Frame();
        }
        ASSERT_FALSE(_game->IsOnlineTable());
        Frame();
        const std::string word = std::string(OnlineNoticeText(CoreManager::GetUIStrings(), OnlineErrorKey(RoomClientCloseReasons::kGaveUp)).c_str());
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(_game->IsOnlineOpen()) << "the panel is the place to say why";
        EXPECT_EQ(Var("onlineNotice"), word);
        EXPECT_TRUE(OnScreen("Online", "OnlineNotice"));
        EXPECT_FALSE(_game->TakeRoomEndNotice()) << "the panel took it";
        EXPECT_FALSE(_game->GetSession().IsRemote());
    }

    TEST_F(MahjongOnlineTableSceneTest, TheReadyCheckDisablesTheButtonCountsTheReadersAndTheNextHandOpensItAgain) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayToHandOver());
        Frame();
        const Seat viewer = Viewer();
        ASSERT_TRUE(Canvas("HandOver")->TakesInput());
        ASSERT_TRUE(OnScreen("HandOverAction", "NextHand"));
        EXPECT_FALSE(NextHandDisabled());
        EXPECT_FALSE(OnScreen("HandOver", "HandCoachRow")) << "nobody has read the plate yet";
        EXPECT_EQ(Text("HandOverAction", "NextHand"), _game->GetMatch().HandsPlayed() + 1 >= _game->GetMatch().HandsTotal() ? "Finish match" : "Next hand");
        const std::string headline = Text("HandOver", "Headline");
        bool namesAPlayer = headline.find("Drawn") != std::string::npos;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            namesAPlayer = namesAPlayer || headline.find(LobbyName(seat)) != std::string::npos;
        }
        EXPECT_TRUE(namesAPlayer) << "the headline names a player of the room: " << headline;

        ASSERT_TRUE(ClickElement("HandOverAction", "NextHand"));
        ASSERT_TRUE(PumpUntil([&] { return Table().ready[static_cast<size_t>(viewer)]; }));
        Frame();
        EXPECT_TRUE(NextHandDisabled());
        EXPECT_EQ(Text("HandOver", "HandCoachRow"), "Ready for the next hand: 1 of 4");
        const uint64_t version = _world.Game()->GetStateVersion();
        ClickElement("HandOverAction", "NextHand");
        Frame();
        EXPECT_EQ(_world.Game()->GetStateVersion(), version) << "a disabled button sends nothing";

        // The others read the plate (each acts again when a view with a ready flag more comes): the next hand is dealt and the plate and the count go
        ASSERT_TRUE(PlayUntil([&] { return _game->GetSession().GetPhase() != SessionPhase::HandOver; }, false));
        Frame();
        EXPECT_FALSE(Canvas("HandOver")->TakesInput());
        EXPECT_FALSE(NextHandDisabled()) << "the next hand's plate starts with an enabled button";
        EXPECT_EQ(eastl::count(Table().ready.begin(), Table().ready.end(), true), 0) << "the count went with the hand";
    }

    TEST_F(MahjongOnlineTableSceneTest, TheScoreTableOfARoomPaysWhatTheServerPaysAndTheStandingsOfAMatchOfTheRoomOfferLeaveRoom) {
        ASSERT_NO_FATAL_FAILURE(StartRoom(kShortMatchHands, 0));  // computers play the other seats: they win hands, and a hand won counts
        constexpr int kMostHands = 40;                            // a drawn hand is dealt again and counts for nothing
        int hands = 0;
        while (_game->GetSession().GetPhase() != SessionPhase::MatchOver && hands < kMostHands) {
            ASSERT_TRUE(PlayUntil([&] { return _game->GetSession().GetPhase() != SessionPhase::Playing; }, true, kMatchFrames)) << "hand " << hands;
            if (_game->GetSession().GetPhase() == SessionPhase::HandOver) {
                Frame();
                const OnlineResult& result = *Table().result;
                const Match& match = _game->GetMatch();
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    const std::string shown = Var(Format("handOverSeat{}", seat).c_str());
                    const std::string total = std::to_string(match.Scores()[seat] + result.deltas[seat]);
                    EXPECT_NE(shown.find("total " + total), std::string::npos) << shown << " / " << total;
                }
                ++hands;
                // all four read the plate
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
        ASSERT_TRUE(OnScreen("MatchOver", "Headline"));
        const Match& match = _game->GetMatch();
        const StandingsTable table = FormatStandings(CoreManager::GetUIStrings(), match, SeatWho(_game->GetSession()));
        EXPECT_EQ(Text("MatchOver", "Headline"), std::string(table.headline.c_str()));
        for (int row = 0; row < kSeatCount; ++row) {
            EXPECT_EQ(Text("MatchOver", Format("Standing{}", row).c_str()), std::string(FormatStandingSentence(CoreManager::GetUIStrings(), table.rows[static_cast<size_t>(row)]).c_str()));
        }
        std::string rows;
        for (int row = 0; row < kSeatCount; ++row) {
            rows += Text("MatchOver", Format("Standing{}", row).c_str()) + "\n";
        }
        EXPECT_NE(rows.find(std::string(kHostName) + " You"), std::string::npos) << rows;
        EXPECT_NE(rows.find("Seat 2"), std::string::npos) << "a computer keeps its number: " << rows;
        EXPECT_EQ(Text("MatchOver", "NewGame"), "Leave room");

        ASSERT_TRUE(ClickElement("MatchOver", "NewGame"));
        ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_FALSE(_game->IsOnlineOpen());
    }

    // ----- a room played through the scene -----

    TEST_F(MahjongOnlineTableSceneTest, AnEightHandRoomIsPlayedToItsEndByTheSceneAndAConnectionCutInTheMiddleIsResumed) {
        // The game's client, one other person (who plays the first action of each list) and two computers: a table of four people who play the first action never wins a hand
        ASSERT_NO_FATAL_FAILURE(StartRoom(kShortMatchHands, 1));
        const uint64_t localStarts = _game->GetLocalSession().StartCount();
        bool severed = false;
        bool resumed = false;
        uint64_t startsBefore = 0;
        uint64_t frozenVersion = 0;
        int handsSeen = 0;
        uint64_t lastStart = _game->GetSession().StartCount();
        const bool ended = PlayThroughTheScene([&] {
            const TableSession& session = _game->GetSession();
            if (session.StartCount() != lastStart) {
                lastStart = session.StartCount();
                ++handsSeen;
            }
            if (!severed && handsSeen >= 2 && session.GetPhase() == SessionPhase::Playing && Hand().Discards().size() >= 3 && _game->GetOnline().Stage() == OnlineStage::Game) {
                severed = true;
                startsBefore = session.StartCount();
                frozenVersion = _game->GetOnline().Table().version;
                _world.Sever(_links.Last());
            }
            if (severed && !resumed && _game->GetOnline().Stage() == OnlineStage::Game && _game->GetOnline().Table().version > frozenVersion) {
                resumed = true;
                EXPECT_EQ(session.StartCount(), startsBefore) << "the table the client came back to is the one it left";
            }
            return session.GetPhase() == SessionPhase::MatchOver;
        });
        ASSERT_TRUE(ended) << "after " << handsSeen << " hands";
        ASSERT_TRUE(severed);
        EXPECT_TRUE(resumed);
        EXPECT_GT(startsBefore, 0u);
        EXPECT_EQ(_world.Judge().Offences().size(), 0u) << (_world.Judge().Offences().empty() ? "" : _world.Judge().Offences().front());

        Frame();
        Frame();
        EXPECT_TRUE(_game->IsOnlineTable());
        EXPECT_EQ(_game->GetOnline().Stage(), OnlineStage::Game) << "the link came back by itself";
        EXPECT_TRUE(Canvas("MatchOver")->TakesInput());
        EXPECT_GT(_ownFacesSeen, 0);
        EXPECT_GT(_otherFacesSeen, 0) << "the hands of the others were drawn face up once a hand was over, and then only";
        EXPECT_EQ(_game->GetLocalSession().StartCount(), localStarts) << "the local match was never touched";
        EXPECT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        EXPECT_EQ(_game->GetMatch().HandsPlayed(), kShortMatchHands);

        ASSERT_TRUE(ClickElement("MatchOver", "NewGame"));
        ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
        EXPECT_TRUE(_game->IsSetupOpen());
    }

    TEST_F(MahjongOnlineTableSceneTest, AViewOfAnotherFormatClosesTheTableAndSetupComesBackWithTheWord) {
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayThroughTheScene([&] { return Hand().Discards().size() >= 3; }));
        Frame();
        ASSERT_TRUE(_game->IsOnlineTable());
        const Seat seat = _game->GetOnline().Seat();
        Json view = _world.Game()->GetSeatView(seat).value();
        view["format"] = kOnlineViewFormat + 1;
        _links.Last().QueueText(MakeViewMessage(_game->GetOnline().Room(), seat, _world.Game()->GetStateVersion() + 1, false, view, _world.Game()->GetSeatActions(seat).value()));
        ASSERT_TRUE(PumpUntil([&] { return !_game->IsOnlineTable(); }));
        Frame();
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(_game->IsOnlineOpen());
        EXPECT_EQ(Var("onlineNotice"), std::string(OnlineNoticeText(CoreManager::GetUIStrings(), kOnlineViewFormatKey).c_str()));
        EXPECT_TRUE(OnScreen("Online", "OnlineNotice"));
        EXPECT_EQ(_game->GetOnline().Stage(), OnlineStage::Idle);
        EXPECT_FALSE(_game->GetSession().IsRemote()) << "the local game is back";
        EXPECT_FALSE(_game->GetOnline().SessionToKeep().IsValid()) << "a room this client cannot read is not offered again";
    }

}  // namespace BECore::Mahjong::Tests
