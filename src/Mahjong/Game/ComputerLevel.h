#pragma once

namespace BECore::Mahjong {

    /// How well a computer seat plays. `Normal` is the player described in `CLAUDE.md` ("Computer player"); `Easy` is the same player
    /// with other numbers (`kEasyProfile`). **`Hard` plays exactly like `Normal`**: it is in the enum, the control API and the match log
    /// because the owner chose three levels, but what Hard should be waits for the owner's decision (the levers tried, a suit focus, a
    /// look-ahead, passing claims to keep a concealed hand and kong claims, all lost wins and did not beat Normal in these rules; the
    /// measurements are in the todo store, in the entry `mahjong-computer-difficulty`).
    CORE_ENUM(ComputerLevel, uint8_t, Easy, Normal, Hard)

    /// The level of a table nobody chose a level for.
    inline constexpr ComputerLevel kDefaultLevel = ComputerLevel::Normal;

    /// The number of levels.
    inline constexpr int kLevelCount = static_cast<int>(EnumUtils<ComputerLevel>::Count());

    /// The name of a level in the control API and the match log: `easy`, `normal`, `hard` (lowercase). A value outside the enum asserts
    /// in debug and answers the name of the default.
    [[nodiscard]] constexpr std::string_view LevelName(ComputerLevel level) noexcept {
        switch (level) {
            case ComputerLevel::Easy:
                return "easy";
            case ComputerLevel::Normal:
                return "normal";
            case ComputerLevel::Hard:
                return "hard";
        }
        ASSERT(false, "a value outside ComputerLevel");
        return "normal";
    }

    /// The level named `text`: exactly the lowercase names of `LevelName`, nothing else (no case folding, no spaces).
    [[nodiscard]] constexpr std::optional<ComputerLevel> TryParseLevel(std::string_view text) noexcept {
        if (text == LevelName(ComputerLevel::Easy)) {
            return ComputerLevel::Easy;
        }
        if (text == LevelName(ComputerLevel::Normal)) {
            return ComputerLevel::Normal;
        }
        if (text == LevelName(ComputerLevel::Hard)) {
            return ComputerLevel::Hard;
        }
        return std::nullopt;
    }

    /// The base of a percentage: a roll is one `MahjongRng::Below(kPercentBase)` against the percent.
    inline constexpr int kPercentBase = 100;

    /**
     * @brief The numbers a level plays by: what the choosers of `ComputerTurn.h` and `ComputerClaim.h` do differently from the Normal
     * player. Every knob of `kNormalProfile` is off, and **a knob that is off draws nothing from the rng**, so Normal's stream is the
     * stream of the player before levels existed. A percentage is 0..100; each roll is one `MahjongRng::Below(kPercentBase)` of the player's own
     * seeded rng, so one seed and one setup is one match.
     */
    struct LevelProfile {
        /// Step 5 of the own turn only: the share of discards chosen among those that leave the lowest shanten by `KeepValue` and the
        /// rng, ignoring the acceptance (the tile that gives the most improving copies). The hand never gets further from a win by it.
        int skipAcceptancePercent = 0;
        /// Step 2 of the own turn: the share of turns on which a discard that could declare the calling hand does not declare (the
        /// hand plays on by the later steps and is asked again next turn). `DeclareWin` (step 1) is never skipped.
        int skipDeclarationPercent = 0;
        /// The share of acceptable pung, kong and chow claims that are passed. A mahjong claim is never passed.
        int skipClaimPercent = 0;
        /// Whether a pung or kong of a value tile (a dragon, the own wind, the round wind) is taken at an equal shanten, for the double.
        bool takeValueClaimsAtEqualShanten = true;
    };

    /// The Normal player: every knob off.
    inline constexpr LevelProfile kNormalProfile{};

    /// What Easy plays by (measured in `ComputerStrengthTest`, whose comment has the numbers): about half of Normal's wins, still
    /// several times a random seat's.
    inline constexpr LevelProfile kEasyProfile{
        .skipAcceptancePercent = 60,
        .skipDeclarationPercent = 85,
        .skipClaimPercent = 35,
        .takeValueClaimsAtEqualShanten = false,
    };

    /// What Hard plays by: Normal's numbers, until the owner decides what Hard is (see `ComputerLevel`). The one place that goes when
    /// Hard gets numbers of its own; `ComputerLevelTest` pins the twin and goes with it.
    inline constexpr LevelProfile kHardProfile = kNormalProfile;

    /// The profile of `level`. A value outside the enum gives Normal's.
    [[nodiscard]] constexpr const LevelProfile& ProfileOf(ComputerLevel level) noexcept {
        switch (level) {
            case ComputerLevel::Easy:
                return kEasyProfile;
            case ComputerLevel::Normal:
                return kNormalProfile;
            case ComputerLevel::Hard:
                return kHardProfile;
        }
        return kNormalProfile;
    }

    /// The names of the levels for an error text: "easy, normal or hard" (`LevelName` of each, in the order of the enum).
    [[nodiscard]] inline eastl::string LevelNamesText() {
        eastl::string names;
        for (int index = 0; index < kLevelCount; ++index) {
            const std::string_view name = LevelName(static_cast<ComputerLevel>(index));
            if (index > 0) {
                names += index + 1 == kLevelCount ? " or " : ", ";
            }
            names.append(name.data(), name.size());
        }
        return names;
    }

}  // namespace BECore::Mahjong
