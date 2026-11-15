#pragma once

#include <Mahjong/Analyzer/AnalyzerReport.h>
#include <Mahjong/Arena/ArenaRun.h>

namespace BECore::Mahjong::Analyzer {

    inline constexpr int kExitOk = 0;
    /// A position could not be read or analyzed; the reports made before it are printed.
    inline constexpr int kExitAnalysisFailed = 1;
    inline constexpr int kExitBadArguments = 2;

    inline constexpr int kMinWorlds = 1;
    inline constexpr int kMaxWorlds = 1'000'000;
    inline constexpr int kMaxJobs = Arena::kMaxJobs;
    inline constexpr int kDefaultJobs = 1;

    /// The reader of the program decides what it reads.
    inline constexpr StringView kStandardInputName = "-";

    struct AnalyzerArgs {
        /// `--log FILE` with `--hand K` and `--action I` (one decision) or `--seat S` (every decision of that seat in the hand).
        String logFile;
        int hand = 0;
        Optional<int> action;
        Optional<Seat> seat;
        /// `--position FILE`: a hand-written position (`ParsePosition`).
        String positionFile;
        /// `--worlds`, `--seed`; `normalSeed` stays the default.
        AnalyzerConfig config;
        /// The threads that play the worlds, 1 to `kMaxJobs`.
        int jobs = kDefaultJobs;
        bool json = false;
        bool help = false;
    };

    [[nodiscard]] String AnalyzerUsage();

    /**
     * @brief Reads the arguments (without the name of the program); the options are in `AnalyzerUsage()`. `--help` wins over
     * everything after it, `--jobs 0` is the hardware concurrency. A bad argument is an error text that names it.
     */
    [[nodiscard]] std::expected<AnalyzerArgs, String> ParseAnalyzerArgs(Span<const StringView> args);

    /// The text of a file, or the error text. The program supplies the real one; `kStandardInputName` is its to interpret.
    using FileReader = std::function<std::expected<String, String>(StringView path)>;

    /// What the program prints: the reports or the usage to `out`, an error (and the usage for bad arguments) to `err`.
    struct AnalyzerCliOutput {
        String out;
        String err;
    };

    /**
     * @brief The whole program: parses `args`, reads the positions through `readFile`, analyzes, formats and returns the exit code.
     *
     * Bad arguments read nothing (`kExitBadArguments`). The first failure stops the run: its error goes to `err`, the reports before
     * it stay in `out` (`kExitAnalysisFailed`). The same arguments and files give the same bytes at any `--jobs`.
     */
    [[nodiscard]] int RunAnalyzerCli(Span<const StringView> args, const FileReader& readFile, AnalyzerCliOutput& output);

}  // namespace BECore::Mahjong::Analyzer
