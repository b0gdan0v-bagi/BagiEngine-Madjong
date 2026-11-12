#include "StringsTestSupport.h"

#include <Mahjong/Online/TextEntry.h>
#include <Rooms/RoomLimits.h>
#include <gtest/gtest.h>
#include <set>

/**
 * `TextEntry` (`Online/TextEntry.h`): the text of a form field of the online screens, edited from a pad of keys or the keyboard. Pure over the shipped string table.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        std::string Std(eastl::string_view text) {
            return std::string(text.data(), text.size());
        }

        void TypeAll(TextEntry& entry, std::initializer_list<const char*> characters) {
            for (const char* character : characters) {
                entry.Append(character);
            }
        }

        constexpr const char* kYa = "\xD0\xAF";       // Я, 2 bytes
        constexpr const char* kA = "\xD0\x90";        // А
        constexpr const char* kYoLower = "\xD1\x91";  // ё

    }  // namespace

    TEST(MahjongOnlineEntry, ThePadOfEachKindInEachLanguageIsSmallAndHasNoRepeatedCharacter) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ForEachLanguage(strings, [&](PoolString language) {
            for (const TextEntry::Kind kind : {TextEntry::Kind::Code, TextEntry::Kind::Name}) {
                const TextEntry entry(kind);
                const eastl::vector<eastl::string> pad = entry.Pad(strings);
                EXPECT_FALSE(pad.empty()) << language.ToStringView().data();
                EXPECT_LE(pad.size(), TextEntry::kPadMaxKeys) << language.ToStringView().data();
                std::set<std::string> seen;
                for (const eastl::string& key : pad) {
                    EXPECT_EQ(Utf8::CountCodePoints(key), 1u) << Std(key);
                    EXPECT_TRUE(seen.insert(Std(key)).second) << Std(key) << " is on the pad twice";
                    // every key of the pad can be typed by it
                    TextEntry typed(kind);
                    EXPECT_TRUE(typed.Append(key)) << Std(key);
                }
            }
        });
    }

    TEST(MahjongOnlineEntry, ThePadOfACodeIsTheRoomAlphabetAndThePadOfANameIsTheLettersOfTheLanguageDigitsAndASpace) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        const eastl::vector<eastl::string> code = TextEntry(TextEntry::Kind::Code).Pad(strings);
        ASSERT_EQ(code.size(), RoomLimits::kRoomCodeAlphabet.size());
        for (size_t i = 0; i < code.size(); ++i) {
            EXPECT_EQ(Std(code[i]), std::string(1, RoomLimits::kRoomCodeAlphabet[i]));
        }

        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        const eastl::vector<eastl::string> latin = TextEntry(TextEntry::Kind::Name).Pad(strings);
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        const eastl::vector<eastl::string> cyrillic = TextEntry(TextEntry::Kind::Name).Pad(strings);
        EXPECT_EQ(Std(latin.front()), "A");
        EXPECT_EQ(Std(cyrillic.front()), kA);
        EXPECT_EQ(Std(latin.back()), " ");
        EXPECT_EQ(Std(cyrillic.back()), " ");
        EXPECT_EQ(Std(cyrillic[cyrillic.size() - 2]), "9");
        EXPECT_NE(std::find(cyrillic.begin(), cyrillic.end(), eastl::string(kYa)), cyrillic.end());
        EXPECT_EQ(std::find(latin.begin(), latin.end(), eastl::string(kYa)), latin.end());
    }

    TEST(MahjongOnlineEntry, ACodeAcceptsOnlyItsAlphabetFoldsCaseAndStopsAtFive) {
        TextEntry code(TextEntry::Kind::Code);
        EXPECT_FALSE(code.Append("I"));
        EXPECT_FALSE(code.Append("O"));
        EXPECT_FALSE(code.Append("0"));
        EXPECT_FALSE(code.Append("1"));
        EXPECT_FALSE(code.Append("-"));
        EXPECT_FALSE(code.Append(" "));
        EXPECT_FALSE(code.Append(""));
        EXPECT_FALSE(code.Append("AB"));
        EXPECT_FALSE(code.Append(kYa));
        EXPECT_TRUE(code.Text().empty());

        TypeAll(code, {"a", "B", "c", "2", "k"});
        EXPECT_EQ(Std(code.Text()), "ABC2K");
        EXPECT_TRUE(code.IsComplete());
        EXPECT_FALSE(code.Append("D"));
        EXPECT_EQ(Std(code.Text()), "ABC2K");

        EXPECT_TRUE(code.Backspace());
        EXPECT_FALSE(code.IsComplete());
        EXPECT_TRUE(code.Append("z"));
        EXPECT_EQ(Std(code.Text()), "ABC2Z");
    }

    TEST(MahjongOnlineEntry, ANameStopsAt24BytesAndNeverCutsACharacter) {
        TextEntry name(TextEntry::Kind::Name);
        for (int i = 0; i < 12; ++i) {
            EXPECT_TRUE(name.Append(kYa));
        }
        EXPECT_EQ(name.Text().size(), RoomLimits::kMaxNameBytes);
        EXPECT_FALSE(name.Append(kYa));
        EXPECT_FALSE(name.Append("a"));
        EXPECT_EQ(name.Text().size(), RoomLimits::kMaxNameBytes);

        // 23 bytes: a one-byte character fits, a two-byte one does not
        name.Clear();
        for (int i = 0; i < 11; ++i) {
            name.Append(kYa);
        }
        name.Append("x");
        EXPECT_EQ(name.Text().size(), 23u);
        EXPECT_FALSE(name.Append(kYa));
        EXPECT_EQ(name.Text().size(), 23u);
        EXPECT_TRUE(name.Append("y"));
        EXPECT_EQ(name.Text().size(), RoomLimits::kMaxNameBytes);
    }

    TEST(MahjongOnlineEntry, ANameKeepsTheCaseTakesBothAlphabetsAndRefusesTheRest) {
        TextEntry name(TextEntry::Kind::Name);
        TypeAll(name, {"a", "B", " ", "7", kYa, kYoLower});
        EXPECT_EQ(Std(name.Text()), std::string("aB 7") + kYa + kYoLower);
        const std::string before = Std(name.Text());
        for (const char* refused : {"!", "-", "\n", "\xC3\xA9" /* é */, "\xE2\x82\xAC" /* euro */, "ab", "\xFF"}) {
            EXPECT_FALSE(name.Append(refused)) << refused;
        }
        EXPECT_EQ(Std(name.Text()), before);
    }

    TEST(MahjongOnlineEntry, BackspaceRemovesOneWholeCharacter) {
        TextEntry name(TextEntry::Kind::Name);
        TypeAll(name, {"a", kYa, kYoLower});
        EXPECT_TRUE(name.Backspace());
        EXPECT_EQ(Std(name.Text()), std::string("a") + kYa);
        EXPECT_TRUE(name.Backspace());
        EXPECT_EQ(Std(name.Text()), "a");
        EXPECT_TRUE(name.Backspace());
        EXPECT_TRUE(name.Text().empty());
        EXPECT_FALSE(name.Backspace());
    }

    TEST(MahjongOnlineEntry, IsCompleteFollowsTheLimitsOfTheServer) {
        TextEntry name(TextEntry::Kind::Name);
        EXPECT_FALSE(name.IsComplete());
        name.Append(" ");
        EXPECT_FALSE(name.IsComplete()) << "only a space is no name";
        name.Append("a");
        EXPECT_TRUE(name.IsComplete());

        TextEntry code(TextEntry::Kind::Code);
        TypeAll(code, {"A", "B", "C", "D"});
        EXPECT_FALSE(code.IsComplete());
        code.Append("E");
        EXPECT_TRUE(code.IsComplete());
        code.Clear();
        EXPECT_FALSE(code.IsComplete());
    }

    TEST(MahjongOnlineEntry, SetTakesAWholeTextOrNothing) {
        TextEntry name(TextEntry::Kind::Name);
        name.Append("a");
        EXPECT_TRUE(name.Set(std::string("Bob ").append(kYa).c_str()));
        EXPECT_EQ(Std(name.Text()), std::string("Bob ") + kYa);
        EXPECT_FALSE(name.Set("Bob!"));
        EXPECT_FALSE(name.Set("a very long name that passes the limit"));
        EXPECT_EQ(Std(name.Text()), std::string("Bob ") + kYa);
        EXPECT_TRUE(name.Set(""));
        EXPECT_TRUE(name.Text().empty());

        TextEntry code(TextEntry::Kind::Code);
        EXPECT_TRUE(code.Set("abcde"));
        EXPECT_EQ(Std(code.Text()), "ABCDE");
        EXPECT_FALSE(code.Set("ABCDEF"));
        EXPECT_EQ(Std(code.Text()), "ABCDE");
    }

    TEST(MahjongOnlineEntry, CharFromKeycodeKnowsLettersDigitsAndTheSpaceOnly) {
        for (uint32_t keycode = 0; keycode < 0x200; ++keycode) {
            const std::optional<char> plain = TextEntry::CharFromKeycode(keycode, false);
            const std::optional<char> shifted = TextEntry::CharFromKeycode(keycode, true);
            const bool letter = keycode >= 'a' && keycode <= 'z';
            const bool digit = keycode >= '0' && keycode <= '9';
            if (letter) {
                EXPECT_EQ(plain, static_cast<char>(keycode));
                EXPECT_EQ(shifted, static_cast<char>(keycode - 'a' + 'A'));
            } else if (digit || keycode == ' ') {
                EXPECT_EQ(plain, static_cast<char>(keycode));
                EXPECT_EQ(shifted, static_cast<char>(keycode));
            } else {
                EXPECT_FALSE(plain.has_value()) << keycode;
                EXPECT_FALSE(shifted.has_value()) << keycode;
            }
        }
        // the keycodes of the other keys of SDL are far from ASCII (a scancode with the 1 << 30 flag)
        EXPECT_FALSE(TextEntry::CharFromKeycode(0x40000058u, false).has_value());
    }

    namespace {

        /// "refused" for nothing
        std::string Cleaned(eastl::string_view text) {
            const std::optional<eastl::string> code = TextEntry::CleanRoomCode(text);
            return code ? Std(*code) : std::string("<refused>");
        }

    }  // namespace

    TEST(MahjongOnlineEntry, APastedTextThatIsACodeGivesItInUpperCaseCutToTheLength) {
        struct Case {
            const char* text;
            const char* code;
        };
        const Case cases[] = {
            {"AB3K9", "AB3K9"},       {"  ab3k9 ", "AB3K9"}, {"ab3k9\n", "AB3K9"}, {"\tab3k9\r\n", "AB3K9"}, {"ab3-k9", "AB3K9"}, {"AB3 K9", "AB3K9"}, {"ab3k9xyz", "AB3K9"},  // a long token is cut
            {"ab3k9 hands", "AB3K9"},  // all of it is made of the code's letters
            {"2345678", "23456"},
        };
        for (const Case& test : cases) {
            EXPECT_EQ(Cleaned(test.text), test.code) << test.text;
        }
    }

    TEST(MahjongOnlineEntry, APastedInviteLineGivesTheOneWordThatCanBeACode) {
        EXPECT_EQ(Cleaned("Join my mahjong room ab3k9, please"), "AB3K9");
        EXPECT_EQ(Cleaned("room: AB3K9"), "AB3K9");
        EXPECT_EQ(Cleaned("example.test/play?room=ab3k9&x=1"), "AB3K9");
        EXPECT_EQ(Cleaned("https://example.test/play?room=ab3k9"), "<refused>") << "\"https\" is a candidate too";
        // "hands" is made of the code's letters too: two candidates are never guessed
        EXPECT_EQ(Cleaned("Join my mahjong room ab3k9, I deal hands"), "<refused>");
        EXPECT_EQ(Cleaned("Join the room, please"), "<refused>") << "no candidate";
        // words of another length, and a word with a letter the alphabet lacks (I, O, 0, 1)
        EXPECT_EQ(Cleaned("room ab3k90, ok"), "<refused>");
        EXPECT_EQ(Cleaned("room ab3o9, ok"), "<refused>");
    }

    TEST(MahjongOnlineEntry, APastedTextThatIsNoCodeIsRefused) {
        for (const char* text : {"", " ", "ab3", "ab3 k", "---", "!!!", "ab3k", "\xD0\x90\xD0\x91\xD0\x92\xD0\x93\xD0\x94", "room \xD0\x90\xD0\x91\xD0\x92\xD0\x93\xD0\x94"}) {
            EXPECT_EQ(Cleaned(text), "<refused>") << text;
        }
        // the code letters I, O, 0 and 1 do not exist, with or without company
        EXPECT_EQ(Cleaned("io01x"), "<refused>");
    }

    TEST(MahjongOnlineEntry, APastedTextOverTheLimitIsNotScanned) {
        const std::string limit(TextEntry::kMaxPasteBytes, 'a');
        EXPECT_EQ(Cleaned(eastl::string_view(limit.data(), limit.size())), "AAAAA") << "the limit itself is read";
        const std::string over(TextEntry::kMaxPasteBytes + 1, 'a');
        EXPECT_EQ(Cleaned(eastl::string_view(over.data(), over.size())), "<refused>");
        const std::string spaces(TextEntry::kMaxPasteBytes + 1, ' ');
        EXPECT_EQ(Cleaned((spaces + "ab3k9").c_str()), "<refused>");
    }

    TEST(MahjongOnlineEntry, ACleanedCodeIsOneTheFieldTakes) {
        TextEntry field(TextEntry::Kind::Code);
        const std::optional<eastl::string> code = TextEntry::CleanRoomCode("ab3-k9");
        ASSERT_TRUE(code.has_value());
        EXPECT_TRUE(field.Set(*code));
        EXPECT_TRUE(field.IsComplete());
    }

    TEST(MahjongOnlineEntry, TheDefaultNameIsAWordOfTheLanguageThatAServerAccepts) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ForEachLanguage(strings, [&](PoolString language) {
            TextEntry name(TextEntry::Kind::Name);
            EXPECT_TRUE(name.Set(TextEntry::DefaultName(strings))) << language.ToStringView().data();
            EXPECT_TRUE(name.IsComplete()) << language.ToStringView().data();
        });
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        EXPECT_NE(TextEntry::DefaultName(strings), "online.name.default");
    }

}  // namespace BECore::Mahjong::Tests
