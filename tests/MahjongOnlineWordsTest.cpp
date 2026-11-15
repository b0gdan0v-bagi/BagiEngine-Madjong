#include <Common/Printers.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Mahjong/Table/Words.h>
#include <gtest/gtest.h>
#include <map>
#include <set>
#include <tests/StringsTestSupport.h>

namespace BECore::Mahjong::Tests {

    namespace {

        String Std(StringView text) {
            return String(text.data(), text.size());
        }

        /// The text of every key of one language of the shipped file.
        Map<String, String> TextsOf(const String& language) {
            Map<String, String> texts;
            for (const LanguageEntries& entries : ReadShippedEntries()) {
                if (entries.id == language) {
                    for (const StringEntry& entry : entries.entries) {
                        texts[entry.key] = entry.text;
                    }
                }
            }
            return texts;
        }

        Set<String> PlaceholdersAllowedFor(const String& key) {
            if (key == kOnlineUnknownKey.data()) {
                return {"code"};
            }
            return {};
        }

        /// Every key of `OnlineWords.h`, the codes' and the fixed ones.
        Set<String> AllOnlineKeys() {
            Set<String> keys;
            for (const OnlineCodeWord& word : kKnownOnlineCodes) {
                keys.insert(Std(word.key));
            }
            keys.insert(Std(kOnlineUnknownKey));
            keys.insert(Std(kOnlineViewFormatKey));
            keys.insert(Std(kOnlineBadViewKey));
            keys.insert(Std(kOnlineConnectingKey));
            keys.insert(Std(kOnlineReconnectingKey));
            keys.insert(Std(kOnlineLobbyHintsAllowedKey));
            keys.insert(Std(kOnlineLobbyHintsOffKey));
            return keys;
        }

    }  // namespace

    TEST(MahjongOnlineWords, EveryKnownCodeHasAKeyInBothLanguagesWithOnlyTheDocumentedPlaceholders) {
        const Map<String, String> en = TextsOf("en");
        const Map<String, String> ru = TextsOf("ru");
        ASSERT_FALSE(en.empty());
        ASSERT_FALSE(ru.empty());

        for (const String& key : AllOnlineKeys()) {
            ASSERT_TRUE(en.contains(key)) << key << " is missing in en";
            ASSERT_TRUE(ru.contains(key)) << key << " is missing in ru";
            EXPECT_EQ(PlaceholderNames(en.at(key)), PlaceholdersAllowedFor(key)) << key;
            EXPECT_EQ(PlaceholderNames(ru.at(key)), PlaceholdersAllowedFor(key)) << key;
        }
    }

    TEST(MahjongOnlineWords, EveryCodeOfTheRoomLayersIsInTheTable) {
        const StringView codes[] = {
            RoomErrors::kBadMessage,
            RoomErrors::kUnsupportedVersion,
            RoomErrors::kMessageTooLarge,
            RoomErrors::kRateLimited,
            RoomErrors::kUnknownGame,
            RoomErrors::kUnknownRoom,
            RoomErrors::kRoomFull,
            RoomErrors::kNotInRoom,
            RoomErrors::kAlreadyInRoom,
            RoomErrors::kNotHost,
            RoomErrors::kGameStarted,
            RoomErrors::kNotStarted,
            RoomErrors::kSeatTaken,
            RoomErrors::kInvalidSeat,
            RoomErrors::kStaleAction,
            RoomErrors::kIllegalAction,
            RoomErrors::kNotYourTurn,
            RoomErrors::kBadName,
            RoomErrors::kBadToken,
            RoomErrors::kTooManyRooms,
            RoomErrors::kInternalError,
            OnlineCodes::kInvalidParams,
            RoomClientErrors::kWrongState,
            RoomClientErrors::kTransportUnavailable,
            RoomClientErrors::kConnectFailed,
            RoomClientErrors::kBadServerMessage,
            RoomClientErrors::kInvalidSession,
            RoomClientCloseReasons::kRoomGone,
            RoomClientCloseReasons::kReplaced,
            RoomClientCloseReasons::kGaveUp,
            RoomClientCloseReasons::kConnectTimeout,
            OnlineCodes::kTooManyFailedJoins,
            OnlineCodes::kServerFull,
            OnlineCodes::kIdle,
            OnlineCodes::kRoomClosed,
            OnlineCodes::kRoomFinished,
        };
        for (const StringView code : codes) {
            EXPECT_NE(OnlineErrorKey(code), kOnlineUnknownKey) << Std(code);
        }
        EXPECT_EQ(OnlineErrorKey(RoomClientCloseReasons::kRateLimited), OnlineErrorKey(RoomErrors::kRateLimited));
        // A code is listed once
        Set<String> seen;
        for (const OnlineCodeWord& word : kKnownOnlineCodes) {
            EXPECT_TRUE(seen.insert(Std(word.code)).second) << Std(word.code) << " is listed twice";
        }
    }

