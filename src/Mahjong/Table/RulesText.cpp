#include "RulesText.h"

#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/Score.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/ScoreText.h>

namespace BECore::Mahjong {

    namespace {

        /// The tiles East starts with: the dealt tiles and the first draw (the first tile is drawn at once).
        inline constexpr int kEastOpeningTiles = kDealtTiles + 1;

        // What the match page says about the default game; a change of either rule fails the build, then the words of the page are the fix.
        static_assert(MahjongSettings{}.replayDrawnHand, "the page of the match says that a drawn hand is played again");
        static_assert(kEastRepeatCountsAsHand, "the page of the match says that an East repeat counts as a hand");

        /// The numbers and the words that the texts of the pages put in: `{dealt}`, `{pung}`, ... Built once per page from the named constants
        /// of the rules and the string table, so no text holds a number or a button word of its own. An unused name is harmless (`Subst`).
        /// Not copyable: the views of `_args` point into `_values`.
        class RulesArgs {
        public:
            explicit RulesArgs(const UIStrings& strings) : _strings(strings) {
                Number("tiles", static_cast<long long>(kTileSetSize));
                Number("ranks", kRanksPerSuit);
                Number("copies", kCopiesPerTile);
                Number("dealt", kDealtTiles);
                Number("winning", kWinningHandTiles);
                Number("eastDealt", kEastOpeningTiles);
                Number("pairs", kPairsInSevenPairs);
                Number("orphans", kOrphanKinds);
                Number("loose", static_cast<long long>(kLooseTiles));
                Number("deadWall", static_cast<long long>(kDeadWallTiles));

                // The points and the doubles (`Score.h`), the payments, the match.
                Number("winPoints", kWinPoints);
                Number("wallTilePoints", kWinTileFromWallPoints);
                Number("onlyTilePoints", kOnlyWinningTilePoints);
                Number("simplePung", kOpenSimplePungPoints);
                Number("honourFactor", kHonourPointsFactor);
                Number("concealedFactor", kConcealedPointsFactor);
                Number("kongFactor", kKongPointsFactor);
                Number("pairPoints", kPairPoints);
                Number("rounding", kScoreRounding);
                Number("limit", LimitCap(kDefaultLimit));
                Number("obviousCopies", kObviouslyLastCopies);
                Number("severalKongs", kSeveralKongsCount);
                Number("eastFactor", kEastPaymentFactor);
                Number("startScore", kStartingScore);
                Number("fullHands", kFullMatchHands);
                Number("shortHands", kShortMatchHands);
                Number("rounds", kRoundsPerMatch);
                Add("east", WindName(strings, Wind::East));
                Add("south", WindName(strings, Wind::South));
                Add("west", WindName(strings, Wind::West));
                Add("north", WindName(strings, Wind::North));

                Add("dots", Tr(strings, "suit.Dots"));
                Add("bamboo", Tr(strings, "suit.Bamboo"));
                Add("characters", Tr(strings, "suit.Characters"));

                Add("mahjong", WordOf(strings, ButtonKind::ClaimMahjong));
                Add("pung", WordOf(strings, ButtonKind::ClaimPung));
                Add("kong", WordOf(strings, ButtonKind::ClaimKong));
                Add("concealedKong", WordOf(strings, ButtonKind::ConcealedKong0));
                Add("chow", WordOf(strings, ButtonKind::ClaimChow0));
                Add("pass", WordOf(strings, ButtonKind::Pass));
                Add("win", WordOf(strings, ButtonKind::Win));
                Add("upgrade", WordOf(strings, ButtonKind::UpgradeKong));
                Add("declare", WordOf(strings, ButtonKind::Declare));
                Add("nextHand", Tr(strings, "handover.next"));

                // Built after the last `Add`, so no growth of `_values` moves a string under a view.
                _args.reserve(_values.size());
                for (const auto& [name, value] : _values) {
                    _args.push_back({name, eastl::string_view(value.data(), value.size())});
                }
            }
            RulesArgs(const RulesArgs&) = delete;
            RulesArgs& operator=(const RulesArgs&) = delete;

            /// The text of `key` with every placeholder filled.
            [[nodiscard]] eastl::string Text(eastl::string_view key) const {
                return Tr(_strings, key, eastl::span<const WordArg>(_args.data(), _args.size()));
            }

