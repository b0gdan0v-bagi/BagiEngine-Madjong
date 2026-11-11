#include "AnalyzerRun.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>

namespace BECore::Mahjong::Analyzer {

    namespace {

        /// What the Normal player chooses on `view`: the turn chooser on the own turn, the claim chooser in a window or a robbed kong.
        HandAction NormalChoice(const TableView& view, uint32_t seed) {
            MahjongRng rng(seed);
            return view.phase == Phase::Discard ? ChooseTurnAction(view, rng, kNormalProfile) : ChooseClaimAction(view, rng, kNormalProfile);
        }

        eastl::vector<double> AsDoubles(const eastl::vector<int>& values) {
            eastl::vector<double> result;
            result.reserve(values.size());
            for (const int value : values) {
                result.push_back(static_cast<double>(value));
            }
            return result;
        }

        /// `a - b` world by world; the rows have one length (the same worlds for every action).
        ActionDifference PairedDifference(const eastl::vector<int>& a, const eastl::vector<int>& b) {
            eastl::vector<double> differences;
            differences.reserve(a.size());
            for (size_t world = 0; world < a.size(); ++world) {
                differences.push_back(static_cast<double>(a[world] - b[world]));
            }
            ActionDifference result;
            result.interval = Arena::MeanCi(differences);
            result.significant = Arena::IsSignificant(result.interval);
            return result;
        }

        /// The base seed of the worlds: a fixed function of the seed of the run.
        uint32_t BaseSeedOf(uint32_t seed) {
            MahjongRng mixer(seed + kBaseSeedSalt);
            return mixer.Below(kSearchBaseSeedRange);
        }

    }  // namespace

    std::expected<PositionAnalysis, eastl::string> AnalyzePosition(const TableView& view, const AnalyzerConfig& config, const WorldLoop& loop) {
        if (!IsSeat(view.seat)) {
            return std::unexpected(eastl::string("the position is the view of no seat"));
        }
        if (view.legal.empty()) {
            return std::unexpected(eastl::string("the seat has no legal action in this position"));
        }
        if (config.worlds < 1) {
            return std::unexpected(eastl::string("the number of worlds must be at least 1"));
        }
        std::optional<SearchRun> run = BeginSearchOver(view, view.legal, BaseSeedOf(config.seed), config.worlds);
        if (!run.has_value()) {
            return std::unexpected(eastl::string("the search over the legal actions could not begin"));
        }
        run->RunWorlds(loop);
        const eastl::vector<eastl::vector<int>>& values = run->Values();
        if (values.empty() || values.front().empty()) {
            return std::unexpected(eastl::string("no world could be sampled for this position"));
        }

        PositionAnalysis analysis;
        analysis.config = config;
        analysis.seat = view.seat;
        analysis.phase = view.phase;
        analysis.pendingTile = view.pendingTile;
        analysis.worldsAsked = config.worlds;
        analysis.worldsPlayed = static_cast<int>(values.front().size());
        analysis.worldsDropped = run->DroppedWorlds();
        analysis.normalChoice = NormalChoice(view, config.normalSeed);

        const size_t count = view.legal.size();
        analysis.rows.resize(count);
        size_t best = 0;
        size_t normal = count;
        for (size_t c = 0; c < count; ++c) {
            ActionRow& row = analysis.rows[c];
            row.action = view.legal[c];
            row.ev = Arena::MeanCi(AsDoubles(values[c]));
            if (row.ev.mean > analysis.rows[best].ev.mean) {
                best = c;
            }
            if (normal == count && row.action == analysis.normalChoice) {
                normal = c;
            }
        }
        if (normal == count) {
            return std::unexpected(eastl::string("Normal's choice is not among the legal actions"));
        }
        analysis.rows[best].isBest = true;
        analysis.rows[normal].isNormal = true;
        for (size_t c = 0; c < count; ++c) {
            if (c != best) {
                analysis.rows[c].vsBest = PairedDifference(values[c], values[best]);
            }
            if (c != normal) {
                analysis.rows[c].vsNormal = PairedDifference(values[c], values[normal]);
            }
        }
        return analysis;
    }

}  // namespace BECore::Mahjong::Analyzer
