#pragma once

#include <Mahjong/Arena/ArenaRun.h>

namespace BECore::Mahjong::Arena {

    /// The multiplier of the interval: 1.96 standard errors of the mean, a 95 percent interval by the normal approximation (fine from
    /// about a hundred seeds).
    inline constexpr double kIntervalZ = 1.96;
    /// The fewest samples that give a standard deviation (and so an interval).
    inline constexpr size_t kMinSeedsForInterval = 2;
    /// A fraction is shown as a percent.
    inline constexpr double kPercent = 100.0;

    /// A mean over seeds and its interval. `half` is the half-width: the interval is `mean - half .. mean + half`.
    struct MeanInterval {
        size_t n = 0;
        double mean = 0.0;
        /// The sample standard deviation (divisor `n - 1`).
        double sd = 0.0;
        double half = 0.0;
        /// False when there are fewer than `kMinSeedsForInterval` samples: `sd` and `half` are 0 and mean nothing.
        bool hasInterval = false;
    };

    /// A number of a seed's four rotations, per hand: what the per-seed series hold.
    CORE_ENUM(SeedMetric, uint8_t, PointsPerHand, WinPercent, DrawnPercent, DeclaredPercent)

    /**
     * @brief The mean of `values`, its sample standard deviation and `half = kIntervalZ * sd / sqrt(n)`.
     *
     * The values are one per seed: the four rotations of a seed play one wall, so the seed is the independent unit. No values give
     * `n` 0 and a mean of 0; one value gives its mean and no interval (`hasInterval` false). A constant series has `half` 0.
     */
    [[nodiscard]] MeanInterval MeanCi(const eastl::vector<double>& values);

    /// The per-seed value of `metric` for one focus player, in seed order: the number of the seed's four rotations divided by
    /// `kSeatCount` (`PointsPerHand` in points, the others in percent of the hands).
    [[nodiscard]] eastl::vector<double> SeedSeries(const FocusResult& focus, SeedMetric metric);

    /// `MeanCi` of the series of `metric` of one focus player.
    [[nodiscard]] MeanInterval MetricInterval(const FocusResult& focus, SeedMetric metric);

    /**
     * @brief The paired difference `a - b` of a metric: the mean and the interval of the per-seed differences.
     *
     * Both players played the same walls, so the seeds are paired and the difference of a seed is a sample of its own; its interval is
     * much tighter than that of two plain means. The two results must hold the same seeds in the same order; if they do not (or are
     * empty) the answer has `n` 0.
     */
    [[nodiscard]] MeanInterval PairedDifference(const FocusResult& a, const FocusResult& b, SeedMetric metric);

    /// True when the interval excludes 0 (it needs an interval; a constant difference other than 0 excludes it).
    [[nodiscard]] bool IsSignificant(const MeanInterval& interval) noexcept;

}  // namespace BECore::Mahjong::Arena
