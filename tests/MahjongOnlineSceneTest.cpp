#include "ComputerTestSupport.h"
#include "MahjongButtonSupport.h"
#include "MahjongHubTransport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "StringsTestSupport.h"

#include <Common/TempPath.h>
#include <CoreSDL/SDLEvents.h>
#include <Mahjong/Components/MahjongOnlineComponent.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Mahjong/Online/TextEntry.h>
#include <Rooms/RoomErrors.h>

/**
 * The online panel in the shipped scene (`Components/MahjongOnlineComponent.h`, the nodes `Online` and `OnlineEntry`): the `Online` button of `Setup` opens it over `Setup`, the pages
 * follow `OnlineScreens`, the pad and the keyboard type, Escape goes one page back, a frame in which nothing moved sets nothing, and every page passes the size matrix. Nothing
 * here connects: the pages that need a room are in the second part of the file.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kOnline = "Online";
        constexpr const char* kEntry = "OnlineEntry";
        constexpr float kButtonAir = 8.0f;
        constexpr float kKeyAir = 4.0f;
        constexpr float kFrameSeconds = 1.0f / 60.0f;
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}, {1707.0f, 480.0f}};

        void PressKeyboard(SDL_Keycode key, bool shift = false, bool repeat = false) {
            SDLEvents::KeyDownEvent::Emit(key, shift ? SDL_KMOD_LSHIFT : SDL_KMOD_NONE, repeat);
        }

    }  // namespace

    class MahjongOnlineSceneTest : public MahjongTableTestBase {
    protected:
        /// `Setup` over a started match, then the panel opened by the button, as a player does it.
        void OpenPanel() {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            _game->OpenSetup();
            Draw();
            ASSERT_TRUE(ClickElement("Setup", "SetupOnline"));
            Draw();
            ASSERT_TRUE(_game->IsOnlineOpen());
        }

        /// The button again, after the panel was closed (`Setup` is still open).
        void ReopenPanel() {
            ASSERT_TRUE(ClickElement("Setup", "SetupOnline"));
            Draw();
            ASSERT_TRUE(_game->IsOnlineOpen());
            ASSERT_EQ(Screens().Page(), OnlinePage::Menu);
        }

        const OnlineScreens& Screens() const {
            return MahjongOnlineComponent::GetActive()->Screens();
        }

        bool CanvasShown(const char* node) const {
            const auto canvas = Canvas(node);
            return canvas && canvas->TakesInput();
        }

        /// Whether the component shows the element (a hidden canvas keeps the rects of its last layout, so the style is asked).
        bool ItemShown(const char* node, const char* element) const {
            const UIElement* found = Element(node, element);
            return found != nullptr && found->style.visible;
        }

        std::string Text(const char* node, const char* element) const {
            return DisplayText(Element(node, element));
        }

        /// One frame as the application runs it: the scene update, then the draw.
        void Frame() {
            AdvanceTime(kFrameSeconds);
            Draw();
        }

        void Act(const char* action, const char* value = "") {
            EmitAction(action, value);
            Draw();
        }

        void GoToTheHostForm() {
            Act("MahjongOnlineHost");
            ASSERT_EQ(Screens().Page(), OnlinePage::Host);
        }

        void GoToTheJoinForm() {
            Act("MahjongOnlineJoin");
            ASSERT_EQ(Screens().Page(), OnlinePage::Join);
        }

        /// The canvas as it is passes the matrix, every word fits in the outputs and on the whole matrix.
        void ExpectTheCanvasFits(const char* node, float air = kButtonAir) {
            Draw();
            ExpectPassesTheMatrix(node, EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit(node, output.width, output.height, air);
            }
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas(node), air);
            EXPECT_TRUE(cut.empty()) << node << ": " << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
            Draw();  // the checks laid the canvas out for other windows: the next click needs the rects of this one
        }
    };

    TEST_F(MahjongOnlineSceneTest, TheOnlineButtonOpensThePanelOverSetupAndClosingItGivesSetupBack) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        EXPECT_EQ(Text("Setup", "SetupOnline"), "Online");
        EXPECT_FALSE(CanvasShown(kOnline));

        ASSERT_TRUE(ClickElement("Setup", "SetupOnline"));
        Draw();
        EXPECT_TRUE(_game->IsOnlineOpen());
        EXPECT_TRUE(_game->IsSetupOpen()) << "the panel is over Setup, which stays open under it";
        EXPECT_TRUE(CanvasShown(kOnline));
        EXPECT_FALSE(CanvasShown(kEntry));
        EXPECT_TRUE(_game->IsTablePaused());
        EXPECT_EQ(Screens().Page(), OnlinePage::Menu);

        ASSERT_TRUE(ClickElement(kOnline, "OnlineClose"));
        Draw();
        EXPECT_FALSE(_game->IsOnlineOpen());
        EXPECT_FALSE(CanvasShown(kOnline));
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(CanvasShown("Setup")) << "the seat choice is back as it was";
    }

    TEST_F(MahjongOnlineSceneTest, TheTableIsNotPlayedWhileThePanelIsOpen) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        Lay(MakeModel(SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "9p"), kSeat0));
        Draw();
        const std::string before = Snapshot(Hand());
        ClickHandTile(0);
        EmitAction("MahjongPass");
        EmitAction("MahjongWin");
        AdvanceTime(kComputerStepSeconds * 4.0f);
        EXPECT_EQ(Snapshot(Hand()), before);
    }

    TEST_F(MahjongOnlineSceneTest, TheActionsOfThePanelAreIgnoredWhileItIsClosedAndTheButtonWhileSetupIsClosed) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->CloseSetup();
        Draw();
        EmitAction("MahjongOnline");
        EXPECT_FALSE(_game->IsOnlineOpen()) << "the button lives on Setup";
        EmitAction("MahjongOnlineHost");
        EmitAction("MahjongOnlineCreate");
        Draw();
        EXPECT_EQ(Screens().Page(), OnlinePage::Closed);

        _game->OpenSetup();
        Draw();
        EmitAction("MahjongOnlineHost");
        Draw();
        EXPECT_EQ(Screens().Page(), OnlinePage::Closed) << "an event of a panel that is not open is out of date";
        EXPECT_FALSE(CanvasShown(kOnline));
    }

    TEST_F(MahjongOnlineSceneTest, EachPageShowsExactlyItsElements) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        EXPECT_TRUE(ItemShown(kOnline, "OnlineTitleMenu"));
        EXPECT_TRUE(ItemShown(kOnline, "OnlineMenuHost"));
        EXPECT_TRUE(ItemShown(kOnline, "OnlineMenuJoin"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineMenuReturn")) << "no session is kept";
        EXPECT_FALSE(ItemShown(kOnline, "OnlineNameRow"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineButtons"));

        ASSERT_NO_FATAL_FAILURE(GoToTheHostForm());
        EXPECT_TRUE(ItemShown(kOnline, "OnlineTitleHost"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineTitleMenu"));
        for (const char* name : {"OnlineNameRow", "OnlineHandsRow", "OnlineLevelRow", "OnlineButtons", "OnlineCreate"}) {
            EXPECT_TRUE(ItemShown(kOnline, name)) << name;
        }
        for (const char* name : {"OnlineCodeRow", "OnlineEnter", "OnlineStart", "OnlineLeave", "OnlineSeatRow0"}) {
            EXPECT_FALSE(ItemShown(kOnline, name)) << name;
        }

        Act("MahjongOnlineClose");
        ASSERT_NO_FATAL_FAILURE(ReopenPanel());  // the panel opens on the Menu again
        EXPECT_TRUE(ItemShown(kOnline, "OnlineTitleMenu"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineTitleHost"));
    }

    TEST_F(MahjongOnlineSceneTest, TheJoinFormShowsTheCodeAndTheHostFormTheOptionsAsTheScreensHoldThem) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheJoinForm());
        EXPECT_TRUE(ItemShown(kOnline, "OnlineCodeRow"));
        EXPECT_TRUE(ItemShown(kOnline, "OnlineEnter"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineHandsRow"));
        EXPECT_EQ(Var("onlineCode"), "");
        EXPECT_EQ(Var("onlineName"), "Player");

        Act("MahjongOnlineEnter");
        EXPECT_EQ(Screens().Page(), OnlinePage::Join) << "a code that is not complete sends nothing";
        EXPECT_TRUE(ItemShown(kOnline, "OnlineNotice"));
        EXPECT_NE(Var("onlineNotice"), "");

        Act("MahjongOnlineClose");
        ASSERT_NO_FATAL_FAILURE(ReopenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheHostForm());
        EXPECT_EQ(Var("onlineHands"), "16");
        EXPECT_EQ(Var("onlineLevel"), "5");
        Act("MahjongOnlineHands", "8");
        Act("MahjongOnlineLevel", "7");
        Act("MahjongOnlineHands", "12");  // not offered
        Act("MahjongOnlineLevel", "11");
        EXPECT_EQ(Var("onlineHands"), "8");
        EXPECT_EQ(Var("onlineLevel"), "7");
        EXPECT_TRUE(Element(kOnline, "OnlineHands8")->selected);
    }

    TEST_F(MahjongOnlineSceneTest, TheNameIsTypedOnThePadAndOnTheKeyboardAndKeptByTheGame) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheHostForm());
        ASSERT_TRUE(ClickElement(kOnline, "OnlineName"));
        Draw();
        ASSERT_EQ(Screens().Page(), OnlinePage::Entry);
        EXPECT_TRUE(CanvasShown(kEntry));
        EXPECT_FALSE(CanvasShown(kOnline)) << "the pad has a dim of its own";
        EXPECT_TRUE(ItemShown(kEntry, "EntryCaptionName"));
        EXPECT_FALSE(ItemShown(kEntry, "EntryCaptionCode"));
        EXPECT_EQ(Var("onlineEntryValue"), "Player");
        EXPECT_EQ(Var("padKey0"), "A");

        // The pad: the keys the model lists are shown and the others are not
        const size_t keys = Screens().Pad().size();
        ASSERT_GT(keys, 0u);
        ASSERT_LE(keys, TextEntry::kPadMaxKeys);
        EXPECT_TRUE(ItemShown(kEntry, Format("PadKey{}", keys - 1).c_str()));
        if (keys < TextEntry::kPadMaxKeys) {
            EXPECT_FALSE(ItemShown(kEntry, Format("PadKey{}", keys).c_str()));
        }
        for (int i = 0; i < 6; ++i) {
            Act("MahjongOnlineBackspace");
        }
        EXPECT_EQ(Var("onlineEntryValue"), "");
        Act("MahjongOnlineKey", "0");
        EXPECT_EQ(Var("onlineEntryValue"), "A");
        Act("MahjongOnlineKey", "999");
        Act("MahjongOnlineKey", "x");
        EXPECT_EQ(Var("onlineEntryValue"), "A") << "a key the pad does not have types nothing";

        // The keyboard: a Latin letter with and without Shift, a digit; an Enter of a held key does nothing, Backspace repeats
        PressKeyboard(SDLK_B);
        PressKeyboard(SDLK_C, true);
        PressKeyboard(SDLK_7);
        PressKeyboard(SDLK_F1);
        PressKeyboard(SDLK_D, false, true);
        Draw();
        EXPECT_EQ(Var("onlineEntryValue"), "AbC7");
        PressKeyboard(SDLK_BACKSPACE, false, true);
        Draw();
        EXPECT_EQ(Var("onlineEntryValue"), "AbC");
        PressKeyboard(SDLK_RETURN, false, true);
        EXPECT_EQ(Screens().Page(), OnlinePage::Entry);

        PressKeyboard(SDLK_RETURN);
        Draw();
        EXPECT_EQ(Screens().Page(), OnlinePage::Host);
        EXPECT_FALSE(CanvasShown(kEntry));
        EXPECT_TRUE(CanvasShown(kOnline));
        EXPECT_EQ(Var("onlineName"), "AbC");
        EXPECT_EQ(_game->StoredOnline().name, "AbC") << "the game keeps the name the player typed";
    }

    TEST_F(MahjongOnlineSceneTest, TheCodeIsTypedInUpperCaseOfTheRoomAlphabet) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheJoinForm());
        ASSERT_TRUE(ClickElement(kOnline, "OnlineCode"));
        Draw();
        ASSERT_EQ(Screens().Page(), OnlinePage::Entry);
        EXPECT_TRUE(ItemShown(kEntry, "EntryCaptionCode"));
        EXPECT_EQ(Screens().Pad().size(), 32u);
        EXPECT_TRUE(ItemShown(kEntry, "PadKey31"));
        EXPECT_FALSE(ItemShown(kEntry, "PadKey32"));
        EXPECT_FALSE(ItemShown(kEntry, "PadRow4")) << "the rows with no key are hidden";
        EXPECT_TRUE(ItemShown(kEntry, "PadRow3"));

        for (const SDL_Keycode key : {SDLK_K, SDLK_7, SDLK_M, SDLK_2, SDLK_Q, SDLK_A}) {
            PressKeyboard(key);
        }
        PressKeyboard(SDLK_O);  // not in the alphabet
        Draw();
        EXPECT_EQ(Var("onlineEntryValue"), "K7M2Q") << "five characters at most, upper case";
        Act("MahjongOnlineDone");
        EXPECT_EQ(Var("onlineCode"), "K7M2Q");
        EXPECT_EQ(Screens().Page(), OnlinePage::Join);
    }

    TEST_F(MahjongOnlineSceneTest, EscapeGoesOnePageBackAndTheLastOneClosesThePanelOnly) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheJoinForm());
        ASSERT_TRUE(ClickElement(kOnline, "OnlineCode"));
        Draw();
        ASSERT_EQ(Screens().Page(), OnlinePage::Entry);

        PressKeyboard(SDLK_ESCAPE, false, true);
        EXPECT_EQ(Screens().Page(), OnlinePage::Entry) << "a held Escape is not a second press";
        PressKeyboard(SDLK_ESCAPE);
        Draw();
        EXPECT_EQ(Screens().Page(), OnlinePage::Join);
        EXPECT_TRUE(CanvasShown(kOnline));
        EXPECT_FALSE(CanvasShown(kEntry));
        PressKeyboard(SDLK_ESCAPE);
        Draw();
        EXPECT_EQ(Screens().Page(), OnlinePage::Menu);
        EXPECT_TRUE(_game->IsOnlineOpen());
        EXPECT_TRUE(_game->IsSetupOpen());
        PressKeyboard(SDLK_ESCAPE);
        Draw();
        EXPECT_FALSE(_game->IsOnlineOpen());
        EXPECT_FALSE(CanvasShown(kOnline));
        EXPECT_TRUE(_game->IsSetupOpen()) << "Escape gave the panel up, not Setup under it";
        EXPECT_TRUE(CanvasShown("Setup"));
        PressKeyboard(SDLK_ESCAPE);
        EXPECT_FALSE(_game->IsSetupOpen()) << "with no panel up, Escape is Setup's again";
    }

    TEST_F(MahjongOnlineSceneTest, AFrameInWhichNothingMovedSetsNoVariableOnAnyPage) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        const auto expectQuiet = [&](const char* label) {
            SCOPED_TRACE(label);
            Frame();
            const uint64_t version = CoreManager::GetUIVars().GetVersion();
            const uint64_t strings = CoreManager::GetUIStrings().GetVersion();
            for (int i = 0; i < 3; ++i) {
                Frame();
            }
            EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), version);
            EXPECT_EQ(CoreManager::GetUIStrings().GetVersion(), strings);
        };
        expectQuiet("menu");
        ASSERT_NO_FATAL_FAILURE(GoToTheHostForm());
        expectQuiet("host");
        ASSERT_TRUE(ClickElement(kOnline, "OnlineName"));
        Draw();
        ASSERT_EQ(Screens().Page(), OnlinePage::Entry);
        expectQuiet("entry");
        Act("MahjongOnlineDone");
        Act("MahjongOnlineClose");
        ASSERT_NO_FATAL_FAILURE(ReopenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheJoinForm());
        expectQuiet("join");
        Act("MahjongOnlineClose");
        expectQuiet("closed");
    }

    TEST_F(MahjongOnlineSceneTest, ThePanelSpeaksTheLanguageOfTheGameAndAFormerNameFollowsItUntilTheNameIsTheirs) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheHostForm());
        EXPECT_EQ(Text(kOnline, "OnlineTitleHost"), "Host a room");
        EXPECT_EQ(Var("onlineName"), "Player");
        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Frame();
        EXPECT_EQ(Text(kOnline, "OnlineTitleHost"), "Создать комнату");
        EXPECT_EQ(Text(kOnline, "OnlineCreate"), "Создать");
        EXPECT_EQ(Var("onlineName"), "Игрок") << "the default name is a word of the language";

        ASSERT_TRUE(ClickElement(kOnline, "OnlineName"));
        Draw();
        ASSERT_EQ(Screens().Page(), OnlinePage::Entry);
        EXPECT_EQ(Var("padKey0"), "А") << "the pad lists the letters of the language";
        ASSERT_TRUE(_game->SetLanguage("en"_intern));
        Frame();
        EXPECT_EQ(Var("padKey0"), "A") << "a language switch with the pad up makes the pad again";
        EXPECT_EQ(Text(kEntry, "EntryDone"), "Done");
        Act("MahjongOnlineKey", "1");
        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Frame();
        Act("MahjongOnlineDone");
        EXPECT_EQ(Var("onlineName"), "PlayerB") << "a name the player has touched stays";
    }

    TEST_F(MahjongOnlineSceneTest, EveryPagePassesTheSizeMatrixInBothLanguages) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            ASSERT_TRUE(_game->SetLanguage(language));
            Act("MahjongOnlineClose");
            ASSERT_NO_FATAL_FAILURE(ReopenPanel());
            {
                SCOPED_TRACE("menu");
                ExpectTheCanvasFits(kOnline);
            }
            ASSERT_NO_FATAL_FAILURE(GoToTheHostForm());
            {
                SCOPED_TRACE("host");
                ExpectTheCanvasFits(kOnline);
            }
            ASSERT_TRUE(ClickElement(kOnline, "OnlineName"));
            Draw();
            ASSERT_EQ(Screens().Page(), OnlinePage::Entry);
            {
                SCOPED_TRACE("entry, the name");
                ExpectTheCanvasFits(kEntry, kKeyAir);
            }
            Act("MahjongOnlineDone");
            Act("MahjongOnlineClose");
            ASSERT_NO_FATAL_FAILURE(ReopenPanel());
            ASSERT_NO_FATAL_FAILURE(GoToTheJoinForm());
            {
                SCOPED_TRACE("join");
                ExpectTheCanvasFits(kOnline);
            }
            ASSERT_TRUE(ClickElement(kOnline, "OnlineCode"));
            Draw();
            ASSERT_EQ(Screens().Page(), OnlinePage::Entry);
            {
                SCOPED_TRACE("entry, the code");
                ExpectTheCanvasFits(kEntry, kKeyAir);
            }
            Act("MahjongOnlineDone");
            Act("MahjongOnlineClose");
        });
    }

    // ----- A room played through the screens, over the real hub -----

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        constexpr int kPumpLimit = 2000;
        constexpr milliseconds kTick{10};
        constexpr size_t kRoomCodeLength = 5;
        constexpr const char* kTempFolder = "BagiMahjongOnlineSceneTest";
        constexpr eastl::string_view kUrl = "ws://hub.test";

    }  // namespace

    /// The game of the scene on the in-memory hub of `MahjongHubTransport.h`, with its save folder in a temp directory (the slot `online` is written there by the HUD's flush).
    class MahjongOnlineRoomTest : public MahjongOnlineSceneTest {
    protected:
        void SetUp() override {
            _folder = TestTempDirectory(kTempFolder);
            std::filesystem::remove_all(_folder);
            SetMatchSaveDirectory(_folder);
            MahjongOnlineSceneTest::SetUp();
            ASSERT_TRUE(_game->SetOnlineSettings(_world.MakeClientSettings(_links)));
        }

        void TearDown() override {
            MahjongOnlineSceneTest::TearDown();  // the game stops: its last flush still goes to the folder
            SetMatchSaveDirectory(std::filesystem::path());
            std::error_code error;
            std::filesystem::remove_all(_folder, error);
        }

        /// Frames of the scene and of the other people of the room until @p done: the clock of the world moves by a tick, the others read what came, the scene updates and draws.
        template <typename Done>
        bool PumpUntil(Done done, const std::vector<HubPlayer*>& others = {}) {
            for (int count = 0; count < kPumpLimit; ++count) {
                if (done()) {
                    return true;
                }
                _world.Advance(kTick);
                for (HubPlayer* other : others) {
                    other->Update();
                }
                Frame();
                TellTheJudge();
            }
            return done();
        }

        /// The seat the game's client holds is the seat the judge compares its views with (a seat it chose is not in its `joined`).
        void TellTheJudge() {
            if (!_links.links.empty() && _game->GetOnline().Seat() >= 0) {
                _links.Last().judgeSeat = _game->GetOnline().Seat();
            }
        }

        /// Only the others read what came: the scene shows what it showed.
        void LetOthersRun(const std::vector<HubPlayer*>& others, const std::function<bool()>& done) {
            for (int count = 0; count < kPumpLimit && !done(); ++count) {
                _world.Advance(kTick);
                for (HubPlayer* other : others) {
                    other->Update();
                }
            }
        }

        void Join(HubPlayer& player, const std::string& code, const char* name, const std::vector<HubPlayer*>& others) {
            ASSERT_TRUE(player.Client().Join(kUrl, eastl::string(code.c_str()), name));
            ASSERT_TRUE(PumpUntil([&] { return player.Client().Stage() == OnlineStage::Lobby; }, others));
        }

        /// The name is typed on the pad page and the form is back.
        void TypeName(const char* name) {
            ASSERT_TRUE(ClickElement(kOnline, "OnlineName"));
            Draw();
            ASSERT_EQ(Screens().Page(), OnlinePage::Entry);
            for (int i = 0; i < 24; ++i) {
                EmitAction("MahjongOnlineBackspace");
            }
            for (const char* at = name; *at != 0; ++at) {
                const bool upper = *at >= 'A' && *at <= 'Z';
                PressKeyboard(static_cast<SDL_Keycode>(upper ? *at - 'A' + 'a' : *at), upper);
            }
            Act("MahjongOnlineDone");
            ASSERT_NE(Screens().Page(), OnlinePage::Entry);
        }

        /// The Join form: the code is typed on the keyboard of the pad page and Enter is clicked; the name stays the default one.
        void EnterByTheCode(const std::string& code) {
            ASSERT_NO_FATAL_FAILURE(OpenPanel());
            ASSERT_NO_FATAL_FAILURE(GoToTheJoinForm());
            ASSERT_TRUE(ClickElement(kOnline, "OnlineCode"));
            Draw();
            for (const char letter : code) {
                PressKeyboard(static_cast<SDL_Keycode>(letter >= 'A' && letter <= 'Z' ? letter - 'A' + 'a' : letter));
            }
            Act("MahjongOnlineDone");
            ASSERT_EQ(Var("onlineCode"), code);
            ASSERT_TRUE(ClickElement(kOnline, "OnlineEnter"));
        }

        std::string SeatText(size_t seat) const {
            return Var(Format("onlineSeat{}", seat).c_str());
        }

        std::string Notice() const {
            return ItemShown(kOnline, "OnlineNotice") ? Var("onlineNotice") : std::string();
        }

        /// The game object made again, as a restart of the program does: the scene stays, the game starts on the same folder and its client on a new transport.
        void RestartTheGame() {
            GameManager::GetInstance().Shutdown();
            _links.links.clear();
            GameManager::GetInstance().Initialize();
            _game = MahjongGame::GetActive();
            ASSERT_NE(_game, nullptr);
            ASSERT_TRUE(_game->SetOnlineSettings(_world.MakeClientSettings(_links)));
        }

        MahjongHubWorld _world;
        HubClientLinks _links;
        std::filesystem::path _folder;
    };

    TEST_F(MahjongOnlineRoomTest, AHostCreatesARoomThroughTheClicksThreePeopleJoinAndTheStartedGameIsShown) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheHostForm());
        ASSERT_NO_FATAL_FAILURE(TypeName("Anna"));
        Act("MahjongOnlineHands", "8");
        ASSERT_TRUE(ClickElement(kOnline, "OnlineCreate"));
        ASSERT_TRUE(PumpUntil([&] { return Screens().Page() == OnlinePage::Lobby && _game->GetOnline().Stage() == OnlineStage::Lobby; }));

        // The lobby: the code, the host on the first seat, the rest empty and free to take
        const std::string code = Var("onlineRoom");
        ASSERT_EQ(code.size(), kRoomCodeLength);
        EXPECT_EQ(code, std::string(_game->GetOnline().Room().c_str()));
        EXPECT_EQ(Text(kOnline, "OnlineTitleLobby"), "Room " + code);
        EXPECT_TRUE(ItemShown(kOnline, "OnlineTitleLobby"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineNameRow"));
        EXPECT_EQ(SeatText(0), "Anna (you)");
        for (size_t seat = 1; seat < 4; ++seat) {
            EXPECT_EQ(SeatText(seat), "Empty") << seat;
            EXPECT_TRUE(ItemShown(kOnline, Format("OnlineSit{}", seat).c_str())) << seat;
        }
        EXPECT_FALSE(ItemShown(kOnline, "OnlineSit0")) << "the own seat is not offered";
        EXPECT_TRUE(ItemShown(kOnline, "OnlineStart"));
        EXPECT_TRUE(ItemShown(kOnline, "OnlineLeave"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineWaiting"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineStarted"));
        EXPECT_TRUE(_game->StoredOnline().session.IsValid()) << "the session is kept as soon as there is one";

        // Two people join by the code: each takes the lowest free seat and the seat rows name them
        HubPlayer bo(_world);
        HubPlayer cy(_world);
        HubPlayer di(_world);
        ASSERT_NO_FATAL_FAILURE(Join(bo, code, "Bo", {&bo}));
        ASSERT_NO_FATAL_FAILURE(Join(cy, code, "Cy", {&bo, &cy}));
        ASSERT_TRUE(PumpUntil([&] { return SeatText(2) == "Cy"; }, {&bo, &cy}));
        EXPECT_EQ(SeatText(1), "Bo");
        EXPECT_EQ(SeatText(3), "Empty");
        EXPECT_FALSE(ItemShown(kOnline, "OnlineSit1"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineSit2"));
        EXPECT_TRUE(ItemShown(kOnline, "OnlineSit3"));

        // The seat click: the host moves to the free seat, and the seat it left is free
        ASSERT_TRUE(ClickElement(kOnline, "OnlineSit3"));
        ASSERT_TRUE(PumpUntil([&] { return SeatText(3) == "Anna (you)"; }, {&bo, &cy}));
        EXPECT_EQ(SeatText(0), "Empty");
        EXPECT_TRUE(ItemShown(kOnline, "OnlineSit0"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineSit3"));
        EXPECT_EQ(Notice(), "");

        // The last one fills the room: it takes the seat the host left
        ASSERT_NO_FATAL_FAILURE(Join(di, code, "Di", {&bo, &cy, &di}));
        ASSERT_TRUE(PumpUntil([&] { return SeatText(0) == "Di"; }, {&bo, &cy, &di}));
        EXPECT_EQ(SeatText(3), "Anna (you)");
        for (size_t seat = 0; seat < 4; ++seat) {
            EXPECT_FALSE(ItemShown(kOnline, Format("OnlineSit{}", seat).c_str())) << "the room is full";
        }

        // Start: the screen shows the started game, in both languages
        ASSERT_TRUE(ItemShown(kOnline, "OnlineStart"));
        ASSERT_TRUE(ClickElement(kOnline, "OnlineStart"));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Game; }, {&bo, &cy, &di}));
        EXPECT_EQ(Screens().Page(), OnlinePage::Lobby);
        EXPECT_TRUE(ItemShown(kOnline, "OnlineStarted"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineStart"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineWaiting"));
        EXPECT_EQ(Text(kOnline, "OnlineStarted"), "The game has started");
        EXPECT_TRUE(ItemShown(kOnline, "OnlineLeave"));
        ASSERT_TRUE(_game->SetLanguage("ru"_intern));
        Frame();
        EXPECT_EQ(Text(kOnline, "OnlineStarted"), "Игра началась");
        EXPECT_EQ(SeatText(3), "Anna (вы)");
        ASSERT_TRUE(_game->SetLanguage("en"_intern));
        Frame();
        EXPECT_EQ(_world.Judge().Offences().size(), 0u) << (_world.Judge().Offences().empty() ? "" : _world.Judge().Offences().front());
        EXPECT_EQ(_world.Judge().NonText(), 0u);
    }

    TEST_F(MahjongOnlineRoomTest, AGuestEntersByTheCodeSeesTheWaitingLineAndTheLobbyPassesTheSizeMatrixInBothLanguages) {
        HubPlayer host(_world);
        ASSERT_TRUE(host.Client().Host(kUrl, "Hanna", OnlineRoomOptions{kShortMatchHands, kMinLevel}));
        ASSERT_TRUE(PumpUntil([&] { return host.Client().Stage() == OnlineStage::Lobby; }, {&host}));
        const std::string code(host.Client().Room().c_str());

        ASSERT_NO_FATAL_FAILURE(EnterByTheCode(code));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Lobby; }, {&host}));
        ASSERT_TRUE(PumpUntil([&] { return SeatText(1) == "Player (you)"; }, {&host}));
        EXPECT_EQ(SeatText(0), "Hanna");
        EXPECT_TRUE(ItemShown(kOnline, "OnlineWaiting"));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineStart")) << "only the host starts";
        EXPECT_EQ(Text(kOnline, "OnlineWaiting"), "Waiting for the host");

        // A seat that was taken meanwhile: the screen has not heard of it yet, the server refuses, and the notice says so once
        HubPlayer cy(_world);
        ASSERT_NO_FATAL_FAILURE(Join(cy, code, "Cy", {&host, &cy}));
        ASSERT_TRUE(PumpUntil([&] { return SeatText(2) == "Cy"; }, {&host, &cy}));
        ASSERT_TRUE(ItemShown(kOnline, "OnlineSit3"));
        ASSERT_TRUE(cy.Client().ChooseSeat(3));
        cy.Sync();
        LetOthersRun({&host, &cy}, [&] { return host.Client().Lobby().seats[3].kind == BECore::SeatKind::Human; });
        ASSERT_TRUE(ItemShown(kOnline, "OnlineSit3")) << "the screen was not drawn since";
        ASSERT_TRUE(ClickElement(kOnline, "OnlineSit3"));
        ASSERT_TRUE(PumpUntil([&] { return !Notice().empty(); }, {&host, &cy}));
        EXPECT_EQ(Notice(), std::string(OnlineNoticeText(CoreManager::GetUIStrings(), OnlineErrorKey(RoomErrors::kSeatTaken)).c_str()));
        ASSERT_TRUE(PumpUntil([&] { return SeatText(3) == "Cy"; }, {&host, &cy}));
        EXPECT_EQ(SeatText(1), "Player (you)") << "the seat of the guest did not move";
        EXPECT_TRUE(ItemShown(kOnline, "OnlineSit2")) << "the seat Cy left is free";
        const std::string notice = Notice();
        for (int i = 0; i < 5; ++i) {
            Frame();
        }
        EXPECT_EQ(Notice(), notice) << "the notice stays until the player moves";
        ASSERT_TRUE(ClickElement(kOnline, "OnlineSit2"));
        ASSERT_TRUE(PumpUntil([&] { return SeatText(2) == "Player (you)"; }, {&host, &cy}));
        EXPECT_EQ(Notice(), "") << "the next move of the player clears it";

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            ASSERT_TRUE(_game->SetLanguage(language));
            Frame();
            ExpectTheCanvasFits(kOnline);
        });
    }

    TEST_F(MahjongOnlineRoomTest, AConnectionLostInTheLobbyShowsTheReconnectLineAndTheLobbyStaysAsItWas) {
        HubPlayer host(_world);
        ASSERT_TRUE(host.Client().Host(kUrl, "Hanna", OnlineRoomOptions{kShortMatchHands, kMinLevel}));
        ASSERT_TRUE(PumpUntil([&] { return host.Client().Stage() == OnlineStage::Lobby; }, {&host}));
        const std::string code(host.Client().Room().c_str());
        ASSERT_NO_FATAL_FAILURE(EnterByTheCode(code));
        ASSERT_TRUE(PumpUntil([&] { return Screens().Page() == OnlinePage::Lobby && SeatText(1) == "Player (you)"; }, {&host}));

        _world.SetRefusing(true);
        _world.Sever(_links.Last());
        ASSERT_TRUE(PumpUntil([&] { return ItemShown(kOnline, "OnlineStatus"); }, {&host}));
        EXPECT_EQ(Var("onlineStatus"), "The connection was lost. Reconnecting...");
        EXPECT_EQ(Screens().Page(), OnlinePage::Lobby);
        EXPECT_EQ(SeatText(0), "Hanna");
        EXPECT_EQ(SeatText(1), "Player (you)");
        EXPECT_FALSE(ItemShown(kOnline, "OnlineNotice")) << "a lost connection is a status, not an error";
        EXPECT_FALSE(ItemShown(kOnline, "OnlineWaiting"));
        EXPECT_TRUE(ItemShown(kOnline, "OnlineLeave"));
        for (const PoolString language : {PoolString("ru"_intern), PoolString("en"_intern)}) {
            ASSERT_TRUE(_game->SetLanguage(language));
            Frame();
            ExpectTheCanvasFits(kOnline);
        }
    }

    TEST_F(MahjongOnlineRoomTest, AStartedGameIsReturnedToAfterTheGameObjectIsMadeAgainAndARoomThatIsGoneSaysSoAndForgetsTheSession) {
        // A game of two people and a computer pair: the room outlives the host's connection
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheHostForm());
        ASSERT_NO_FATAL_FAILURE(TypeName("Anna"));
        ASSERT_TRUE(ClickElement(kOnline, "OnlineCreate"));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Lobby; }));
        const std::string code = Var("onlineRoom");
        HubPlayer bo(_world);
        ASSERT_NO_FATAL_FAILURE(Join(bo, code, "Bo", {&bo}));
        ASSERT_TRUE(ClickElement(kOnline, "OnlineStart"));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Game; }, {&bo}));
        const RoomSession kept = _game->GetOnline().SessionToKeep();
        ASSERT_TRUE(kept.IsValid());
        Frame();
        EXPECT_TRUE(_game->StoredOnline().session.IsValid());
        EXPECT_EQ(_game->StoredOnline().name, "Anna");
        EXPECT_TRUE(std::filesystem::exists(_folder / "online.save")) << "the slot is written by the flush of the frame";

        // The program ends, the host's connection with it; the room goes on with Bo in it
        ASSERT_NO_FATAL_FAILURE(RestartTheGame());
        EXPECT_EQ(_game->StoredOnline().name, "Anna");
        ASSERT_TRUE(_game->StoredOnline().session.IsValid());
        EXPECT_TRUE(_game->StoredOnline().session.token == kept.token) << "the token came back from the slot";

        // The first screen again: the Online button, the Menu offers Return to the room of the slot
        Draw();
        ASSERT_TRUE(_game->IsSetupOpen());
        ASSERT_TRUE(ClickElement("Setup", "SetupOnline"));
        Draw();
        ASSERT_EQ(Screens().Page(), OnlinePage::Menu);
        EXPECT_TRUE(ItemShown(kOnline, "OnlineMenuReturn"));
        EXPECT_EQ(Text(kOnline, "OnlineMenuReturn"), "Return to room " + code);
        ASSERT_TRUE(ClickElement(kOnline, "OnlineMenuReturn"));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Game; }, {&bo}));
        EXPECT_EQ(Screens().Page(), OnlinePage::Lobby);
        EXPECT_EQ(Var("onlineRoom"), code);
        EXPECT_TRUE(ItemShown(kOnline, "OnlineStarted"));
        EXPECT_EQ(SeatText(0), "Anna (you)");
        EXPECT_EQ(SeatText(1), "Bo");
        EXPECT_EQ(Notice(), "");
        EXPECT_EQ(_world.Judge().Offences().size(), 0u);

        // Leave: the session is forgotten and the slot goes with it
        ASSERT_TRUE(ClickElement(kOnline, "OnlineLeave"));
        ASSERT_TRUE(PumpUntil([&] { return Screens().Page() == OnlinePage::Menu; }, {&bo}));
        EXPECT_FALSE(ItemShown(kOnline, "OnlineMenuReturn"));
        EXPECT_FALSE(_game->StoredOnline().session.IsValid());
    }

    TEST_F(MahjongOnlineRoomTest, ARoomThatIsGoneIsReportedOnReturnAndTheStoredSessionIsForgotten) {
        ASSERT_NO_FATAL_FAILURE(OpenPanel());
        ASSERT_NO_FATAL_FAILURE(GoToTheHostForm());
        ASSERT_TRUE(ClickElement(kOnline, "OnlineCreate"));
        ASSERT_TRUE(PumpUntil([&] { return _game->GetOnline().Stage() == OnlineStage::Lobby; }));
        const std::string code = Var("onlineRoom");
        Frame();
        ASSERT_TRUE(_game->StoredOnline().session.IsValid());

        // Alone in a lobby: the room goes with the connection, the session stays in the slot
        _game->GetOnline().Disconnect();
        ASSERT_TRUE(PumpUntil([&] { return Screens().Page() == OnlinePage::Menu; }));
        ASSERT_TRUE(ItemShown(kOnline, "OnlineMenuReturn"));
        EXPECT_EQ(Text(kOnline, "OnlineMenuReturn"), "Return to room " + code);

        ASSERT_TRUE(ClickElement(kOnline, "OnlineMenuReturn"));
        ASSERT_TRUE(PumpUntil([&] { return !Notice().empty(); }));
        EXPECT_EQ(Notice(), std::string(OnlineNoticeText(CoreManager::GetUIStrings(), OnlineErrorKey(RoomClientCloseReasons::kRoomGone)).c_str()));
        EXPECT_EQ(Screens().Page(), OnlinePage::Menu);
        EXPECT_FALSE(ItemShown(kOnline, "OnlineMenuReturn")) << "there is no room to return to";
        EXPECT_FALSE(_game->StoredOnline().session.IsValid());
        EXPECT_FALSE(_game->GetOnline().SessionToKeep().IsValid());
        Frame();
        EXPECT_FALSE(std::filesystem::exists(_folder / "online.save")) << "no name and no session: the slot is gone";
    }

}  // namespace BECore::Mahjong::Tests
