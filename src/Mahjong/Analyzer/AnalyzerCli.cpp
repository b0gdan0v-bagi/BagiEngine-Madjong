#include "AnalyzerCli.h"

#include <Mahjong/Arena/ArenaWorlds.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MatchLogJson.h>
#include <thread>

namespace BECore::Mahjong::Analyzer {

    namespace {

        constexpr std::string_view kLogOption = "--log";
        constexpr std::string_view kHandOption = "--hand";
        constexpr std::string_view kActionOption = "--action";
        constexpr std::string_view kSeatOption = "--seat";
        constexpr std::string_view kPositionOption = "--position";
        constexpr std::string_view kWorldsOption = "--worlds";
        constexpr std::string_view kSeedOption = "--seed";
        constexpr std::string_view kJobsOption = "--jobs";
        constexpr std::string_view kJsonOption = "--json";
        constexpr std::string_view kHelpOption = "--help";

        constexpr std::string_view kSkippedReason = "single legal action";

        /// The value of `text` as a whole number of type `T`; nullopt when anything else is in it (a sign, a space, a letter, too large).
        template <typename T>
        std::optional<T> ParseWhole(std::string_view text) {
            T value{};
            const char* const end = text.data() + text.size();
            const auto parsed = std::from_chars(text.data(), end, value);
            if (text.empty() || text.front() == '+' || parsed.ec != std::errc() || parsed.ptr != end) {
                return std::nullopt;
            }
            return value;
        }

        /// `option`'s value as a number from `smallest` to `largest`, or the error that names the option and the range.
        std::expected<int, eastl::string> ParseRanged(std::string_view option, std::string_view value, int smallest, int largest) {
            const std::optional<int> number = ParseWhole<int>(value);
            if (!number.has_value() || *number < smallest || *number > largest) {
                return std::unexpected(Format("{}: \"{}\" is not a whole number from {} to {}", option, value, smallest, largest));
            }
            return *number;
        }

        /// The threads of `--jobs N`: N from 1 to `kMaxJobs`, and 0 for the hardware concurrency (at least 1, at most `kMaxJobs`).
        std::expected<int, eastl::string> ParseJobs(std::string_view value) {
            const std::expected<int, eastl::string> jobs = ParseRanged(kJobsOption, value, 0, kMaxJobs);
            if (!jobs.has_value()) {
                return jobs;
            }
            return *jobs == 0 ? std::clamp(static_cast<int>(std::thread::hardware_concurrency()), 1, kMaxJobs) : *jobs;
        }

        eastl::string Text(std::string_view view) {
            return eastl::string(view.data(), view.size());
        }

        /// The problem with the combination of options, or an empty text.
        eastl::string CheckCombination(const AnalyzerArgs& args) {
            const bool fromLog = !args.logFile.empty();
            const bool fromText = !args.positionFile.empty();
            if (fromLog && fromText) {
                return Format("{} and {} cannot be combined: a position comes from one source", kLogOption, kPositionOption);
            }
            if (!fromLog && !fromText) {
                return Format("give the position: {} FILE with {} and {} or {}, or {} FILE", kLogOption, kHandOption, kActionOption, kSeatOption, kPositionOption);
            }
            if (fromText) {
                for (const auto& [set, name] : {std::pair{args.hand != 0, kHandOption}, std::pair{args.action.has_value(), kActionOption}, std::pair{args.seat.has_value(), kSeatOption}}) {
                    if (set) {
                        return Format("{} is for {}, not for {}", name, kLogOption, kPositionOption);
                    }
                }
                return {};
            }
            if (args.hand == 0) {
                return Format("{} needs {} (the number of the hand, counted from 1)", kLogOption, kHandOption);
            }
            if (args.action.has_value() && args.seat.has_value()) {
                return Format("{} and {} cannot be combined: one decision or every decision of a seat", kActionOption, kSeatOption);
            }
            if (!args.action.has_value() && !args.seat.has_value()) {
                return Format("{} needs {} I (one decision) or {} S (every decision of a seat)", kLogOption, kActionOption, kSeatOption);
            }
            return {};
        }

        /// The one-line answer for a decision with nothing to compare.
        eastl::string SkippedText(const LogPosition& position) {
            return Format("skipped: hand {} action {} seat {}: {} ({})", position.hand, position.actionIndex, position.seat, kSkippedReason, ActionNotation(position.view.legal.front()).c_str());
        }

        eastl::string SkippedJson(const std::string_view file, const LogPosition& position) {
            Json json = Json::object();
            json["version"] = kAnalysisJsonVersion;
            json["source"] = std::string(file);
            json["hand"] = position.hand;
            json["actionIndex"] = position.actionIndex;
            json["seat"] = position.seat;
            json["skipped"] = std::string(kSkippedReason);
            json["action"] = ActionToJson(position.view.legal.front());
            return DumpJson(json);
        }

        /// Appends reports to the output, in the order they are asked.
        class Reporter {
        public:
            Reporter(const AnalyzerConfig& config, WorldLoop loop, bool json, eastl::string& out) : _config(config), _loop(std::move(loop)), _json(json), _out(out) {}