        private:
            void Add(eastl::string_view name, eastl::string value) {
                _values.push_back({name, eastl::move(value)});
            }
            void Number(eastl::string_view name, long long value) {
                Add(name, Format("{}", value));
            }

            const UIStrings& _strings;
            eastl::vector<eastl::pair<eastl::string_view, eastl::string>> _values;
            eastl::vector<WordArg> _args;
        };

        using TileRows = eastl::vector<eastl::vector<Tile>>;

        /// A row of tiles from the notation of `Tile.h`; the examples are written the way a player reads them.
        eastl::vector<Tile> Row(std::string_view notation) {
            const auto tiles = ParseTiles(notation);
            ASSERT(tiles.has_value(), "RulesText: an example row is not in the notation of Tile.h");
            return tiles.value_or(eastl::vector<Tile>{});
        }

        /// Page 1: one suit from the lowest rank to the highest, then the honours (four winds, three dragons).
        TileRows TileRowsOfTiles() {
            eastl::vector<Tile> suit;
            for (int rank = 1; rank <= kRanksPerSuit; ++rank) {
                suit.push_back(Tile::Suited(TileSuit::Dots, rank));
            }
            eastl::vector<Tile> honours;
            for (int wind = 0; wind < kWindCount; ++wind) {
                honours.push_back(Tile::FromWind(wind));
            }
            for (int dragon = 0; dragon < kDragonCount; ++dragon) {
                honours.push_back(Tile::FromDragon(dragon));
            }
            return {eastl::move(suit), eastl::move(honours)};
        }

        /// Page 2: a whole winning hand (`kWinningHandTiles`): a chow, a pung, a chow, a pung of dragons and a pair.
        TileRows TileRowsOfSets() {
            return {Row("2p 3p 4p 5s 5s 5s 7m 8m 9m Rd Rd Rd 9p 9p")};
        }

        /// Page 6: a hand that waits (`kDealtTiles` tiles): a chow, a pung, a chow, a pair and two tiles of a run that wait for 5p or 8p.
        TileRows TileRowsOfCalling() {
            return {Row("2p 3p 4p 5s 5s 5s 7m 8m 9m Ew Ew 6p 7p")};
        }

        using Items = eastl::vector<eastl::string>;

        /// A line of the score as the score table prints it ("Pung of 5 Dots: 2", "Dragon set: x2"), with the numbers given by the caller.
        eastl::string ScoreItem(const UIStrings& strings, ScoreKind kind, Tile tile, int points, int doubles) {
            return ScoreLineText(strings, ScoreLine{kind, tile, points, doubles});
        }

        /// The kinds of `first` to `last` as items, each with the doubles `doublesOf` says; the kinds are walked in the order of the enum, so
        /// a kind added between the two shows up by itself (and `doublesOf` must know it: the callers with a switch assert in its `default`).
        template <typename DoublesOf>
        Items DoublesItems(const UIStrings& strings, ScoreKind first, ScoreKind last, DoublesOf doublesOf) {
            Items items;
            for (size_t kind = static_cast<size_t>(first); kind <= static_cast<size_t>(last); ++kind) {
                const auto scoreKind = static_cast<ScoreKind>(kind);
                items.push_back(ScoreItem(strings, scoreKind, Tile{}, 0, doublesOf(scoreKind)));
            }
            return items;
        }

        /// Page 8: worked examples of the points of a set. Each number is the product of the named factors of `Score.h`, the same ones the
        /// scoring multiplies (`RulesTextTest` holds the examples to `ScoreSeat`). The tiles are examples, not numbers of the rules: any simple,
        /// terminal and dragon would do.
        Items ItemsOfPoints(const UIStrings& strings) {
            const Tile simple = Tile::Suited(TileSuit::Dots, 5);
            const Tile terminal = Tile::Suited(TileSuit::Bamboo, kRanksPerSuit);
            const Tile dragon = Tile::FromDragon(0);
            const int simplePung = kOpenSimplePungPoints;
            const int honourPung = simplePung * kHonourPointsFactor;
            const auto open = [&](const eastl::string& line) { return Tr(strings, "rules.points.open", {{"line", line}}); };
            const auto concealed = [&](const eastl::string& line) { return Tr(strings, "rules.points.concealed", {{"line", line}}); };
            Items items;
            items.push_back(open(ScoreItem(strings, ScoreKind::Pung, simple, simplePung, 0)));
            items.push_back(open(ScoreItem(strings, ScoreKind::Pung, terminal, honourPung, 0)));
            items.push_back(concealed(ScoreItem(strings, ScoreKind::Pung, simple, simplePung * kConcealedPointsFactor, 0)));
            items.push_back(open(ScoreItem(strings, ScoreKind::Kong, simple, simplePung * kKongPointsFactor, 0)));
            items.push_back(concealed(ScoreItem(strings, ScoreKind::Kong, dragon, honourPung * kConcealedPointsFactor * kKongPointsFactor, 0)));
            items.push_back(ScoreItem(strings, ScoreKind::Pair, dragon, kPairPoints, 0));
            return items;
        }

