#include "ScoreTestSupport.h"
#include "StringsTestSupport.h"

#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/RulesText.h>
#include <Mahjong/Table/ScoreText.h>
#include <set>

/**
 * The rules pages (`Table/RulesText.h`). Pure logic over the shipped string table: no engine services. The tests hold the limits of a page, the
 * examples to what the page says, the height to the plate and, above all, that no number is retyped in a text (the drift guard) and that
 * the numbers the pages print are what the scoring and the payments really do (`ScoreSeat`, `Settle`).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// The shipped table, loaded once; the tests change its language through `ForEachLanguage` and put it back.
        UIStrings& Words() {
            static UIStrings strings;
            static const bool loaded = LoadShippedStrings(strings).empty() && strings.SetLanguage("en"_intern);
            EXPECT_TRUE(loaded);
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

        /// The pages by name, as the tests below find them.
        constexpr int kPointsPage = 7;
        constexpr int kDoublesPage = 8;
        constexpr int kWinnerPage = 9;
        constexpr int kLimitPage = 10;
        constexpr int kPaymentsPage = 11;
        constexpr int kMatchPage = 12;

        std::string Where(PoolString language, int page) {
            return std::string(language.ToStringView().data(), language.ToStringView().size()) + ", page " + std::to_string(page + 1);
        }

    }  // namespace

    // ----- the pages -----

    TEST(RulesTextTest, ThePagesAreThere) {
        // tiles, sets, a turn, claims, kongs, the calling hand, the end of a hand, points, doubles, the winner's doubles, limit hands, payments, the match
        EXPECT_EQ(RulesPageCount(), 13);
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
            for (const eastl::string& item : content.items) {
                EXPECT_FALSE(item.empty()) << where;
            }
            if (!content.items.empty()) {
                EXPECT_LE(content.lines.size(), 4u) << where << ": the plate holds the items with four lines at most";
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
        for (int page = 6; page < RulesPageCount(); ++page) {       // the end of a hand, then the pages with numbers
            EXPECT_TRUE(FormatRulesPage(strings, page).tileRows.empty()) << page;
        }
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
        EXPECT_EQ(Std(RulesPagerText(strings, 0)), "Page 1 of 13");
        EXPECT_EQ(Std(RulesPagerText(strings, 2)), "Page 3 of 13");
        EXPECT_EQ(Std(RulesPagerText(strings, RulesPageCount() - 1)), "Page 13 of 13");
        EXPECT_EQ(Std(RulesPagerText(strings, -4)), "Page 1 of 13");  // clamped
        EXPECT_EQ(Std(RulesPagerText(strings, 100)), "Page 13 of 13");
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_EQ(Std(RulesPagerText(strings, 0)), "Страница 1 из 13");
        EXPECT_EQ(Std(RulesPagerText(strings, 12)), "Страница 13 из 13");
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
        EXPECT_GE(checked, 2 * (1 + 13 + 4 * 13));  // at least the pager, the titles and four lines of each page in both languages
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
                case kPointsPage:
                    EXPECT_TRUE(Contains(text, kWinPoints)) << where;
                    EXPECT_TRUE(Contains(text, kWinTileFromWallPoints)) << where;
                    EXPECT_TRUE(Contains(text, kOnlyWinningTilePoints)) << where;
                    EXPECT_TRUE(Contains(text, kHonourPointsFactor)) << where;
                    EXPECT_TRUE(Contains(text, kKongPointsFactor)) << where;
                    break;
                case kDoublesPage:
                    EXPECT_TRUE(Contains(text, LimitCap(kDefaultLimit))) << where;
                    EXPECT_TRUE(Contains(text, kScoreRounding)) << where;
                    break;
                case kWinnerPage:
                    EXPECT_TRUE(Contains(text, kWinPoints)) << where;
                    EXPECT_TRUE(Contains(text, kObviouslyLastCopies)) << where;
                    EXPECT_TRUE(Contains(text, kSeveralKongsCount)) << where;
                    break;
                case kLimitPage:
                    EXPECT_TRUE(Contains(text, LimitCap(kDefaultLimit))) << where;
                    EXPECT_TRUE(Contains(text, kOrphanKinds)) << where;
                    break;
                case kPaymentsPage:
                    EXPECT_TRUE(Contains(text, kEastPaymentFactor)) << where;
                    break;
                case kMatchPage:
                    EXPECT_TRUE(Contains(text, kStartingScore)) << where;
                    EXPECT_TRUE(Contains(text, kFullMatchHands)) << where;
                    EXPECT_TRUE(Contains(text, kShortMatchHands)) << where;
                    EXPECT_TRUE(Contains(text, kRoundsPerMatch)) << where;
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
            ASSERT_EQ(russian.items.size(), base.items.size()) << page;
            for (size_t item = 0; item < russian.items.size(); ++item) {
                EXPECT_NE(russian.items[item], base.items[item]) << page << "/item " << item;
            }
            for (size_t line = 0; line < russian.lines.size() && line < base.lines.size(); ++line) {
                EXPECT_NE(russian.lines[line], base.lines[line]) << page << "/" << line;
            }
            EXPECT_EQ(russian.tileRows, base.tileRows) << page;  // a picture has no language
        }
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
    }

    // ----- the numbers of the pages are what the scoring and the payments do -----

    namespace {

        /// The first line of `kind` of a breakdown; the test fails when there is none.
        ScoreLine LineOfKind(const ScoreBreakdown& breakdown, ScoreKind kind) {
            for (const ScoreLine& line : breakdown.lines) {
                if (line.kind == kind) {
                    return line;
                }
            }
            ADD_FAILURE() << "no line " << Name(kind);
            return ScoreLine{kind, Tile{}, 0, 0};
        }

        bool HasItem(const RulesPage& page, const eastl::string& text) {
            return eastl::find(page.items.begin(), page.items.end(), text) != page.items.end();
        }

        /// An example item as the page words it: the score line of `ScoreSeat` behind the prefix of its kind ("Open: Pung of 5 Dots: 2").
        eastl::string PointsItem(const UIStrings& strings, const char* prefixKey, const ScoreLine& line) {
            return prefixKey == nullptr ? ScoreLineText(strings, line) : Tr(strings, prefixKey, {{"line", ScoreLineText(strings, line)}});
        }

        /// A kind as the doubles pages print it: its name and the doubles of the score, no tile.
        eastl::string DoublesItem(const UIStrings& strings, const ScoreBreakdown& breakdown, ScoreKind kind) {
            return ScoreLineText(strings, ScoreLine{kind, Tile{}, 0, LineOfKind(breakdown, kind).doubles});
        }

    }  // namespace

    TEST(RulesTextTest, ThePointsPageHoldsTheNumbersTheScoringGives) {
        UIStrings& strings = Words();
        ForEachLanguage(strings, [&](PoolString language) {
            const std::string where = Where(language, kPointsPage);
            const RulesPage page = FormatRulesPage(strings, kPointsPage);

            // the examples: each is a hand scored by `ScoreSeat`, and the line it gives is in the items as the score table would print it
            struct Example {
                ScoreInput input;
                ScoreKind kind;
                const char* prefixKey;  // null for a line the page prints bare
            };
            const std::string quiet = kQuietChows;
            const Example examples[] = {
                {MakeInput(quiet, {OpenPung(T("5p"))}), ScoreKind::Pung, "rules.points.open"},            // an open pung of a simple tile
                {MakeInput(quiet, {OpenPung(T("9s"))}), ScoreKind::Pung, "rules.points.open"},            // of a terminal
                {MakeInput(quiet + " 5p 5p 5p"), ScoreKind::Pung, "rules.points.concealed"},              // a concealed pung
                {MakeInput(quiet, {OpenKong(T("5p"))}), ScoreKind::Kong, "rules.points.open"},            // an open kong
                {MakeInput(quiet, {ConcealedKong(T("Rd"))}), ScoreKind::Kong, "rules.points.concealed"},  // a concealed kong of an honour
                {MakeInput(quiet + " Rd Rd"), ScoreKind::Pair, nullptr},                                  // the pair of dragons
            };
            ASSERT_EQ(page.items.size(), std::size(examples)) << where;
            for (size_t i = 0; i < std::size(examples); ++i) {
                const ScoreLine line = LineOfKind(ScoreSeat(examples[i].input), examples[i].kind);
                EXPECT_EQ(page.items[i], PointsItem(strings, examples[i].prefixKey, line)) << where << ": item " << i;
            }

            // the winner's points
            const ScoreBreakdown winner = ScoreSeat(MakeWinnerInput("1p 2p 3p 7m 8m 9m 5s 5s 5s Rd Rd Rd 9p 9p", "9p"));
            EXPECT_EQ(LinePoints(winner, ScoreKind::Win), kWinPoints) << where;
            EXPECT_EQ(LinePoints(winner, ScoreKind::WinTileFromWall), kWinTileFromWallPoints) << where;
            EXPECT_EQ(LinePoints(winner, ScoreKind::OnlyWinningTile), kOnlyWinningTilePoints) << where;
            const std::string text = AllText(page);
            EXPECT_TRUE(Contains(text, LinePoints(winner, ScoreKind::Win))) << where;
            EXPECT_TRUE(Contains(text, LinePoints(winner, ScoreKind::WinTileFromWall))) << where;
            EXPECT_TRUE(Contains(text, LinePoints(winner, ScoreKind::OnlyWinningTile))) << where;

            // the chow is worth nothing and the pairs of the page are the ones that score
            EXPECT_EQ(ScoreSeat(MakeInput(kQuietChows, {OpenChow(T("4s"))})).points, 0) << where;
            EXPECT_EQ(ScoreSeat(MakeInput(quiet + " Sw Sw", {}, "", Wind::South)).points, kPairPoints) << where;
            EXPECT_EQ(ScoreSeat(MakeInput(quiet + " Sw Sw", {}, "", Wind::West, Wind::South)).points, kPairPoints) << where;
        });
    }

    TEST(RulesTextTest, TheDoublesForEveryoneAreTheDoublesTheScoringGives) {
        UIStrings& strings = Words();
        const std::string quiet = kQuietChows;
        const ScoreBreakdown dragon = ScoreSeat(MakeInput(quiet + " Rd Rd Rd"));
        const ScoreBreakdown own = ScoreSeat(MakeInput(quiet + " Sw Sw Sw", {}, "", Wind::South, Wind::East));
        const ScoreBreakdown round = ScoreSeat(MakeInput(quiet + " Sw Sw Sw", {}, "", Wind::East, Wind::South));
        const ScoreBreakdown withHonours = ScoreSeat(MakeInput("1p 2p 3p 4p 5p 6p 7p 8p 9p Ww Ww"));
        const ScoreBreakdown noHonours = ScoreSeat(MakeInput("1p 2p 3p 4p 5p 6p 7p 8p 9p"));
        ForEachLanguage(strings, [&](PoolString language) {
            const std::string where = Where(language, kDoublesPage);
            const RulesPage page = FormatRulesPage(strings, kDoublesPage);
            ASSERT_EQ(page.items.size(), 5u) << where;  // the five kinds from `DragonSet` to `OneSuitNoHonours`
            EXPECT_EQ(page.items[0], DoublesItem(strings, dragon, ScoreKind::DragonSet)) << where;
            EXPECT_EQ(page.items[1], DoublesItem(strings, own, ScoreKind::OwnWindSet)) << where;
            EXPECT_EQ(page.items[2], DoublesItem(strings, round, ScoreKind::RoundWindSet)) << where;
            EXPECT_EQ(page.items[3], DoublesItem(strings, withHonours, ScoreKind::OneSuitWithHonours)) << where;
            EXPECT_EQ(page.items[4], DoublesItem(strings, noHonours, ScoreKind::OneSuitNoHonours)) << where;
            // the order the page says: points times the multiplier, then the limit, then rounded up
            const ScoreBreakdown score = ScoreSeat(MakeInput(quiet + " Rd Rd Rd 5p 5p"));
            EXPECT_EQ(score.raw, score.points << score.doubles) << where;
            EXPECT_EQ(score.score, RoundUpToTen(std::min(score.raw, LimitCap(kDefaultLimit)))) << where;
        });
    }

    TEST(RulesTextTest, TheDoublesForTheWinnerAreTheDoublesTheScoringGives) {
        UIStrings& strings = Words();
        // one hand per kind from `OnlyPungs` to `CallingFirstHand`, each win scored by `ScoreSeat`
        constexpr const char* kNoPointsTiles = "5s 6s 7s 7m 8m 9m 3m 4m 5m 2p 3p 4p Ww Ww";
        const auto win = [](const char* concealed, const char* tile, WinSource source = WinSource::SelfDraw, std::initializer_list<Meld> melds = {}) {
            return MakeWinnerInput(concealed, tile, source, melds);
        };
        ScoreInput loose = win(kNoPointsTiles, "4p");
        loose.result.loose = true;
        ScoreInput last = win(kNoPointsTiles, "4p");
        last.result.lastLiveTile = true;
        ScoreInput obvious = win(kNoPointsTiles, "4p", WinSource::Discard);
        obvious.visibleWinningTileCopies = kObviouslyLastCopies;
        ScoreInput calling = win(kNoPointsTiles, "4p");
        calling.hand.callingDeclared = true;
        calling.hand.callingAtDiscardCount = 0;
        const eastl::vector<eastl::pair<ScoreKind, ScoreInput>> cases = {
            {ScoreKind::OnlyPungs, win("2p 2p 2p 5s 5s 5s 7m 7m 7m 9m 9m 9m 4s 4s", "4s")},
            {ScoreKind::NoPoints, win(kNoPointsTiles, "4p", WinSource::Discard)},
            {ScoreKind::LooseTile, loose},
            {ScoreKind::LastTile, last},
            {ScoreKind::RobbedKong, win(kNoPointsTiles, "4p", WinSource::RobbedKong)},
            {ScoreKind::LittleFourWinds, win("Ew Ew Ew Sw Sw Sw Ww Ww Ww Nw Nw 1p 2p 3p", "1p")},
            {ScoreKind::LittleThreeDragons, win("Rd Rd Rd Wd Wd Wd Gd Gd 1p 2p 3p 4s 5s 6s", "1p")},
            {ScoreKind::TerminalsOrHonoursEverywhere, win("1p 2p 3p 7m 8m 9m 9s 9s 9s Ew Ew Ew 1m 1m", "1m")},
            {ScoreKind::FullyConcealed, win(kNoPointsTiles, "4p")},
            {ScoreKind::SeveralKongs, win("5s 6s 7s 3m 4m 5m Ww Ww", "Ww", WinSource::SelfDraw, {OpenKong(T("2p")), ConcealedKong(T("9s"))})},
            {ScoreKind::ObviouslyLast, obvious},
            {ScoreKind::CallingFirstHand, calling},
        };
        constexpr int kWinnerKinds = static_cast<int>(ScoreKind::CallingFirstHand) - static_cast<int>(ScoreKind::OnlyPungs) + 1;
        ASSERT_EQ(cases.size(), static_cast<size_t>(kWinnerKinds));
        ForEachLanguage(strings, [&](PoolString language) {
            const std::string where = Where(language, kWinnerPage);
            const RulesPage page = FormatRulesPage(strings, kWinnerPage);
            ASSERT_EQ(page.items.size(), static_cast<size_t>(kWinnerKinds)) << where;
            for (size_t i = 0; i < cases.size(); ++i) {
                const ScoreKind kind = static_cast<ScoreKind>(static_cast<int>(ScoreKind::OnlyPungs) + static_cast<int>(i));
                ASSERT_EQ(cases[i].first, kind) << where;  // the cases run in the order of the enum, as the page does
                const ScoreBreakdown breakdown = ScoreSeat(cases[i].second);
                ASSERT_EQ(CountLines(breakdown, kind), 1) << where << ": " << Name(kind);
                EXPECT_EQ(page.items[i], DoublesItem(strings, breakdown, kind)) << where << ": " << Name(kind);
            }
        });
    }

    TEST(RulesTextTest, TheLimitHandsOfThePageAreTheKindsOfTheEnumRange) {
        UIStrings& strings = Words();
        constexpr int kFirst = static_cast<int>(ScoreKind::ConcealedPungsOneSuit);
        constexpr int kLast = static_cast<int>(ScoreKind::LooseTileFiveDots);
        constexpr int kLimitKinds = kLast - kFirst + 1;
        EXPECT_EQ(kLimitKinds, 12);
        ForEachLanguage(strings, [&](PoolString language) {
            const std::string where = Where(language, kLimitPage);
            const RulesPage page = FormatRulesPage(strings, kLimitPage);
            ASSERT_EQ(page.items.size(), static_cast<size_t>(kLimitKinds)) << where;
            std::set<std::string> names;
            for (int kind = kFirst; kind <= kLast; ++kind) {
                const eastl::string expected = ScoreLineText(strings, ScoreLine{static_cast<ScoreKind>(kind), Tile{}, 0, 0});
                EXPECT_EQ(page.items[static_cast<size_t>(kind - kFirst)], expected) << where;
                EXPECT_TRUE(HasItem(page, expected)) << where;
                names.insert(Std(expected));
            }
            EXPECT_EQ(names.size(), static_cast<size_t>(kLimitKinds)) << where << ": two limit hands have one name";
        });
    }

    TEST(RulesTextTest, EveryScoreKindFromTheDoublesOnIsOnSomePageExceptTheFlowers) {
        UIStrings& strings = Words();
        // The flowers and seasons are not offered to the player (`MahjongSettings::flowers` is false), so their doubles are not explained.
        const auto isFlowers = [](ScoreKind kind) { return kind == ScoreKind::OwnBonus || kind == ScoreKind::RoundBonus; };
        ForEachLanguage(strings, [&](PoolString language) {
            std::string all;
            for (int page = 0; page < RulesPageCount(); ++page) {
                all += AllText(FormatRulesPage(strings, page)) + '\n';
            }
            for (int kind = static_cast<int>(ScoreKind::DragonSet); kind < static_cast<int>(EnumUtils<ScoreKind>::Count()); ++kind) {
                const ScoreKind scoreKind = static_cast<ScoreKind>(kind);
                if (isFlowers(scoreKind)) {
                    continue;
                }
                EXPECT_TRUE(Contains(all, ScoreKindName(strings, scoreKind)))
                    << Std(eastl::string(language.ToStringView().data(), language.ToStringView().size())) << ": " << Name(scoreKind) << " is on no page";
            }
        });
    }

    TEST(RulesTextTest, ThePaymentsPageSaysWhatSettleDoes) {
        // The loser East pays the winner's score times the factor, a loser that is not East pays it once.
        constexpr int kWinnerScore = 100;
        const SeatScores deltas = Settle({0, kWinnerScore, 0, 0}, kSeat1, kSeat0);
        EXPECT_EQ(deltas[kSeat0], -kWinnerScore * kEastPaymentFactor);
        EXPECT_EQ(deltas[kSeat2], -kWinnerScore);
        EXPECT_EQ(deltas[kSeat3], -kWinnerScore);
        EXPECT_EQ(Sum(deltas), 0);  // "the points of the table always add up to the same total"
        // East as the winner receives the factor from everyone; the losers settle among themselves once (seat 2 has 40 more than seats 1 and 3)
        constexpr int kLead = 40;
        const SeatScores eastWins = Settle({kWinnerScore, 0, kLead, 0}, kSeat0, kSeat0);
        EXPECT_EQ(eastWins[kSeat0], 3 * kWinnerScore * kEastPaymentFactor);
        EXPECT_EQ(eastWins[kSeat1], -kWinnerScore * kEastPaymentFactor - kLead);
        EXPECT_EQ(eastWins[kSeat2], -kWinnerScore * kEastPaymentFactor + 2 * kLead);
        // a drawn hand pays nothing
        const SeatScores drawn = Settle({100, 50, 30, 0}, kNoSeat, kSeat0);
        for (const int delta : drawn) {
            EXPECT_EQ(delta, 0);
        }
        // the page names the factor in both languages, and as a number only: no word for it ("double", "вдвое") that would outlive a change
        UIStrings& strings = Words();
        ForEachLanguage(strings, [&](PoolString language) {
            const std::string text = AllText(FormatRulesPage(strings, kPaymentsPage));
            EXPECT_TRUE(Contains(text, kEastPaymentFactor)) << Where(language, kPaymentsPage);
            for (const char* word : {"double", "twice", "вдво", "дважды", "удво"}) {
                EXPECT_EQ(text.find(word), std::string::npos) << Where(language, kPaymentsPage) << ": " << word;
            }
        });
    }

}  // namespace BECore::Mahjong::Tests