            [[nodiscard]] bool IsJson() const noexcept {
                return _json;
            }

            /// Analyzes `view` and appends its report; the error text when the analysis fails.
            std::optional<eastl::string> Report(const TableView& view, const PositionLabel& label) {
                const std::expected<PositionAnalysis, eastl::string> analysis = AnalyzePosition(view, _config, _loop);
                if (!analysis.has_value()) {
                    return analysis.error();
                }
                if (_json) {
                    _out.append(FormatAnalysisJson(view, *analysis, label));
                    _out.push_back('\n');
                } else {
                    Separate();
                    _out.append(FormatAnalysisReport(view, *analysis, label));
                }
                return std::nullopt;
            }

            /// A line (no newline in `text`) for a decision that was not analyzed.
            void Skip(const eastl::string& text) {
                if (!_json) {
                    Separate();
                }
                _out.append(text);
                _out.push_back('\n');
            }

        private:
            void Separate() {
                if (!_out.empty()) {
                    _out.push_back('\n');
                }
            }

            const AnalyzerConfig& _config;
            WorldLoop _loop;
            bool _json;
            eastl::string& _out;
        };

        /// The decisions asked of the log, analyzed one after another; the error text of the first that fails.
        std::optional<eastl::string> ReportLog(const AnalyzerArgs& args, const FileReader& readFile, Reporter& reporter) {
            const std::string_view file(args.logFile.data(), args.logFile.size());
            const std::expected<eastl::string, eastl::string> text = readFile(file);
            if (!text.has_value()) {
                return text.error();
            }
            const std::expected<eastl::vector<MatchLogEvent>, eastl::string> events = ParseMatchLog(std::string_view(text->data(), text->size()));
            if (!events.has_value()) {
                return Format("{}: {}", file, events.error().c_str());
            }
            if (args.action.has_value()) {
                const std::expected<LogPosition, eastl::string> position = PositionFromLog(*events, args.hand, *args.action);
                if (!position.has_value()) {
                    return Format("{}: {}", file, position.error().c_str());
                }
                return reporter.Report(position->view, LabelOfLogPosition(args.logFile, *position));
            }
            const std::expected<eastl::vector<LogPosition>, eastl::string> positions = PositionsOfSeat(*events, args.hand, *args.seat);
            if (!positions.has_value()) {
                return Format("{}: {}", file, positions.error().c_str());
            }
            for (const LogPosition& position : *positions) {
                if (position.view.legal.size() < 2) {
                    reporter.Skip(reporter.IsJson() ? SkippedJson(file, position) : SkippedText(position));
                    continue;
                }
                if (const std::optional<eastl::string> error = reporter.Report(position.view, LabelOfLogPosition(args.logFile, position))) {
                    return Format("{}: hand {} action {}: {}", file, position.hand, position.actionIndex, error->c_str());
                }
            }
            return std::nullopt;
        }

        std::optional<eastl::string> ReportPosition(const AnalyzerArgs& args, const FileReader& readFile, Reporter& reporter) {
            const std::string_view file(args.positionFile.data(), args.positionFile.size());
            const std::expected<eastl::string, eastl::string> text = readFile(file);
            if (!text.has_value()) {
                return text.error();
            }
            const std::expected<TableView, eastl::string> view = ParsePosition(std::string_view(text->data(), text->size()));
            if (!view.has_value()) {
                return Format("{}: {}", file, view.error().c_str());
            }
            PositionLabel label;
            label.source = args.positionFile;
            return reporter.Report(*view, label);
        }

    }  // namespace

    eastl::string AnalyzerUsage() {
        return Format("usage: BagiMahjongAnalyzer --log FILE --hand K (--action I | --seat S) [options]\n"
                      "       BagiMahjongAnalyzer --position FILE [options]\n"
                      "\n"
                      "Takes one position of one seat and prints, for every legal action, the expected points at the end of the hand with a 95 percent\n"
                      "interval, and the paired difference to the best action and to the action of a Normal player (same worlds, same rollouts).\n"
                      "The opponents and the seat's own later plays are Normal players; the hidden tiles are sampled uniformly. The numbers are against\n"
                      "Normal bots, not an equilibrium. docs/ev-analyzer.md explains how to read them.\n"
                      "\n"
                      "positions:\n"
                      "  {log} FILE        a match log (logs/matches/*.jsonl); with {hand} and {action} or {seat}\n"
                      "  {hand} K          the hand, counted from 1 (hand K is what happens after K-1 settlements)\n"
                      "  {action} I        the decision: the action of that hand counted from 0, passes of a claim window included\n"
                      "  {seat} S          every decision of seat S (0..3) in that hand, one report after another; one with a single legal action is one line\n"
                      "  {position} FILE   a hand-written position in the tile notation (docs/ev-analyzer.md); - reads the standard input\n"
                      "\n"
                      "options:\n"
                      "  {worlds} N        the sampled worlds, {minWorlds} to {maxWorlds}; default {worldsDefault}\n"
                      "  {seed} N          the seed of the worlds and their rollouts; default {seedDefault}\n"
                      "  {jobs} N          N threads for the worlds (1 to {maxJobs}; 0 is one per core); the output is the same bytes whatever N is; default {jobsDefault}\n"
                      "  {json}            one JSON object per position on its own line, instead of the text\n"
                      "  {help}            this text\n"
                      "\n"
                      "exit code: {ok} done, {failed} a position could not be read or analyzed (the error is printed), {bad} bad arguments.\n",
                      fmt::arg("log", kLogOption), fmt::arg("hand", kHandOption), fmt::arg("action", kActionOption), fmt::arg("seat", kSeatOption), fmt::arg("position", kPositionOption),
                      fmt::arg("worlds", kWorldsOption), fmt::arg("minWorlds", kMinWorlds), fmt::arg("maxWorlds", kMaxWorlds), fmt::arg("worldsDefault", kDefaultWorlds), fmt::arg("seed", kSeedOption),
                      fmt::arg("seedDefault", kDefaultSeed), fmt::arg("jobs", kJobsOption), fmt::arg("maxJobs", kMaxJobs), fmt::arg("jobsDefault", kDefaultJobs), fmt::arg("json", kJsonOption),
                      fmt::arg("help", kHelpOption), fmt::arg("ok", kExitOk), fmt::arg("failed", kExitAnalysisFailed), fmt::arg("bad", kExitBadArguments));
    }

