#include "StringsTestSupport.h"

#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/RulesText.h>
#include <set>

/**
 * The rules pages (`Table/RulesText.h`). Pure logic over the shipped string table: no engine services. The tests hold the limits of a page, the
 * examples to what the page says, the height to the plate and, above all, that no number is retyped in a text (the drift guard).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// The shipped table, loaded once; the tests change its language through `ForEachLanguage` and put it back.
        UIStrings& Words() {
            static UIStrings strings;
            static const bool loaded = [] {
                EXPECT_TRUE(LoadShippedStrings(strings).empty());
                EXPECT_TRUE(strings.SetLanguage("en"_intern));
                return true;
            }();
            (void)loaded;
            return strings;
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.data(), text.size());
        }

        /// Whether `text` holds a digit outside its `{placeholders}`: a number that was typed in and not taken from the rules.
        bool HasDigitOutsidePlaceholders(std::string_view text) {
            bool inside = false;
            for (const char c : text) {
                if (c == '{') {
                    inside = true;
                } else if (c == '}') {
                    inside = false;
                } else if (!inside && c >= '0' && c <= '9') {
                    return true;
                }
            }
            return false;
        }

        template <typename Check>
        void ForEveryPage(Check&& check) {
            UIStrings& strings = Words();
            ForEachLanguage(strings, [&](PoolString language) {
                for (int page = 0; page < RulesPageCount(); ++page) {
                    check(language, page, FormatRulesPage(strings, page));
                }
            });
        }

        /// The whole text of a page in one string.
        std::string AllText(const RulesPage& page) {
            std::string text = Std(page.title);
            for (const eastl::string& line : page.lines) {
                text += '\n' + Std(line);
            }
            for (const eastl::string& item : page.items) {
                text += '\n' + Std(item);
            }
            return text;
        }

        bool Contains(const std::string& text, int number) {
            return text.find(std::to_string(number)) != std::string::npos;
        }

        bool Contains(const std::string& text, const eastl::string& word) {
            return text.find(Std(word)) != std::string::npos;
        }

        std::string Where(PoolString language, int page) {
            return std::string(language.ToStringView().data(), language.ToStringView().size()) + ", page " + std::to_string(page + 1);
        }

    }  // namespace

    // ----- the pages -----

    TEST(RulesTextTest, ThePagesAreThere) {
        EXPECT_EQ(RulesPageCount(), 7);  // pages 1 to 7: tiles, sets, a turn, claims, kongs, the calling hand, the end of a hand
    }

    TEST(RulesTextTest, EveryPageHasATitleAndTextWithinTheLimitsInEveryLanguage) {
        ForEveryPage([](PoolString language, int page, const RulesPage& content) {
            const std::string where = Where(language, page);
            EXPECT_FALSE(content.title.empty()) << where;
            EXPECT_FALSE(content.lines.empty() && content.items.empty()) << where;
            EXPECT_LE(content.lines.size(), static_cast<size_t>(kRulesMaxLines)) << where;
            EXPECT_LE(content.items.size(), static_cast<size_t>(kRulesMaxItems)) << where;
            EXPECT_LE(content.tileRows.size(), static_cast<size_t>(kRulesMaxTileRows)) << where;
            for (const eastl::vector<Tile>& row : content.tileRows) {
                EXPECT_FALSE(row.empty()) << where;
                EXPECT_LE(row.size(), static_cast<size_t>(kRulesMaxRowTiles)) << where;
            }
            for (const eastl::string& line : content.lines) {
                EXPECT_FALSE(line.empty()) << where;
                EXPECT_LE(Utf8::CountCodePoints(line), kRulesMaxLineLength) << where << ": " << line.c_str();
            }
        });
    }

    TEST(RulesTextTest, NoTextIsLeftWithAPlaceholderOrAMissingKey) {
        ForEveryPage([](PoolString language, int page, const RulesPage& content) {
            const std::string text = AllText(content);
            EXPECT_EQ(text.find('{'), std::string::npos) << Where(language, page) << ": " << text;
            EXPECT_EQ(text.find('}'), std::string::npos) << Where(language, page) << ": " << text;
            EXPECT_EQ(text.find("rules."), std::string::npos) << Where(language, page) << ": a key is missing in " << text;
        });
    }

    TEST(RulesTextTest, NoTwoPagesShareATitleAndEveryLanguageSaysItsOwn) {
        std::set<std::string> english;
        std::set<std::string> russian;
        ForEveryPage([&](PoolString language, int, const RulesPage& content) {
            const bool isRussian = language == "ru"_intern;
            EXPECT_TRUE((isRussian ? russian : english).insert(Std(content.title)).second) << content.title.c_str() << " is the title of two pages";
        });
        EXPECT_EQ(english.size(), static_cast<size_t>(RulesPageCount()));
        EXPECT_EQ(russian.size(), static_cast<size_t>(RulesPageCount()));
        for (const std::string& title : russian) {
            EXPECT_FALSE(english.contains(title)) << title << " is not translated";
        }
    }

    TEST(RulesTextTest, EveryTileOfAnExampleIsAPlayableTile) {
        ForEveryPage([](PoolString language, int page, const RulesPage& content) {
            for (const eastl::vector<Tile>& row : content.tileRows) {
                for (const Tile tile : row) {
                    EXPECT_TRUE(tile.IsValid() && tile.IsPlayable()) << Where(language, page);
                }
            }
        });
    }

    TEST(RulesTextTest, TheTilePagesShowTheTilesAndTheOthersNone) {
        const UIStrings& strings = Words();
        const RulesPage tiles = FormatRulesPage(strings, 0);
        ASSERT_EQ(tiles.tileRows.size(), 2u);
        ASSERT_EQ(tiles.tileRows[0].size(), static_cast<size_t>(kRanksPerSuit));  // one suit, from the lowest rank to the highest
        for (int rank = 1; rank <= kRanksPerSuit; ++rank) {
            EXPECT_EQ(tiles.tileRows[0][static_cast<size_t>(rank - 1)], Tile::Suited(TileSuit::Dots, rank));
        }
        EXPECT_EQ(tiles.tileRows[1].size(), static_cast<size_t>(kWindCount + kDragonCount));  // the honours
        for (const Tile tile : tiles.tileRows[1]) {
            EXPECT_TRUE(tile.IsHonour());
        }
        EXPECT_TRUE(FormatRulesPage(strings, 2).tileRows.empty());  // a turn
        EXPECT_TRUE(FormatRulesPage(strings, 3).tileRows.empty());  // claims
        EXPECT_TRUE(FormatRulesPage(strings, 4).tileRows.empty());  // kongs
        EXPECT_TRUE(FormatRulesPage(strings, 6).tileRows.empty());  // the end of a hand
    }

    TEST(RulesTextTest, TheExampleOfTheSetsPageIsAWholeWinningHand) {
        const RulesPage page = FormatRulesPage(Words(), 1);
        ASSERT_EQ(page.tileRows.size(), 1u);
        const eastl::vector<Tile>& hand = page.tileRows[0];
        EXPECT_EQ(hand.size(), static_cast<size_t>(kDealtTiles + 1));
        EXPECT_EQ(hand.size(), static_cast<size_t>(kWinningHandTiles));
        const TileCounts counts = CountTiles(hand);
        EXPECT_EQ(TotalTiles(counts), kWinningHandTiles);
        EXPECT_TRUE(IsWinningShape(counts, 0));
        const eastl::vector<Decomposition> splits = Decompose(counts, kSetsInHand);
        ASSERT_FALSE(splits.empty());  // four sets and a pair, not one of the special hands
        // the page says: a chow, a pung, a chow, a pung of dragons and a pair
        const Decomposition& split = splits[0];
        ASSERT_EQ(split.sets.size(), static_cast<size_t>(kSetsInHand));
        int chows = 0;
        int pungs = 0;
        int dragonPungs = 0;
        for (const ShapeSet& set : split.sets) {
            if (set.kind == MeldKind::Chow) {
                ++chows;
            } else {
                ++pungs;
                dragonPungs += set.first.IsDragon() ? 1 : 0;
            }
        }
        EXPECT_EQ(chows, 2);
        EXPECT_EQ(pungs, 2);
        EXPECT_EQ(dragonPungs, 1);
    }

    TEST(RulesTextTest, TheExampleOfTheCallingPageIsAWaitingHandThatIsNotComplete) {
        const RulesPage page = FormatRulesPage(Words(), 5);
        ASSERT_EQ(page.tileRows.size(), 1u);
        const eastl::vector<Tile>& hand = page.tileRows[0];
        EXPECT_EQ(hand.size(), static_cast<size_t>(kDealtTiles));
        const TileCounts counts = CountTiles(hand);
        EXPECT_TRUE(IsWaiting(counts, 0, TileCounts{}));
        const eastl::vector<Tile> waits = WinningTiles(counts, 0);
        EXPECT_EQ(waits.size(), 2u);  // "this hand waits for two kinds"
        for (const Tile wait : waits) {
            TileCounts completed = counts;
            ++completed[wait.Id()];
            EXPECT_TRUE(IsWinningShape(completed, 0));
        }
    }

    // ----- the height -----

    TEST(RulesTextTest, TheHeightOfEveryPageFitsThePlate) {
        ForEveryPage([](PoolString language, int page, const RulesPage& content) {
            EXPECT_LE(PageHeight(content), kRulesInnerHeight) << Where(language, page);
            EXPECT_GT(PageHeight(content), kRulesTitleHeight + kRulesPagerHeight) << Where(language, page);
        });
    }

    TEST(RulesTextTest, PageHeightCountsTheBlocksAPageUses) {
        RulesPage page;
        const int head = kRulesTitleHeight + kRulesBlockGap + kRulesPagerHeight;
        EXPECT_EQ(PageHeight(page), head);

        page.lines.resize(1);
        EXPECT_EQ(PageHeight(page), head + kRulesBlockGap + kRulesLineHeight);
        page.lines.resize(3);
        EXPECT_EQ(PageHeight(page), head + kRulesBlockGap + 3 * kRulesLineHeight + 2 * kRulesLineGap);

        page.items.resize(3);  // two columns: two rows
        const int withItems = head + kRulesBlockGap + 3 * kRulesLineHeight + 2 * kRulesLineGap + kRulesBlockGap + 2 * kRulesItemHeight + kRulesItemGap;
        EXPECT_EQ(PageHeight(page), withItems);

        page.tileRows.resize(2);
        EXPECT_EQ(PageHeight(page), withItems + 2 * (kRulesBlockGap + kRulesTileHeight));
    }

    TEST(RulesTextTest, TheLimitsOfAPageAreWhatThePlateHoldsOnlyInTheCombinationsThePagesUse) {
        RulesPage lines;  // the lines with the tile rows
        lines.lines.resize(kRulesMaxLines);
        lines.tileRows.resize(kRulesMaxTileRows);
        EXPECT_LE(PageHeight(lines), kRulesInnerHeight);

        RulesPage items;  // at most four lines with all the items
        items.lines.resize(4);
        items.items.resize(kRulesMaxItems);
        EXPECT_LE(PageHeight(items), kRulesInnerHeight);

        RulesPage everything;  // the worst of every group does not fit: a page chooses
        everything.lines.resize(kRulesMaxLines);
        everything.items.resize(kRulesMaxItems);
        everything.tileRows.resize(kRulesMaxTileRows);
        EXPECT_GT(PageHeight(everything), kRulesInnerHeight);
    }

    // ----- out of range and the pager -----

    TEST(RulesTextTest, AnIndexOutOfRangeIsAnEmptyPage) {
        for (const int page : {-1, RulesPageCount(), RulesPageCount() + 5, 1000, -1000}) {
            const RulesPage content = FormatRulesPage(Words(), page);
            EXPECT_TRUE(content.title.empty()) << page;
            EXPECT_TRUE(content.lines.empty()) << page;
            EXPECT_TRUE(content.items.empty()) << page;
            EXPECT_TRUE(content.tileRows.empty()) << page;
        }
    }

    TEST(RulesTextTest, ThePagerSaysWhereYouAre) {
        UIStrings& strings = Words();
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        EXPECT_EQ(Std(RulesPagerText(strings, 0)), "Page 1 of 7");
        EXPECT_EQ(Std(RulesPagerText(strings, 2)), "Page 3 of 7");
        EXPECT_EQ(Std(RulesPagerText(strings, RulesPageCount() - 1)), "Page 7 of 7");
        EXPECT_EQ(Std(RulesPagerText(strings, -4)), "Page 1 of 7");  // clamped
        EXPECT_EQ(Std(RulesPagerText(strings, 100)), "Page 7 of 7");
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_EQ(Std(RulesPagerText(strings, 0)), "Страница 1 из 7");
        EXPECT_EQ(Std(RulesPagerText(strings, 6)), "Страница 7 из 7");
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
    }

    // ----- the numbers come from the rules -----

    TEST(RulesTextTest, NoTextOfThePagesHoldsANumberThatWasTypedIn) {
        EXPECT_FALSE(HasDigitOutsidePlaceholders("Page {n} of {total}"));
        EXPECT_FALSE(HasDigitOutsidePlaceholders("three in a row"));
        EXPECT_TRUE(HasDigitOutsidePlaceholders("13 tiles"));
        EXPECT_TRUE(HasDigitOutsidePlaceholders("{dealt} tiles and 2 more"));
        EXPECT_TRUE(HasDigitOutsidePlaceholders("x{n}2"));

        int checked = 0;
        for (const LanguageEntries& language : ReadShippedEntries()) {
            for (const StringEntry& entry : language.entries) {
                if (!entry.key.starts_with("rules.")) {
                    continue;
                }
                ++checked;
                EXPECT_FALSE(HasDigitOutsidePlaceholders(entry.text)) << language.id << ": " << entry.key << ": " << entry.text;
            }
        }
        EXPECT_GE(checked, 2 * (1 + 7 + 4 * 7));  // at least the pager, the titles and four lines of each page in both languages
    }

    TEST(RulesTextTest, ThePagesNameTheNumbersOfTheRulesAsTheCodeHasThem) {
        ForEveryPage([](PoolString language, int page, const RulesPage& content) {
            const std::string text = AllText(content);
            const std::string where = Where(language, page);
            switch (page) {
                case 0:  // tiles
                    EXPECT_TRUE(Contains(text, static_cast<int>(kTileSetSize))) << where;
                    EXPECT_TRUE(Contains(text, kCopiesPerTile)) << where;
                    EXPECT_TRUE(Contains(text, kRanksPerSuit)) << where;
                    break;
                case 1:  // sets
                    EXPECT_TRUE(Contains(text, kWinningHandTiles)) << where;
                    EXPECT_TRUE(Contains(text, kDealtTiles)) << where;
                    EXPECT_TRUE(Contains(text, kPairsInSevenPairs)) << where;
                    EXPECT_TRUE(Contains(text, kOrphanKinds)) << where;
                    break;
                case 2:  // a turn
                    EXPECT_TRUE(Contains(text, kDealtTiles)) << where;
                    EXPECT_TRUE(Contains(text, kWinningHandTiles)) << where;
                    break;
                case 4:  // kongs
                    EXPECT_TRUE(Contains(text, static_cast<int>(kLooseTiles))) << where;
                    EXPECT_TRUE(Contains(text, static_cast<int>(kDeadWallTiles))) << where;
                    break;
                case 5:  // the calling hand
                    EXPECT_TRUE(Contains(text, kDealtTiles)) << where;
                    break;
                default:
                    break;
            }
        });
    }

    TEST(RulesTextTest, ThePagesNameTheButtonsAsTheTableShowsThem) {
        UIStrings& strings = Words();
        ForEachLanguage(strings, [&](PoolString language) {
            const std::string where = std::string(language.ToStringView().data(), language.ToStringView().size());
            const std::string turn = AllText(FormatRulesPage(strings, 2));
            for (const ButtonKind kind : {ButtonKind::ClaimPung, ButtonKind::ClaimKong, ButtonKind::ClaimChow0, ButtonKind::Pass, ButtonKind::ClaimMahjong, ButtonKind::Declare}) {
                EXPECT_TRUE(Contains(turn, WordOf(strings, kind))) << where << ": " << WordOf(strings, kind).c_str();
            }
            const std::string claims = AllText(FormatRulesPage(strings, 3));
            for (const ButtonKind kind : {ButtonKind::ClaimPung, ButtonKind::ClaimKong, ButtonKind::ClaimChow0, ButtonKind::Pass, ButtonKind::ClaimMahjong}) {
                EXPECT_TRUE(Contains(claims, WordOf(strings, kind))) << where << ": " << WordOf(strings, kind).c_str();
            }
            EXPECT_TRUE(Contains(AllText(FormatRulesPage(strings, 4)), WordOf(strings, ButtonKind::UpgradeKong))) << where;
            EXPECT_TRUE(Contains(AllText(FormatRulesPage(strings, 5)), WordOf(strings, ButtonKind::Declare))) << where;
            EXPECT_TRUE(Contains(AllText(FormatRulesPage(strings, 6)), Tr(strings, "handover.next"))) << where;
        });
    }

    TEST(RulesTextTest, TheTextFollowsTheLanguageAndTheTilesDoNot) {
        UIStrings& strings = Words();
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        eastl::vector<RulesPage> english;
        for (int page = 0; page < RulesPageCount(); ++page) {
            english.push_back(FormatRulesPage(strings, page));
        }
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        for (int page = 0; page < RulesPageCount(); ++page) {
            const RulesPage russian = FormatRulesPage(strings, page);
            const RulesPage& base = english[static_cast<size_t>(page)];
            EXPECT_NE(russian.title, base.title) << page;
            EXPECT_EQ(russian.lines.size(), base.lines.size()) << page;
            for (size_t line = 0; line < russian.lines.size() && line < base.lines.size(); ++line) {
                EXPECT_NE(russian.lines[line], base.lines[line]) << page << "/" << line;
            }
            EXPECT_EQ(russian.tileRows, base.tileRows) << page;  // a picture has no language
        }
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
    }

}  // namespace BECore::Mahjong::Tests
