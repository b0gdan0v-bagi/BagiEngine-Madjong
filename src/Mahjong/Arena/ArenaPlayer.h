#pragma once

#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/OwnDraws.h>

namespace BECore::Mahjong::Arena {

    /// One knob of `LevelProfile` the arena can set from a text: the member's name, where it lives and its largest value (the smallest
    /// is always 0). **The one place to add a knob for an experiment:** a new field of `LevelProfile` needs a row here and a larger
    /// `kProfileFieldCount`; the build stops in ArenaPlayer.cpp until it has.
    struct ProfileKnob {
        StringView name;
        /// Exactly one of the two members is set: the field is an `int` percent, or a `bool`.
        int LevelProfile::* percent = nullptr;
        bool LevelProfile::* flag = nullptr;
        int maxValue = kPercentBase;
    };

    /// One row of `kProfileKnobs` per field of `LevelProfile`.
    inline constexpr size_t kProfileFieldCount = 13;

    inline constexpr Array<ProfileKnob, kProfileFieldCount> kProfileKnobs{{
        {"skipAcceptancePercent", &LevelProfile::skipAcceptancePercent, nullptr, kPercentBase},
        {"skipDeclarationPercent", &LevelProfile::skipDeclarationPercent, nullptr, kPercentBase},
        {"skipClaimPercent", &LevelProfile::skipClaimPercent, nullptr, kPercentBase},
        {"takeValueClaimsAtEqualShanten", nullptr, &LevelProfile::takeValueClaimsAtEqualShanten, 1},
        {"searchWorlds", &LevelProfile::searchWorlds, nullptr, kSearchMaxWorlds},
        {"searchCandidates", &LevelProfile::searchCandidates, nullptr, kSearchMaxCandidates},
        {"searchMargin", &LevelProfile::searchMargin, nullptr, kSearchMaxMargin},
        {"ownDrawsDepth", &LevelProfile::ownDrawsDepth, nullptr, kOwnDrawsMaxDepth},
        {"ownDrawsPickWait", nullptr, &LevelProfile::ownDrawsPickWait, 1},
        {"ownDrawsDiscardWins", nullptr, &LevelProfile::ownDrawsDiscardWins, 1},
        {"ownDrawsClaimDepth", &LevelProfile::ownDrawsClaimDepth, nullptr, kOwnDrawsMaxDepth},
        {"readDanger", nullptr, &LevelProfile::readDanger, 1},
        {"waitingKongByPoints", nullptr, &LevelProfile::waitingKongByPoints, 1},
    }};

    /// What an arena-only player knows beyond a computer player's view (`ArenaOracle.h`). `None` is every ordinary player.
    CORE_ENUM(OracleKind, uint8_t, None, Acceptance, Hands, WholeHand, Defence)

    /// Who sits on a seat: the random "seek win" chooser, a computer player of `profile`, or an oracle (plays through `ChooseOracle`,
    /// on the Normal profile with the spec's knobs). `name` is the spec it was made from, what a report prints.
    struct ArenaPlayer {
        bool random = false;
        OracleKind oracle = OracleKind::None;
        LevelProfile profile = kNormalProfile;
        String name = "normal";
    };

    /// The grammar is `ArenaUsage()` and `docs/bot-arena.md`. A bad spec is an error text that names the part that is wrong; nothing
    /// is thrown.
    [[nodiscard]] std::expected<ArenaPlayer, String> ParseArenaPlayer(StringView spec);

    [[nodiscard]] inline const String& PlayerName(const ArenaPlayer& player) noexcept {
        return player.name;
    }

    /**
     * @brief The random seats' choice: the rule of `Choose(actions, Mode::SeekWin, rng)` of `tests/HandTestSupport.h` with the same
     *        draws from `rng`, so the arena plays the hands of `ComputerStrengthTest`.
     *
     * A win when it can; else one of the declaring discards, kongs and claims, picked by `rng`; else any listed action. `actions` must
     * not be empty.
     */
    [[nodiscard]] HandAction ChooseSeekWin(const Vector<HandAction>& actions, MahjongRng& rng);

}  // namespace BECore::Mahjong::Arena
