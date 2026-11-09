#include "HandTestSupport.h"
#include "StringsTestSupport.h"

#include <Mahjong/Table/MoveText.h>
#include <Mahjong/Table/SeatName.h>
#include <set>

/**
 * The move line of the HUD (`Table/MoveText.h`). Pure logic: no engine services. The English and Russian sentences are written out here,
 * and every kind of move is asked in every language of the table.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        const UIStrings& Words(std::string_view language) {
            static UIStrings english;
            static UIStrings russian;
            static const bool loaded = [] {
                EXPECT_TRUE(LoadShippedStrings(english).empty());
                EXPECT_TRUE(LoadShippedStrings(russian).empty());
                EXPECT_TRUE(english.SetLanguage("en"_intern));
                EXPECT_TRUE(russian.SetLanguage("ru"_intern));
                return true;
            }();
            (void)loaded;
            return language == "ru" ? russian : english;
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.data(), text.size());
        }

        std::string En(const HandMove& move, Wind wind) {
            return Std(MoveText(Words("en"), move, wind));
        }

        std::string Ru(const HandMove& move, Wind wind) {
            return Std(MoveText(Words("ru"), move, wind));
        }

        HandMove Discard(Seat seat, const char* tile, bool declaring = false) {
            return HandMove{.kind = MoveKind::Discard, .seat = seat, .tile = T(tile), .declaredCalling = declaring};
        }

        HandMove Claim(MoveKind kind, Seat seat, const char* tile, Seat from) {
            return HandMove{.kind = kind, .seat = seat, .tile = T(tile), .from = from, .meld = 0};
        }

        HandMove ConcealedKong(Seat seat) {
            return HandMove{.kind = MoveKind::ConcealedKong, .seat = seat, .meld = 1};
        }

        HandMove UpgradeKong(Seat seat, const char* tile) {
            return HandMove{.kind = MoveKind::UpgradeKong, .seat = seat, .tile = T(tile), .meld = 0};
        }

        /// Every kind of move, with the tiles and seats the sentences name.
        std::vector<HandMove> EveryMove() {
            return {Discard(1, "5p"), Discard(2, "Ew", true), Claim(MoveKind::ClaimChow, 2, "4s", 1), Claim(MoveKind::ClaimPung, 3, "Rd", 1), Claim(MoveKind::ClaimKong, 0, "9m", 2),
                    ConcealedKong(3), UpgradeKong(1, "F1")};
        }

    }  // namespace

    TEST(MoveTextTest, EnglishSentences) {
        EXPECT_EQ(En(Discard(1, "5p"), Wind::South), "Seat 2 (South) discarded 5 Dots");
        EXPECT_EQ(En(Discard(2, "Ew", true), Wind::West), "Seat 3 (West) discarded East Wind and declared a ready hand");
        EXPECT_EQ(En(Claim(MoveKind::ClaimChow, 2, "4s", 1), Wind::West), "Seat 3 (West) claimed 4 Bamboo for a chow");
        EXPECT_EQ(En(Claim(MoveKind::ClaimPung, 3, "Rd", 1), Wind::North), "Seat 4 (North) claimed Red Dragon for a pung");
        EXPECT_EQ(En(Claim(MoveKind::ClaimKong, 0, "9m", 2), Wind::East), "Seat 1 (East) claimed 9 Characters for a kong");
        EXPECT_EQ(En(ConcealedKong(3), Wind::North), "Seat 4 (North) declared a concealed kong");
        EXPECT_EQ(En(UpgradeKong(1, "F1"), Wind::South), "Seat 2 (South) added Flower 1 to a pung to make a kong");
    }

    TEST(MoveTextTest, RussianSentences) {
        EXPECT_EQ(Ru(Discard(1, "5p"), Wind::South), "Место 2 (Юг) сбросило Круги 5");
        EXPECT_EQ(Ru(Discard(2, "Ew", true), Wind::West), "Место 3 (Запад) сбросило Ветер: Восток и объявило заявку");
        EXPECT_EQ(Ru(Claim(MoveKind::ClaimChow, 2, "4s", 1), Wind::West), "Место 3 (Запад) взяло Бамбук 4 в чи");
        EXPECT_EQ(Ru(Claim(MoveKind::ClaimPung, 3, "Rd", 1), Wind::North), "Место 4 (Север) взяло Красный дракон в пун");
        EXPECT_EQ(Ru(Claim(MoveKind::ClaimKong, 0, "9m", 2), Wind::East), "Место 1 (Восток) взяло Знаки 9 в конг");
        EXPECT_EQ(Ru(ConcealedKong(3), Wind::North), "Место 4 (Север) выложило скрытый конг");
        EXPECT_EQ(Ru(UpgradeKong(1, "F1"), Wind::South), "Место 2 (Юг) добавило Цветок 1 к пуну, получился конг");
    }

    TEST(MoveTextTest, EveryKindIsWordedInEveryLanguageWithNoPlaceholderLeftAndTheKindsAreDistinct) {
        for (const char* language : {"en", "ru"}) {
            SCOPED_TRACE(language);
            const UIStrings& strings = Words(language);
            std::set<std::string> sentences;
            for (const HandMove& move : EveryMove()) {
                const std::string text = Std(MoveText(strings, move, Wind::South));
                EXPECT_FALSE(text.empty());
                EXPECT_EQ(text.find('{'), std::string::npos) << text;
                EXPECT_EQ(text.find('}'), std::string::npos) << text;
                EXPECT_EQ(text.find("move."), std::string::npos) << "the key is missing: " << text;
                EXPECT_NE(text.find(Std(SeatName(strings, move.seat, Wind::South))), std::string::npos) << text << " names its seat";
                sentences.insert(text);
            }
            EXPECT_EQ(sentences.size(), EveryMove().size());
        }
    }

    TEST(MoveTextTest, TheSentencesOfAClaimNameTheTileAndTheConcealedKongNamesNone) {
        for (const UIStrings* strings : {&Words("en"), &Words("ru")}) {
            const HandMove concealed = ConcealedKong(2);
            const std::string kong = Std(MoveText(*strings, concealed, Wind::West));
            // No tile of the four: not a rank, not a suit, not a dragon, not a wind
            for (const char* word : {"Dots", "Bamboo", "Characters", "Dragon", "Wind", "Круги", "Бамбук", "Знаки", "дракон", "Ветер"}) {
                EXPECT_EQ(kong.find(word), std::string::npos) << kong;
            }
            // A concealed kong carries a tile in no case: even a move that has one says none
            HandMove withTile = concealed;
            withTile.tile = T("5p");
            EXPECT_EQ(Std(MoveText(*strings, withTile, Wind::West)), kong);
        }
    }

    TEST(MoveTextTest, TheRussianWordsKeepTheirOrderOfTheTranslation) {
        // The tile is not at the end in every sentence: the order belongs to the translation, the code joins nothing
        const std::string chow = Ru(Claim(MoveKind::ClaimChow, 1, "1s", 0), Wind::South);
        EXPECT_LT(chow.find("Бамбук 1"), chow.find("в чи"));
        const std::string upgrade = Ru(UpgradeKong(1, "5p"), Wind::South);
        EXPECT_LT(upgrade.find("Круги 5"), upgrade.find("к пуну"));
    }

    TEST(MoveTextTest, AMoveOfNoSeatSaysNothing) {
        HandMove none;
        none.seat = kNoSeat;
        EXPECT_TRUE(MoveText(Words("en"), none, Wind::East).empty());
        none.seat = kSeatCount;
        EXPECT_TRUE(MoveText(Words("ru"), none, Wind::East).empty());
    }

    TEST(MoveTextTest, ADiscardOfNoTileStillSaysWhoDidIt) {
        HandMove move = Discard(0, "5p");
        move.tile = Tile{};
        EXPECT_EQ(En(move, Wind::East), "Seat 1 (East) discarded No tile");
    }

}  // namespace BECore::Mahjong::Tests