    std::expected<AnalyzerArgs, eastl::string> ParseAnalyzerArgs(eastl::span<const std::string_view> args) {
        AnalyzerArgs parsed;
        for (size_t index = 0; index < args.size(); ++index) {
            const std::string_view option = args[index];
            if (option == kHelpOption) {
                parsed.help = true;
                return parsed;
            }
            if (option == kJsonOption) {
                parsed.json = true;
                continue;
            }
            const bool takesValue = option == kLogOption || option == kPositionOption || option == kHandOption || option == kActionOption || option == kSeatOption || option == kWorldsOption ||
                                    option == kSeedOption || option == kJobsOption;
            if (!takesValue) {
                return std::unexpected(Format("unknown argument \"{}\"", option));
            }
            if (index + 1 >= args.size()) {
                return std::unexpected(Format("{} needs a value", option));
            }
            const std::string_view value = args[++index];
            if (option == kLogOption) {
                parsed.logFile = Text(value);
            } else if (option == kPositionOption) {
                parsed.positionFile = Text(value);
            } else if (option == kSeedOption) {
                const std::optional<uint32_t> seed = ParseWhole<uint32_t>(value);
                if (!seed.has_value()) {
                    return std::unexpected(Format("{}: \"{}\" is not a whole number from 0 to {}", option, value, std::numeric_limits<uint32_t>::max()));
                }
                parsed.config.seed = *seed;
            } else if (option == kJobsOption) {
                const std::expected<int, eastl::string> jobs = ParseJobs(value);
                if (!jobs.has_value()) {
                    return std::unexpected(jobs.error());
                }
                parsed.jobs = *jobs;
            } else {
                const bool isSeat = option == kSeatOption;
                const bool isHand = option == kHandOption;
                const bool isWorlds = option == kWorldsOption;
                const int smallest = isHand ? 1 : (isWorlds ? kMinWorlds : 0);
                const int largest = isSeat ? kSeatCount - 1 : (isWorlds ? kMaxWorlds : std::numeric_limits<int>::max());
                const std::expected<int, eastl::string> number = ParseRanged(option, value, smallest, largest);
                if (!number.has_value()) {
                    return std::unexpected(number.error());
                }
                if (isHand) {
                    parsed.hand = *number;
                } else if (isWorlds) {
                    parsed.config.worlds = *number;
                } else if (isSeat) {
                    parsed.seat = static_cast<Seat>(*number);
                } else {
                    parsed.action = *number;
                }
            }
        }
        const eastl::string problem = CheckCombination(parsed);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        return parsed;
    }

    int RunAnalyzerCli(eastl::span<const std::string_view> args, const FileReader& readFile, AnalyzerCliOutput& output) {
        const std::expected<AnalyzerArgs, eastl::string> parsed = ParseAnalyzerArgs(args);
        if (!parsed.has_value()) {
            output.err = Format("error: {}\n\n{}", parsed.error().c_str(), AnalyzerUsage().c_str());
            return kExitBadArguments;
        }
        if (parsed->help) {
            output.out = AnalyzerUsage();
            return kExitOk;
        }
        Arena::WorldThreads threads(parsed->jobs);
        Reporter reporter(parsed->config, threads.AsLoop(), parsed->json, output.out);
        const std::optional<eastl::string> error = parsed->logFile.empty() ? ReportPosition(*parsed, readFile, reporter) : ReportLog(*parsed, readFile, reporter);
        if (error.has_value()) {
            output.err = Format("error: {}\n", error->c_str());
            return kExitAnalysisFailed;
        }
        return kExitOk;
    }

}  // namespace BECore::Mahjong::Analyzer
