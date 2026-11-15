#pragma once

#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong::Arena {

    /// `name` is relative to the folder that was named, `/` separated.
    struct LogFile {
        String name;
        String text;
    };

    /// The seam for the files, as the analyzer has `FileReader`: the `*.jsonl` files of `path` (a folder, searched recursively, or one
    /// file), or the error text. The program supplies the real one; a test supplies strings. `ReadLogs` sorts by name, so the report
    /// does not depend on the reader's order.
    using LogSource = std::function<std::expected<Vector<LogFile>, String>(StringView path)>;

    /// Every tab and line break turned into a space, so a reason or a name is one cell of a tab separated `--check` line.
    [[nodiscard]] String OneLine(String text);

    CORE_ENUM(LogProblem, uint8_t, Unreadable, DoesNotReplay)

    struct LogVerdict {
        bool ok = false;
        /// For a log that is not `ok`; the reason has no tab or newline (`unreadable: line 3: ...`, `does not replay: event 17 (...)`).
        LogProblem problem = LogProblem::Unreadable;
        String reason;
        /// A match that was given up stops before its `End`: still `ok`, `complete` false.
        Vector<MatchLogEvent> events;
        bool complete = false;
    };

    /**
     * @brief Parses `text` as a match log and replays it on a fresh `Match`.
     *
     * `ok` when both work. A rules change makes old logs stop replaying; the reason names the event and the `ReplayProblem` so that
     * is told, not lost.
     */
    [[nodiscard]] LogVerdict CheckMatchLog(StringView text);

    struct CheckedLog {
        String name;
        LogVerdict verdict;
    };

    /// Sorted by name (byte order), each checked. The error text is the source's.
    [[nodiscard]] std::expected<Vector<CheckedLog>, String> ReadLogs(const LogSource& source, StringView path);

    using CountList = Vector<Pair<String, int>>;

    struct Spread {
        int min = 0;
        double mean = 0.0;
        int max = 0;
    };

    /// Counted over the files that replay; the dropped ones only by kind and name.
    struct LogCorpus {
        int files = 0;
        int replayed = 0;
        Array<int, EnumUtils<LogProblem>::Count()> dropped{};
        Vector<Pair<String, String>> droppedFiles;
        int complete = 0;
        int abandoned = 0;
        int hands = 0;
        int personSeats = 0;
        int computerSeats = 0;
        CountList setups;
        /// A person's seat has a level in the log too; it is not counted.
        Array<int, kMaxLevel> computerLevels{};
        Array<int, EnumUtils<MatchPlatform>::Count()> platforms{};
        /// In id order; a log with no build id is under `kNoBuild`.
        CountList builds;
        Spread matchHands;
        Spread matchActions;
    };

    /// A log of an older format, or from a source archive, has no build id.
    inline constexpr StringView kNoBuild = "unknown";

    [[nodiscard]] LogCorpus SummariseLogs(const Vector<CheckedLog>& logs);

}  // namespace BECore::Mahjong::Arena
