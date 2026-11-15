#include <Common/Printers.h>
#include <Mahjong/Game/Score.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/Words.h>
#include <gtest/gtest.h>
#include <map>
#include <regex>
#include <set>
#include <tests/ShippedScene.h>
#include <tests/StringsTestSupport.h>

namespace BECore::Mahjong::Tests {

    namespace {

        String ReadFile(const std::filesystem::path& path) {
            std::ifstream file(path, std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            return FromStdString(text.str());
        }

        std::filesystem::path GameRoot() {
            return std::filesystem::path(__FILE__).parent_path().parent_path();
        }

        /// The shipped scene with its node files expanded (`ShippedScene.h`).
        String SceneText() {
            return ShippedSceneXml();
        }

        /// The scene without its comments (which quote attributes as examples).
        String WithoutComments(const String& xml) {
            const std::string text = ToStdString(xml);  //-NoStd: std::regex runs over std::string
            return FromStdString(std::regex_replace(text, std::regex(R"(<!--[\s\S]*?-->)"), ""));
        }

        /// Every key the scene asks for with `{#key}`.
        Set<String> KeysOfTheScene() {
            const std::string text = ToStdString(SceneText());  //-NoStd: std::regex runs over std::string
            Set<String> keys;
            const std::regex reference(R"(\{#([^}]+)\})");
            for (auto it = std::sregex_iterator(text.begin(), text.end(), reference); it != std::sregex_iterator(); ++it) {
                keys.insert(FromStdString((*it)[1].str()));
            }
            return keys;
        }

        /// Every string literal of the sources of the game that has the shape of a key (`area.name`, `area.name.more`): the keys code asks for by name.
        Set<String> KeysNamedInCode() {
            Set<String> keys;
            const std::regex literal(R"re("([a-z]+(?:\.[A-Za-z0-9]+)+)")re");
            for (const auto& entry : std::filesystem::recursive_directory_iterator(GameRoot() / "src" / "Mahjong")) {
                const String extension = FromStdString(entry.path().extension().string());
                if (!entry.is_regular_file() || (extension != ".h" && extension != ".cpp")) {
                    continue;
                }
                const std::string text = ToStdString(ReadFile(entry.path()));  //-NoStd: std::regex runs over std::string
                for (auto it = std::sregex_iterator(text.begin(), text.end(), literal); it != std::sregex_iterator(); ++it) {
                    keys.insert(FromStdString((*it)[1].str()));
                }
            }
            return keys;
        }

        /// The keys code builds from an enum value: the families that are walked, not named (`wind.<Wind>`, `wind.letter.<Wind>`, `suit.<suit>`, `score.kind.<ScoreKind>`, `button.<ButtonKind>`,
        /// `hint.<ButtonKind>`).
        Set<String> KeysBuiltFromEnums() {
            Set<String> keys;
            for (const Wind wind : EnumUtils<Wind>::Values()) {
                keys.insert(Format("wind.{}", EnumUtils<Wind>::ToString(wind)));
                keys.insert(Format("wind.letter.{}", EnumUtils<Wind>::ToString(wind)));
            }
            for (const TileSuit suit : EnumUtils<TileSuit>::Values()) {
                if (suit != TileSuit::None) {
                    keys.insert(Format("suit.{}", EnumUtils<TileSuit>::ToString(suit)));
                }
            }
            for (const ScoreKind kind : EnumUtils<ScoreKind>::Values()) {
                keys.insert(Format("score.kind.{}", EnumUtils<ScoreKind>::ToString(kind)));
            }
            for (const ButtonKind kind : EnumUtils<ButtonKind>::Values()) {
                keys.insert(Format("button.{}", EnumUtils<ButtonKind>::ToString(kind)));
                keys.insert(String(HintKey(kind).data(), HintKey(kind).size()));
            }
            return keys;
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
        const Vector<String> problems = LoadShippedStrings(strings);

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
            EXPECT_EQ(Tr(strings, "lang.en"), "English");
            EXPECT_EQ(Tr(strings, "lang.ru"), "Русский");
        });
    }

