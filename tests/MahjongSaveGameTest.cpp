#include "StringsTestSupport.h"

#include <BECore/Control/Json.h>
#include <BECore/GameManager/CoreManager.h>
#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/MahjongGame.h>
#include <fstream>
#include <gtest/gtest.h>
#include <sstream>

/**
 * `MahjongGame` saves and loads (`Save/MatchSave.h`, `Save/MatchSaveStore.h`): the match slot is written by `FlushSave()` and only
 * then, once for a burst of play; a second game that `Start()`s on the same folder has the saved match behind `Setup`
 * (`CanContinue()`, `ContinueMatch()`) in the state it was left in (mid-hand, a claim window, the score table, the cover for two
 * people); the match that `Start()` deals otherwise is not recorded and cannot overwrite the save; the choices of `Setup` come back;
 * a save that is broken, of another game or version, that does not replay or that is finished is not offered and is left in place until
 * a match begun by a person replaces it. The game is built directly (no scene) and played through its control adapter. Also the global
 * environment that switches saving off for every other test of the binary.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;
    namespace fs = std::filesystem;

    namespace {

        constexpr uint32_t kSeed = 5;
        constexpr int kSeedsToSearch = 60;
        constexpr int kStepsPerSeed = 300;
        constexpr int kPersonActions = 2;
        constexpr int kMatchHands = 8;
        constexpr const char* kFolderPrefix = "BagiMahjongSaveGameTest";
        constexpr const char* kMatchFile = "match.save";
        constexpr const char* kPrefsFile = "settings.save";

        /// Switches saving off for the whole run of the test binary: the games that other tests build must not write `saves/`.
        class SavingOffEnvironment final : public ::testing::Environment {
        public:
            void SetUp() override {
                _before = MatchSaveDirectory();
                SetMatchSaveDirectory(fs::path());
            }

            void TearDown() override {
                SetMatchSaveDirectory(_before);
            }

        private:
            fs::path _before;
        };

        [[maybe_unused]] ::testing::Environment* const kSavingOffEnvironment = ::testing::AddGlobalTestEnvironment(new SavingOffEnvironment);

        Json Parse(const char* text) {
            return ParseJson(text).value();
        }

        std::string ReadFile(const fs::path& path) {
            std::ifstream file(path, std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }

        std::string Str(const Json& json) {
            return json.is_string() ? json.get<std::string>() : std::string("<not a string>");
        }

        class MahjongSaveGameTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }

            void SetUp() override {
                EXPECT_TRUE(LoadShippedStrings(CoreManager::GetUIStrings()).empty());
                folder = TestTempDirectory(kFolderPrefix);
                fs::remove_all(folder);
                SetMatchSaveDirectory(folder);
            }

            void TearDown() override {
                SetMatchSaveDirectory(fs::path());
                std::error_code error;
                fs::remove_all(folder, error);
                CoreManager::GetUIStrings().Clear();
            }

            IntrusivePtr<MahjongGame> NewGame() const {
                return New<MahjongGame>();
            }

            /// The state a client sees, without the version (two games reach the same table by different numbers of steps).
            static Json State(MahjongGame& game) {
                auto state = game.AsControllable()->GetState();
                EXPECT_TRUE(state.has_value());
                Json json = state ? *state : Json();
                json.erase("version");
                return json;
            }

            static Json Actions(MahjongGame& game) {
                auto actions = game.AsControllable()->GetActions();
                EXPECT_TRUE(actions.has_value());
                return actions ? *actions : Json();
            }

            /// `reset` with options such as {"seed": 5, "seats": "PCCC", "hands": 8}.
            static void Reset(MahjongGame& game, const char* options) {
                const auto reset = game.AsControllable()->Reset(Parse(options));
                ASSERT_TRUE(reset.has_value()) << reset.error().message.c_str();
            }

            /// The person's first listed action, `count` times (fewer when nothing is listed any more).
            static void PlayFirstActions(MahjongGame& game, int count) {
                for (int played = 0; played < count; ++played) {
                    const Json actions = Actions(game);
                    if (actions.empty()) {
                        return;
                    }
                    ASSERT_TRUE(game.AsControllable()->PerformAction(actions[0]).has_value());
                }
            }

            /// A person at seat 0 against three computers in a fresh match, played until the person is asked in a claim window. False when
            /// no seed of the search gives one.
            static bool ResetToAClaimWindow(MahjongGame& game) {
                for (int seed = 1; seed <= kSeedsToSearch; ++seed) {
                    Reset(game, Format("{{\"seed\": {}, \"seats\": \"PCCC\", \"hands\": {}}}", seed, kMatchHands).c_str());
                    for (int step = 0; step < kStepsPerSeed; ++step) {
                        const Json state = State(game);
                        if (Str(state["phase"]) != "playing") {
                            break;
                        }
                        if (Str(state["turn"]["phase"]) == "claim" && state["turn"]["yourTurn"].get<bool>()) {
                            return true;
                        }
                        const Json actions = Actions(game);
                        if (actions.empty()) {
                            break;
                        }
                        Json chosen = actions[0];
                        for (const Json& action : actions) {
                            if (Str(action["type"]) == "pass") {
                                chosen = action;
                            }
                        }
                        if (!game.AsControllable()->PerformAction(chosen).has_value()) {
                            break;
                        }
                    }
                }
                return false;
            }

            /// Four computers: the hands play themselves, `next_hand` settles and plays the next.
            static void NextHand(MahjongGame& game) {
                ASSERT_TRUE(game.AsControllable()->PerformAction(Parse("{\"type\": \"next_hand\"}")).has_value());
            }

            /// A game that starts on the folder as the executable does.
            IntrusivePtr<MahjongGame> StartedGame() const {
                IntrusivePtr<MahjongGame> game = NewGame();
                game->Start();
                return game;
            }

            /// The match slot written with `events`, by a store of its own.
            void WriteMatch(const eastl::vector<MatchLogEvent>& events) const {
                MatchSaveStore store(CoreManager::GetFileSystem());
                ASSERT_TRUE(store.Open(folder));
                ASSERT_TRUE(store.SaveMatch(events));
            }

            eastl::vector<MatchLogEvent> ReadMatch() const {
                MatchSaveStore store(CoreManager::GetFileSystem());
                EXPECT_TRUE(store.Open(folder));
                auto events = store.LoadMatch();
                EXPECT_TRUE(events.has_value());
                return events ? *events : eastl::vector<MatchLogEvent>();
            }

            fs::path Slot(const char* name) const {
                return folder / name;
            }

            /// A save that is not used: a random match is dealt, the game is playable and the file stays as it is until a match replaces it.
            void ExpectNotOffered(const char* what) const {
                const std::string before = ReadFile(Slot(kMatchFile));
                IntrusivePtr<MahjongGame> game = StartedGame();
                EXPECT_FALSE(game->CanContinue()) << what;
                EXPECT_TRUE(game->IsSetupOpen()) << what;
                EXPECT_EQ(game->GetSession().GetPhase(), SessionPhase::Playing) << what;
                game->GetSession().RunComputers();  // and the table plays
                EXPECT_NE(game->GetSession().GetPhase(), SessionPhase::MatchOver) << what;
                game->FlushSave();
                EXPECT_EQ(ReadFile(Slot(kMatchFile)), before) << what;  // the match behind the panel is not recorded
                ASSERT_TRUE(game->StartRandomMatch().has_value()) << what;
                game->FlushSave();
                EXPECT_NE(ReadFile(Slot(kMatchFile)), before) << what;  // a match begun by a person replaces it
                EXPECT_TRUE(StartedGame()->CanContinue()) << what;
            }

            fs::path folder;
        };

    }  // namespace

    TEST(MahjongSaveEnvironmentTest, SavingIsOffForTestsThatDoNotSwitchItOn) {
        EXPECT_TRUE(MatchSaveDirectory().empty());
    }

    TEST_F(MahjongSaveGameTest, TheSlotIsWrittenByFlushOnceForABurstOfPlay) {
        IntrusivePtr<MahjongGame> game = NewGame();
        Reset(*game, "{\"seed\": 3, \"seats\": \"CCCC\", \"hands\": 8}");  // a whole hand of computers is thousands of events
        EXPECT_FALSE(fs::exists(Slot(kMatchFile)));
        game->FlushSave();
        ASSERT_TRUE(fs::exists(Slot(kMatchFile)));

        fs::remove(Slot(kMatchFile));
        game->FlushSave();
        EXPECT_FALSE(fs::exists(Slot(kMatchFile))) << "nothing changed, so nothing is written";

        NextHand(*game);
        game->FlushSave();
        EXPECT_TRUE(fs::exists(Slot(kMatchFile)));

        fs::remove(Slot(kMatchFile));
        NextHand(*game);
        game->Stop();  // the game ends: whatever changed is written
        EXPECT_TRUE(fs::exists(Slot(kMatchFile)));
    }

    TEST_F(MahjongSaveGameTest, AMatchInTheMiddleOfAHandIsTheTableBehindTheSeatChoice) {
        IntrusivePtr<MahjongGame> game = NewGame();
        Reset(*game, "{\"seed\": 5, \"seats\": \"PCCC\", \"hands\": 8, \"level\": 1}");
        PlayFirstActions(*game, kPersonActions);
        game->FlushSave();

        IntrusivePtr<MahjongGame> second = StartedGame();
        EXPECT_TRUE(second->CanContinue());
        EXPECT_TRUE(second->IsSetupOpen());
        EXPECT_EQ(State(*second), State(*game));
        EXPECT_EQ(Actions(*second), Actions(*game));
        EXPECT_EQ(second->GetLevel(), ComputerLevel::Easy);
        EXPECT_EQ(second->GetSettings().handsPerMatch, kShortMatchHands);

        const uint64_t version = second->GetSession().Version();
        EXPECT_TRUE(second->ContinueMatch());
        EXPECT_FALSE(second->IsSetupOpen());
        EXPECT_FALSE(second->CanContinue());
        EXPECT_FALSE(second->ContinueMatch());
        EXPECT_EQ(second->GetSession().Version(), version);  // continuing plays nothing by itself
        PlayFirstActions(*second, 1);                        // and the match goes on legally
    }

    TEST_F(MahjongSaveGameTest, ALevelPerSeatIsKeptByTheMatchContinueAndThePrefs) {
        const SeatLevels mixed{ComputerLevel{2}, ComputerLevel{6}, ComputerLevel::Easy, ComputerLevel{10}};
        IntrusivePtr<MahjongGame> game = NewGame();
        game->SetSeats(SeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer});
        game->SetLevels(mixed);
        MahjongSettings settings;
        settings.handsPerMatch = kShortMatchHands;
        game->SetSettings(settings);
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        PlayFirstActions(*game, kPersonActions);
        // The panel's choices move on after the match began: the resumed match's own levels win
        game->SetLevels(UniformLevels(ComputerLevel::Normal));
        game->FlushSave();

        IntrusivePtr<MahjongGame> second = StartedGame();
        ASSERT_TRUE(second->CanContinue());
        EXPECT_EQ(second->GetSession().Levels(), mixed);
        EXPECT_EQ(second->GetLevels(), mixed) << "the game's levels become those of the resumed Start";
        EXPECT_EQ(State(*second), State(*game));
        EXPECT_TRUE(second->ContinueMatch());
        PlayFirstActions(*second, 1);
    }

    TEST_F(MahjongSaveGameTest, TheLevelsOfAMatchOfPeopleAreKeptByContinueToo) {
        const SeatLevels mixed{ComputerLevel{3}, ComputerLevel{4}, ComputerLevel{7}, ComputerLevel{9}};
        IntrusivePtr<MahjongGame> game = NewGame();
        game->SetSeats(SeatSetup{SeatKind::Person, SeatKind::Person, SeatKind::Person, SeatKind::Person});
        game->SetLevels(mixed);
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        game->FlushSave();

        IntrusivePtr<MahjongGame> second = StartedGame();
        ASSERT_TRUE(second->CanContinue());
        EXPECT_EQ(second->GetLevels(), mixed) << "a person's seat turns computer again at the level it had";
    }

    TEST_F(MahjongSaveGameTest, ThePrefsKeepALevelPerSeatAndAnOldSlotGivesOneLevel) {
        const SeatLevels mixed{ComputerLevel{1}, ComputerLevel{2}, ComputerLevel{6}, ComputerLevel{10}};
        IntrusivePtr<MahjongGame> game = NewGame();
        game->SetLevels(mixed);
        game->FlushSave();
        EXPECT_EQ(StartedGame()->GetLevels(), mixed) << "after a restart";

        // A slot of the old kind: `level` only
        MatchSaveStore store(CoreManager::GetFileSystem());
        ASSERT_TRUE(store.Open(folder));
        MahjongPrefsSave old;
        old.level = "3";
        ASSERT_TRUE(store.SavePrefs(old));
        EXPECT_EQ(StartedGame()->GetLevels(), UniformLevels(ComputerLevel{3}));
    }

    TEST_F(MahjongSaveGameTest, BrokenLevelsInThePrefsApplyNothing) {
        MatchSaveStore store(CoreManager::GetFileSystem());
        ASSERT_TRUE(store.Open(folder));
        for (const char* bad : {"5,5,5", "5,5,5,11", "a"}) {
            MahjongPrefsSave prefs = MakePrefs("ru", false, kDefaultSeatSetup, kShortMatchHands, ComputerLevel::Easy);
            prefs.levels = bad;
            ASSERT_TRUE(store.SavePrefs(prefs));
            IntrusivePtr<MahjongGame> game = StartedGame();
            EXPECT_EQ(game->GetLevels(), UniformLevels(kDefaultLevel)) << bad;
            EXPECT_TRUE(game->HintsOn()) << bad;
            EXPECT_EQ(game->GetSettings().handsPerMatch, kFullMatchHands) << bad;
            EXPECT_EQ(game->GetLanguage(), PoolString::Intern(kDefaultLanguage)) << bad;
        }
    }

    TEST_F(MahjongSaveGameTest, TheSeatsLevelAndHandsOfAResumedMatchAreWhatTheGameHasNext) {
        IntrusivePtr<MahjongGame> game = NewGame();
        Reset(*game, "{\"seed\": 5, \"seats\": \"PCCC\", \"hands\": 8, \"level\": 1}");
        // The choices of the game moved on after the match began (a client set them): the prefs slot says so
        game->SetSeats(SeatSetup{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Person});
        game->SetLevel(ComputerLevel::Normal);
        MahjongSettings settings = game->GetSettings();
        settings.handsPerMatch = kFullMatchHands;
        game->SetSettings(settings);
        game->FlushSave();

        IntrusivePtr<MahjongGame> second = StartedGame();
        ASSERT_TRUE(second->CanContinue());
        EXPECT_EQ(second->GetSeats(), (SeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer})) << "the panel shows the match behind it";
        EXPECT_EQ(second->GetLevel(), ComputerLevel::Easy);
        EXPECT_EQ(second->GetSettings().handsPerMatch, kShortMatchHands);
        EXPECT_EQ(second->GetSession().Level(), ComputerLevel::Easy);
    }

    TEST_F(MahjongSaveGameTest, AClaimWindowIsResumedAsItWas) {
        IntrusivePtr<MahjongGame> game = NewGame();
        ASSERT_TRUE(ResetToAClaimWindow(*game));
        game->FlushSave();

        IntrusivePtr<MahjongGame> second = StartedGame();
        ASSERT_TRUE(second->CanContinue());
        const Json state = State(*second);
        EXPECT_EQ(state, State(*game));
        EXPECT_EQ(Str(state["turn"]["phase"]), "claim");
        EXPECT_EQ(Actions(*second), Actions(*game));
        ASSERT_TRUE(second->AsControllable()->PerformAction(Parse("{\"type\": \"pass\"}")).has_value());
    }

    TEST_F(MahjongSaveGameTest, TheScoreTableIsResumedBeforeNextHandAndTheLastHandDeletesTheSlot) {
        IntrusivePtr<MahjongGame> game = NewGame();
        Reset(*game, "{\"seed\": 3, \"seats\": \"CCCC\", \"hands\": 8}");
        ASSERT_EQ(Str(State(*game)["phase"]), "hand_over");
        game->FlushSave();

        IntrusivePtr<MahjongGame> second = StartedGame();
        ASSERT_TRUE(second->CanContinue());
        EXPECT_EQ(State(*second), State(*game));
        EXPECT_EQ(second->GetSession().GetPhase(), SessionPhase::HandOver);
        NextHand(*second);  // and Next hand still works

        for (int hand = 0; hand < kMatchHands && Str(State(*game)["phase"]) != "match_over"; ++hand) {
            game->FlushSave();
            EXPECT_TRUE(fs::exists(Slot(kMatchFile))) << hand;
            NextHand(*game);
        }
        ASSERT_EQ(Str(State(*game)["phase"]), "match_over");
        game->FlushSave();
        EXPECT_FALSE(fs::exists(Slot(kMatchFile)));
        EXPECT_FALSE(StartedGame()->CanContinue());
    }

    TEST_F(MahjongSaveGameTest, WithTwoPeopleTheCoverIsUpOnTheResumedTable) {
        IntrusivePtr<MahjongGame> two = NewGame();
        Reset(*two, "{\"seed\": 5, \"seats\": \"PPCC\", \"hands\": 8}");
        two->FlushSave();
        IntrusivePtr<MahjongGame> resumedTwo = StartedGame();
        ASSERT_TRUE(resumedTwo->CanContinue());
        EXPECT_TRUE(resumedTwo->GetSession().HandoffPending());
        resumedTwo->GetSession().Reveal();
        EXPECT_FALSE(resumedTwo->GetSession().HandoffPending());

        IntrusivePtr<MahjongGame> one = NewGame();
        Reset(*one, "{\"seed\": 5, \"seats\": \"PCCC\", \"hands\": 8}");
        one->FlushSave();
        IntrusivePtr<MahjongGame> resumedOne = StartedGame();
        ASSERT_TRUE(resumedOne->CanContinue());
        EXPECT_FALSE(resumedOne->GetSession().HandoffPending());
    }

    TEST_F(MahjongSaveGameTest, TheStepsOfAComputerAtAPaceAreSavedLikeTheControlPath) {
        IntrusivePtr<MahjongGame> game = NewGame();
        game->SetSeats(SeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer});
        MahjongSettings settings;
        settings.handsPerMatch = kShortMatchHands;
        game->SetSettings(settings);
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        TableSession& session = game->GetSession();
        for (int steps = 0; steps < 10 && session.GetPhase() == SessionPhase::Playing && !session.ActorIsPerson(); ++steps) {
            ASSERT_TRUE(session.StepComputer().has_value());
            game->FlushSave();  // a frame at the pace of the table
        }

        IntrusivePtr<MahjongGame> second = StartedGame();
        ASSERT_TRUE(second->CanContinue());
        EXPECT_EQ(State(*second), State(*game));
        EXPECT_EQ(second->GetMatch().Hand().Moves().size(), game->GetMatch().Hand().Moves().size());
    }

    TEST_F(MahjongSaveGameTest, StartDoesNotOverwriteTheSavedMatchButTheMatchOfAPersonDoes) {
        IntrusivePtr<MahjongGame> game = NewGame();
        Reset(*game, "{\"seed\": 5, \"seats\": \"PCCC\", \"hands\": 8}");
        PlayFirstActions(*game, kPersonActions);
        game->FlushSave();
        const std::string saved = ReadFile(Slot(kMatchFile));

        IntrusivePtr<MahjongGame> second = StartedGame();  // deals a background match only if nothing is saved: here it resumes
        second->FlushSave();
        EXPECT_EQ(ReadFile(Slot(kMatchFile)), saved) << "a resumed match is already in the slot";
        IntrusivePtr<MahjongGame> third = StartedGame();
        EXPECT_TRUE(third->CanContinue());
        EXPECT_EQ(ReadFile(Slot(kMatchFile)), saved);

        // The panel's Start: a match of a person, written at the next flush
        ASSERT_TRUE(third->StartRandomMatch().has_value());
        EXPECT_FALSE(third->CanContinue());
        third->FlushSave();
        EXPECT_NE(ReadFile(Slot(kMatchFile)), saved);

        // Reset() records too
        fs::remove(Slot(kMatchFile));
        IntrusivePtr<MahjongGame> fourth = NewGame();
        fourth->Reset();
        fourth->FlushSave();
        EXPECT_TRUE(fs::exists(Slot(kMatchFile)));
    }

    TEST_F(MahjongSaveGameTest, TheBackgroundMatchOfStartIsNotRecordedWhenNothingIsSaved) {
        IntrusivePtr<MahjongGame> game = StartedGame();
        EXPECT_FALSE(game->CanContinue());
        game->FlushSave();
        EXPECT_FALSE(fs::exists(Slot(kMatchFile)));
        ASSERT_TRUE(game->StartMatch(kSeed).has_value());
        game->FlushSave();
        EXPECT_TRUE(fs::exists(Slot(kMatchFile)));
    }

    TEST_F(MahjongSaveGameTest, TheChoicesOfSetupAreTheDefaultsOfTheNextGame) {
        IntrusivePtr<MahjongGame> game = NewGame();
        ASSERT_TRUE(game->SetLanguage("ru"_intern));
        game->SetHintsOn(false);
        game->SetSeats(SeatSetup{SeatKind::Person, SeatKind::Person, SeatKind::Computer, SeatKind::Computer});
        MahjongSettings settings;
        settings.handsPerMatch = kShortMatchHands;
        settings.flowers = true;  // a rule that only the control reset sets: not a choice of Setup
        settings.limit = 300;
        game->SetSettings(settings);
        game->SetLevel(ComputerLevel::Easy);
        game->SetAnimationSpeed(AnimationSpeed::Slow);
        game->FlushSave();
        CoreManager::GetUIStrings().SetLanguage("en"_intern);

        IntrusivePtr<MahjongGame> second = StartedGame();
        EXPECT_EQ(second->GetLanguage(), "ru"_intern);
        EXPECT_FALSE(second->HintsOn());
        EXPECT_EQ(second->GetSeats(), game->GetSeats());
        EXPECT_EQ(second->GetSettings().handsPerMatch, kShortMatchHands);
        EXPECT_FALSE(second->GetSettings().flowers);
        EXPECT_EQ(second->GetSettings().limit, MahjongSettings{}.limit);
        EXPECT_EQ(second->GetLevel(), ComputerLevel::Easy);
        EXPECT_EQ(second->GetAnimationSpeed(), AnimationSpeed::Slow);
        EXPECT_FALSE(fs::exists(Slot(kMatchFile)));
    }

    TEST_F(MahjongSaveGameTest, APrefsSlotThatCannotBeUsedLeavesTheDefaults) {
        MatchSaveStore store(CoreManager::GetFileSystem());
        ASSERT_TRUE(store.Open(folder));

        // A language the table does not have is ignored, the rest is applied
        MahjongPrefsSave unknown = MakePrefs("xx", false, kDefaultSeatSetup, kShortMatchHands, ComputerLevel::Easy);
        ASSERT_TRUE(store.SavePrefs(unknown));
        IntrusivePtr<MahjongGame> game = StartedGame();
        EXPECT_EQ(game->GetLanguage(), PoolString::Intern(kDefaultLanguage));
        EXPECT_FALSE(game->HintsOn());
        EXPECT_EQ(game->GetSettings().handsPerMatch, kShortMatchHands);
        EXPECT_EQ(game->GetLevel(), ComputerLevel::Easy);

        // Hands that are not 16 or 8 are refused: the game keeps 16
        MahjongPrefsSave odd = unknown;
        odd.hands = 12;
        ASSERT_TRUE(store.SavePrefs(odd));
        IntrusivePtr<MahjongGame> kept = StartedGame();
        EXPECT_EQ(kept->GetSettings().handsPerMatch, kFullMatchHands);
        EXPECT_EQ(kept->GetSession().GetPhase(), SessionPhase::Playing);
    }

    TEST_F(MahjongSaveGameTest, ASaveThatIsBrokenOrOfAnotherGameOrVersionIsNotOffered) {
        IntrusivePtr<MahjongGame> game = NewGame();
        Reset(*game, "{\"seed\": 3, \"seats\": \"CCCC\", \"hands\": 8}");
        NextHand(*game);  // a settlement is in the log
        game->FlushSave();
        const eastl::vector<MatchLogEvent> good = ReadMatch();
        ASSERT_GT(good.size(), 2u);

        {
            std::ofstream(Slot(kMatchFile), std::ios::binary) << "this is not a save";
            ExpectNotOffered("garbage");
        }
        {
            MahjongMatchSave state;
            state.log = MatchSaveText(good);
            SaveGameService service(CoreManager::GetFileSystem());
            ASSERT_TRUE(service.Initialize(folder));
            ASSERT_TRUE(service.Save(kMatchSlot, SaveSchema{"Chess", kMahjongSaveVersion, kMahjongSaveMinVersion}, state).has_value());
            ExpectNotOffered("another game");
            ASSERT_TRUE(service.Save(kMatchSlot, SaveSchema{kMahjongSaveGame, kMahjongSaveVersion + 1, kMahjongSaveMinVersion}, state).has_value());
            ExpectNotOffered("a newer version");
        }
        {
            eastl::vector<MatchLogEvent> wrongSeat = good;
            for (MatchLogEvent& event : wrongSeat) {
                if (event.kind == MatchLogKind::Action) {
                    event.seat = NextSeat(event.seat);
                    break;
                }
            }
            WriteMatch(wrongSeat);
            ExpectNotOffered("an action of the wrong seat");
        }
        {
            eastl::vector<MatchLogEvent> paid = good;
            for (MatchLogEvent& event : paid) {
                if (event.kind == MatchLogKind::Settlement) {
                    event.settlement.deltas[0] += 10;
                }
            }
            WriteMatch(paid);
            ExpectNotOffered("a settlement that pays otherwise");
        }
        {
            WriteMatch(eastl::vector<MatchLogEvent>(good.begin() + 1, good.end()));
            ExpectNotOffered("a first line that is not a start");
        }
    }

    TEST_F(MahjongSaveGameTest, AFinishedMatchInTheSlotIsNotOffered) {
        // The log of a whole match of four computers, as the recorder would keep it before its End
        Match match;
        TableSession session(match);
        MatchRecorder recorder;
        session.SetLogSink(&recorder);
        MahjongSettings settings;
        settings.handsPerMatch = kShortMatchHands;
        ASSERT_TRUE(session.StartMatch(settings, kSeed, SeatSetup{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer}).has_value());
        while (session.GetPhase() != SessionPhase::MatchOver) {
            session.RunComputers();
            ASSERT_TRUE(session.NextHand().has_value());
        }
        ASSERT_TRUE(recorder.Finished());
        WriteMatch(recorder.Events());
        ExpectNotOffered("a finished match");
    }

}  // namespace BECore::Mahjong::Tests
