#include "RulesText.h"

#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <Mahjong/Table/ActionButtons.h>

namespace BECore::Mahjong {

    namespace {

        /// The tiles East starts with: the dealt tiles and the first draw (the first tile is drawn at once).
        inline constexpr int kEastOpeningTiles = kDealtTiles + 1;

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

        /// One page of the table: the key of its title, the keys of its lines (a null one ends the list) and the builder of its examples.
        struct PageSpec {
            const char* title;
            eastl::array<const char*, kRulesMaxLines> lines;
            TileRows (*tileRows)();
        };

        // The keys are literals on purpose: the test of the strings finds them in the sources and holds every key of the file to a user.
        const PageSpec kPages[] = {
            {"rules.tiles.title", {"rules.tiles.line1", "rules.tiles.line2", "rules.tiles.line3", "rules.tiles.line4", nullptr, nullptr}, &TileRowsOfTiles},
            {"rules.sets.title", {"rules.sets.line1", "rules.sets.line2", "rules.sets.line3", "rules.sets.line4", "rules.sets.line5", nullptr}, &TileRowsOfSets},
            {"rules.turn.title", {"rules.turn.line1", "rules.turn.line2", "rules.turn.line3", "rules.turn.line4", nullptr, nullptr}, nullptr},
            {"rules.claims.title", {"rules.claims.line1", "rules.claims.line2", "rules.claims.line3", "rules.claims.line4", "rules.claims.line5", nullptr}, nullptr},
            {"rules.kongs.title", {"rules.kongs.line1", "rules.kongs.line2", "rules.kongs.line3", "rules.kongs.line4", "rules.kongs.line5", nullptr}, nullptr},
            {"rules.calling.title", {"rules.calling.line1", "rules.calling.line2", "rules.calling.line3", "rules.calling.line4", "rules.calling.line5", nullptr}, &TileRowsOfCalling},
            {"rules.end.title", {"rules.end.line1", "rules.end.line2", "rules.end.line3", "rules.end.line4", nullptr, nullptr}, nullptr},
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
