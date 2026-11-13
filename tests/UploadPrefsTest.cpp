#include <BECore/Save/SaveGame.h>
#include <BECore/Save/SaveStorage.h>
#include <Common/TempPath.h>
#include <Mahjong/UploadPrefs.h>
#include <gtest/gtest.h>

/**
 * The tester's choice about sending the matches and the file it is kept in (`UploadPrefs.h`): the text round trip, what a damaged
 * file reads as, the temp-and-rename write, and that the game's own saves never list the file. A temporary folder per test.
 */
namespace BECore::Mahjong::Tests {

    namespace fs = std::filesystem;

    namespace {

        constexpr const char* kFolderPrefix = "BagiMahjongUploadPrefsTest";

        UploadPrefs Prefs(UploadConsent consent, const char* nickname = "") {
            UploadPrefs prefs;
            prefs.consent = consent;
            prefs.nickname = nickname;
            return prefs;
        }

        eastl::string_view View(const char* text) {
            return eastl::string_view(text);
        }

        class UploadPrefsTest : public ::testing::Test {
        protected:
            void SetUp() override {
                folder = BECore::Tests::TestTempDirectory(kFolderPrefix);
                fs::remove_all(folder);
                fs::create_directories(folder);
                SetSaveChangedHandler([this] { ++flushes; });
            }

            void TearDown() override {
                SetSaveChangedHandler({});
                std::error_code error;
                fs::remove_all(folder, error);
            }

            void Write(const std::string& bytes) const {
                std::ofstream(folder / "upload.txt", std::ios::binary) << bytes;
            }

            fs::path folder;
            int flushes = 0;
        };

    }  // namespace

    TEST_F(UploadPrefsTest, EveryConsentAndANicknameSurviveTheRoundTrip) {
        for (const UploadConsent consent : {UploadConsent::Unasked, UploadConsent::Granted, UploadConsent::Declined}) {
            for (const char* nickname : {"", "Anna", "Anna B 7", "Анна Белова"}) {
                const UploadPrefs prefs = Prefs(consent, nickname);
                EXPECT_EQ(ParseUploadPrefs(FormatUploadPrefs(prefs).c_str()), prefs) << nickname;
                ASSERT_TRUE(StoreUploadPrefs(folder, prefs));
                EXPECT_EQ(LoadUploadPrefs(folder), prefs) << nickname;
            }
        }
    }

    TEST_F(UploadPrefsTest, TheTextHasOneLinePerKeyAndNoNicknameLineWhenThereIsNone) {
        EXPECT_EQ(FormatUploadPrefs(Prefs(UploadConsent::Granted, "Anna")), "consent=granted\nnickname=Anna\n");
        EXPECT_EQ(FormatUploadPrefs(Prefs(UploadConsent::Declined)), "consent=declined\n");
        EXPECT_EQ(ParseUploadPrefs(View("consent=granted\r\nnickname=Anna\r\n")), Prefs(UploadConsent::Granted, "Anna")) << "CRLF is read";
        EXPECT_EQ(ParseUploadPrefs(View("nickname=Anna\nconsent=declined")), Prefs(UploadConsent::Declined, "Anna")) << "order and the last newline do not matter";
    }

    TEST_F(UploadPrefsTest, DamagedUnknownAndEmptyTextReadAsTheDefaults) {
        const UploadPrefs defaults;
        EXPECT_EQ(defaults.consent, UploadConsent::Unasked);
        for (const char* text : {"", "\n", "garbage", "consent=maybe\n", "consent=\n", "nickname=Anna\n", "consent=granted\nconsent=granted\n", "consent=granted\nnickname=A\nnickname=B\n",
                                 "consent=granted\nemail=a@b.c\n", "consent=granted\nnoequals\n", "Consent=granted\n", "\xff\xfe\x00\x01"}) {
            EXPECT_EQ(ParseUploadPrefs(View(text)), defaults) << text;
        }
        std::string big = "consent=granted\nnickname=";
        big.append(kMaxUploadPrefsBytes, 'a');
        EXPECT_EQ(ParseUploadPrefs(eastl::string_view(big.data(), big.size())), defaults) << "over the limit";
        Write(big);
        EXPECT_EQ(LoadUploadPrefs(folder), defaults) << "a big file is not read whole";
        Write("consent=granted\n\xff\xfe");
        EXPECT_EQ(LoadUploadPrefs(folder), defaults);
    }