        /// Page 9: the doubles every seat can have. The value is the constant of the kind, so the page prints what the score table prints.
        Items ItemsOfDoubles(const UIStrings& strings) {
            return DoublesItems(strings, ScoreKind::DragonSet, ScoreKind::OneSuitNoHonours, [](ScoreKind kind) {
                switch (kind) {
                    case ScoreKind::DragonSet:
                        return kDragonSetDoubles;
                    case ScoreKind::OwnWindSet:
                        return kOwnWindSetDoubles;
                    case ScoreKind::RoundWindSet:
                        return kRoundWindSetDoubles;
                    case ScoreKind::OneSuitWithHonours:
                        return kOneSuitWithHonoursDoubles;
                    case ScoreKind::OneSuitNoHonours:
                        return kOneSuitNoHonoursDoubles;
                    default:
                        ASSERT(false, "RulesText: a kind between DragonSet and OneSuitNoHonours has no doubles here");
                        return 0;
                }
            });
        }

        /// Page 10: the doubles only the winner can have, from `OnlyPungs` to `CallingFirstHand`.
        Items ItemsOfWinnerDoubles(const UIStrings& strings) {
            return DoublesItems(strings, ScoreKind::OnlyPungs, ScoreKind::CallingFirstHand, [](ScoreKind kind) {
                switch (kind) {
                    case ScoreKind::OnlyPungs:
                        return kOnlyPungsDoubles;
                    case ScoreKind::NoPoints:
                        return kNoPointsDoubles;
                    case ScoreKind::LooseTile:
                        return kLooseTileDoubles;
                    case ScoreKind::LastTile:
                        return kLastTileDoubles;
                    case ScoreKind::RobbedKong:
                        return kRobbedKongDoubles;
                    case ScoreKind::LittleFourWinds:
                        return kLittleFourWindsDoubles;
                    case ScoreKind::LittleThreeDragons:
                        return kLittleThreeDragonsDoubles;
                    case ScoreKind::TerminalsOrHonoursEverywhere:
                        return kTerminalsOrHonoursEverywhereDoubles;
                    case ScoreKind::FullyConcealed:
                        return kFullyConcealedDoubles;
                    case ScoreKind::SeveralKongs:
                        return kSeveralKongsDoubles;
                    case ScoreKind::ObviouslyLast:
                        return kObviouslyLastDoubles;
                    case ScoreKind::CallingFirstHand:
                        return kCallingFirstHandDoubles;
                    default:
                        ASSERT(false, "RulesText: a kind between OnlyPungs and CallingFirstHand has no doubles here");
                        return 0;
                }
            });
        }

        /// Page 11: every limit hand, by name ("Limit: Thirteen orphans"); the walk is the range of the enum, as in `ScoreText.cpp`.
        Items ItemsOfLimitHands(const UIStrings& strings) {
            return DoublesItems(strings, ScoreKind::ConcealedPungsOneSuit, ScoreKind::LooseTileFiveDots, [](ScoreKind) { return 0; });
        }

        /// One page of the table: the key of its title, the keys of its lines (a null one ends the list) and the builders of its items and
        /// of its examples (null for none).
        struct PageSpec {
            const char* title;
            eastl::array<const char*, kRulesMaxLines> lines;
            Items (*items)(const UIStrings&);
            TileRows (*tileRows)();
        };

