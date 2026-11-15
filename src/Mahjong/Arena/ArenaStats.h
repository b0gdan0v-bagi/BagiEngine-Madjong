#pragma once

#include <Mahjong/Arena/ArenaRun.h>

namespace BECore::Mahjong::Arena {

    /// The normal approximation of a 95 percent interval; fine from about a hundred seeds.
    inline constexpr double kIntervalZ = 1.96;
    inline constexpr size_t kMinSeedsForInterval = 2;
    inline constexpr double kPercent = 100.0;

    /// A mean over seeds and its interval `mean - half .. mean + half`.
    struct MeanInterval {
        size_t n = 0;
        double mean = 0.0;
        double sd = 0.0;
        double half = 0.0;
        /// False when there are fewer than `kMinSeedsForInterval` samples: `sd` and `half` are 0 and mean nothing.
        bool hasInterval = false;
    };

    /// What the per-seed series hold, per hand of the seed's four rotations. `StandingScore` is `SeedRow::standingScore / lostHands` and
    /// has no value for a seed in which the focus player lost none.
    CORE_ENUM(SeedMetric, uint8_t, PointsPerHand, WinPercent, DrawnPercent, DeclaredPercent, GaveWinPercent, StandingScore)

    /**
     * @brief The mean of `values`, its sample standard deviation and `half = kIntervalZ * sd / sqrt(n)`.
     *
     * The values are one per seed: the four rotations of a seed play one wall, so the seed is the independent unit. No values give
     * `n` 0; one value gives its mean and no interval.
     */
    [[nodiscard]] MeanInterval MeanCi(const Vector<double>& values);

    /// The per-seed value of `metric` in seed order. A seed without a value is left out, so the series can be shorter than the rows.
    [[nodiscard]] Vector<double> SeedSeries(const FocusResult& focus, SeedMetric metric);

    [[nodiscard]] MeanInterval MetricInterval(const FocusResult& focus, SeedMetric metric);

    /**
     * @brief The paired difference `a - b` of a metric: the mean and the interval of the per-seed differences.
     *
     * Both players played the same walls, so the difference of a seed is a sample of its own and the interval is much tighter than
     * that of two plain means. The two results must hold the same seeds in the same order, else (or when empty) `n` is 0. A seed where
     * either player has no value is left out.
     */
    [[nodiscard]] MeanInterval PairedDifference(const FocusResult& a, const FocusResult& b, SeedMetric metric);

    /// The interval excludes 0; without an interval never.
    [[nodiscard]] bool IsSignificant(const MeanInterval& interval) noexcept;

}  // namespace BECore::Mahjong::Arena
