#include "ArenaStats.h"

#include <cmath>

namespace BECore::Mahjong::Arena {

    namespace {

        /// The number of `metric` in one seed row (over its `kSeatCount` hands); empty when the row has none.
        std::optional<double> RowNumber(const SeedRow& row, SeedMetric metric) {
            switch (metric) {
                case SeedMetric::PointsPerHand:
                    return static_cast<double>(row.pointsOfFocus) / kSeatCount;
                case SeedMetric::WinPercent:
                    return kPercent * row.winsOfFocus / kSeatCount;
                case SeedMetric::DrawnPercent:
                    return kPercent * row.drawn / kSeatCount;
                case SeedMetric::DeclaredPercent:
                    return kPercent * row.declared / kSeatCount;
                case SeedMetric::GaveWinPercent:
                    return kPercent * row.gaveWin / kSeatCount;
                case SeedMetric::StandingScore:
                    if (row.lostHands == 0) {
                        return std::nullopt;
                    }
                    return static_cast<double>(row.standingScore) / row.lostHands;
            }
            return 0.0;
        }

    }  // namespace

    MeanInterval MeanCi(const eastl::vector<double>& values) {
        MeanInterval interval;
        interval.n = values.size();
        if (values.empty()) {
            return interval;
        }
        double sum = 0.0;
        for (const double value : values) {
            sum += value;
        }
        const double count = static_cast<double>(values.size());
        interval.mean = sum / count;
        if (values.size() < kMinSeedsForInterval) {
            return interval;
        }
        double squares = 0.0;
        for (const double value : values) {
            const double deviation = value - interval.mean;
            squares += deviation * deviation;
        }
        interval.sd = std::sqrt(squares / (count - 1.0));
        interval.half = kIntervalZ * interval.sd / std::sqrt(count);
        interval.hasInterval = true;
        return interval;
    }

    eastl::vector<double> SeedSeries(const FocusResult& focus, SeedMetric metric) {
        eastl::vector<double> series;
        series.reserve(focus.rows.size());
        for (const SeedRow& row : focus.rows) {
            if (const std::optional<double> value = RowNumber(row, metric)) {
                series.push_back(*value);
            }
        }
        return series;
    }

    MeanInterval MetricInterval(const FocusResult& focus, SeedMetric metric) {
        return MeanCi(SeedSeries(focus, metric));
    }

    MeanInterval PairedDifference(const FocusResult& a, const FocusResult& b, SeedMetric metric) {
        if (a.rows.size() != b.rows.size()) {
            return {};
        }
        eastl::vector<double> differences;
        differences.reserve(a.rows.size());
        for (size_t index = 0; index < a.rows.size(); ++index) {
            if (a.rows[index].seed != b.rows[index].seed) {
                return {};
            }
            const std::optional<double> left = RowNumber(a.rows[index], metric);
            const std::optional<double> right = RowNumber(b.rows[index], metric);
            if (left && right) {
                differences.push_back(*left - *right);
            }
        }
        return MeanCi(differences);
    }

    bool IsSignificant(const MeanInterval& interval) noexcept {
        return interval.hasInterval && (interval.mean - interval.half > 0.0 || interval.mean + interval.half < 0.0);
    }

}  // namespace BECore::Mahjong::Arena