        // The keys are literals on purpose: the test of the strings finds them in the sources and holds every key of the file to a user.
        const PageSpec kPages[] = {
            {"rules.tiles.title", {"rules.tiles.line1", "rules.tiles.line2", "rules.tiles.line3", "rules.tiles.line4", nullptr, nullptr}, nullptr, &TileRowsOfTiles},
            {"rules.sets.title", {"rules.sets.line1", "rules.sets.line2", "rules.sets.line3", "rules.sets.line4", "rules.sets.line5", nullptr}, nullptr, &TileRowsOfSets},
            {"rules.turn.title", {"rules.turn.line1", "rules.turn.line2", "rules.turn.line3", "rules.turn.line4", nullptr, nullptr}, nullptr, nullptr},
            {"rules.claims.title", {"rules.claims.line1", "rules.claims.line2", "rules.claims.line3", "rules.claims.line4", "rules.claims.line5", nullptr}, nullptr, nullptr},
            {"rules.kongs.title", {"rules.kongs.line1", "rules.kongs.line2", "rules.kongs.line3", "rules.kongs.line4", "rules.kongs.line5", nullptr}, nullptr, nullptr},
            {"rules.calling.title", {"rules.calling.line1", "rules.calling.line2", "rules.calling.line3", "rules.calling.line4", "rules.calling.line5", nullptr}, nullptr, &TileRowsOfCalling},
            {"rules.end.title", {"rules.end.line1", "rules.end.line2", "rules.end.line3", "rules.end.line4", nullptr, nullptr}, nullptr, nullptr},
            {"rules.points.title", {"rules.points.line1", "rules.points.line2", "rules.points.line3", "rules.points.line4", nullptr, nullptr}, &ItemsOfPoints, nullptr},
            {"rules.doubles.title", {"rules.doubles.line1", "rules.doubles.line2", "rules.doubles.line3", "rules.doubles.line4", nullptr, nullptr}, &ItemsOfDoubles, nullptr},
            {"rules.winner.title", {"rules.winner.line1", "rules.winner.line2", "rules.winner.line3", "rules.winner.line4", nullptr, nullptr}, &ItemsOfWinnerDoubles, nullptr},
            {"rules.limit.title", {"rules.limit.line1", "rules.limit.line2", "rules.limit.line3", "rules.limit.line4", nullptr, nullptr}, &ItemsOfLimitHands, nullptr},
            {"rules.payments.title", {"rules.payments.line1", "rules.payments.line2", "rules.payments.line3", "rules.payments.line4", nullptr, nullptr}, nullptr, nullptr},
            {"rules.match.title", {"rules.match.line1", "rules.match.line2", "rules.match.line3", "rules.match.line4", nullptr, nullptr}, nullptr, nullptr},
        };

    }  // namespace

    int RulesPageCount() noexcept {
        return static_cast<int>(std::size(kPages));
    }

    RulesPage FormatRulesPage(const UIStrings& strings, int page) {
        RulesPage out;
        if (page < 0 || page >= RulesPageCount()) {
            return out;
        }
        const PageSpec& spec = kPages[static_cast<size_t>(page)];
        const RulesArgs args(strings);
        out.title = args.Text(spec.title);
        for (const char* key : spec.lines) {
            if (key != nullptr) {
                out.lines.push_back(args.Text(key));
            }
        }
        if (spec.items != nullptr) {
            out.items = spec.items(strings);
        }
        if (spec.tileRows != nullptr) {
            out.tileRows = spec.tileRows();
        }
        return out;
    }

    eastl::string RulesPagerText(const UIStrings& strings, int page) {
        const int shown = std::clamp(page, 0, std::max(RulesPageCount() - 1, 0)) + 1;
        return Tr(strings, "rules.pager", {{"n", Format("{}", shown)}, {"total", Format("{}", RulesPageCount())}});
    }

    int PageHeight(const RulesPage& page) noexcept {
        int height = kRulesTitleHeight + kRulesBlockGap + kRulesPagerHeight;
        const int lines = static_cast<int>(page.lines.size());
        if (lines > 0) {
            height += kRulesBlockGap + lines * kRulesLineHeight + (lines - 1) * kRulesLineGap;
        }
        const int itemRows = (static_cast<int>(page.items.size()) + kRulesItemColumns - 1) / kRulesItemColumns;
        if (itemRows > 0) {
            height += kRulesBlockGap + itemRows * kRulesItemHeight + (itemRows - 1) * kRulesItemGap;
        }
        height += static_cast<int>(page.tileRows.size()) * (kRulesBlockGap + kRulesTileHeight);
        return height;
    }

}  // namespace BECore::Mahjong
