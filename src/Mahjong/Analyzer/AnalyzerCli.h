#pragma once

#include <Mahjong/Analyzer/AnalyzerReport.h>
#include <Mahjong/Arena/ArenaRun.h>

namespace BECore::Mahjong::Analyzer {

    /// The exit codes of `BagiMahjongAnalyzer`.
    inline constexpr int kExitOk = 0;
    /// A position could not be read or analyzed (a file that cannot be read, a log that does not replay, a text that is no position,
    /// a hand or an action out of range, no world that could be sampled); the error names it and the reports made before it are printed.
    inline constexpr int kExitAnalysisFailed = 1;
    /// The arguments are wrong (the error and the usage are printed).
    inline constexpr int kExitBadArguments = 2;

    /// The range of `--worlds`.
    inline constexpr int kMinWorlds = 1;
    inline constexpr int kMaxWorlds = 1'000'000;
    /// The most threads of `--jobs` (the arena's limit).
    inline constexpr int kMaxJobs = Arena::kMaxJobs;
    inline constexpr int kDefaultJobs = 1;

    /// The name of a file that stands for the standard input (the reader of the program decides what it reads).
    inline constexpr std::string_view kStandardInputName = "-";

    /// What the command line asks for.
    struct AnalyzerArgs {
        /// `--log FILE` with `--hand K` and `--action I` (one decision) or `--seat S` (every decision of that seat in the hand).
        eastl::string logFile;
        int hand = 0;
        std::optional<int> action;
        std::optional<Seat> seat;
        /// `--position FILE`: a hand-written position (`ParsePosition`).
        eastl::string positionFile;
        /// `--worlds`, `--seed`; `normalSeed` stays the default.
        AnalyzerConfig config;
        /// The threads that play the worlds, 1 to `kMaxJobs`.
        int jobs = kDefaultJobs;
        bool json = false;
        /// `--help` was given: print the usage and analyze nothing.
        bool help = false;
    };

    /// The text of `--help`: the options with their defaults, the numbering of `--hand` / `--action`, the exit codes.
    [[nodiscard]] eastl::string AnalyzerUsage();

    /**
     * @brief Reads the arguments (without the name of the program).
     *
     * `--log FILE` with `--hand K` and either `--action I` or `--seat S`, or `--position FILE`; `--worlds N`, `--seed N`, `--jobs N`
     * (0 is the hardware concurrency), `--json`, `--help` (which wins over everything after it). A bad argument is an error text that
     * names it: an unknown option, a missing or malformed value, a value out of its range, a `--log` and a `--position` together, a
     * `--log` without `--hand` or without one of `--action` / `--seat`, `--action` together with `--seat`, or `--hand`, `--action`
     * or `--seat` without a `--log`.
     */
    [[nodiscard]] std::expected<AnalyzerArgs, eastl::string> ParseAnalyzerArgs(eastl::span<const std::string_view> args);

    /// The text of a file, or the error text. The program supplies the real one; `kStandardInputName` is its to interpret.
    using FileReader = std::function<std::expected<eastl::string, eastl::string>(std::string_view path)>;

    /// What the program prints: the reports or the usage to `out`, an error (and the usage for bad arguments) to `err`.
    struct AnalyzerCliOutput {
        eastl::string out;
        eastl::string err;
    };

    /**
     * @brief The whole program: parses `args`, reads the positions through `readFile`, analyzes, formats and returns the exit code.
     *
     * `--help` prints the usage to `out`; bad arguments print the error and the usage to `err` and read nothing
     * (`kExitBadArguments`). Else one report (or, with `--json`, one JSON line) per position goes to `out`, in the order of the
     * decisions, text reports separated by an empty line; with `--seat` a decision that has a single legal action is one line that says
     * so (a JSON object with `skipped`). The first failure stops the run: its error goes to `err`, the reports before it stay in `out`
     * (`kExitAnalysisFailed`). The same arguments and files give the same bytes at any `--jobs`.
     */
    [[nodiscard]] int RunAnalyzerCli(eastl::span<const std::string_view> args, const FileReader& readFile, AnalyzerCliOutput& output);

}  // namespace BECore::Mahjong::Analyzer