    TEST_F(UploadPrefsTest, ANicknameTheNameFieldRefusesReadsAsEmptyAndTheConsentStays) {
        EXPECT_EQ(ParseUploadPrefs(View("consent=granted\nnickname=Anna!\n")), Prefs(UploadConsent::Granted));
        EXPECT_EQ(ParseUploadPrefs(View("consent=granted\nnickname=a\tb\n")), Prefs(UploadConsent::Granted));
        EXPECT_EQ(ParseUploadPrefs(View("consent=granted\nnickname=\xc3\xa9\n")), Prefs(UploadConsent::Granted)) << "a letter of neither alphabet";
        const char* longest = "123456789012345678901234";
        EXPECT_EQ(ParseUploadPrefs(View(Format("consent=granted\nnickname={}\n", longest).c_str())), Prefs(UploadConsent::Granted, longest));
        EXPECT_EQ(ParseUploadPrefs(View(Format("consent=granted\nnickname={}5\n", longest).c_str())), Prefs(UploadConsent::Granted)) << "25 bytes";
        EXPECT_EQ(FormatUploadPrefs(Prefs(UploadConsent::Granted, "Anna!")), "consent=granted\n") << "what the field refuses is not written";
    }

    TEST_F(UploadPrefsTest, TheFileIsWrittenInPlaceAndTheFolderIsToldToFlush) {
        EXPECT_EQ(LoadUploadPrefs(folder), UploadPrefs()) << "no file yet";
        ASSERT_TRUE(StoreUploadPrefs(folder, Prefs(UploadConsent::Granted, "Anna")));
        EXPECT_EQ(flushes, 1);
        EXPECT_TRUE(fs::is_regular_file(folder / "upload.txt"));
        EXPECT_FALSE(fs::exists(folder / "upload.txt.tmp")) << "the temp name is renamed away";
        ASSERT_TRUE(StoreUploadPrefs(folder, Prefs(UploadConsent::Declined)));
        EXPECT_EQ(flushes, 2);
        EXPECT_EQ(LoadUploadPrefs(folder), Prefs(UploadConsent::Declined));
    }

    TEST_F(UploadPrefsTest, ANewFolderIsMade) {
        const fs::path inner = folder / "not" / "yet";
        ASSERT_TRUE(StoreUploadPrefs(inner, Prefs(UploadConsent::Granted)));
        EXPECT_EQ(LoadUploadPrefs(inner), Prefs(UploadConsent::Granted));
    }

    TEST_F(UploadPrefsTest, AnEmptyFolderPathIsNowhere) {
        EXPECT_FALSE(StoreUploadPrefs({}, Prefs(UploadConsent::Granted, "Anna")));
        EXPECT_EQ(flushes, 0);
        EXPECT_EQ(LoadUploadPrefs({}), UploadPrefs());
        EXPECT_TRUE(UploadPrefsFolder().empty()) << "a desktop build keeps nothing here";
        EXPECT_FALSE(UploadChoiceIsRemembered());
    }

    TEST_F(UploadPrefsTest, AFailedWriteLeavesTheOldFile) {
        ASSERT_TRUE(StoreUploadPrefs(folder, Prefs(UploadConsent::Granted, "Anna")));
        flushes = 0;
        fs::create_directory(folder / "upload.txt.tmp");  // the temp name cannot be made
        EXPECT_FALSE(StoreUploadPrefs(folder, Prefs(UploadConsent::Declined)));
        EXPECT_EQ(flushes, 0);
        EXPECT_EQ(LoadUploadPrefs(folder), Prefs(UploadConsent::Granted, "Anna"));
    }

    TEST_F(UploadPrefsTest, TheGamesSavesNeverListTheFile) {
        ASSERT_TRUE(StoreUploadPrefs(folder, Prefs(UploadConsent::Granted, "Anna")));
        FileSystem fileSystem;
        SaveGameService service(fileSystem);
        ASSERT_TRUE(service.Initialize(folder));
        EXPECT_TRUE(service.List().empty());
        EXPECT_TRUE(fs::exists(folder / "upload.txt"));
    }

}  // namespace BECore::Mahjong::Tests
