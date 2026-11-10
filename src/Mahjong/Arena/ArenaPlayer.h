#pragma once

#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongRng.h>

namespace BECore::Mahjong::Arena {

    /// One knob of `LevelProfile` the arena can set from a text: its name (the member's name), where it lives in the profile and the
    /// largest value (a percent is 0..100, a bool 0..1, a count of the search `kSearchMax...`; the smallest is always 0). **The one place to add a knob for an experiment:** a
    /// new field of `LevelProfile` needs a row here and a larger `kProfileFieldCount` (the build stops in ArenaPlayer.cpp, which names
    /// every field, and `ArenaPlayerTest` fails when a row does not move its own field).
    struct ProfileKnob {
        eastl::string_view name;
        /// Exactly one of the two members is set: the field is an `int` percent, or a `bool`.
        int LevelProfile::* percent = nullptr;
        bool LevelProfile::* flag = nullptr;
        int maxValue = kPercentBase;
    };

    /// The number of fields of `LevelProfile`, and so of rows of `kProfileKnobs`.
    inline constexpr size_t kProfileFieldCount = 7;

    inline constexpr eastl::array<ProfileKnob, kProfileFieldCount> kProfileKnobs{{
        {"skipAcceptancePercent", &LevelProfile::skipAcceptancePercent, nullptr, kPercentBase},
        {"skipDeclarationPercent", &LevelProfile::skipDeclarationPercent, nullptr, kPercentBase},
        {"skipClaimPercent", &LevelProfile::skipClaimPercent, nullptr, kPercentBase},
        {"takeValueClaimsAtEqualShanten", nullptr, &LevelProfile::takeValueClaimsAtEqualShanten, 1},
        {"searchWorlds", &LevelProfile::searchWorlds, nullptr, kSearchMaxWorlds},
        {"searchCandidates", &LevelProfile::searchCandidates, nullptr, kSearchMaxCandidates},
        {"searchMargin", &LevelProfile::searchMargin, nullptr, kSearchMaxMargin},
    }};

    /// Who sits on a seat: the random "seek win" chooser, or a computer player that plays by `profile`. `name` is the text it was made
    /// from (`normal`, `easy:skipClaimPercent=20`), what a report prints.
    struct ArenaPlayer {
        bool random = false;
        LevelProfile profile = kNormalProfile;
        eastl::string name = "normal";
    };

    /// The player of `spec`: `random`, a level (`1` .. `10`, or the names `easy` = 1 and `normal` = 5), or a level with knobs, `<level>:<knob>=<value>,...` (the
    /// knobs of `kProfileKnobs`, each at most once; the level's profile is the start and the listed knobs are set on it). The names are
    /// exactly the lowercase ones. A bad spec is an error text that names the part that is wrong; nothing is thrown.
    [[nodiscard]] std::expected<ArenaPlayer, eastl::string> ParseArenaPlayer(std::string_view spec);

    /// The name of a player for a table (`ArenaPlayer::name`).
    [[nodiscard]] inline const eastl::string& PlayerName(const ArenaPlayer& player) noexcept {
        return player.name;
    }

    /**
     * @brief The "seek win" choice of the random seats: the same rule as `Choose(actions, Mode::SeekWin, rng)` of
     *        `tests/HandTestSupport.h` and the same draws from `rng`, so the arena plays the hands of `ComputerStrengthTest`.
     *
     * A win when it can; else one of the discards that declare the calling hand and the kongs and claims (a pung, a kong, a chow, a
     * concealed or an upgraded kong), picked by `rng`; else any listed action. `actions` must not be empty.
     */
    [[nodiscard]] HandAction ChooseSeekWin(const eastl::vector<HandAction>& actions, MahjongRng& rng);

}  // namespace BECore::Mahjong::Arena
