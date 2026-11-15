#include "ArenaChartsReport.h"

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr int kPrecision = 2;
        constexpr int kLabelWidth = 26;
        /// The longest label of a discard cell is "pair or set, 3+, same shanten".
        constexpr int kDiscardLabelWidth = 30;
        constexpr int kRowsWidth = 7;
        constexpr int kSeedsWidth = 6;
        /// "-1234.56 +- 123.45".
        constexpr int kGainWidth = 18;
        constexpr const char* kColumnGap = "  ";
        constexpr const char* kIndent = "  ";
        constexpr StringView kSignificantWord = "significant";
        constexpr StringView kFewWord = "few";
        constexpr StringView kTakesLabel = "Normal takes";
        constexpr StringView kPassesLabel = "Normal passes";

        /// One table: a title and the cells in the order they are printed. A claim table splits every cell by whether Normal takes the
        /// claim; a discard table has only the cell itself (`takes` and `passes` stay empty and are not printed).
        struct ChartRow {
            String label;
            Cell cell;
        };

        struct ChartTable {
            String title;
            bool split = true;
            int labelWidth = kLabelWidth;
            Vector<ChartRow> rows;
        };

        String GainText(const CellStats& stats) {
            if (stats.rows == 0 || stats.gain.n == 0) {
                return {};
            }
            if (!stats.gain.hasInterval) {
                return Format("{:+.{}f}", stats.gain.mean, kPrecision);
            }
            return Format("{:+.{}f} +- {:.{}f}", stats.gain.mean, kPrecision, stats.gain.half, kPrecision);
        }

        StringView MarkOf(const CellStats& stats) noexcept {
            if (stats.significant) {
                return kSignificantWord;
            }
            return stats.few ? kFewWord : StringView();
        }

        template <typename Enum, typename Describe>
        void AddOneWay(Vector<ChartTable>& tables, const ChartsResult& result, StringView title, Describe describe) {
            ChartTable table;
            table.title = String(title.data(), title.size());
            for (const Enum value : EnumUtils<Enum>::Values()) {
                ClaimFilter filter;
                const String label = describe(filter, value);
                table.rows.push_back({label, Summarise(result, filter)});
            }
            tables.push_back(eastl::move(table));
        }

        Vector<ChartTable> BuildTables(const ChartsResult& result) {
            Vector<ChartTable> tables;
            ChartTable main;
            main.title = "Main table: kind x shanten before x stage of the wall";
            for (const ClaimChartKind kind : EnumUtils<ClaimChartKind>::Values()) {
                for (const ShantenBefore shanten : EnumUtils<ShantenBefore>::Values()) {
                    for (const WallStage stage : EnumUtils<WallStage>::Values()) {
                        ClaimFilter filter;
                        filter.kind = kind;
                        filter.shantenBefore = shanten;
                        filter.stage = stage;
                        main.rows.push_back({Format("{}, {} shanten, {}", KindName(kind), ShantenBeforeName(shanten), WallStageName(stage)), Summarise(result, filter)});
                    }
                }
            }
            tables.push_back(eastl::move(main));
            AddOneWay<ClaimChartKind>(tables, result, "By kind", [](ClaimFilter& filter, ClaimChartKind kind) {
                filter.kind = kind;
                return String(KindName(kind));
            });
            AddOneWay<TileGroup>(tables, result, "By tile group of the claimed tile", [](ClaimFilter& filter, TileGroup group) {
                filter.tile = group;
                return String(TileGroupName(group));
            });
            AddOneWay<ShantenChange>(tables, result, "By shanten change of the claim", [](ClaimFilter& filter, ShantenChange change) {
                filter.shantenChange = change;
                return String(ShantenChangeName(change));
            });
            ChartTable east;
            east.title = "By East";
            for (const bool isEast : {true, false}) {
                ClaimFilter filter;
                filter.east = isEast;
                east.rows.push_back({String(EastName(isEast)), Summarise(result, filter)});
            }
            tables.push_back(eastl::move(east));
            AddOneWay<OpenSets>(tables, result, "By open sets before the claim", [](ClaimFilter& filter, OpenSets sets) {
                filter.openSets = sets;
                return String(OpenSetsName(sets));
            });
            return tables;
        }

        String RangeText(const ChartsResult& result) {
            const int64_t last = static_cast<int64_t>(result.first) + result.count - 1;
            return Format("seeds {} .. {} ({} seeds), {} worlds per window", result.first, last, result.count, result.worlds);
        }

        String CountersText(const ChartsCounters& counters) {
            return Format("hands {}, windows with a claim {}, left out for a listed win {}, not analysable {}, rows {}", counters.hands, counters.claimWindows, counters.excludedForWin,
                          counters.dropped, counters.rows);
        }

        String GainRuleText() {
            return Format("gain = mean points of the claim minus points of passing, in the same worlds; +- is {} standard errors over the seeds (a 95 percent interval), a seed is one sample; "
                          "a cell of fewer than {} rows is few: shown, never significant",
                          kIntervalZ, kFewRows);
        }

        StringView CaveatText() noexcept {
            return "caveat: against three Normal bots, hidden tiles uniform, the points of this hand only; about one significant cell in 20 is chance, so read only what holds in both "
                   "ranges (docs/charts-claims.md)";
        }

        void EndLine(String& text, size_t lineStart) {
            size_t end = text.size();
            while (end > lineStart && text[end - 1] == ' ') {
                --end;
            }
            text.resize(end);
            text.push_back('\n');
        }

        void AddCellLine(String& text, int labelWidth, const String& label, const CellStats& stats) {
            const size_t start = text.size();
            text.append(Format("{:<{}}", label, labelWidth));
            text.append(Format("{}{:>{}}", kColumnGap, stats.rows, kRowsWidth));
            text.append(Format("{}{:>{}}", kColumnGap, stats.seeds, kSeedsWidth));
            text.append(Format("{}{:>{}}", kColumnGap, GainText(stats), kGainWidth));
            text.append(Format("{}{}", kColumnGap, MarkOf(stats)));
            EndLine(text, start);
        }

        void AddTextTable(String& text, const ChartTable& table) {
            text.append(Format("\n{}\n", table.title));
            const size_t start = text.size();
            text.append(Format("{:<{}}", "cell", table.labelWidth));
            text.append(Format("{}{:>{}}", kColumnGap, "rows", kRowsWidth));
            text.append(Format("{}{:>{}}", kColumnGap, "seeds", kSeedsWidth));
            text.append(Format("{}{:>{}}", kColumnGap, "gain +- 95 %", kGainWidth));
            EndLine(text, start);
            for (const ChartRow& row : table.rows) {
                AddCellLine(text, table.labelWidth, row.label, row.cell);
                if (table.split) {
                    AddCellLine(text, table.labelWidth, Format("{}{}", kIndent, kTakesLabel), row.cell.takes);
                    AddCellLine(text, table.labelWidth, Format("{}{}", kIndent, kPassesLabel), row.cell.passes);
                }
            }
        }

        void AddMarkdownLine(String& text, const String& label, StringView claims, const CellStats& stats) {
            text.append(Format("| {} | {} | {} | {} | {} | {} |\n", label, claims, stats.rows, stats.seeds, GainText(stats), MarkOf(stats)));
        }

        void AddMarkdownDiscardLine(String& text, const String& label, const CellStats& stats) {
            text.append(Format("| {} | {} | {} | {} | {} |\n", label, stats.rows, stats.seeds, GainText(stats), MarkOf(stats)));
        }

        void AddMarkdownTable(String& text, const ChartTable& table) {
            text.append(Format("\n#### {}\n\n", table.title));
            if (!table.split) {
                text.append("| class | rows | seeds | gain +- 95 % | |\n");
                text.append("|---|---:|---:|---:|---|\n");
                for (const ChartRow& row : table.rows) {
                    AddMarkdownDiscardLine(text, row.label, row.cell);
                }
                return;
            }
            text.append("| class | claims | rows | seeds | gain +- 95 % | |\n");
            text.append("|---|---|---:|---:|---:|---|\n");
            for (const ChartRow& row : table.rows) {
                AddMarkdownLine(text, row.label, "all", row.cell);
                AddMarkdownLine(text, String(), kTakesLabel, row.cell.takes);
                AddMarkdownLine(text, String(), kPassesLabel, row.cell.passes);
            }
        }

        template <typename Enum, typename Describe>
        void AddDiscardOneWay(Vector<ChartTable>& tables, const DiscardChartsResult& result, StringView title, Describe describe) {
            ChartTable table;
            table.title = String(title.data(), title.size());
            table.split = false;
            table.labelWidth = kDiscardLabelWidth;
            for (const Enum value : EnumUtils<Enum>::Values()) {
                DiscardFilter filter;
                ChartRow row;
                row.label = describe(filter, value);
                static_cast<CellStats&>(row.cell) = SummariseDiscards(result, filter);
                table.rows.push_back(eastl::move(row));
            }
            tables.push_back(eastl::move(table));
        }

        Vector<ChartTable> BuildDiscardTables(const DiscardChartsResult& result) {
            Vector<ChartTable> tables;
            ChartTable main;
            main.title = "Main table: value held x shanten after Normal's discard x cost of the valued discard";
            main.split = false;
            main.labelWidth = kDiscardLabelWidth;
            for (const ValueHeld held : EnumUtils<ValueHeld>::Values()) {
                for (const DiscardShanten shanten : EnumUtils<DiscardShanten>::Values()) {
                    for (const ValueCost cost : EnumUtils<ValueCost>::Values()) {
                        DiscardFilter filter;
                        filter.valueHeld = held;
                        filter.shanten = shanten;
                        filter.cost = cost;
                        ChartRow row;
                        row.label = Format("{}, {}, {}", ValueHeldName(held), DiscardShantenName(shanten), ValueCostName(cost));
                        static_cast<CellStats&>(row.cell) = SummariseDiscards(result, filter);
                        main.rows.push_back(eastl::move(row));
                    }
                }
            }
            tables.push_back(eastl::move(main));
            AddDiscardOneWay<WallStage>(tables, result, "By stage of the wall", [](DiscardFilter& filter, WallStage stage) {
                filter.stage = stage;
                return String(WallStageName(stage));
            });
            AddDiscardOneWay<OpenSets>(tables, result, "By open sets", [](DiscardFilter& filter, OpenSets sets) {
                filter.openSets = sets;
                return String(OpenSetsName(sets));
            });
            ChartTable east;
            east.title = "By East";
            east.split = false;
            east.labelWidth = kDiscardLabelWidth;
            for (const bool isEast : {true, false}) {
                DiscardFilter filter;
                filter.east = isEast;
                ChartRow row;
                row.label = String(EastName(isEast));
                static_cast<CellStats&>(row.cell) = SummariseDiscards(result, filter);
                east.rows.push_back(eastl::move(row));
            }
            tables.push_back(eastl::move(east));
            AddDiscardOneWay<DiscardShanten>(tables, result, "By shanten after Normal's discard", [](DiscardFilter& filter, DiscardShanten shanten) {
                filter.shanten = shanten;
                return String(DiscardShantenName(shanten));
            });
            AddDiscardOneWay<ValueCost>(tables, result, "By cost of the valued discard", [](DiscardFilter& filter, ValueCost cost) {
                filter.cost = cost;
                return String(ValueCostName(cost));
            });
            AddDiscardOneWay<ValueHeld>(tables, result, "By value tiles held", [](DiscardFilter& filter, ValueHeld held) {
                filter.valueHeld = held;
                return String(ValueHeldName(held));
            });
            return tables;
        }

        String DiscardRangeText(const DiscardChartsResult& result) {
            const int64_t last = static_cast<int64_t>(result.first) + result.count - 1;
            return Format("seeds {} .. {} ({} seeds), {} worlds per turn", result.first, last, result.count, result.worlds);
        }

        String DiscardCountersText(const DiscardCounters& counters) {
            return Format("hands {}, own turns {}, other step {}, no valued discard {}, same as Normal {}, not analysable {}, rows {}", counters.hands, counters.ownTurns, counters.otherStep,
                          counters.noValue, counters.sameAsNormal, counters.dropped, counters.rows);
        }

        String DiscardGainRuleText() {
            return Format("gain = mean points of the valued discard minus points of Normal's discard, in the same worlds; positive: the valued discard is better and Normal's rule loses points "
                          "in the class; +- is {} standard errors over the seeds (a 95 percent interval), a seed is one sample; a cell of fewer than {} rows is few: shown, never significant",
                          kIntervalZ, kFewRows);
        }

        StringView DiscardCaveatText() noexcept {
            return "caveat: against three Normal bots, hidden tiles uniform, both discards followed by Normal play, the points of this hand only; the valued discard is a reading that lost over "
                   "whole matches, so a class where it wins is a lead, not a rule; about one significant cell in 20 is chance, so read only what holds in both ranges (docs/charts-discards.md)";
        }

    }  // namespace

    String FormatChartsReport(const ChartsResult& result) {
        String text = Format("Claim charts: {}\n{}\n{}\n{}\n", RangeText(result), CountersText(result.counters), GainRuleText(), CaveatText());
        for (const ChartTable& table : BuildTables(result)) {
            AddTextTable(text, table);
        }
        return text;
    }

    String FormatChartsMarkdown(const ChartsResult& result) {
        String text = Format("Claim charts: {}.\n\n{}.\n\n{}.\n\n{}.\n", RangeText(result), CountersText(result.counters), GainRuleText(), CaveatText());
        for (const ChartTable& table : BuildTables(result)) {
            AddMarkdownTable(text, table);
        }
        return text;
    }

    String FormatDiscardChartsReport(const DiscardChartsResult& result) {
        String text = Format("Discard charts: {}\n{}\n{}\n{}\n", DiscardRangeText(result), DiscardCountersText(result.counters), DiscardGainRuleText(), DiscardCaveatText());
        for (const ChartTable& table : BuildDiscardTables(result)) {
            AddTextTable(text, table);
        }
        return text;
    }

    String FormatDiscardChartsMarkdown(const DiscardChartsResult& result) {
        String text = Format("Discard charts: {}.\n\n{}.\n\n{}.\n\n{}.\n", DiscardRangeText(result), DiscardCountersText(result.counters), DiscardGainRuleText(), DiscardCaveatText());
        for (const ChartTable& table : BuildDiscardTables(result)) {
            AddMarkdownTable(text, table);
        }
        return text;
    }

}  // namespace BECore::Mahjong::Arena
