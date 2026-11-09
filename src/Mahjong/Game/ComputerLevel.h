#pragma once


namespace BECore::Mahjong {

    /// How well a computer seat plays. `Normal` is the player described in `CLAUDE.md` ("Computer player"); `Easy` and `Hard` are
    /// the same player with other numbers (`ProfileOf`, once the profiles exist: today every level plays like `Normal`).
    CORE_ENUM(ComputerLevel, uint8_t, Easy, Normal, Hard)

    /// The level of a table nobody chose a level for.
    inline constexpr ComputerLevel kDefaultLevel = ComputerLevel::Normal;

    /// The number of levels.
    inline constexpr int kLevelCount = 3;

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

}  // namespace BECore::Mahjong
