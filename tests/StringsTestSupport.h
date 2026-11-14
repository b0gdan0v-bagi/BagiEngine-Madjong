#pragma once

#include <BECore/Config/XmlConfig.h>
#include <BECore/Text/Utf8.h>
#include <BECore/UI/UIStrings.h>
#include <filesystem>
#include <gtest/gtest.h>
#include <set>
#include <string>
#include <vector>

/**
 * What the tests of the words share: the shipped string table read from the source tree (a game's content is not mounted in the test environment, as for
 * `SceneConfig.xml`) and a walk over its languages. Header only, pure: a `UIStrings` of the test's own needs no engine services.
 */
namespace BECore::Mahjong::Tests {

    inline std::filesystem::path ShippedStringsPath() {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "UIStrings.xml";
    }

    /// Replaces the contents of `strings` by the shipped `config/UIStrings.xml` and returns what `LoadFromXml` complained about (empty when the file is sound).
    inline eastl::vector<String> LoadShippedStrings(UIStrings& strings) {
        strings.Clear();
        auto config = XmlConfig::Create();
        if (!config.LoadFromFile(ShippedStringsPath())) {
            return {String("the file did not load")};
        }
        return strings.LoadFromXml(config.GetRoot());
    }

    /// One `<string key text>` of the shipped file.
    struct StringEntry {
        String key;
        String text;
    };

    /// The strings of one `<language>` of the shipped file, in file order.
    struct LanguageEntries {
        String id;
        std::vector<StringEntry> entries;
    };

    /// Every language of the shipped `config/UIStrings.xml` with its strings: the table itself cannot list its keys, the parity checks read the file.
    inline std::vector<LanguageEntries> ReadShippedEntries() {
        std::vector<LanguageEntries> languages;
        auto config = XmlConfig::Create();
        if (!config.LoadFromFile(ShippedStringsPath())) {
            return languages;
        }
        for (const XmlNode language : config.GetRoot().Children()) {
            LanguageEntries one;
            const auto id = language.ParseAttribute<StringView>("id");
            one.id = id ? String(id->data(), id->size()) : String();
            for (const XmlNode string : language.Children()) {
                const auto key = string.ParseAttribute<StringView>("key");
                const auto text = string.ParseAttribute<StringView>("text");
                one.entries.push_back({key ? String(key->data(), key->size()) : String(), text ? String(text->data(), text->size()) : String()});
            }
            languages.push_back(std::move(one));
        }
        return languages;
    }

    /// The names of the `{placeholders}` of a text (a `{#key}` reference is not one).
    inline std::set<String> PlaceholderNames(StringView text) {
        std::set<String> names;
        size_t at = 0;
        while ((at = text.find('{', at)) != StringView::npos) {
            const size_t close = text.find('}', at);
            if (close == StringView::npos) {
                break;
            }
            const StringView name = text.substr(at + 1, close - at - 1);
            if (!name.empty() && name.find('{') == StringView::npos && name.front() != '#') {
                names.insert(String(name));
            }
            at = close + 1;
        }
        return names;
    }

    /// Sets each language of the table in turn, calls `check(languageId)` and puts the language back.
    template <typename Check>
    void ForEachLanguage(UIStrings& strings, Check&& check) {
        const PoolString saved = strings.GetLanguage();
        const eastl::vector<PoolString> languages = strings.GetLanguages();
        for (const PoolString language : languages) {
            ASSERT_TRUE(strings.SetLanguage(language)) << language.ToStringView().data();
            check(language);
        }
        strings.SetLanguage(saved);
    }

    /// The longest text of `candidates` by code points (how a line of text is measured in any language, the bytes of Cyrillic are twice as many); the first of equals; empty for none.
    inline String LongestText(const std::vector<String>& candidates) {
        String longest;
        size_t most = 0;
        for (const String& candidate : candidates) {
            const size_t length = Utf8::CountCodePoints(candidate);
            if (length > most) {
                most = length;
                longest = candidate;
            }
        }
        return longest;
    }

}  // namespace BECore::Mahjong::Tests