    TEST(MahjongOnlineWords, AnUnknownCodeGoesToTheUnknownKeyAndNamesTheCode) {
        EXPECT_EQ(OnlineErrorKey("from_a_newer_server"), kOnlineUnknownKey);
        EXPECT_EQ(OnlineErrorKey(""), kOnlineUnknownKey);

        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ForEachLanguage(strings, [&](PoolString) {
            const String text = OnlineNoticeText(strings, OnlineErrorKey("from_a_newer_server"), "from_a_newer_server");
            EXPECT_NE(Std(text).find("from_a_newer_server"), String::npos) << Std(text);
            EXPECT_EQ(Std(text).find('{'), String::npos) << Std(text);
        });
    }

    TEST(MahjongOnlineWords, OnlyStaleActionIsSilent) {
        EXPECT_TRUE(IsSilentOnlineCode(RoomErrors::kStaleAction));
        for (const OnlineCodeWord& word : kKnownOnlineCodes) {
            if (word.code != RoomErrors::kStaleAction) {
                EXPECT_FALSE(IsSilentOnlineCode(word.code)) << Std(word.code);
            }
        }
        EXPECT_FALSE(IsSilentOnlineCode("unheard_of"));
    }

    TEST(MahjongOnlineWords, EveryKeyIsShownAsAWordInBothLanguages) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ForEachLanguage(strings, [&](PoolString language) {
            for (const String& key : AllOnlineKeys()) {
                const String text = OnlineNoticeText(strings, key, "x");
                EXPECT_NE(Std(text), key) << language.ToStringView().data() << ": " << key << " has no text";
                EXPECT_FALSE(text.empty()) << key;
            }
        });
    }

    TEST(MahjongOnlineWords, SeatTextForEachKindInBothLanguages) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        const StringView name = "Анна 🀄";

        ForEachLanguage(strings, [&](PoolString language) {
            const bool russian = language == "ru"_intern;
            EXPECT_EQ(OnlineSeatText(strings, BECore::SeatKind::Empty, "", false, false), russian ? "Свободно" : "Empty");
            EXPECT_EQ(OnlineSeatText(strings, BECore::SeatKind::Computer, "", false, false), russian ? "Компьютер" : "Computer");
            EXPECT_EQ(OnlineSeatText(strings, BECore::SeatKind::Human, name, false, true), name);
            EXPECT_EQ(OnlineSeatText(strings, BECore::SeatKind::Human, name, true, true), (russian ? "Анна 🀄 (вы)" : "Анна 🀄 (you)"));
            EXPECT_EQ(OnlineSeatText(strings, BECore::SeatKind::Human, name, false, false), (russian ? "Анна 🀄 (не в сети)" : "Анна 🀄 (away)"));
            // Mine wins over away
            EXPECT_EQ(OnlineSeatText(strings, BECore::SeatKind::Human, name, true, false), OnlineSeatText(strings, BECore::SeatKind::Human, name, true, true));
        });
    }

}  // namespace BECore::Mahjong::Tests
