#include "ArenaCli.h"

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr std::string_view kFocusOption = "--focus";
        constexpr std::string_view kOthersOption = "--others";
        constexpr std::string_view kFirstOption = "--first";
        constexpr std::string_view kSeedsOption = "--seeds";
        constexpr std::string_view kTimeOption = "--time";
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
                      "  {time}          also print the time per decision (wall clock: the one number that differs between runs)\n"
                      "  {help}          this text\n"
                      "\n"
                      "exit code: {ok} done, {refused} an action was refused by the rules (a defect; the seed and the seat are printed), {bad} bad arguments.\n"
                      "\n"
                      "interval: the four rotations of a seed share one wall, so the seed is the sample; a number is the mean over the seeds and\n"
                      "+- is 1.96 standard errors (normal approximation, fine from about 100 seeds). Read the paired rows for a difference.\n",
                      fmt::arg("levels", LevelNamesText().c_str()), fmt::arg("focus", kFocusOption), fmt::arg("others", kOthersOption), fmt::arg("first", kFirstOption),
                      fmt::arg("seeds", kSeedsOption), fmt::arg("time", kTimeOption), fmt::arg("help", kHelpOption), fmt::arg("focusDefault", kDefaultFocusSpec),
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
        for (size_t index = 0; index < args.size(); ++index) {
            const std::string_view option = args[index];
            if (option == kHelpOption) {
                parsed.help = true;
                return parsed;
            }
            if (option == kTimeOption) {
                parsed.config.timing = true;
                continue;
            }
            const bool takesValue = option == kFocusOption || option == kOthersOption || option == kFirstOption || option == kSeedsOption;
            if (!takesValue) {
                return std::unexpected(Format("unknown argument \"{}\"", option));
            }
            if (index + 1 >= args.size()) {
                return std::unexpected(Format("{} needs a value", option));
            }
            const std::string_view value = args[++index];
            if (option == kFocusOption) {
                std::expected<eastl::vector<ArenaPlayer>, eastl::string> focus = ParseFocusList(value);
                if (!focus.has_value()) {
                    return std::unexpected(focus.error());
                }
                parsed.config.focus = std::move(*focus);
            } else if (option == kOthersOption) {
                std::expected<ArenaPlayer, eastl::string> others = ParseArenaPlayer(value);
                if (!others.has_value()) {
                    return std::unexpected(Format("{}: {}", option, others.error().c_str()));
                }
                parsed.config.others = std::move(*others);
            } else if (option == kFirstOption) {
                const std::expected<uint32_t, eastl::string> first = ParseNumberOption<uint32_t>(option, value, 0);
                if (!first.has_value()) {
                    return std::unexpected(first.error());
                }
                parsed.config.first = *first;
            } else {
                const std::expected<int, eastl::string> count = ParseNumberOption<int>(option, value, 1);
                if (!count.has_value()) {
                    return std::unexpected(count.error());
                }
                parsed.config.count = *count;
            }
        }
        const eastl::string problem = ValidateArenaConfig(parsed.config);
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        return parsed;
    }

    int RunArenaCli(eastl::span<const std::string_view> args, ArenaCliOutput& output) {
        const std::expected<ArenaArgs, eastl::string> parsed = ParseArenaArgs(args);
        if (!parsed.has_value()) {
            output.err = Format("error: {}\n\n{}", parsed.error().c_str(), ArenaUsage().c_str());
            return kExitBadArguments;
        }
        if (parsed->help) {
            output.out = ArenaUsage();
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
