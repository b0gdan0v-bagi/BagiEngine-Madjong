#include "ArenaReportJson.h"

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr double kScale = 1000000.0;
        static_assert(kArenaReportJsonPrecision == 6, "Rounded() scales by 10^kArenaReportJsonPrecision");

        /// Rounded to the precision of the document, and never a negative zero.
        double Rounded(double value) {
            const double rounded = std::round(value * kScale) / kScale;
            return rounded == 0.0 ? 0.0 : rounded;
        }

        Json IntervalToJson(const MeanInterval& interval) {
            Json json = Json::object();
            json["mean"] = Rounded(interval.mean);
            json["half"] = interval.hasInterval ? Rounded(interval.half) : 0.0;
            json["hasInterval"] = interval.hasInterval;
            return json;
        }

        Json PairedToJson(const MeanInterval& interval) {
            Json json = IntervalToJson(interval);
            json["significant"] = IsSignificant(interval);
            return json;
        }

        Json OthersRowToJson(const ArenaTally& others, int64_t seats) {
            Json json = Json::object();
            json["hands"] = others.hands / seats;
            json["winPercent"] = Rounded(PercentOf(others.wins, others.hands));
            json["pointsPerHand"] = Rounded(AverageOf(others.points, others.hands));
            json["drawnPercent"] = Rounded(PercentOf(others.drawn, others.hands));
            json["declaredPercent"] = Rounded(PercentOf(others.declared, others.hands));
            json["avgWinScore"] = Rounded(AverageOf(others.winScore, others.wins));
            return json;
        }

        Json OthersEndsToJson(const ArenaTally& others) {
            Json json = Json::object();
            json["gaveWinPercent"] = Rounded(PercentOf(others.gaveWin, others.hands));
            json["standingScore"] = Rounded(AverageOf(others.standingScore, others.lost));
            json["selfDrawPercentOfWins"] = Rounded(PercentOf(others.selfDraws, others.wins));
            json["pointsPerLostHand"] = Rounded(AverageOf(others.lostPoints, others.lost));
            return json;
        }

        Json EndsToJson(const FocusResult& focus) {
            Json json = Json::object();
            json["gaveWinPercent"] = IntervalToJson(MetricInterval(focus, SeedMetric::GaveWinPercent));
            json["standingScore"] = IntervalToJson(MetricInterval(focus, SeedMetric::StandingScore));
            json["selfDrawPercentOfWins"] = Rounded(PercentOf(focus.focus.selfDraws, focus.focus.wins));
            json["pointsPerLostHand"] = Rounded(AverageOf(focus.focus.lostPoints, focus.focus.lost));
            json["others"] = OthersEndsToJson(focus.others);
            return json;
        }

        Json PairedAgainstToJson(const FocusResult& focus, const FocusResult& baseline) {
            Json json = Json::object();
            json["against"] = MakeJsonString(baseline.player.name.c_str());
            json["pointsPerHand"] = PairedToJson(PairedDifference(focus, baseline, SeedMetric::PointsPerHand));
            json["winPercent"] = PairedToJson(PairedDifference(focus, baseline, SeedMetric::WinPercent));
            json["gaveWinPercent"] = PairedToJson(PairedDifference(focus, baseline, SeedMetric::GaveWinPercent));
            json["standingScore"] = PairedToJson(PairedDifference(focus, baseline, SeedMetric::StandingScore));
            return json;
        }

        Json FocusToJson(const FocusResult& focus, const FocusResult& baseline, bool isBaseline) {
            Json json = Json::object();
            json["name"] = MakeJsonString(focus.player.name.c_str());
            json["hands"] = focus.focus.hands;
            json["winPercent"] = IntervalToJson(MetricInterval(focus, SeedMetric::WinPercent));
            json["pointsPerHand"] = IntervalToJson(MetricInterval(focus, SeedMetric::PointsPerHand));
            json["drawnPercent"] = Rounded(PercentOf(focus.focus.drawn, focus.focus.hands));
            json["declaredPercent"] = Rounded(PercentOf(focus.focus.declared, focus.focus.hands));
            json["avgWinScore"] = Rounded(AverageOf(focus.focus.winScore, focus.focus.wins));
            json["others"] = OthersRowToJson(focus.others, static_cast<int64_t>(kSeatCount - 1));
            json["ends"] = EndsToJson(focus);
            json["paired"] = isBaseline ? Json(nullptr) : PairedAgainstToJson(focus, baseline);
            return json;
        }

    }  // namespace

    Json ArenaReportToJson(const ArenaResult& result) {
        Json json = Json::object();
        json["format"] = MakeJsonString(kArenaReportJsonFormat);
        json["version"] = kArenaReportJsonVersion;
        json["first"] = result.first;
        json["seeds"] = result.count;
        json["handsPerFocus"] = result.count * static_cast<int>(kSeatCount);
        json["others"] = MakeJsonString(result.others.name.c_str());
        Json focusList = Json::array();
        for (size_t index = 0; index < result.focus.size(); ++index) {
            focusList.push_back(FocusToJson(result.focus[index], result.focus.front(), index == 0));
        }
        json["focus"] = focusList;
        return json;
    }

    String FormatArenaReportJson(const ArenaResult& result) {
        return DumpJson(ArenaReportToJson(result));
    }

}  // namespace BECore::Mahjong::Arena
