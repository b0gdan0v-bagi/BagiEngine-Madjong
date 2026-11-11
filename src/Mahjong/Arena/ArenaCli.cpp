#include "ArenaCli.h"

#include <Mahjong/Arena/ArenaLogsReport.h>
#include <Mahjong/Arena/ArenaTester.h>
#include <thread>

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr std::string_view kFocusOption = "--focus";
        constexpr std::string_view kOthersOption = "--others";
        constexpr std::string_view kFirstOption = "--first";
        constexpr std::string_view kSeedsOption = "--seeds";
        constexpr std::string_view kJobsOption = "--jobs";
        constexpr std::string_view kTimeOption = "--time";
        constexpr std::string_view kLogsOption = "--logs";
        constexpr std::string_view kCheckOption = "--check";
        constexpr std::string_view kDifferencesOption = "--differences";
        constexpr std::string_view kTableStatsOption = "--table-stats";
        constexpr std::string_view kHelpOption = "--help";

        constexpr char kListSeparator = ',';
        constexpr char kKnobMarker = '=';
        constexpr char kLevelMarker = ':';

        /// The value of `text` as a whole number of type `T`; nullopt when anything else is in it (a `+`, a space, a letter, too large;
        /// a `-` only for an unsigned `T`: a signed one reads it and `ValidateArenaConfig` refuses the number).
        template <typename T>
        std::optional<T> ParseWhole(std::string_view text) {
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
        std::expected<eastl::vector<ArenaPlayer>, eastl::string> ParseFocusList(std::string_view list) {
            eastl::vector<eastl::string> specs;
            size_t begin = 0;
            while (begin <= list.size()) {
                const size_t comma = list.find(kListSeparator, begin);
                const size_t end = comma == std::string_view::npos ? list.size() : comma;
                const std::string_view part = list.substr(begin, end - begin);
                const bool continuesKnobs = !specs.empty() && part.find(kKnobMarker) != std::string_view::npos && part.find(kLevelMarker) == std::string_view::npos;
                if (continuesKnobs) {
                    specs.back().push_back(kListSeparator);
                    specs.back().append(part.data(), part.size());
                } else {
                    specs.emplace_back(part.data(), part.size());
                }
                begin = end + 1;
            }
            eastl::vector<ArenaPlayer> players;
            for (const eastl::string& spec : specs) {
                std::expected<ArenaPlayer, eastl::string> player = ParseArenaPlayer(std::string_view(spec.data(), spec.size()));
                if (!player.has_value()) {
                    return std::unexpected(Format("{}: {}", kFocusOption, player.error().c_str()));
                }
                players.push_back(*player);
            }
            return players;
        }

        template <typename T>
        std::expected<T, eastl::string> ParseNumberOption(std::string_view option, std::string_view value, T smallest) {
            const std::optional<T> number = ParseWhole<T>(value);
            if (!number.has_value()) {
                return std::unexpected(Format("{}: \"{}\" is not a whole number from {} to {}", option, value, smallest, std::numeric_limits<T>::max()));
            }
            return *number;
        }

        /// The threads of `--jobs N`: N from 1 to `kMaxJobs`, and 0 for the hardware concurrency (at least 1, at most `kMaxJobs`).
        std::expected<int, eastl::string> ParseJobs(std::string_view value) {
            const std::optional<uint32_t> number = ParseWhole<uint32_t>(value);
            if (!number.has_value() || *number > static_cast<uint32_t>(kMaxJobs)) {
                return std::unexpected(Format("{}: \"{}\" is not a whole number from 0 to {}", kJobsOption, value, kMaxJobs));
            }
            if (*number == 0) {
                return std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, kMaxJobs);
            }
            return static_cast<int>(*number);
        }

        /// The refusal of an option that plays seeds when the program reads logs.
        eastl::string NotWithLogs(std::string_view option, std::string_view why) {
            return Format("{} cannot be used with {}: {}", option, kLogsOption, why);
        }

    }  // namespace

    eastl::string ArenaUsage() {
        return Format("usage: BagiMahjongArena [options]\n"
                      "\n"
                      "Plays the computer players with no window. For each focus player and each seat in turn the focus player plays the hand of a\n"
                      "seed against three others; the same seeds are played for every focus player. Prints a table with 95 percent intervals.\n"
                      "\n"
                      "options:\n"
                      "  {focus} LIST    the focus players, separated by commas: random, a level ({levels}), or a level with knobs\n"
                      "                  (easy:skipClaimPercent=20,skipDeclarationPercent=10); default {focusDefault}\n"
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
                      fmt::arg("levels", Format("1..{}, easy = 1, normal = {}", kMaxLevel, LevelNumber(ComputerLevel::Normal)).c_str()), fmt::arg("focus", kFocusOption),
                      fmt::arg("others", kOthersOption), fmt::arg("first", kFirstOption), fmt::arg("seeds", kSeedsOption), fmt::arg("jobs", kJobsOption), fmt::arg("maxJobs", kMaxJobs),
                      fmt::arg("jobsDefault", kDefaultJobs), fmt::arg("time", kTimeOption), fmt::arg("logs", kLogsOption), fmt::arg("check", kCheckOption), fmt::arg("differences", kDifferencesOption),
                      fmt::arg("differencesDefault", kDefaultDifferences), fmt::arg("tableStats", kTableStatsOption), fmt::arg("help", kHelpOption), fmt::arg("focusDefault", kDefaultFocusSpec),
                      fmt::arg("othersDefault", kDefaultOthersSpec), fmt::arg("firstDefault", kDefaultFirstSeed), fmt::arg("seedsDefault", kDefaultSeedCount), fmt::arg("ok", kExitOk),
                      fmt::arg("refused", kExitRefusedAction), fmt::arg("bad", kExitBadArguments));
    }

    std::expected<ArenaArgs, eastl::string> ParseArenaArgs(eastl::span<const std::string_view> args) {
        ArenaArgs parsed;
        const std::expected<ArenaPlayer, eastl::string> defaultFocus = ParseArenaPlayer(kDefaultFocusSpec);
        const std::expected<ArenaPlayer, eastl::string> defaultOthers = ParseArenaPlayer(kDefaultOthersSpec);
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
            const std::string_view option = args[index];
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
            const std::string_view value = args[++index];
            playersGiven = playersGiven || option == kFocusOption || option == kOthersOption;
            if (option == kFocusOption) {
                std::expected<eastl::vector<ArenaPlayer>, eastl::string> focus = ParseFocusList(value);
                if (!focus.has_value()) {
                    return std::unexpected(focus.error());
                }
                parsed.config.focus = std::move(*focus);
            } else if (option == kDifferencesOption) {
                gaveDifferences = true;
                const std::expected<int, eastl::string> count = ParseNumberOption<int>(option, value, 0);
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
                std::expected<ArenaPlayer, eastl::string> others = ParseArenaPlayer(value);
                if (!others.has_value()) {
                    return std::unexpected(Format("{}: {}", option, others.error().c_str()));
                }
                parsed.config.others = std::move(*others);
            } else if (option == kFirstOption) {
                gaveFirst = true;
                const std::expected<uint32_t, eastl::string> first = ParseNumberOption<uint32_t>(option, value, 0);
                if (!first.has_value()) {
                    return std::unexpected(first.error());
                }
                parsed.config.first = *first;
            } else if (option == kJobsOption) {
                const std::expected<int, eastl::string> jobs = ParseJobs(value);
                if (!jobs.has_value()) {
                    return std::unexpected(jobs.error());
                }
                parsed.config.jobs = *jobs;
            } else {
                gaveSeeds = true;
                const std::expected<int, eastl::string> count = ParseNumberOption<int>(option, value, 1);
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
            const std::string_view why = "it plays seeds, and the logs are already played";
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
            if (const eastl::string problem = ValidateTesterBots(parsed.config.focus); !problem.empty()) {
                return std::unexpected(Format("{}: {}", kFocusOption, problem.c_str()));
            }
        }
        const eastl::string problem = ValidateArenaConfig(parsed.config);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        return parsed;
    }

    int RunArenaCli(eastl::span<const std::string_view> args, ArenaCliOutput& output) {
        const LogSource noFiles = [](std::string_view) -> std::expected<eastl::vector<LogFile>, eastl::string> { return std::unexpected(eastl::string("this program cannot read files")); };
        return RunArenaCli(args, noFiles, output);
    }

    int RunArenaCli(eastl::span<const std::string_view> args, const LogSource& source, ArenaCliOutput& output) {
        const std::expected<ArenaArgs, eastl::string> parsed = ParseArenaArgs(args);
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
            const std::expected<TableStats, eastl::string> stats = RunTableStats(tableConfig);
            if (!stats.has_value()) {
                output.err = Format("error: {}\n", stats.error().c_str());
                return kExitRefusedAction;
            }
            output.out = FormatTableStats(*stats);
            return kExitOk;
        }
        if (parsed->logMode) {
            const std::expected<eastl::vector<CheckedLog>, eastl::string> logs = ReadLogs(source, std::string_view(parsed->logsPath.data(), parsed->logsPath.size()));
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
        const std::expected<ArenaResult, eastl::string> result = RunArena(parsed->config);
        if (!result.has_value()) {
            output.err = Format("error: {}\n", result.error().c_str());
            return kExitRefusedAction;
        }
        output.out = FormatArenaReport(*result);
        return kExitOk;
    }

}  // namespace BECore::Mahjong::Arena