    TEST(StringsTest, BothLanguagesHaveTheSameKeysAndThePlaceholdersOfEachKey) {
        const Vector<LanguageEntries> languages = ReadShippedEntries();
        ASSERT_EQ(languages.size(), 2u);
        const LanguageEntries& base = languages[0];

        for (const LanguageEntries& other : languages) {
            Map<String, String> byKey;
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
        const std::string text = ToStdString(SceneText());  //-NoStd: std::regex runs over std::string
        ASSERT_FALSE(text.empty()) << ShippedSceneConfig().string();
        // Looked up in the entries of each language: `Find` falls back to the default language and would hide a missing translation
        Map<String, Set<String>> keysOf;
        for (const LanguageEntries& language : ReadShippedEntries()) {
            for (const StringEntry& entry : language.entries) {
                keysOf[language.id].insert(entry.key);
            }
        }
        ASSERT_FALSE(keysOf.empty());

        const std::regex reference(R"(\{#([^}]+)\})");
        int references = 0;
        for (auto it = std::sregex_iterator(text.begin(), text.end(), reference); it != std::sregex_iterator(); ++it) {
            const String key = FromStdString((*it)[1].str());
            ++references;
            for (const auto& [language, keys] : keysOf) {
                EXPECT_TRUE(keys.contains(key)) << "{#" << key << "} of the scene is missing in " << language;
            }
        }
        EXPECT_GT(references, 0);
    }

    TEST(StringsTest, EveryKeyOfTheFileIsUsedByTheSceneOrByCode) {
        const Set<String> scene = KeysOfTheScene();
        const Set<String> named = KeysNamedInCode();
        const Set<String> walked = KeysBuiltFromEnums();
        ASSERT_FALSE(scene.empty());
        ASSERT_FALSE(named.empty());
        ASSERT_FALSE(walked.empty());

        Set<String> inTheFile;
        for (const LanguageEntries& language : ReadShippedEntries()) {
            for (const StringEntry& entry : language.entries) {
                inTheFile.insert(entry.key);
                EXPECT_TRUE(scene.contains(entry.key) || named.contains(entry.key) || walked.contains(entry.key)) << language.id << ": " << entry.key << " is used by nobody";
            }
        }
        for (const String& key : walked) {
            EXPECT_TRUE(inTheFile.contains(key)) << "code builds the key " << key << " and the file has no text for it";
        }
        for (const String& key : named) {
            const StringView name = key;
            if (name.starts_with("handover.") || name.starts_with("matchover.") || name.starts_with("tile.") || name.starts_with("seat.") || name.starts_with("hud.") || name.starts_with("cover.")) {
                EXPECT_TRUE(inTheFile.contains(key)) << "code asks for the key " << key << " and the file has no text for it";
            }
        }
    }

    TEST(StringsTest, NoTextOfTheSceneIsAPlainWord) {
        const std::string scene = ToStdString(WithoutComments(SceneText()));  //-NoStd: std::regex runs over std::string
        ASSERT_FALSE(scene.empty());

        // A `text` is empty, a `{variable}`, a `{#key}`, or a mix of those with signs and spaces: no letter outside the braces
        const std::regex attribute(R"re(\stext="([^"]*)")re");
        int texts = 0;
        for (auto it = std::sregex_iterator(scene.begin(), scene.end(), attribute); it != std::sregex_iterator(); ++it) {
            const std::string text = (*it)[1].str();  //-NoStd: std::regex runs over std::string
            ++texts;
            const std::string outside = std::regex_replace(text, std::regex(R"(\{[^}]*\})"), "");  //-NoStd: std::regex runs over std::string
            const bool word = std::any_of(outside.begin(), outside.end(), [](unsigned char c) { return std::isalpha(c) != 0 || c >= 0x80; });
            EXPECT_FALSE(word) << "text=\"" << text << "\" holds a word that belongs in config/UIStrings.xml";
        }
        EXPECT_GT(texts, 50);
    }

    TEST(StringsTest, TrFindsTheTextOfTheCurrentLanguageElseTheDefaultElseTheKey) {
        UIStrings strings = MakeTable();

        EXPECT_EQ(Tr(strings, "only.en"), "English only");
        EXPECT_EQ(Tr(strings, "no.such.key"), "no.such.key");

        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_EQ(Tr(strings, "greeting"), "Привет, {name}!");
        EXPECT_EQ(Tr(strings, "only.en"), "English only");
        EXPECT_EQ(Tr(strings, "no.such.key"), "no.such.key");
    }

    TEST(StringsTest, TrSubstitutesThePlaceholdersAndRussianComesOutAsUtf8) {
        UIStrings strings = MakeTable();

        EXPECT_EQ(Tr(strings, "greeting", {{"name", "Anna"}}), "Hello, Anna!");
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_EQ(Tr(strings, "greeting", {{"name", "Анна"}}), "Привет, Анна!");
        EXPECT_EQ(Tr(strings, "greeting", {{"name", "x"}}).size(), String("Привет, x!").size());  // bytes, not characters
        EXPECT_EQ(Tr(strings, "no.such.key", {{"name", "x"}}), "no.such.key");
    }

    TEST(StringsTest, SubstReplacesEveryPlaceholderAndLeavesTheRestAsWritten) {
        EXPECT_EQ(Subst("Seat {n} ({wind})", {{"n", "2"}, {"wind", "South"}}), "Seat 2 (South)");
        EXPECT_EQ(Subst("{a}{a}-{b}", {{"a", "1"}, {"b", "2"}}), "11-2");
        EXPECT_EQ(Subst("{unknown} and {n}", {{"n", "2"}}), "{unknown} and 2");
        EXPECT_EQ(Subst("no placeholders", {}), "no placeholders");
        EXPECT_EQ(Subst("", {{"n", "2"}}), "");
        EXPECT_EQ(Subst("open { and {n}", {{"n", "2"}}), "open { and 2");
        EXPECT_EQ(Subst("unclosed {n", {{"n", "2"}}), "unclosed {n");
        EXPECT_EQ(Subst("{}", {{"", "empty"}}), "empty");
        EXPECT_EQ(Subst("{n}", {{"n", "{n}"}}), "{n}");  // a value is not expanded again
    }

    TEST(StringsTest, PlaceholderNamesFindsTheNamesAndSkipsReferences) {
        EXPECT_EQ((PlaceholderNames("Seat {n} ({wind}) {#key} {n}")), (Set<String>{"n", "wind"}));
        EXPECT_TRUE(PlaceholderNames("plain").empty());
    }

}  // namespace BECore::Mahjong::Tests
