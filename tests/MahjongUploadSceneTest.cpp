#include "MahjongButtonSupport.h"
#include "MahjongOnlineTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <CoreSDL/SDLEvents.h>
#include <Mahjong/Components/MahjongUploadComponent.h>
#include <Mahjong/MatchLogJson.h>
#include <Mahjong/Online/TextEntry.h>
#include <Mahjong/UploadPrefs.h>

/**
 * The glue of the tester upload in the shipped scene (`Components/MahjongUploadComponent.h`, the node `Consent`): where the page cannot post
 * nothing is done; where it can, the question is up exactly while the consent is `Unasked`, the answer is kept in a file and applied at once,
 * and the matches played afterwards reach the poster as pieces that assemble and replay. The poster is a fake that answers 200 to everything,
 * the folder of `upload.txt` a temp folder. The second half is the page `Options`: the rows of the upload (shown only where the page can post)
 * and the pad of the nickname, which is the pad of the online panel.
 */
namespace BECore::Mahjong::Tests {

    namespace fs = std::filesystem;

    namespace {

        constexpr const char* kConsent = "Consent";
        constexpr const char* kAccept = "ConsentAccept";
        constexpr const char* kDecline = "ConsentDecline";
        constexpr const char* kText = "ConsentText";
        constexpr const char* kFolderStem = "bagi-mahjong-upload-scene-prefs";
        constexpr const char* kNickname = "Anna";
        constexpr float kFrameStep = 1.0f / 60.0f;
        constexpr int kPumpFrames = 400;
        constexpr float kButtonAir = 8.0f;
        constexpr float kMinControlHeight = 48.0f;
        constexpr int kMostHands = 200;
        constexpr int kRoomVersions = 10;
        constexpr const char* kOptions = "Options";
        constexpr const char* kEntry = "OnlineEntry";
        constexpr const char* kSend = "UploadSend";
        constexpr const char* kKeep = "UploadKeep";
        constexpr const char* kNicknameField = "NicknameField";
        constexpr const char* kEntryDone = "EntryDone";
        constexpr const char* kUploadRows[] = {"UploadRow", "NicknameRow", "NicknameNote"};
        constexpr const char* kUploadControls[] = {"UploadSend", "UploadKeep", "NicknameField"};
        constexpr float kDesignPlateHeight = 376.0f;

        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}, {1707.0f, 480.0f}};

        /// The page: answers 200 to every post the moment it is asked, and keeps what was posted
        class AnsweringPoster final : public IWebPoster {
        public:
            struct Post {
                std::string path;
                std::string body;
            };

            [[nodiscard]] bool Available() const override {
                return available;
            }
            std::expected<WebPostId, eastl::string> Start(eastl::string_view path, eastl::string_view body) override {
                posts.push_back({std::string(path.data(), path.size()), std::string(body.data(), body.size())});
                return static_cast<WebPostId>(posts.size());
            }
            WebPostResult Poll(WebPostId /*id*/) override {
                return {WebPostStatus::Done, 200};
            }

            /// The bodies in the order they were posted, put together
            [[nodiscard]] eastl::string Assembled() const {
                eastl::string all;
                for (const Post& post : posts) {
                    all.append(post.body.data(), post.body.size());
                }
                return all;
            }

            bool available = true;
            std::vector<Post> posts;
        };

        class KeptSink final : public IMatchUploadSink {
        public:
            void Send(const UploadPiece& piece) override {
                pieces.push_back(piece);
            }
            std::vector<UploadPiece> pieces;
        };

        std::string ReadFile(const fs::path& path) {
            std::ifstream file(path, std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }

    }  // namespace

    class MahjongUploadSceneTest : public MahjongOnlineTableTestBase {
    protected:
        void SetUp() override {
            _prefsFolder = BECore::Tests::TestTempDirectory(kFolderStem);
            fs::remove_all(_prefsFolder);
            fs::create_directories(_prefsFolder);
            MahjongOnlineTableTestBase::SetUp();
            MahjongUploadComponent::Overrides& overrides = MahjongUploadComponent::TestOverrides();
            overrides.poster = &_poster;
            overrides.folder = _prefsFolder;
            overrides.remembered = true;
        }

        void TearDown() override {
            MahjongUploadComponent::TestOverrides() = {};
            MahjongOnlineTableTestBase::TearDown();
            std::error_code error;
            fs::remove_all(_prefsFolder, error);
        }

        MahjongUploadComponent& Component() const {
            MahjongUploadComponent* component = MahjongUploadComponent::GetActive();
            EXPECT_NE(component, nullptr);
            return *component;
        }

        void Frames(int count = 2) {
            for (int i = 0; i < count; ++i) {
                AdvanceTime(kFrameStep);
                Draw();
            }
        }

        bool ConsentShown() const {
            const auto canvas = Canvas(kConsent);
            return canvas && canvas->TakesInput();
        }

        fs::path PrefsFile() const {
            return _prefsFolder / "upload.txt";
        }

        UploadPrefs Stored() const {
            return LoadUploadPrefs(_prefsFolder);
        }

        /// The page up, the question showing
        void ShowQuestion() {
            Frames();
            ASSERT_TRUE(Component().IsRunning());
            ASSERT_TRUE(ConsentShown());
        }

        void Answer(const char* button) {
            ASSERT_TRUE(ClickElement(kConsent, button));
            Frames();
        }

        /// A match of four computers, played to its end through the session (no frame between, so the table does not step anything)
        void PlayMatch() {
            MahjongSettings settings = _game->GetSettings();
            settings.handsPerMatch = kShortMatchHands;
            _game->SetSettings(settings);
            ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
            TableSession& session = _game->GetSession();
            for (int hands = 0; session.GetPhase() != SessionPhase::MatchOver && hands < kMostHands; ++hands) {
                session.RunComputers();
                if (session.GetPhase() == SessionPhase::HandOver) {
                    ASSERT_TRUE(session.NextHand().has_value());
                }
            }
            ASSERT_EQ(session.GetPhase(), SessionPhase::MatchOver);
        }

        /// Frames until the sink has nothing queued and no post in flight
        void Drain() {
            for (int i = 0; i < kPumpFrames && (Component().Sink().QueuedPieces() > 0 || Component().Sink().IsPosting()); ++i) {
                Frames(1);
            }
            ASSERT_EQ(Component().Sink().QueuedPieces(), 0U);
        }

        void ExpectPostedMatchReplays() const {
            ASSERT_FALSE(_poster.posts.empty());
            for (size_t i = 0; i < _poster.posts.size(); ++i) {
                const std::string& path = _poster.posts[i].path;
                EXPECT_EQ(path.rfind("/matches/", 0), 0U) << path;
                EXPECT_EQ(path.substr(path.rfind('/') + 1), std::to_string(i)) << "the pieces go in order";
            }
            const eastl::string all = _poster.Assembled();
            const auto events = ParseMatchLog(std::string_view(all.c_str(), all.size()));
            ASSERT_TRUE(events.has_value()) << events.error().c_str();
            Match replayed;
            const ReplayStatus status = ReplayMatchLog(*events, replayed);
            EXPECT_TRUE(status.has_value());
        }

        /// The question answered with Send and the page `Options` up over the table, drawn
        void OpenOptionsGranted() {
            ASSERT_NO_FATAL_FAILURE(ShowQuestion());
            ASSERT_NO_FATAL_FAILURE(Answer(kAccept));
            OpenOptionsPage();
        }

        void OpenOptionsPage() {
            _game->CloseSetup();
            _game->OpenOptions();
            Frames();
            ASSERT_TRUE(OptionsShown());
        }

        bool OptionsShown() const {
            const auto canvas = Canvas(kOptions);
            return canvas && canvas->TakesInput();
        }

        bool EntryShown() const {
            const auto canvas = Canvas(kEntry);
            return canvas && canvas->TakesInput();
        }

        bool Selected(const char* segment) const {
            const UIElement* element = Element(kOptions, segment);
            return element != nullptr && element->selected;
        }

        /// The pad key whose letter is @p letter (the pad is the name pad of the language); the index, else -1
        int PadIndexOf(const char* letter) const {
            for (int key = 0; key < static_cast<int>(TextEntry::kPadMaxKeys); ++key) {
                if (Var(("padKey" + std::to_string(key)).c_str()) == letter) {
                    return key;
                }
            }
            return -1;
        }

        void TypeKey(const char* letter) {
            const int index = PadIndexOf(letter);
            ASSERT_GE(index, 0) << letter;
            EmitAction("MahjongOnlineKey", std::to_string(index).c_str());
            Frames(1);
        }

        void PressKeyboard(SDL_Keycode key, bool shift = false) {
            SDLEvents::KeyDownEvent::Emit(key, shift ? SDL_KMOD_LSHIFT : SDL_KMOD_NONE, false);
            Frames(1);
        }

        void OpenPad() {
            ASSERT_TRUE(ClickElement(kOptions, kNicknameField));
            Frames();
            ASSERT_TRUE(EntryShown());
        }

        AnsweringPoster _poster;
        KeptSink _kept;  // outlives the game that is stopped in `TearDown`
        fs::path _prefsFolder;
    };

    TEST_F(MahjongUploadSceneTest, WhereThePageCannotPostNothingIsDoneNoScreenNoFileNoRecording) {
        _poster.available = false;
        Frames();
        EXPECT_FALSE(Component().IsRunning());
        EXPECT_FALSE(Component().IsConsentShown());
        EXPECT_FALSE(ConsentShown());
        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        Frames();
        EXPECT_FALSE(_game->GetUpload().IsRecording());
        EXPECT_EQ(_game->GetUpload().Consent(), UploadConsent::Unasked);
        EXPECT_EQ(_game->GetUploadPrefs(), UploadPrefs());
        EmitAction("MahjongUploadAccept");  // a stale click finds nothing
        Frames();
        EXPECT_FALSE(fs::exists(PrefsFile()));
        EXPECT_TRUE(_poster.posts.empty());
        EXPECT_EQ(_game->GetUpload().Consent(), UploadConsent::Unasked);
    }

    TEST_F(MahjongUploadSceneTest, TheRealPosterOfADesktopBuildIsNotAvailableEither) {
        MahjongUploadComponent::TestOverrides().poster = nullptr;
        Frames();
        EXPECT_FALSE(Component().IsRunning());
        EXPECT_FALSE(ConsentShown());
        EXPECT_FALSE(fs::exists(PrefsFile()));
    }

    TEST_F(MahjongUploadSceneTest, TheQuestionIsUpWhileTheConsentIsUnaskedAndNothingIsSentEvenWhenAMatchIsPlayed) {
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        EXPECT_EQ(_game->GetUpload().Consent(), UploadConsent::Unasked);
        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        Frames(5);
        EXPECT_FALSE(_game->GetUpload().IsRecording());
        EXPECT_TRUE(_poster.posts.empty());
        EXPECT_FALSE(fs::exists(PrefsFile())) << "nothing is stored before the answer";
        EXPECT_TRUE(ConsentShown());
    }

    TEST_F(MahjongUploadSceneTest, AcceptStoresTheChoiceClosesTheQuestionAndTheNextMatchIsPostedAsPiecesThatReplay) {
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        ASSERT_NO_FATAL_FAILURE(Answer(kAccept));
        EXPECT_FALSE(ConsentShown());
        EXPECT_EQ(Stored().consent, UploadConsent::Granted);
        EXPECT_EQ(_game->GetUpload().Consent(), UploadConsent::Granted);
        EXPECT_EQ(_game->GetUploadPrefs().consent, UploadConsent::Granted);

        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        ASSERT_NO_FATAL_FAILURE(Drain());
        ExpectPostedMatchReplays();
        EXPECT_GE(_poster.posts.size(), 2U) << "a piece after each hand";
        EXPECT_EQ(Component().Sink().State(), WebUploadState::Active);
        EXPECT_EQ(Component().Sink().Counters().sent, _poster.posts.size());
    }

    TEST_F(MahjongUploadSceneTest, DeclineStoresTheChoiceAndNothingIsPosted) {
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        ASSERT_NO_FATAL_FAILURE(Answer(kDecline));
        EXPECT_FALSE(ConsentShown());
        EXPECT_EQ(Stored().consent, UploadConsent::Declined);
        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        Frames(5);
        EXPECT_FALSE(_game->GetUpload().IsRecording());
        EXPECT_TRUE(_poster.posts.empty());
    }

    TEST_F(MahjongUploadSceneTest, AnAnswerHonoursOnlyTheQuestionThatIsUp) {
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        ASSERT_NO_FATAL_FAILURE(Answer(kAccept));
        EmitAction("MahjongUploadDecline");  // arrives after the question is gone
        Frames();
        EXPECT_EQ(Stored().consent, UploadConsent::Granted);
        EXPECT_EQ(_game->GetUpload().Consent(), UploadConsent::Granted);
    }

    TEST_F(MahjongUploadSceneTest, AChoiceInTheFileIsReadAtTheFirstFrameAndTheQuestionDoesNotComeBack) {
        UploadPrefs kept;
        kept.consent = UploadConsent::Granted;
        kept.nickname = kNickname;
        ASSERT_TRUE(StoreUploadPrefs(_prefsFolder, kept));
        Frames();
        ASSERT_TRUE(Component().IsRunning());
        EXPECT_FALSE(ConsentShown());
        EXPECT_EQ(_game->GetUploadPrefs(), kept);
        EXPECT_EQ(_game->GetUpload().Consent(), UploadConsent::Granted);
        EXPECT_EQ(_game->GetLocalSession().LogOrigin().nickname, eastl::string(kNickname));

        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        ASSERT_NO_FATAL_FAILURE(Drain());
        ExpectPostedMatchReplays();
        const std::string& first = _poster.posts.front().body;
        EXPECT_NE(first.find("\"nickname\":\"Anna\""), std::string::npos) << first;
    }

    TEST_F(MahjongUploadSceneTest, ADeclinedChoiceInTheFileIsReadAndNothingIsAsked) {
        UploadPrefs kept;
        kept.consent = UploadConsent::Declined;
        ASSERT_TRUE(StoreUploadPrefs(_prefsFolder, kept));
        Frames();
        EXPECT_FALSE(ConsentShown());
        EXPECT_EQ(_game->GetUpload().Consent(), UploadConsent::Declined);
    }

    TEST_F(MahjongUploadSceneTest, ANicknameIsInTheOriginOnlyAfterTheTesterSaidSend) {
        UploadPrefs typed;
        typed.nickname = kNickname;  // typed before, the consent still open
        ASSERT_TRUE(StoreUploadPrefs(_prefsFolder, typed));
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        EXPECT_EQ(_game->GetUploadPrefs().nickname, eastl::string(kNickname));
        EXPECT_TRUE(_game->GetLocalSession().LogOrigin().nickname.empty());
        ASSERT_NO_FATAL_FAILURE(Answer(kAccept));
        EXPECT_EQ(_game->GetLocalSession().LogOrigin().nickname, eastl::string(kNickname));
        EXPECT_EQ(Stored().nickname, eastl::string(kNickname));

        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        ASSERT_NO_FATAL_FAILURE(Drain());
        ASSERT_FALSE(_poster.posts.empty());
        EXPECT_NE(_poster.posts.front().body.find("\"nickname\":\"Anna\""), std::string::npos);
    }

    TEST_F(MahjongUploadSceneTest, DeclineKeepsTheNicknameOutOfEveryLog) {
        UploadPrefs typed;
        typed.nickname = kNickname;
        ASSERT_TRUE(StoreUploadPrefs(_prefsFolder, typed));
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        ASSERT_NO_FATAL_FAILURE(Answer(kDecline));
        EXPECT_TRUE(_game->GetLocalSession().LogOrigin().nickname.empty());
        EXPECT_EQ(Stored().nickname, eastl::string(kNickname)) << "kept for later, not sent";
    }

    TEST_F(MahjongUploadSceneTest, TheChoiceThatIsNotRememberedIsSaidInEveryPiece) {
        MahjongUploadComponent::TestOverrides().remembered = false;
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        ASSERT_NO_FATAL_FAILURE(Answer(kAccept));
        _game->GetUpload().SetSink(&_kept);  // takes the pieces in place of the component's sink: the flag is the uploader's
        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        ASSERT_FALSE(_kept.pieces.empty());
        for (const UploadPiece& piece : _kept.pieces) {
            EXPECT_FALSE(piece.consentRemembered);
        }
    }

    TEST_F(MahjongUploadSceneTest, TheChoiceThatIsRememberedIsSaidInEveryPiece) {
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        ASSERT_NO_FATAL_FAILURE(Answer(kAccept));
        _game->GetUpload().SetSink(&_kept);
        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        ASSERT_FALSE(_kept.pieces.empty());
        for (const UploadPiece& piece : _kept.pieces) {
            EXPECT_TRUE(piece.consentRemembered);
        }
    }

    TEST_F(MahjongUploadSceneTest, TheTableOfARoomPostsNothing) {
        UploadPrefs kept;
        kept.consent = UploadConsent::Granted;
        ASSERT_TRUE(StoreUploadPrefs(_prefsFolder, kept));
        Frames();
        ASSERT_TRUE(Component().IsRunning());
        ASSERT_NO_FATAL_FAILURE(StartRoom());
        ASSERT_TRUE(PlayUntil([&] { return Table().version >= kRoomVersions; }));
        Frames();
        EXPECT_TRUE(_game->IsOnlineTable());
        EXPECT_TRUE(_poster.posts.empty());
        EXPECT_EQ(Component().Sink().QueuedPieces(), 0U);
    }

    TEST_F(MahjongUploadSceneTest, AMatchGivenUpWhenTheSceneGoesIsPostedAtOnceAsItsLastPiece) {
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        ASSERT_NO_FATAL_FAILURE(Answer(kAccept));
        MahjongSettings settings = _game->GetSettings();
        settings.handsPerMatch = kShortMatchHands;
        _game->SetSettings(settings);
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        ASSERT_TRUE(_game->GetUpload().IsRecording());
        Frames();
        EXPECT_TRUE(_poster.posts.empty()) << "a piece is cut after a hand, none has ended";

        // What the application does at its end: the game stops (the match is given up), then the scene goes
        GameManager::GetInstance().Shutdown();
        EngineEnvironment::UseSceneManager({});
        _manager.Reset();
        ASSERT_EQ(_poster.posts.size(), 1U);
        EXPECT_EQ(_poster.posts.front().path.substr(_poster.posts.front().path.rfind('/')), "/0");
        const std::string& body = _poster.posts.front().body;
        EXPECT_NE(body.find("\"start\""), std::string::npos) << body.substr(0, 120);
    }

    TEST_F(MahjongUploadSceneTest, TheQuestionSaysTheWordsOfTheOwnerInBothLanguagesAndPassesTheSizeMatrixAtTheTouchSize) {
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const UIStrings& strings = CoreManager::GetUIStrings();
            Frames();
            EXPECT_EQ(DisplayText(Element(kConsent, kText)), Tr(strings, "upload.consent.text").c_str());
            EXPECT_EQ(DisplayText(Element(kConsent, kAccept)), Tr(strings, "upload.consent.accept").c_str());
            EXPECT_EQ(DisplayText(Element(kConsent, kDecline)), Tr(strings, "upload.consent.decline").c_str());
            Canvas(kConsent)->UpdateLayout(kViewWidth, kViewHeight);
            for (const char* name : {kAccept, kDecline}) {
                const UIElement* control = Element(kConsent, name);
                ASSERT_NE(control, nullptr) << name;
                EXPECT_GE(control->rect.h, kMinControlHeight) << name;
            }
            ExpectPassesTheMatrix(kConsent, EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit(kConsent, output.width, output.height, kButtonAir);
            }
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas(kConsent), kButtonAir);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
    }

    TEST_F(MahjongUploadSceneTest, TheOwnersWordsAreTheTextsOfTheKeys) {
        // The words are the owner's, verbatim (2026-11-12)
        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("en"_intern));
        const UIStrings& strings = CoreManager::GetUIStrings();
        EXPECT_EQ(Tr(strings, "upload.consent.text"),
                  "Send your finished matches to the author? They help improve the computer players. Only the moves and the score are sent, no name or address. You can change this in Options.");
        EXPECT_EQ(Tr(strings, "upload.consent.accept"), "Send");
        EXPECT_EQ(Tr(strings, "upload.consent.decline"), "Don't send");
        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        EXPECT_EQ(Tr(strings, "upload.consent.text"),
                  "Отправлять сыгранные матчи автору? Они нужны, чтобы улучшать компьютерных игроков. Уходят только ходы и счёт — без имени и адреса. Поменять можно в настройках.");
        EXPECT_EQ(Tr(strings, "upload.consent.accept"), "Отправлять");
        EXPECT_EQ(Tr(strings, "upload.consent.decline"), "Не отправлять");
    }

    // ----- the rows of the page Options -----

    TEST_F(MahjongUploadSceneTest, WhereThePageCannotPostTheOptionsPageIsWhatItWas) {
        _poster.available = false;
        Frames();
        ASSERT_NO_FATAL_FAILURE(OpenOptionsPage());
        EXPECT_NEAR(Element(kOptions, "Plate")->rect.h, kDesignPlateHeight, 0.01f);
        for (const char* row : kUploadRows) {
            EXPECT_TRUE(Element(kOptions, row)->rect.IsEmpty()) << row;
        }
        EmitAction("MahjongSetUpload", "on");
        EmitAction("MahjongNickname");
        Frames();
        EXPECT_FALSE(EntryShown());
        EXPECT_EQ(_game->GetUploadPrefs(), UploadPrefs());
        EXPECT_FALSE(fs::exists(PrefsFile()));
    }

    TEST_F(MahjongUploadSceneTest, WherePostingIsAvailableTheRowsAreOnThePageAndTheSegmentedFollowsTheConsent) {
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        _game->CloseSetup();
        _game->OpenOptions();
        Frames();
        EXPECT_FALSE(Selected(kSend));
        EXPECT_FALSE(Selected(kKeep)) << "neither is selected while the question is up";
        EXPECT_TRUE(ConsentShown());
        EmitAction("MahjongSetUpload", "on");  // under the question: stale
        Frames();
        EXPECT_EQ(_game->GetUploadPrefs().consent, UploadConsent::Unasked);

        ASSERT_NO_FATAL_FAILURE(Answer(kAccept));
        ASSERT_TRUE(OptionsShown());
        for (const char* row : kUploadRows) {
            EXPECT_FALSE(Element(kOptions, row)->rect.IsEmpty()) << row;
        }
        EXPECT_GT(Element(kOptions, "Plate")->rect.h, kDesignPlateHeight) << "the plate grew by the rows";
        EXPECT_TRUE(Selected(kSend));
        EXPECT_FALSE(Selected(kKeep));
        ASSERT_TRUE(ClickElement(kOptions, kKeep));
        Frames();
        EXPECT_EQ(Stored().consent, UploadConsent::Declined);
        EXPECT_EQ(_game->GetUpload().Consent(), UploadConsent::Declined);
        EXPECT_TRUE(Selected(kKeep));
        EXPECT_FALSE(Selected(kSend));
        ASSERT_TRUE(ClickElement(kOptions, kSend));
        Frames();
        EXPECT_EQ(Stored().consent, UploadConsent::Granted);
        EXPECT_EQ(_game->GetUpload().Consent(), UploadConsent::Granted);
        EXPECT_FALSE(ConsentShown());
    }

    TEST_F(MahjongUploadSceneTest, DontSendAfterAGrantStopsTheNextMatchAndDropsARunningOneWithNoAbandonedPiece) {
        ASSERT_NO_FATAL_FAILURE(OpenOptionsGranted());
        _game->CloseOptions();
        MahjongSettings settings = _game->GetSettings();
        settings.handsPerMatch = kShortMatchHands;
        _game->SetSettings(settings);
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        ASSERT_TRUE(_game->GetUpload().IsRecording());
        _game->OpenOptions();
        Frames();
        ASSERT_TRUE(ClickElement(kOptions, kKeep));
        Frames();
        EXPECT_FALSE(_game->GetUpload().IsRecording());
        _game->CloseOptions();
        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        Frames(5);
        EXPECT_TRUE(_poster.posts.empty()) << "the running match was dropped with no piece, and the next one is not recorded";
        EXPECT_EQ(Component().Sink().QueuedPieces(), 0U);
    }

    TEST_F(MahjongUploadSceneTest, TheNicknameFieldShowsNoneInGreyThenTheNameAndTheNoteSaysWhatItIsFor) {
        ASSERT_NO_FATAL_FAILURE(OpenOptionsGranted());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const UIStrings& strings = CoreManager::GetUIStrings();
            Frames();
            EXPECT_EQ(DisplayText(Element(kOptions, kNicknameField)), Tr(strings, "options.nicknameNone").c_str());
            EXPECT_TRUE(SameColor(Element(kOptions, kNicknameField)->GetComponent<UITextComponent>()->textColor, PaletteColor("textMuted")));
            EXPECT_EQ(DisplayText(Element(kOptions, "NicknameNote")), Tr(strings, "options.nicknameNote").c_str());
            EXPECT_EQ(DisplayText(Element(kOptions, "NicknameCaption")), Tr(strings, "options.nickname").c_str());
            EXPECT_EQ(DisplayText(Element(kOptions, "UploadCaption")), Tr(strings, "options.upload").c_str());
            EXPECT_EQ(DisplayText(Element(kOptions, kSend)), Tr(strings, "upload.consent.accept").c_str());
            EXPECT_EQ(DisplayText(Element(kOptions, kKeep)), Tr(strings, "upload.consent.decline").c_str());
        });
        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("en"_intern));
        EXPECT_EQ(Tr(CoreManager::GetUIStrings(), "options.nicknameNote"), "Sent with your matches");
        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("ru"_intern));
        EXPECT_EQ(Tr(CoreManager::GetUIStrings(), "options.nicknameNote"), "Уходит вместе с матчами");
        ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("en"_intern));

        UploadPrefs typed = _game->GetUploadPrefs();
        typed.nickname = kNickname;
        _game->GetUploadPrefs() = typed;
        Frames();
        EXPECT_EQ(DisplayText(Element(kOptions, kNicknameField)), kNickname);
        EXPECT_TRUE(SameColor(Element(kOptions, kNicknameField)->GetComponent<UITextComponent>()->textColor, PaletteColor("text")));
    }

    TEST_F(MahjongUploadSceneTest, TheNicknameTypedOnThePadLandsInTheFileAndInTheStartOfTheNextPostedMatch) {
        ASSERT_NO_FATAL_FAILURE(OpenOptionsGranted());
        ASSERT_NO_FATAL_FAILURE(OpenPad());
        EXPECT_TRUE(OptionsShown()) << "the pad lies over the page";
        EXPECT_FALSE(Canvas("Online")->TakesInput());
        EXPECT_FALSE(Element(kEntry, "EntryCaptionNickname")->rect.IsEmpty());
        EXPECT_TRUE(Element(kEntry, "EntryCaptionName")->rect.IsEmpty());
        ASSERT_NO_FATAL_FAILURE(TypeKey("B"));
        ASSERT_NO_FATAL_FAILURE(TypeKey("O"));
        EXPECT_EQ(Var("onlineEntryValue"), "BO");
        EmitAction("MahjongOnlineBackspace");
        Frames(1);
        EXPECT_EQ(Var("onlineEntryValue"), "B");
        EXPECT_EQ(Stored().nickname, eastl::string()) << "stored when the pad is done";
        ASSERT_NO_FATAL_FAILURE(PressKeyboard(SDLK_A, true));
        ASSERT_NO_FATAL_FAILURE(PressKeyboard(SDLK_N));
        EXPECT_EQ(Var("onlineEntryValue"), "BAn");
        ASSERT_TRUE(ClickElement(kEntry, kEntryDone));
        Frames();
        EXPECT_FALSE(EntryShown());
        EXPECT_TRUE(OptionsShown());
        EXPECT_FALSE(Canvas("Online")->TakesInput());
        EXPECT_EQ(Stored().nickname, eastl::string("BAn"));
        EXPECT_EQ(_game->GetUploadPrefs().nickname, eastl::string("BAn"));
        EXPECT_EQ(DisplayText(Element(kOptions, kNicknameField)), "BAn");
        EXPECT_EQ(_game->GetLocalSession().LogOrigin().nickname, eastl::string("BAn"));

        _game->CloseOptions();
        ASSERT_NO_FATAL_FAILURE(PlayMatch());
        ASSERT_NO_FATAL_FAILURE(Drain());
        ExpectPostedMatchReplays();
        EXPECT_NE(_poster.posts.front().body.find("\"nickname\":\"BAn\""), std::string::npos);
    }

    TEST_F(MahjongUploadSceneTest, EscapeAndEnterOnThePadStoreTheNicknameAndGoBackToTheOptionsPage) {
        ASSERT_NO_FATAL_FAILURE(OpenOptionsGranted());
        ASSERT_NO_FATAL_FAILURE(OpenPad());
        ASSERT_NO_FATAL_FAILURE(PressKeyboard(SDLK_X));
        ASSERT_NO_FATAL_FAILURE(PressKeyboard(SDLK_ESCAPE));
        EXPECT_FALSE(EntryShown());
        EXPECT_TRUE(OptionsShown()) << "the Escape of the pad is not the page's";
        EXPECT_TRUE(_game->IsOptionsOpen());
        EXPECT_EQ(Stored().nickname, eastl::string("x"));

        ASSERT_NO_FATAL_FAILURE(OpenPad());
        EXPECT_EQ(Var("onlineEntryValue"), "x") << "the pad starts on the nickname";
        ASSERT_NO_FATAL_FAILURE(PressKeyboard(SDLK_Y));
        ASSERT_NO_FATAL_FAILURE(PressKeyboard(SDLK_RETURN));
        EXPECT_FALSE(EntryShown());
        EXPECT_TRUE(OptionsShown());
        EXPECT_EQ(Stored().nickname, eastl::string("xy"));

        PressKeyboard(SDLK_ESCAPE);  // with the pad down this one is the page's
        EXPECT_FALSE(OptionsShown());
    }

    TEST_F(MahjongUploadSceneTest, ANicknameTypedWhileTheConsentIsDeclinedIsKeptAndGoesIntoNoLog) {
        ASSERT_NO_FATAL_FAILURE(ShowQuestion());
        ASSERT_NO_FATAL_FAILURE(Answer(kDecline));
        OpenOptionsPage();
        ASSERT_NO_FATAL_FAILURE(OpenPad());
        ASSERT_NO_FATAL_FAILURE(TypeKey("Z"));
        ASSERT_TRUE(ClickElement(kEntry, kEntryDone));
        Frames();
        EXPECT_EQ(Stored().nickname, eastl::string("Z"));
        EXPECT_EQ(Stored().consent, UploadConsent::Declined);
        EXPECT_TRUE(_game->GetLocalSession().LogOrigin().nickname.empty());
        ASSERT_TRUE(ClickElement(kOptions, kSend));
        Frames();
        EXPECT_EQ(_game->GetLocalSession().LogOrigin().nickname, eastl::string("Z"));
    }

    TEST_F(MahjongUploadSceneTest, ThePadForTheNicknameGoesWhenThePageIsClosedUnderIt) {
        ASSERT_NO_FATAL_FAILURE(OpenOptionsGranted());
        ASSERT_NO_FATAL_FAILURE(OpenPad());
        ASSERT_NO_FATAL_FAILURE(TypeKey("Q"));
        _game->CloseSetup();  // a control action or a new match closes every page
        Frames();
        EXPECT_FALSE(EntryShown());
        EXPECT_FALSE(OptionsShown());
        EXPECT_TRUE(Stored().nickname.empty()) << "nothing was confirmed";
    }

    TEST_F(MahjongUploadSceneTest, TheOptionsPageWithTheRowsPassesTheSizeMatrixAtTheTouchSizeInBothLanguages) {
        ASSERT_NO_FATAL_FAILURE(OpenOptionsGranted());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Frames();
            Canvas(kOptions)->UpdateLayout(kViewWidth, kViewHeight);
            for (const char* name : kUploadControls) {
                const UIElement* control = Element(kOptions, name);
                ASSERT_NE(control, nullptr) << name;
                EXPECT_GE(control->rect.h, kMinControlHeight) << name;
            }
            ExpectPassesTheMatrix(kOptions, EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit(kOptions, output.width, output.height, kButtonAir);
            }
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas(kOptions), kButtonAir);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
    }

    TEST_F(MahjongUploadSceneTest, TheRowsAndTheNoteLieInThePlateOneUnderTheOtherAndEveryControlIsATouchTargetInEveryWindow) {
        ASSERT_NO_FATAL_FAILURE(OpenOptionsGranted());
        auto page = Canvas(kOptions);
        const float minTouch = EngineTouchMinimum();
        const char* bands[] = {"Title", "LanguageRow", "HintsRow", "SpeedRow", "UploadRow", "NicknameRow", "NicknameNote", "ButtonRow"};
        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            for (const CanvasScaleMode mode : modes) {
                for (const float userScale : {1.0f, 1.5f}) {
                    const std::string label = Format("{} {} x{}", window.label.c_str(), EnumUtils<CanvasScaleMode>::ToString(mode).data(), userScale).c_str();
                    const UICanvasResolved resolved = LayOutInWindow(*page, window, mode, userScale);
                    const UIElement* plate = Element(kOptions, "Plate");
                    ASSERT_NE(plate, nullptr);
                    const Rect& safe = resolved.contentBounds;
                    EXPECT_GE(plate->rect.y, safe.y - 0.01f) << label;
                    EXPECT_LE(plate->rect.y + plate->rect.h, safe.y + safe.h + 0.01f) << label;
                    const UIElement* previous = nullptr;
                    for (const char* name : bands) {
                        const UIElement* band = Element(kOptions, name);
                        ASSERT_NE(band, nullptr) << name;
                        if (band->rect.IsEmpty()) {
                            EXPECT_TRUE(std::string(name) == "Title" || std::string(name) == "NicknameNote") << label << ": " << name << " may give way, no row may";
                            continue;
                        }
                        EXPECT_GE(band->rect.y, plate->rect.y - 0.01f) << label << " " << name;
                        EXPECT_LE(band->rect.y + band->rect.h, plate->rect.y + plate->rect.h + 0.01f) << label << " " << name;
                        if (previous != nullptr) {
                            EXPECT_GE(band->rect.y, previous->rect.y + previous->rect.h - 0.01f) << label << " " << name << " overlaps the one above";
                        }
                        previous = band;
                    }
                    for (const char* name : kUploadControls) {
                        const UIElement* control = Element(kOptions, name);
                        ASSERT_NE(control, nullptr) << name;
                        ASSERT_FALSE(control->rect.IsEmpty()) << label << " " << name << ": a button is never hidden";
                        EXPECT_GE(std::min(control->rect.w, control->rect.h) * resolved.scale / window.pixelDensity, minTouch - 0.01f) << label << " " << name;
                    }
                }
            }
        });
    }

    TEST_F(MahjongUploadSceneTest, ThePadForTheNicknameSaysItsCaptionInBothLanguagesAndFitsTheMatrix) {
        ASSERT_NO_FATAL_FAILURE(OpenOptionsGranted());
        ASSERT_NO_FATAL_FAILURE(OpenPad());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Frames();
            EXPECT_EQ(DisplayText(Element(kEntry, "EntryCaptionNickname")), Tr(CoreManager::GetUIStrings(), "upload.field.nickname").c_str());
            ExpectPassesTheMatrix(kEntry, EngineTouchMinimum());
            for (const Output& output : kOutputs) {
                ExpectWordsFit(kEntry, output.width, output.height, kButtonAir);
            }
        });
    }

    TEST_F(MahjongUploadSceneTest, TheOnlinePadStillHasItsOwnCaptionsAfterTheNicknamePadWasUsed) {
        ASSERT_NO_FATAL_FAILURE(OpenOptionsGranted());
        ASSERT_NO_FATAL_FAILURE(OpenPad());
        ASSERT_TRUE(ClickElement(kEntry, kEntryDone));
        Frames();
        ASSERT_FALSE(EntryShown());
        _game->CloseOptions();
        _game->OpenSetup();
        _game->OpenOnline();
        Frames();
        EmitAction("MahjongOnlineHost");
        EmitAction("MahjongOnlineField", "name");  // the name field of the room's form opens its pad
        Frames();
        ASSERT_TRUE(EntryShown());
        EXPECT_FALSE(Element(kEntry, "EntryCaptionName")->rect.IsEmpty());
        EXPECT_TRUE(Element(kEntry, "EntryCaptionNickname")->rect.IsEmpty());
    }

}  // namespace BECore::Mahjong::Tests
