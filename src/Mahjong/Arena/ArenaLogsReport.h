#pragma once

#include <Mahjong/Arena/ArenaLogs.h>

namespace BECore::Mahjong::Arena {

    /// The first cell of a `--check` line.
    inline constexpr std::string_view kCheckOk = "ok";
    inline constexpr std::string_view kCheckBad = "bad";

    /**
     * @brief The text of `--check`: one line per file in the order of `logs`, `ok<TAB><name>` or `bad<TAB><name><TAB><reason>`, and
     *        nothing else (a tab or a line break in a name is written as a space). `tools/collect_logs.py` reads exactly this.
     */
    [[nodiscard]] eastl::string FormatCheckLines(const eastl::vector<CheckedLog>& logs);

    /**
     * @brief The text of the corpus summary: files read, replayed and dropped (per reason, and the dropped files with their reasons),
     *        matches complete and abandoned, hands, person and computer seats, seat setups, the platforms, the builds, the length of a
     *        match (hands and actions: smallest, mean, largest) and the computer seats at each level.
     *
     * Fixed-width text, numbers by `Format` with a fixed precision and no locale: the same corpus gives the same bytes everywhere.
     */
    [[nodiscard]] eastl::string FormatCorpusSummary(const LogCorpus& corpus);

}  // namespace BECore::Mahjong::Arena
