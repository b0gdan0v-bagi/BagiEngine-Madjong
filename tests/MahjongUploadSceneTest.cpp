#include "MahjongOnlineTableTestSupport.h"
#include "MahjongTouchTestSupport.h"

#include <Mahjong/Components/MahjongUploadComponent.h>
#include <Mahjong/MatchLogJson.h>
#include <Mahjong/UploadPrefs.h>
#include <algorithm>
#include <fstream>

/**
 * The glue of the tester upload in the shipped scene (`Components/MahjongUploadComponent.h`, the node `Consent`): where the page cannot post
 * nothing is done; where it can, the question is up exactly while the consent is `Unasked`, the answer is kept in a file and applied at once,
 * and the matches played afterwards reach the poster as pieces that assemble and replay. The poster is a fake that answers 200 to everything,
 * the folder of `upload.txt` a temp folder.
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

}  // namespace BECore::Mahjong::Tests
