#pragma once

#include <Mahjong/Arena/ArenaReport.h>

namespace BECore::Mahjong::Arena {

    /// The exit codes of `BagiMahjongArena`.
    inline constexpr int kExitOk = 0;
    /// The model refused an action of a player (a defect of the rules or of a player; the error names the seed and the seat).
    inline constexpr int kExitRefusedAction = 1;
    /// The arguments are wrong (the error and the usage are printed).
    inline constexpr int kExitBadArguments = 2;

    /// The player of `--focus` when it is left out, and of `--others`.
    inline constexpr std::string_view kDefaultFocusSpec = "normal";
    inline constexpr std::string_view kDefaultOthersSpec = "random";

    /// What the command line asks for.
    struct ArenaArgs {
        /// The seeds, the players and `timing`, valid for `RunArena` (`ValidateArenaConfig` is empty).
        ArenaConfig config;
        /// `--help` was given: print the usage and play nothing.
        bool help = false;
    };

    /// The text of `--help`: the options with their defaults, the exit codes and the interval rule in two lines.
    [[nodiscard]] eastl::string ArenaUsage();

    /**
     * @brief Reads the arguments (without the name of the program).
     *
     * `--focus LIST` (players separated by commas; a part with an `=` and no `:` goes on the knobs of the player before it, so
     * `easy:skipClaimPercent=20,skipDeclarationPercent=10,normal` is two players), `--others SPEC`, `--first N`, `--seeds COUNT`,
     * `--time` and `--help` (which wins over everything after it). A bad argument is an error text that names it: an unknown option, a
     * missing or malformed value, a bad player spec, a range of seeds that `ValidateArenaConfig` refuses, a word that is no option.
     */
    [[nodiscard]] std::expected<ArenaArgs, eastl::string> ParseArenaArgs(eastl::span<const std::string_view> args);

    /// What the program prints: the report or the usage to `out`, an error and the usage to `err`.
    struct ArenaCliOutput {
        eastl::string out;
        eastl::string err;
    };

    /**
     * @brief The whole program: parses `args`, plays, formats and returns the exit code.
     *
     * `--help` prints the usage to `out` (`kExitOk`); bad arguments print the error and the usage to `err` and play nothing
     * (`kExitBadArguments`); a refused action of the model prints its error to `err` (`kExitRefusedAction`); else the report of
     * `FormatArenaReport` goes to `out`. The same arguments give the same bytes, `--time` apart.
     */
    [[nodiscard]] int RunArenaCli(eastl::span<const std::string_view> args, ArenaCliOutput& output);

}  // namespace BECore::Mahjong::Arena
