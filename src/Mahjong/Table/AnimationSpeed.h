#pragma once

namespace BECore::Mahjong {

    /**
     * The speed of the table, the one setting of the player (the page `Options`) that scales together the pause of the computers
     * (`kComputerStepSeconds`), the flights of the tiles (`kTileFlightSeconds`) and the marks of a claim (`kClaimHighlightSeconds`,
     * `kClaimBlinkSeconds`, `kClaimLostSeconds`, also the hold of the score table behind it). Plain C++, no scene: the table component
     * multiplies those base times by `SpeedFactor`, so every one of them stays what it was at `Normal` and a flight stays inside the
     * step at every speed. The control API does not wait, so it ignores the speed.
     */
    CORE_ENUM(AnimationSpeed, uint8_t, Slow, Normal, Fast)

    inline constexpr AnimationSpeed kDefaultAnimationSpeed = AnimationSpeed::Normal;

    /// The factors of the base times: more is slower. `Normal` is 1.
    inline constexpr float kSlowFactor = 1.6f;
    inline constexpr float kFastFactor = 0.6f;

    /// The factor of the base times at `speed`.
    [[nodiscard]] constexpr float SpeedFactor(AnimationSpeed speed) noexcept {
        switch (speed) {
            case AnimationSpeed::Slow:
                return kSlowFactor;
            case AnimationSpeed::Fast:
                return kFastFactor;
            case AnimationSpeed::Normal:
                break;
        }
        return 1.0f;
    }

    /// `baseSeconds` at `speed`.
    [[nodiscard]] constexpr float ScaledSeconds(float baseSeconds, AnimationSpeed speed) noexcept {
        return baseSeconds * SpeedFactor(speed);
    }

    /// The id of a speed in the saved settings, the variable `speedChoice` and the values of the buttons of the page: `slow`, `normal`, `fast`.
    [[nodiscard]] constexpr std::string_view SpeedText(AnimationSpeed speed) noexcept {
        switch (speed) {
            case AnimationSpeed::Slow:
                return "slow";
            case AnimationSpeed::Fast:
                return "fast";
            case AnimationSpeed::Normal:
                break;
        }
        return "normal";
    }

    /// The speed of an id, or nothing for any other text (client text, an old or damaged save).
    [[nodiscard]] constexpr std::optional<AnimationSpeed> TryParseSpeed(std::string_view text) noexcept {
        for (const AnimationSpeed speed : EnumUtils<AnimationSpeed>::Values()) {
            if (SpeedText(speed) == text) {
                return speed;
            }
        }
        return std::nullopt;
    }

}  // namespace BECore::Mahjong
