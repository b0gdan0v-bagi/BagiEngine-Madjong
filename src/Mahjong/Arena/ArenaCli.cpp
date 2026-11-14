#include "ArenaCli.h"

#include <Mahjong/Arena/ArenaLogsReport.h>
#include <Mahjong/Arena/ArenaTester.h>
#include <thread>

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr StringView kFocusOption = "--focus";
        constexpr StringView kOthersOption = "--others";
        constexpr StringView kFirstOption = "--first";
        constexpr StringView kSeedsOption = "--seeds";
        constexpr StringView kJobsOption = "--jobs";
        constexpr StringView kTimeOption = "--time";
        constexpr StringView kLogsOption = "--logs";
        constexpr StringView kCheckOption = "--check";
        constexpr StringView kDifferencesOption = "--differences";
        constexpr StringView kTableStatsOption = "--table-stats";
        constexpr StringView kHelpOption = "--help";

        constexpr char kListSeparator = ',';
        constexpr char kKnobMarker = '=';
        constexpr char kLevelMarker = ':';

        /// The value of `text` as a whole number of type `T`; nullopt when anything else is in it (a `+`, a space, a letter, too large;
        /// a `-` only for an unsigned `T`: a signed one reads it and `ValidateArenaConfig` refuses the number).
        template <typename T>
        Optional<T> ParseWhole(StringView text) {
            T value{};
            const char* const end = text.data() + text.size();
            const auto parsed = std::from_chars(text.data(), end, value);
            if (text.empty() || parsed.ec != std::errc() || parsed.ptr != end) {
                return std::nullopt;
            }
            return value;
        }

        /// The players of `--focus`: the parts between commas, where a part that sets a knob (`name=value`, no colon) continues the
        /// spec before it.
        std::expected<Vector<ArenaPlayer>, String> ParseFocusList(StringView list) {
            Vector<String> specs;
            size_t begin = 0;
            while (begin <= list.size()) {
                const size_t comma = list.find(kListSeparator, begin);
                const size_t end = comma == StringView::npos ? list.size() : comma;
                const StringView part = list.substr(begin, end - begin);
                const bool continuesKnobs = !specs.empty() && part.find(kKnobMarker) != StringView::npos && part.find(kLevelMarker) == StringView::npos;
                if (continuesKnobs) {
                    specs.back().push_back(kListSeparator);
                    specs.back().append(part.data(), part.size());
                } else {
                    specs.emplace_back(part.data(), part.size());
                }
                begin = end + 1;
            }
            Vector<ArenaPlayer> players;
            for (const String& spec : specs) {
                std::expected<ArenaPlayer, String> player = ParseArenaPlayer(spec);
                if (!player.has_value()) {
                    return std::unexpected(Format("{}: {}", kFocusOption, player.error().c_str()));
                }
                players.push_back(*player);
            }
            return players;
        }

        template <typename T>
        std::expected<T, String> ParseNumberOption(StringView option, StringView value, T smallest) {
            const Optional<T> number = ParseWhole<T>(value);
            if (!number.has_value()) {
                return std::unexpected(Format("{}: \"{}\" is not a whole number from {} to {}", option, value, smallest, std::numeric_limits<T>::max()));
            }
            return *number;
        }

        /// The threads of `--jobs N`: N from 1 to `kMaxJobs`, and 0 for the hardware concurrency (at least 1, at most `kMaxJobs`).
        std::expected<int, String> ParseJobs(StringView value) {
            const Optional<uint32_t> number = ParseWhole<uint32_t>(value);
            if (!number.has_value() || *number > static_cast<uint32_t>(kMaxJobs)) {
                return std::unexpected(Format("{}: \"{}\" is not a whole number from 0 to {}", kJobsOption, value, kMaxJobs));
            }
            if (*number == 0) {
                return std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, kMaxJobs);
            }
            return static_cast<int>(*number);
        }

        /// The refusal of an option that plays seeds when the program reads logs.
        String NotWithLogs(StringView option, StringView why) {
            return Format("{} cannot be used with {}: {}", option, kLogsOption, why);
        }

    }  // namespace

    String ArenaUsage() {
        return Format("usage: BagiMahjongArena [options]\n"
                      "\n"
                      "Plays the computer players with no window. For each focus player and each seat in turn the focus player plays the hand of a\n"
                      "seed against three others; the same seeds are played for every focus player. Prints a table with 95 percent intervals.\n"
                      "\n"
                      "options:\n"
                      "  {focus} LIST    the focus players, separated by commas: random, a level ({levels}), or a level with knobs\n"
                      "                  (easy:skipClaimPercent=20,skipDeclarationPercent=10), or an oracle that cheats and exists only here (oracle-acceptance,\n"
                      "                  oracle-hands, oracle-whole, oracle-defence; knobs the same way: oracle-hands:searchWorlds=8); default {focusDefault}\n"
                      "  {others} SPEC   the player on the three other seats; default {othersDefault}\n"
                      "  {first} N       the first seed; default {firstDefault}\n"
                      "  {seeds} COUNT   the number of seeds, four hands for each focus player per seed; default {seedsDefault}\n"
                      "  {jobs} N        N threads in all (1 to {maxJobs}; 0 is one per core): the seeds first; when there are fewer seeds than threads, the\n"
                      "                  rest play the worlds of a search decision; the report is the same bytes whatever N is; default {jobsDefault}\n"
                      "  {time}          also print the time per decision (wall clock: the one number that differs between runs)\n"
                      "  {logs} PATH     read the match logs (*.jsonl) of a folder, or one file, instead of playing: replays each, prints a summary of the\n"
                      "                  ones that replay, the agreement of the people with the bots ({focus}), the logged results by the level of the computers met and the\n"
                      "                  differences; {jobs} plays whole matches in threads, the same bytes whatever N is; {first}, {seeds}, {others} and {time} are not used with it; {focus} then "
                      "names the bots the people are\n"
                      "                  compared with (no random)\n"
                      "  {differences} N list the first N differences between the people and the first bot of {focus}; default {differencesDefault}, 0 lists none\n"
                      "                  (the count of all is always printed)\n"
                      "  {check}         with {logs}: only one line per file, ok<TAB>name or bad<TAB>name<TAB>reason (exit {ok} even when files are bad)\n"
                      "  {tableStats}    instead of the table of players: plays the seeds once with four normal seats and prints the table statistics\n"
                      "                  (the chance that another seat wins in a round, the mean scores, how hands are won); with {first}, {seeds}, {jobs}; not with\n"
                      "                  {focus}, {others}, {time} or {logs}\n"
                      "  {help}          this text\n"
                      "\n"
                      "exit code: {ok} done, {refused} an action was refused by the rules (a defect; the seed and the seat are printed) or the path of {logs} cannot be read,\n"
                      "{bad} bad arguments.\n"
                      "\n"
                      "interval: the four rotations of a seed share one wall, so the seed is the sample; a number is the mean over the seeds and\n"
                      "+- is 1.96 standard errors (normal approximation, fine from about 100 seeds). Read the paired rows for a difference.\n",
                      FormatArg("levels", Format("1..{}, easy = 1, normal = {}", kMaxLevel, LevelNumber(ComputerLevel::Normal)).c_str()), FormatArg("focus", kFocusOption),
                      FormatArg("others", kOthersOption), FormatArg("first", kFirstOption), FormatArg("seeds", kSeedsOption), FormatArg("jobs", kJobsOption), FormatArg("maxJobs", kMaxJobs),
                      FormatArg("jobsDefault", kDefaultJobs), FormatArg("time", kTimeOption), FormatArg("logs", kLogsOption), FormatArg("check", kCheckOption),
                      FormatArg("differences", kDifferencesOption), FormatArg("differencesDefault", kDefaultDifferences), FormatArg("tableStats", kTableStatsOption), FormatArg("help", kHelpOption),
                      FormatArg("focusDefault", kDefaultFocusSpec), FormatArg("othersDefault", kDefaultOthersSpec), FormatArg("firstDefault", kDefaultFirstSeed),
                      FormatArg("seedsDefault", kDefaultSeedCount), FormatArg("ok", kExitOk), FormatArg("refused", kExitRefusedAction), FormatArg("bad", kExitBadArguments));
    }

    std::expected<ArenaArgs, String> ParseArenaArgs(Span<const StringView> args) {
        ArenaArgs parsed;
        const std::expected<ArenaPlayer, String> defaultFocus = ParseArenaPlayer(kDefaultFocusSpec);
        const std::expected<ArenaPlayer, String> defaultOthers = ParseArenaPlayer(kDefaultOthersSpec);
        ASSERT(defaultFocus.has_value() && defaultOthers.has_value());
        parsed.config.focus.push_back(*defaultFocus);
        parsed.config.others = *defaultOthers;
        bool gaveFirst = false;
        bool gaveSeeds = false;
        bool gaveOthers = false;
        bool gaveDifferences = false;
        bool timeGiven = false;
        bool playersGiven = false;
        for (size_t index = 0; index < args.size(); ++index) {
            const StringView option = args[index];
            if (option == kHelpOption) {
                parsed.help = true;
                return parsed;
            }
            if (option == kTimeOption) {
                parsed.config.timing = true;
                timeGiven = true;
                continue;
            }
            if (option == kTableStatsOption) {
                parsed.tableStats = true;
                continue;
            }
            if (option == kCheckOption) {
                parsed.check = true;
                continue;
            }
            const bool takesValue =
                option == kFocusOption || option == kOthersOption || option == kFirstOption || option == kSeedsOption || option == kJobsOption || option == kLogsOption || option == kDifferencesOption;
            if (!takesValue) {
                return std::unexpected(Format("unknown argument \"{}\"", option));
            }
            if (index + 1 >= args.size()) {
                return std::unexpected(Format("{} needs a value", option));
            }
            const StringView value = args[++index];
            playersGiven = playersGiven || option == kFocusOption || option == kOthersOption;
            if (option == kFocusOption) {
                std::expected<Vector<ArenaPlayer>, String> focus = ParseFocusList(value);
                if (!focus.has_value()) {
                    return std::unexpected(focus.error());
                }
                parsed.config.focus = std::move(*focus);
            } else if (option == kDifferencesOption) {
                gaveDifferences = true;
                const std::expected<int, String> count = ParseNumberOption<int>(option, value, 0);
                if (!count.has_value()) {
                    return std::unexpected(count.error());
                }
                if (*count < 0) {
                    return std::unexpected(Format("{}: \"{}\" is not a whole number from 0 to {}", option, value, std::numeric_limits<int>::max()));
                }
                parsed.differences = *count;
            } else if (option == kLogsOption) {
                parsed.logMode = true;
                parsed.logsPath.assign(value.data(), value.size());
            } else if (option == kOthersOption) {
                gaveOthers = true;
                std::expected<ArenaPlayer, String> others = ParseArenaPlayer(value);
                if (!others.has_value()) {
                    return std::unexpected(Format("{}: {}", option, others.error().c_str()));
                }
                parsed.config.others = std::move(*others);
            } else if (option == kFirstOption) {
                gaveFirst = true;
                const std::expected<uint32_t, String> first = ParseNumberOption<uint32_t>(option, value, 0);
                if (!first.has_value()) {
                    return std::unexpected(first.error());
                }
                parsed.config.first = *first;
            } else if (option == kJobsOption) {
                const std::expected<int, String> jobs = ParseJobs(value);
                if (!jobs.has_value()) {
                    return std::unexpected(jobs.error());
                }
                parsed.config.jobs = *jobs;
            } else {
                gaveSeeds = true;
                const std::expected<int, String> count = ParseNumberOption<int>(option, value, 1);
                if (!count.has_value()) {
                    return std::unexpected(count.error());
                }
                parsed.config.count = *count;
            }
        }
        if (parsed.tableStats && (playersGiven || timeGiven)) {
            return std::unexpected(Format("{} plays four normal seats on its own and does not go with {}, {} or {}", kTableStatsOption, kFocusOption, kOthersOption, kTimeOption));
        }
        if (parsed.tableStats && parsed.logMode) {
            return std::unexpected(NotWithLogs(kTableStatsOption, "it plays seeds on its own, and the logs are already played"));
        }
        if (parsed.check && !parsed.logMode) {
            return std::unexpected(Format("{} needs {}", kCheckOption, kLogsOption));
        }
        if (gaveDifferences && !parsed.logMode) {
            return std::unexpected(Format("{} needs {}", kDifferencesOption, kLogsOption));
        }
        if (parsed.logMode) {
            const StringView why = "it plays seeds, and the logs are already played";
            if (gaveFirst) {
                return std::unexpected(NotWithLogs(kFirstOption, why));
            }
            if (gaveSeeds) {
                return std::unexpected(NotWithLogs(kSeedsOption, why));
            }
            if (gaveOthers) {
                return std::unexpected(NotWithLogs(kOthersOption, why));
            }
            if (parsed.config.timing) {
                return std::unexpected(NotWithLogs(kTimeOption, "there is nothing to time"));
            }
            if (const String problem = ValidateTesterBots(parsed.config.focus); !problem.empty()) {
                return std::unexpected(Format("{}: {}", kFocusOption, problem.c_str()));
            }
        }
        const String problem = ValidateArenaConfig(parsed.config);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        return parsed;
    }

    int RunArenaCli(Span<const StringView> args, ArenaCliOutput& output) {
        const LogSource noFiles = [](StringView) -> std::expected<Vector<LogFile>, String> { return std::unexpected(String("this program cannot read files")); };
        return RunArenaCli(args, noFiles, output);
    }

    int RunArenaCli(Span<const StringView> args, const LogSource& source, ArenaCliOutput& output) {
        const std::expected<ArenaArgs, String> parsed = ParseArenaArgs(args);
        if (!parsed.has_value()) {
            output.err = Format("error: {}\n\n{}", parsed.error().c_str(), ArenaUsage().c_str());
            return kExitBadArguments;
        }
        if (parsed->help) {
            output.out = ArenaUsage();
            return kExitOk;
        }
        if (parsed->tableStats) {
            TableStatsConfig tableConfig;
            tableConfig.first = parsed->config.first;
            tableConfig.count = parsed->config.count;
            tableConfig.jobs = parsed->config.jobs;
            const std::expected<TableStats, String> stats = RunTableStats(tableConfig);
            if (!stats.has_value()) {
                output.err = Format("error: {}\n", stats.error().c_str());
                return kExitRefusedAction;
            }
            output.out = FormatTableStats(*stats);
            return kExitOk;
        }
        if (parsed->logMode) {
            const std::expected<Vector<CheckedLog>, String> logs = ReadLogs(source, parsed->logsPath);
            if (!logs.has_value()) {
                output.err = Format("error: {}\n", logs.error().c_str());
                return kExitCannotRead;
            }
            if (parsed->check) {
                output.out = FormatCheckLines(*logs);
                return kExitOk;
            }
            output.out = FormatCorpusSummary(SummariseLogs(*logs));
            output.out.append(FormatLogAnalysis(AnalyseLogs(*logs, parsed->config.focus, parsed->config.jobs), parsed->differences));
            return kExitOk;
        }
        const std::expected<ArenaResult, String> result = RunArena(parsed->config);
        if (!result.has_value()) {
            output.err = Format("error: {}\n", result.error().c_str());
            return kExitRefusedAction;
        }
        output.out = FormatArenaReport(*result);
        return kExitOk;
    }

}  // namespace BECore::Mahjong::Arena
