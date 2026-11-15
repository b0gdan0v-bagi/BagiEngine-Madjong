#include "ArenaChartsCli.h"

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr StringView kKindOption = "--kind";
        constexpr StringView kFirstOption = "--first";
        constexpr StringView kSeedsOption = "--seeds";
        constexpr StringView kWorldsOption = "--worlds";
        constexpr StringView kJobsOption = "--jobs";
        constexpr StringView kMarkdownOption = "--markdown";
        constexpr StringView kHelpOption = "--help";

        /// Nullopt when anything else is in `text` (a sign, a space, a letter, too large).
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

        template <typename T>
        std::expected<T, String> ParseNumberOption(StringView option, StringView value, T smallest) {
            const Optional<T> number = ParseWhole<T>(value);
            if (!number.has_value() || *number < smallest) {
                return std::unexpected(Format("{}: \"{}\" is not a whole number from {} to {}", option, value, smallest, std::numeric_limits<T>::max()));
            }
            return *number;
        }

        std::expected<ChartKind, String> ParseKind(StringView value) {
            for (const ChartKind kind : EnumUtils<ChartKind>::Values()) {
                if (value == ChartKindName(kind)) {
                    return kind;
                }
            }
            return std::unexpected(Format("{}: \"{}\" is not a kind, use {} or {}", kKindOption, value, ChartKindName(ChartKind::Claims), ChartKindName(ChartKind::Discards)));
        }

        /// 0 is the hardware concurrency.
        std::expected<int, String> ParseJobs(StringView value) {
            const Optional<uint32_t> number = ParseWhole<uint32_t>(value);
            if (!number.has_value() || *number > static_cast<uint32_t>(kMaxJobs)) {
                return std::unexpected(Format("{}: \"{}\" is not a whole number from 0 to {}", kJobsOption, value, kMaxJobs));
            }
            if (*number == 0) {
                return std::clamp(static_cast<int>(Thread::HardwareConcurrency()), 1, kMaxJobs);
            }
            return static_cast<int>(*number);
        }

    }  // namespace

    StringView ChartKindName(ChartKind kind) noexcept {
        return kind == ChartKind::Claims ? "claims" : "discards";
    }

    String ChartsUsage() {
        return Format("usage: BagiMahjongCharts [options]\n"
                      "\n"
                      "Plays one hand per seed with four Normal seats and prints, per class, the mean points a choice gains with a 95 percent\n"
                      "interval over the seeds. What is measured is the kind:\n"
                      "  claims    every claim window: a chow, pung or kong of the discard against passing, the same split by whether Normal takes\n"
                      "            the claim (docs/charts-claims.md)\n"
                      "  discards  every own turn where Normal's rule picks a discard and the value reading picks another: the value reading's\n"
                      "            discard against Normal's (docs/charts-discards.md)\n"
                      "\n"
                      "options:\n"
                      "  {kind} KIND     claims or discards; default {kindDefault}\n"
                      "  {first} N       the first seed; default {firstDefault}\n"
                      "  {seeds} COUNT   the number of seeds, one hand each; default {seedsDefault}\n"
                      "  {worlds} N      the worlds (sampled hidden tiles, each with a rollout per action) per window or turn; default {worldsDefault}\n"
                      "  {jobs} N        N threads in all (1 to {maxJobs}; 0 is one per core), whole seeds each; the report is the same bytes whatever N is;\n"
                      "                  default {jobsDefault}\n"
                      "  {markdown}      print the tables as markdown (the numbers of the doc are pasted from it)\n"
                      "  {help}          this text\n"
                      "\n"
                      "exit code: {ok} done, {refused} an action was refused by the rules or a window or turn could not be measured (a defect; the seed is\n"
                      "printed), {bad} bad arguments.\n",
                      FormatArg("kind", kKindOption), FormatArg("kindDefault", ChartKindName(ChartKind::Claims)), FormatArg("first", kFirstOption), FormatArg("seeds", kSeedsOption),
                      FormatArg("worlds", kWorldsOption), FormatArg("jobs", kJobsOption), FormatArg("markdown", kMarkdownOption), FormatArg("help", kHelpOption),
                      FormatArg("firstDefault", kDefaultFirstSeed), FormatArg("seedsDefault", kDefaultChartSeeds), FormatArg("worldsDefault", kDefaultChartWorlds), FormatArg("maxJobs", kMaxJobs),
                      FormatArg("jobsDefault", kDefaultJobs), FormatArg("ok", kExitOk), FormatArg("refused", kExitRefusedAction), FormatArg("bad", kExitBadArguments));
    }

    std::expected<ChartsArgs, String> ParseChartsArgs(Span<const StringView> args) {
        ChartsArgs parsed;
        for (size_t index = 0; index < args.size(); ++index) {
            const StringView option = args[index];
            if (option == kHelpOption) {
                parsed.help = true;
                return parsed;
            }
            if (option == kMarkdownOption) {
                parsed.markdown = true;
                continue;
            }
            const bool takesValue = option == kKindOption || option == kFirstOption || option == kSeedsOption || option == kWorldsOption || option == kJobsOption;
            if (!takesValue) {
                return std::unexpected(Format("unknown argument \"{}\"", option));
            }
            if (index + 1 >= args.size()) {
                return std::unexpected(Format("{} needs a value", option));
            }
            const StringView value = args[++index];
            if (option == kKindOption) {
                const std::expected<ChartKind, String> kind = ParseKind(value);
                if (!kind.has_value()) {
                    return std::unexpected(kind.error());
                }
                parsed.kind = *kind;
            } else if (option == kJobsOption) {
                const std::expected<int, String> jobs = ParseJobs(value);
                if (!jobs.has_value()) {
                    return std::unexpected(jobs.error());
                }
                parsed.config.jobs = *jobs;
            } else if (option == kFirstOption) {
                const std::expected<uint32_t, String> first = ParseNumberOption<uint32_t>(option, value, 0);
                if (!first.has_value()) {
                    return std::unexpected(first.error());
                }
                parsed.config.first = *first;
            } else {
                const std::expected<int, String> number = ParseNumberOption<int>(option, value, 1);
                if (!number.has_value()) {
                    return std::unexpected(number.error());
                }
                (option == kSeedsOption ? parsed.config.count : parsed.config.worlds) = *number;
            }
        }
        const String problem = ValidateChartsConfig(parsed.config);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        return parsed;
    }

    int RunChartsCli(Span<const StringView> args, ArenaCliOutput& output) {
        const std::expected<ChartsArgs, String> parsed = ParseChartsArgs(args);
        if (!parsed.has_value()) {
            output.err = Format("error: {}\n\n{}", parsed.error().c_str(), ChartsUsage().c_str());
            return kExitBadArguments;
        }
        if (parsed->help) {
            output.out = ChartsUsage();
            return kExitOk;
        }
        if (parsed->kind == ChartKind::Discards) {
            const std::expected<DiscardChartsResult, String> result = RunDiscardCharts(parsed->config);
            if (!result.has_value()) {
                output.err = Format("error: {}\n", result.error().c_str());
                return kExitRefusedAction;
            }
            output.out = parsed->markdown ? FormatDiscardChartsMarkdown(*result) : FormatDiscardChartsReport(*result);
            return kExitOk;
        }
        const std::expected<ChartsResult, String> result = RunCharts(parsed->config);
        if (!result.has_value()) {
            output.err = Format("error: {}\n", result.error().c_str());
            return kExitRefusedAction;
        }
        output.out = parsed->markdown ? FormatChartsMarkdown(*result) : FormatChartsReport(*result);
        return kExitOk;
    }

}  // namespace BECore::Mahjong::Arena
