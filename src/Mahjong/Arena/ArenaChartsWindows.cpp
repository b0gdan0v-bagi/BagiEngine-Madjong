#include "ArenaChartsWindows.h"

namespace BECore::Mahjong::Arena {

    namespace {

        /// A multiplier that spreads the hand seeds before the window index is added (the prime of FNV-1a).
        constexpr uint32_t kHandSeedFactor = 16777619U;

        struct WindowShape {
            bool hasPass = false;
            bool hasCharted = false;
            bool hasMahjong = false;
        };

        WindowShape ShapeOf(const TableView& view) noexcept {
            WindowShape shape;
            for (const HandAction& action : view.legal) {
                shape.hasPass = shape.hasPass || action.kind == ActionKind::Pass;
                shape.hasCharted = shape.hasCharted || IsChartedClaim(action);
                shape.hasMahjong = shape.hasMahjong || (action.kind == ActionKind::Claim && action.claim == ClaimKind::Mahjong);
            }
            return shape;
        }

        const Analyzer::ActionRow* FindPass(const Analyzer::PositionAnalysis& analysis) {
            for (const Analyzer::ActionRow& row : analysis.rows) {
                if (row.action.kind == ActionKind::Pass) {
                    return &row;
                }
            }
            return nullptr;
        }

    }  // namespace

    std::expected<ClaimWindows, String> CollectClaimWindows(uint32_t seed, const ArenaPlayers& players) {
        ClaimWindows collected;
        String problem;
        const HandObserver observer = [&collected, &problem, seed](const HandModel& hand, Seat actor) {
            if (hand.GetPhase() != Phase::Claim) {
                return;
            }
            TableView view = MakeTableView(hand, actor);
            const WindowShape shape = ShapeOf(view);
            if (!shape.hasCharted && !shape.hasMahjong) {
                return;
            }
            const int index = collected.claimWindows++;
            if (!shape.hasPass) {
                if (problem.empty()) {
                    problem = Format("seed {} seat {}: a claim window lists a claim but not Pass", seed, static_cast<int>(actor));
                }
                return;
            }
            if (shape.hasMahjong) {
                ++collected.excludedForWin;
                return;
            }
            collected.windows.push_back({index, eastl::move(view)});
        };
        const std::expected<HandRecord, String> record = PlayArenaHand(seed, players, false, nullptr, &observer);
        if (!record.has_value()) {
            return std::unexpected(record.error());
        }
        if (!problem.empty()) {
            return std::unexpected(problem);
        }
        return collected;
    }

    uint32_t AnalysisSeedOf(uint32_t handSeed, int windowIndex) noexcept {
        return handSeed * kHandSeedFactor + static_cast<uint32_t>(windowIndex);
    }

    void SerialWorldLoop(int count, const std::function<void(int index)>& body) {  //-NoStd: the body type of `WorldLoop`
        for (int index = 0; index < count; ++index) {
            body(index);
        }
    }

    std::expected<WindowRows, String> MeasureWindow(const TableView& view, uint32_t handSeed, int windowIndex, int worlds, const WorldLoop& loop) {
        if (worlds < 1) {
            return std::unexpected(String("the number of worlds must be at least 1"));
        }
        const WindowShape shape = ShapeOf(view);
        if (view.phase != Phase::Claim || !shape.hasPass || !shape.hasCharted || shape.hasMahjong) {
            return std::unexpected(String("the view is not a claim window with Pass, a claim to chart and no mahjong claim"));
        }
        Analyzer::AnalyzerConfig config;
        config.worlds = worlds;
        config.seed = AnalysisSeedOf(handSeed, windowIndex);
        const std::expected<Analyzer::PositionAnalysis, String> analysis = Analyzer::AnalyzePosition(view, config, loop);
        WindowRows measured;
        if (!analysis.has_value()) {
            measured.dropped = true;
            measured.reason = analysis.error();
            return measured;
        }
        const Analyzer::ActionRow* const pass = FindPass(*analysis);
        if (pass == nullptr) {
            return std::unexpected(String("the analysis has no row for Pass"));
        }
        for (const Analyzer::ActionRow& row : analysis->rows) {
            if (IsChartedClaim(row.action)) {
                measured.rows.push_back({ClassifyClaim(view, row.action), row.action, row.ev.mean - pass->ev.mean, row.isNormal});
            }
        }
        return measured;
    }

}  // namespace BECore::Mahjong::Arena
