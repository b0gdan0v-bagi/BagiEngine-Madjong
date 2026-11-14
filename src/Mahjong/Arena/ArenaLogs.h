#pragma once

#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong::Arena {

    /// One match log file as the program read it: `name` is relative to the folder that was named, `/` separated.
    struct LogFile {
        String name;
        String text;
    };

    /// The seam for the files, as the analyzer has `FileReader`: the `*.jsonl` files of `path` (a folder, searched recursively, or one
    /// file) with their text, or the error text when the path cannot be read. The program supplies the real one; a test supplies strings.
    /// The order is the reader's own: `ReadLogs` sorts by name, so the report does not depend on it.
    using LogSource = std::function<std::expected<Vector<LogFile>, String>(StringView path)>;

    /// `text` with every tab and line break turned into a space: a cell of a tab separated line, a reason on one line.
    [[nodiscard]] String OneLine(String text);

    /// Why a log is dropped from the corpus.
    CORE_ENUM(LogProblem, uint8_t, Unreadable, DoesNotReplay)

    /// What `CheckMatchLog` says of one log.
    struct LogVerdict {
        bool ok = false;
        /// A log that is not `ok`: which kind of problem, and the reason as text without a tab or a newline (it is a column of the
        /// `--check` output): `unreadable: line 3: ...` or `does not replay: event 17 (ActionRefused)`.
        LogProblem problem = LogProblem::Unreadable;
        String reason;
        /// An `ok` log: its events, and whether it reached its `End` (a match that was given up stops early and is still `ok`).
        Vector<MatchLogEvent> events;
        bool complete = false;
    };

    /**
     * @brief Parses `text` as a match log and replays it on a fresh `Match`.
     *
     * `ok` when both work. A rules change makes old logs stop replaying; the reason names the event and the `ReplayProblem` so that is
     * told, not lost. A log with no `End` replays as far as it goes and is `ok` (given up), `complete` false.
     */
    [[nodiscard]] LogVerdict CheckMatchLog(StringView text);

    /// A file and its verdict.
    struct CheckedLog {
        String name;
        LogVerdict verdict;
    };

    /// The files of `path` through `source`, sorted by name (byte order), each checked. The error text is the source's.
    [[nodiscard]] std::expected<Vector<CheckedLog>, String> ReadLogs(const LogSource& source, StringView path);

    /// A key and how many times it was met, in the report's order.
    using CountList = Vector<eastl::pair<String, int>>;

    /// The smallest, mean and largest of a count over the matches of the corpus (all 0 for none).
    struct Spread {
        int min = 0;
        double mean = 0.0;
        int max = 0;
    };

    /// What a corpus of logs holds, counted over the files that replay (the dropped ones only by kind and name).
    struct LogCorpus {
        int files = 0;
        int replayed = 0;
        /// Dropped files per `LogProblem`, and the dropped files in name order with their reasons.
        Array<int, EnumUtils<LogProblem>::Count()> dropped{};
        Vector<eastl::pair<String, String>> droppedFiles;
        int complete = 0;
        int abandoned = 0;
        /// The hands settled, over all matches.
        int hands = 0;
        int personSeats = 0;
        int computerSeats = 0;
        /// Seat setups (`PCCC`) in letter order.
        CountList setups;
        /// The computer seats at each level `1..kMaxLevel` (a person's seat has a level in the log too; it is not counted).
        Array<int, kMaxLevel> computerLevels{};
        /// Matches per `MatchPlatform`, indexed by the enum value.
        Array<int, EnumUtils<MatchPlatform>::Count()> platforms{};
        /// Matches per build id in id order; a log with no build id is under `kNoBuild`.
        CountList builds;
        /// Hands settled and actions taken per match.
        Spread matchHands;
        Spread matchActions;
    };

    /// The key of `LogCorpus::builds` for a log that has no build id (an older format, or a source archive).
    inline constexpr StringView kNoBuild = "unknown";

    /// Counts `logs` (from `ReadLogs`). The same logs give the same corpus.
    [[nodiscard]] LogCorpus SummariseLogs(const Vector<CheckedLog>& logs);

}  // namespace BECore::Mahjong::Arena
