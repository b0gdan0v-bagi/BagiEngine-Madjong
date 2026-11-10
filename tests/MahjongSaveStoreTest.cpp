#include <Common/TempPath.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Save/MatchSaveStore.h>
#include <gtest/gtest.h>

/**
 * The two slots of Mahjong over a `SaveGameService` (`Save/MatchSaveStore.h`): a played match and the prefs round trip, a missing slot
 * is `Missing`, and every way a file can be wrong (garbage, another game, a newer version, a state whose text is broken) is `Broken`
 * with a sentence and nothing thrown. A local `FileSystem` and a temporary folder per test: no engine services, no game.
 */
namespace BECore::Mahjong::Tests {

    namespace fs = std::filesystem;

    namespace {

        constexpr uint32_t kSeed = 9;
        constexpr const char* kFolderPrefix = "BagiMahjongSaveStoreTest";
        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

        /// The saved events of a short stretch of a match: the start, a whole hand of computers, its settlement and the first moves of the next.
        eastl::vector<MatchLogEvent> PlayedEvents() {
            Match match;
            TableSession session(match);
            MatchRecorder recorder;
            session.SetLogSink(&recorder);
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            EXPECT_TRUE(session.StartMatch(settings, kSeed, kAllComputers, ComputerLevel::Easy).has_value());
            session.RunComputers();
            EXPECT_TRUE(session.NextHand().has_value());
            for (int step = 0; step < 5; ++step) {
                EXPECT_TRUE(session.StepComputer().has_value());
            }
            return recorder.Events();
        }

        MahjongPrefsSave SomePrefs() {
            return MakePrefs("ru", false, SeatSetup{SeatKind::Person, SeatKind::Person, SeatKind::Computer, SeatKind::Computer}, kShortMatchHands, ComputerLevel::Easy);
        }

        class MatchSaveStoreTest : public ::testing::Test {
        protected:
            void SetUp() override {
                _base = BECore::Tests::TestTempDirectory(kFolderPrefix);
                fs::remove_all(_base);
                fs::create_directories(_base);
                folder = _base / "saves";
                store = std::make_unique<MatchSaveStore>(_fileSystem);
            }

            void TearDown() override {
                store.reset();
                std::error_code error;
                fs::remove_all(_base, error);
            }

            /// A service on the same folder, to write what the store itself would not.
            void Put(const SaveSchema& schema, const MahjongMatchSave& state) {
                SaveGameService service(_fileSystem);
                ASSERT_TRUE(service.Initialize(folder));
                ASSERT_TRUE(service.Save(kMatchSlot, schema, state).has_value());
            }

            void PutBytes(const char* name, const std::string& bytes) const {
                fs::create_directories(folder);
                std::ofstream(folder / name, std::ios::binary) << bytes;
            }

            /// The problem of loading the match slot: `Broken` with a sentence.
            void ExpectBroken() const {
                const auto loaded = store->LoadMatch();
                ASSERT_FALSE(loaded.has_value());
                EXPECT_EQ(loaded.error().kind, SaveProblemKind::Broken);
                EXPECT_FALSE(loaded.error().detail.empty());
            }

            fs::path folder;
            std::unique_ptr<MatchSaveStore> store;

        private:
            fs::path _base;
            FileSystem _fileSystem;
        };

    }  // namespace

    TEST_F(MatchSaveStoreTest, AMatchSurvivesTheRoundTrip) {
        const eastl::vector<MatchLogEvent> events = PlayedEvents();
        ASSERT_TRUE(store->Open(folder));
        EXPECT_TRUE(store->IsOn());
        ASSERT_TRUE(store->SaveMatch(events));
        EXPECT_TRUE(fs::exists(folder / "match.save"));

        const auto loaded = store->LoadMatch();
        ASSERT_TRUE(loaded.has_value());
        ASSERT_EQ(loaded->size(), events.size());
        for (size_t index = 0; index < events.size(); ++index) {
            EXPECT_EQ((*loaded)[index].kind, events[index].kind) << index;
        }
        EXPECT_EQ(loaded->front().seed, kSeed);
        EXPECT_EQ(loaded->front().level, ComputerLevel::Easy);

        store->ClearMatch();
        EXPECT_FALSE(fs::exists(folder / "match.save"));
        const auto gone = store->LoadMatch();
        ASSERT_FALSE(gone.has_value());
        EXPECT_EQ(gone.error().kind, SaveProblemKind::Missing);
    }

    TEST_F(MatchSaveStoreTest, ThePrefsSurviveTheRoundTripAndAreASlotOfTheirOwn) {
        ASSERT_TRUE(store->Open(folder));
        const auto missing = store->LoadPrefs();
        ASSERT_FALSE(missing.has_value());
        EXPECT_EQ(missing.error().kind, SaveProblemKind::Missing);

        ASSERT_TRUE(store->SavePrefs(SomePrefs()));
        const auto loaded = store->LoadPrefs();
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(*loaded, SomePrefs());
        EXPECT_FALSE(store->LoadMatch().has_value());  // the match slot is not the prefs slot
        EXPECT_TRUE(fs::exists(folder / "settings.save"));
    }

