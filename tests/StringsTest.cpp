#include "ShippedScene.h"
#include "StringsTestSupport.h"

#include <Mahjong/Game/Score.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/Words.h>
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>

/**
 * The string table of the game (`config/UIStrings.xml`) and the helpers that read it (`Table/Words.h`). Pure logic over a `UIStrings` of the test's own: no scene and no engine
 * services. The parity checks read the shipped file, so they grow with every key that is added, in either language.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        std::string ReadFile(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.data(), text.size());
        }

        std::filesystem::path GameRoot() {
            return std::filesystem::path(__FILE__).parent_path().parent_path();
        }

        /// The shipped scene with its node files expanded (`ShippedScene.h`).
        std::string SceneText() {
            return ShippedSceneXml();
        }

        /// The scene without its comments (which quote attributes as examples).
        std::string WithoutComments(const std::string& xml) {
            return std::regex_replace(xml, std::regex(R"(<!--[\s\S]*?-->)"), "");
        }

        /// Every key the scene asks for with `{#key}`.
        std::set<std::string> KeysOfTheScene() {
            const std::string text = SceneText();
            std::set<std::string> keys;
            const std::regex reference(R"(\{#([^}]+)\})");
            for (auto it = std::sregex_iterator(text.begin(), text.end(), reference); it != std::sregex_iterator(); ++it) {
                keys.insert((*it)[1].str());
            }
            return keys;
        }

        /// Every string literal of the sources of the game that has the shape of a key (`area.name`, `area.name.more`): the keys code asks for by name.
        std::set<std::string> KeysNamedInCode() {
            std::set<std::string> keys;
            const std::regex literal(R"re("([a-z]+(?:\.[A-Za-z0-9]+)+)")re");
            for (const auto& entry : std::filesystem::recursive_directory_iterator(GameRoot() / "src" / "Mahjong")) {
                const std::string extension = entry.path().extension().string();
                if (!entry.is_regular_file() || (extension != ".h" && extension != ".cpp")) {
                    continue;
                }
                const std::string text = ReadFile(entry.path());
                for (auto it = std::sregex_iterator(text.begin(), text.end(), literal); it != std::sregex_iterator(); ++it) {
                    keys.insert((*it)[1].str());
                }
            }
            return keys;
        }

        /// The keys code builds from an enum value: the families that are walked, not named (`wind.<Wind>`, `suit.<suit>`, `score.kind.<ScoreKind>`, `button.<ButtonKind>`, `hint.<ButtonKind>`).
        std::set<std::string> KeysBuiltFromEnums() {
            std::set<std::string> keys;
            for (const Wind wind : EnumUtils<Wind>::Values()) {
                keys.insert(Std(Format("wind.{}", EnumUtils<Wind>::ToString(wind))));
            }
            for (const TileSuit suit : EnumUtils<TileSuit>::Values()) {
                if (suit != TileSuit::None) {
                    keys.insert(Std(Format("suit.{}", EnumUtils<TileSuit>::ToString(suit))));
                }
            }
            for (const ScoreKind kind : EnumUtils<ScoreKind>::Values()) {
                keys.insert(Std(Format("score.kind.{}", EnumUtils<ScoreKind>::ToString(kind))));
            }
            for (const ButtonKind kind : EnumUtils<ButtonKind>::Values()) {
                keys.insert(Std(Format("button.{}", EnumUtils<ButtonKind>::ToString(kind))));
                keys.insert(std::string(HintKey(kind).data(), HintKey(kind).size()));
            }
            return keys;
        }

        /// The words of the page `Options` while the scene does not show the page yet (the node comes with its own work package, which deletes this list).
        bool IsWordOfThePageNotInTheSceneYet(const std::string& key) {
            static const std::set<std::string> keys = {"setup.options", "setup.level", "options.title", "options.back", "level.easy", "level.normal"};
            return keys.contains(key);
        }

        UIStrings MakeTable() {
            UIStrings strings;
            strings.Set("en"_intern, "greeting", "Hello, {name}!");
            strings.Set("en"_intern, "only.en", "English only");
            strings.Set("ru"_intern, "greeting", "Привет, {name}!");
            return strings;
        }

    }  // namespace

    TEST(StringsTest, TheShippedFileLoadsWithNoProblems) {
        UIStrings strings;
        const eastl::vector<eastl::string> problems = LoadShippedStrings(strings);

        EXPECT_TRUE(problems.empty()) << (problems.empty() ? "" : problems[0].c_str());
    }

    TEST(StringsTest, EnglishAndRussianAndEnglishIsTheDefault) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());

        ASSERT_EQ(strings.GetLanguages().size(), 2u);
        EXPECT_TRUE(strings.HasLanguage("en"_intern));
        EXPECT_TRUE(strings.HasLanguage("ru"_intern));
        EXPECT_EQ(strings.GetDefaultLanguage(), "en"_intern);
        EXPECT_EQ(strings.GetLanguage(), "en"_intern);
    }

    TEST(StringsTest, EachLanguageIsWrittenInItself) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());

        ForEachLanguage(strings, [&](PoolString) {
            EXPECT_EQ(Std(Tr(strings, "lang.en")), "English");
            EXPECT_EQ(Std(Tr(strings, "lang.ru")), "Русский");
        });
    }

    TEST(StringsTest, BothLanguagesHaveTheSameKeysAndThePlaceholdersOfEachKey) {
        const std::vector<LanguageEntries> languages = ReadShippedEntries();
        ASSERT_EQ(languages.size(), 2u);
        const LanguageEntries& base = languages[0];

        for (const LanguageEntries& other : languages) {
            std::map<std::string, std::string> byKey;
            for (const StringEntry& entry : other.entries) {
                EXPECT_TRUE(byKey.emplace(entry.key, entry.text).second) << other.id << ": the key " << entry.key << " is there twice";
            }
            for (const StringEntry& entry : base.entries) {
                const auto found = byKey.find(entry.key);
                ASSERT_NE(found, byKey.end()) << other.id << " lacks " << entry.key;
                EXPECT_EQ(PlaceholderNames(found->second), PlaceholderNames(entry.text)) << other.id << ": the placeholders of " << entry.key;
            }
            EXPECT_EQ(other.entries.size(), base.entries.size()) << other.id << " has keys " << base.id << " has not";
        }
    }

    TEST(StringsTest, NoKeyIsEmptyAndNoValueIsEmpty) {
        for (const LanguageEntries& language : ReadShippedEntries()) {
            for (const StringEntry& entry : language.entries) {
                EXPECT_FALSE(entry.key.empty()) << language.id;
                EXPECT_FALSE(entry.text.empty()) << language.id << ": " << entry.key;
            }
        }
    }

    TEST(StringsTest, EveryReferenceOfTheSceneIsInBothLanguages) {
        const std::string text = SceneText();
        ASSERT_FALSE(text.empty()) << ShippedSceneConfig().string();
        // Looked up in the entries of each language: `Find` falls back to the default language and would hide a missing translation
        std::map<std::string, std::set<std::string>> keysOf;
        for (const LanguageEntries& language : ReadShippedEntries()) {
            for (const StringEntry& entry : language.entries) {
                keysOf[language.id].insert(entry.key);
            }
        }
        ASSERT_FALSE(keysOf.empty());

        const std::regex reference(R"(\{#([^}]+)\})");
        int references = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), reference); it != std::sregex_iterator(); ++it) {
            const std::string key = (*it)[1].str();
            ++references;
            for (const auto& [language, keys] : keysOf) {
                EXPECT_TRUE(keys.contains(key)) << "{#" << key << "} of the scene is missing in " << language;
            }
        }
        EXPECT_GT(references, 0);
    }

    TEST(StringsTest, EveryKeyOfTheFileIsUsedByTheSceneOrByCode) {
        const std::set<std::string> scene = KeysOfTheScene();
        const std::set<std::string> named = KeysNamedInCode();
        const std::set<std::string> walked = KeysBuiltFromEnums();
        ASSERT_FALSE(scene.empty());
        ASSERT_FALSE(named.empty());
        ASSERT_FALSE(walked.empty());

        std::set<std::string> inTheFile;
        for (const LanguageEntries& language : ReadShippedEntries()) {
            for (const StringEntry& entry : language.entries) {
                inTheFile.insert(entry.key);
                EXPECT_TRUE(scene.contains(entry.key) || named.contains(entry.key) || walked.contains(entry.key) || IsWordOfThePageNotInTheSceneYet(entry.key))
                    << language.id << ": " << entry.key << " is used by nobody";
            }
        }
        for (const std::string& key : walked) {
            EXPECT_TRUE(inTheFile.contains(key)) << "code builds the key " << key << " and the file has no text for it";
        }
        for (const std::string& key : named) {
            if (key.starts_with("handover.") || key.starts_with("matchover.") || key.starts_with("tile.") || key.starts_with("seat.") || key.starts_with("hud.") || key.starts_with("cover.")) {
                EXPECT_TRUE(inTheFile.contains(key)) << "code asks for the key " << key << " and the file has no text for it";
            }
        }
    }

    TEST(StringsTest, NoTextOfTheSceneIsAPlainWord) {
        const std::string scene = WithoutComments(SceneText());
        ASSERT_FALSE(scene.empty());

        // A `text` is empty, a `{variable}`, a `{#key}`, or a mix of those with signs and spaces: no letter outside the braces
        const std::regex attribute(R"re(\stext="([^"]*)")re");
        int texts = 0;
        for (auto it = std::sregex_iterator(scene.begin(), scene.end(), attribute); it != std::sregex_iterator(); ++it) {
            const std::string text = (*it)[1].str();
            ++texts;
            const std::string outside = std::regex_replace(text, std::regex(R"(\{[^}]*\})"), "");
            const bool word = std::any_of(outside.begin(), outside.end(), [](unsigned char c) { return std::isalpha(c) != 0 || c >= 0x80; });
            EXPECT_FALSE(word) << "text=\"" << text << "\" holds a word that belongs in config/UIStrings.xml";
        }
        EXPECT_GT(texts, 50);
    }

    TEST(StringsTest, TrFindsTheTextOfTheCurrentLanguageElseTheDefaultElseTheKey) {
        UIStrings strings = MakeTable();

        EXPECT_EQ(Std(Tr(strings, "only.en")), "English only");
        EXPECT_EQ(Std(Tr(strings, "no.such.key")), "no.such.key");

        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_EQ(Std(Tr(strings, "greeting")), "Привет, {name}!");
        EXPECT_EQ(Std(Tr(strings, "only.en")), "English only");  // the default language fills the gap
        EXPECT_EQ(Std(Tr(strings, "no.such.key")), "no.such.key");
    }

    TEST(StringsTest, TrSubstitutesThePlaceholdersAndRussianComesOutAsUtf8) {
        UIStrings strings = MakeTable();

        EXPECT_EQ(Std(Tr(strings, "greeting", {{"name", "Anna"}})), "Hello, Anna!");
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_EQ(Std(Tr(strings, "greeting", {{"name", "Анна"}})), "Привет, Анна!");
        EXPECT_EQ(Tr(strings, "greeting", {{"name", "x"}}).size(), std::string("Привет, x!").size());  // bytes, not characters
        EXPECT_EQ(Std(Tr(strings, "no.such.key", {{"name", "x"}})), "no.such.key");
    }

    TEST(StringsTest, SubstReplacesEveryPlaceholderAndLeavesTheRestAsWritten) {
        EXPECT_EQ(Std(Subst("Seat {n} ({wind})", {{"n", "2"}, {"wind", "South"}})), "Seat 2 (South)");
        EXPECT_EQ(Std(Subst("{a}{a}-{b}", {{"a", "1"}, {"b", "2"}})), "11-2");
        EXPECT_EQ(Std(Subst("{unknown} and {n}", {{"n", "2"}})), "{unknown} and 2");
        EXPECT_EQ(Std(Subst("no placeholders", {})), "no placeholders");
        EXPECT_EQ(Std(Subst("", {{"n", "2"}})), "");
        EXPECT_EQ(Std(Subst("open { and {n}", {{"n", "2"}})), "open { and 2");
        EXPECT_EQ(Std(Subst("unclosed {n", {{"n", "2"}})), "unclosed {n");
        EXPECT_EQ(Std(Subst("{}", {{"", "empty"}})), "empty");
        EXPECT_EQ(Std(Subst("{n}", {{"n", "{n}"}})), "{n}");  // a value is not expanded again
    }

    TEST(StringsTest, PlaceholderNamesFindsTheNamesAndSkipsReferences) {
        EXPECT_EQ((PlaceholderNames("Seat {n} ({wind}) {#key} {n}")), (std::set<std::string>{"n", "wind"}));
        EXPECT_TRUE(PlaceholderNames("plain").empty());
    }

}  // namespace BECore::Mahjong::Tests