    TEST_F(MatchSaveStoreTest, ASlotThatIsNotThereIsMissing) {
        ASSERT_TRUE(store->Open(folder));
        const auto loaded = store->LoadMatch();
        ASSERT_FALSE(loaded.has_value());
        EXPECT_EQ(loaded.error().kind, SaveProblemKind::Missing);
    }

    TEST_F(MatchSaveStoreTest, EveryWayAFileCanBeWrongIsBrokenWithASentence) {
        ASSERT_TRUE(store->Open(folder));
        PutBytes("match.save", "this is not a save file at all");
        ExpectBroken();

        MahjongMatchSave good;
        good.log = MatchSaveText(PlayedEvents());
        Put(SaveSchema{"Chess", kMahjongSaveVersion, kMahjongSaveMinVersion}, good);  // another game
        ExpectBroken();
        Put(SaveSchema{kMahjongSaveGame, kMahjongSaveVersion + 1, kMahjongSaveMinVersion}, good);  // a newer version
        ExpectBroken();

        MahjongMatchSave brokenText;
        brokenText.log = "{\"event\":\"start\"";  // the state is there, its text is not
        Put(kMahjongSaveSchema, brokenText);
        ExpectBroken();
        brokenText.log = "";
        Put(kMahjongSaveSchema, brokenText);
        ExpectBroken();

        PutBytes("settings.save", "garbage");
        const auto prefs = store->LoadPrefs();
        ASSERT_FALSE(prefs.has_value());
        EXPECT_EQ(prefs.error().kind, SaveProblemKind::Broken);

        Put(kMahjongSaveSchema, good);  // and a good file after all of that is read again
        EXPECT_TRUE(store->LoadMatch().has_value());
    }

    TEST_F(MatchSaveStoreTest, ALeftoverTemporaryFileIsNotASave) {
        ASSERT_TRUE(store->Open(folder));
        PutBytes("match.save.tmp", "half a write");
        const auto loaded = store->LoadMatch();
        ASSERT_FALSE(loaded.has_value());
        EXPECT_EQ(loaded.error().kind, SaveProblemKind::Missing);
        EXPECT_TRUE(store->SaveMatch(PlayedEvents()));
        EXPECT_TRUE(store->LoadMatch().has_value());
    }

    TEST_F(MatchSaveStoreTest, WithAnEmptyFolderSavingIsOffAndEveryCallDoesNothing) {
        EXPECT_FALSE(store->Open(fs::path()));
        EXPECT_FALSE(store->IsOn());
        EXPECT_FALSE(store->SaveMatch(PlayedEvents()));
        EXPECT_FALSE(store->SavePrefs(SomePrefs()));
        store->ClearMatch();
        ASSERT_FALSE(store->LoadMatch().has_value());
        EXPECT_EQ(store->LoadMatch().error().kind, SaveProblemKind::Missing);
        ASSERT_FALSE(store->LoadPrefs().has_value());
        EXPECT_FALSE(fs::exists(folder));
    }

    TEST_F(MatchSaveStoreTest, AFolderThatIsAFileSwitchesSavingOffWithoutFailing) {
        fs::create_directories(folder.parent_path());
        std::ofstream(folder) << "in the way";
        EXPECT_FALSE(store->Open(folder));
        EXPECT_FALSE(store->Open(folder));  // asked again: still off, nothing thrown
        EXPECT_FALSE(store->IsOn());
        EXPECT_FALSE(store->SaveMatch(PlayedEvents()));
        EXPECT_FALSE(store->LoadMatch().has_value());
        EXPECT_TRUE(store->Open(folder.parent_path() / "other"));  // another folder works
        EXPECT_TRUE(store->SaveMatch(PlayedEvents()));
    }

    TEST(MatchSaveDirectoryTest, TheDefaultFolderIsSavesOrTheEnvironmentAndNoneInTheBrowser) {
        EXPECT_EQ(DefaultMatchSaveDirectory(false, nullptr), fs::path("saves"));
        EXPECT_EQ(DefaultMatchSaveDirectory(false, "elsewhere"), fs::path("elsewhere"));
        EXPECT_TRUE(DefaultMatchSaveDirectory(false, "").empty()) << "an empty value switches saving off";
        EXPECT_TRUE(DefaultMatchSaveDirectory(true, nullptr).empty());
        EXPECT_TRUE(DefaultMatchSaveDirectory(true, "elsewhere").empty()) << "the browser has no files to keep, whatever the environment says";
    }

    TEST(MatchSaveDirectoryTest, TheProcessWideFolderCanBeChangedAndSwitchedOff) {
        const fs::path before = MatchSaveDirectory();
        SetMatchSaveDirectory("somewhere");
        EXPECT_EQ(MatchSaveDirectory(), fs::path("somewhere"));
        SetMatchSaveDirectory(fs::path());
        EXPECT_TRUE(MatchSaveDirectory().empty());
        SetMatchSaveDirectory(before);
    }

}  // namespace BECore::Mahjong::Tests
