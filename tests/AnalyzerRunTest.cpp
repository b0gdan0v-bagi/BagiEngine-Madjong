#include "ComputerTestSupport.h"

#include <Mahjong/Analyzer/AnalyzerRun.h>
#include <Mahjong/Arena/ArenaWorlds.h>
#include <Mahjong/Game/ComputerClaim.h>
#include <gtest/gtest.h>

/**
 * The analysis of one position (Analyzer/AnalyzerRun.h): one row per legal action in the order of `view.legal`, the EV of each over
 * the sampled worlds, the paired differences to the best row and to Normal's choice, the same bytes for any loop over the worlds,
 * and nothing seen that the view does not show. The positions are built with `FixedWall` and reduced with `MakeTableView`; the worlds
 * are a few hundred at most so a Debug run stays short. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        using Analyzer::ActionRow;
        using Analyzer::AnalyzerConfig;
        using Analyzer::PositionAnalysis;

        constexpr int kWorlds = 60;
        constexpr int kFewWorlds = 12;
        constexpr uint32_t kSeed = 5;
        constexpr int kThreads = 3;
        constexpr double kMeanTolerance = 1e-9;

        AnalyzerConfig ConfigOf(int worlds = kWorlds, uint32_t seed = kSeed) {
            AnalyzerConfig config;
            config.worlds = worlds;
            config.seed = seed;
            return config;
        }

        void PlainLoop(int count, const std::function<void(int)>& body) {
            for (int i = 0; i < count; ++i) {
                body(i);
            }
        }

        void BackwardsLoop(int count, const std::function<void(int)>& body) {
            for (int i = count - 1; i >= 0; --i) {
                body(i);
            }
        }

        PositionAnalysis Analyze(const TableView& view, const AnalyzerConfig& config = ConfigOf()) {
            const std::expected<PositionAnalysis, eastl::string> analysis = Analyzer::AnalyzePosition(view, config, PlainLoop);
            EXPECT_TRUE(analysis.has_value()) << (analysis.has_value() ? "" : analysis.error().c_str());
            return analysis.has_value() ? *analysis : PositionAnalysis{};
        }

        /// An action as text for a failure message and for `Describe`.
        std::string Label(const HandAction& action) {
            return Name(action.kind) + " " + (action.tile.IsValid() ? std::string(ToString(action.tile)) : std::string("-")) + (action.declareCalling ? " declare" : "") + " " + Name(action.claim) +
                   " " + (action.chowFirst.IsValid() ? std::string(ToString(action.chowFirst)) : std::string("-"));
        }

        std::string Interval(const Arena::MeanInterval& interval) {
            char text[160];
            std::snprintf(text, sizeof text, "n%zu m%.17g sd%.17g h%.17g i%d", interval.n, interval.mean, interval.sd, interval.half, interval.hasInterval ? 1 : 0);
            return text;
        }

        /// Every field of an analysis as text, to compare two analyses exactly.
        std::string Describe(const PositionAnalysis& analysis) {
            std::string text = std::to_string(analysis.seat) + " " + Name(analysis.phase) + " " + std::to_string(analysis.worldsAsked) + "/" + std::to_string(analysis.worldsPlayed) + "/" +
                               std::to_string(analysis.worldsDropped) + " normal " + Label(analysis.normalChoice) + "\n";
            for (const ActionRow& row : analysis.rows) {
                text += Label(row.action) + " | " + Interval(row.ev) + " | " + Interval(row.vsBest.interval) + (row.vsBest.significant ? "*" : "") + " | " + Interval(row.vsNormal.interval) +
                        (row.vsNormal.significant ? "*" : "") + " | " + (row.isBest ? "best" : "") + (row.isNormal ? "normal" : "") + "\n";
            }
            return text;
        }

        const ActionRow* RowOf(const PositionAnalysis& analysis, const HandAction& action) {
            for (const ActionRow& row : analysis.rows) {
                if (row.action == action) {
                    return &row;
                }
            }
            return nullptr;
        }

        const ActionRow& NormalRow(const PositionAnalysis& analysis) {
            return *eastl::find_if(analysis.rows.begin(), analysis.rows.end(), [](const ActionRow& row) { return row.isNormal; });
        }

        // East has declared and draws its winning tile (a pair of red dragons completes the hand).
        HandModel WinModel() {
            HandModel model = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Rd"));
            Lap(model, "9p Wd Gd Nw", {kSeat0});
            return model;
        }

        // East can declare with the 5p or the 6p.
        HandModel TurnModel() {
            return MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew 1p 2p 3p"));
        }

        // Seat 2 has declared and can rob the upgrade of seat 1: the window holds a mahjong claim and a pass.
        HandModel RobbingModel() {
            HandModel model = UpgradeModel({kSeat2});
            EXPECT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::RobKong);
            return model;
        }

    }  // namespace

    TEST(AnalyzerRunTest, ADeclaredWinBeatsEveryDiscardAndTheDifferencesExcludeZero) {
        const TableView view = MakeTableView(WinModel(), kSeat0);
        const HandAction win = HandAction::MakeDeclareWin();
        ASSERT_TRUE(Has(view.legal, win));
        ASSERT_GT(view.legal.size(), 1U);
        const PositionAnalysis analysis = Analyze(view);

        const ActionRow* winRow = RowOf(analysis, win);
        ASSERT_NE(winRow, nullptr);
        EXPECT_TRUE(winRow->isBest);
        EXPECT_GT(winRow->ev.mean, 0.0);
        EXPECT_EQ(winRow->vsBest.interval.n, 0U);
        EXPECT_FALSE(winRow->vsBest.significant);
        for (const ActionRow& row : analysis.rows) {
            if (row.action == win) {
                continue;
            }
            EXPECT_FALSE(row.isBest) << Label(row.action);
            EXPECT_LT(row.ev.mean, winRow->ev.mean) << Label(row.action);
            EXPECT_TRUE(row.vsBest.significant) << Label(row.action);
            EXPECT_LT(row.vsBest.interval.mean + row.vsBest.interval.half, 0.0) << Label(row.action);
            EXPECT_EQ(row.vsBest.interval.n, static_cast<size_t>(analysis.worldsPlayed));
        }
    }

    TEST(AnalyzerRunTest, AMahjongClaimBeatsPassWithAnIntervalThatExcludesZero) {
        const TableView view = MakeTableView(RobbingModel(), kSeat2);
        const PositionAnalysis analysis = Analyze(view);
        ASSERT_NE(FindMahjong(view), nullptr);
        const ActionRow* claim = RowOf(analysis, *FindMahjong(view));
        const ActionRow* pass = RowOf(analysis, HandAction::MakePass());
        ASSERT_NE(claim, nullptr);
        ASSERT_NE(pass, nullptr);
        EXPECT_TRUE(claim->isBest);
        EXPECT_GT(claim->ev.mean, pass->ev.mean);
        EXPECT_TRUE(pass->vsBest.significant);
        EXPECT_LT(pass->vsBest.interval.mean + pass->vsBest.interval.half, 0.0);
        // Normal claims the mahjong: the pass is below Normal's choice by the same pairing.
        EXPECT_EQ(analysis.normalChoice, *FindMahjong(view));
        EXPECT_TRUE(claim->isNormal);
        EXPECT_TRUE(pass->vsNormal.significant);
    }

    TEST(AnalyzerRunTest, TheAnswerIsTheSameForRepeatedCallsAndForAnyLoopOverTheWorlds) {
        const TableView turn = MakeTableView(TurnModel(), kSeat0);
        const TableView window = MakeTableView(RobbingModel(), kSeat2);
        for (const TableView* view : {&turn, &window}) {
            const AnalyzerConfig config = ConfigOf(kFewWorlds);
            const std::string reference = Describe(Analyze(*view, config));
            EXPECT_EQ(Describe(Analyze(*view, config)), reference);

            const auto backwards = Analyzer::AnalyzePosition(*view, config, BackwardsLoop);
            ASSERT_TRUE(backwards.has_value());
            EXPECT_EQ(Describe(*backwards), reference);

            Arena::WorldThreads pool(kThreads);
            const auto threaded = Analyzer::AnalyzePosition(*view, config, pool.AsLoop());
            ASSERT_TRUE(threaded.has_value());
            EXPECT_EQ(Describe(*threaded), reference);

            // Another seed samples other worlds.
            EXPECT_NE(Describe(Analyze(*view, ConfigOf(kFewWorlds, kSeed + 1))), reference);
        }
    }

    TEST(AnalyzerRunTest, TwoTablesThatDifferOnlyInHiddenTilesGiveEqualViewsAndEqualAnalyses) {
        const HandModel model = TurnModel();
        HandState state = model.Capture();
        // Swap a concealed tile of seat 1 with a live-wall tile of another kind: what East sees is the same.
        bool swapped = false;
        for (size_t h = 0; h < state.hands[kSeat1].concealed.size() && !swapped; ++h) {
            for (size_t l = 0; l < state.live.size() && !swapped; ++l) {
                if (state.hands[kSeat1].concealed[h] == state.live[l]) {
                    continue;
                }
                const Tile hidden = state.hands[kSeat1].concealed[h];
                ASSERT_TRUE(state.hands[kSeat1].RemoveConcealed(hidden));
                state.hands[kSeat1].AddConcealed(state.live[l]);
                state.live[l] = hidden;
                swapped = true;
            }
        }
        ASSERT_TRUE(swapped);
        const std::expected<HandModel, eastl::string> other = HandModel::Restore(state);
        ASSERT_TRUE(other.has_value()) << other.error().c_str();
        ASSERT_NE(Snapshot(model), Snapshot(*other));

        const TableView a = MakeTableView(model, kSeat0);
        const TableView b = MakeTableView(*other, kSeat0);
        EXPECT_EQ(a.own.concealed, b.own.concealed);
        EXPECT_EQ(a.visible, b.visible);
        EXPECT_EQ(a.liveCount, b.liveCount);
        EXPECT_EQ(a.legal, b.legal);
        EXPECT_EQ(Describe(Analyze(a, ConfigOf(kFewWorlds))), Describe(Analyze(b, ConfigOf(kFewWorlds))));
    }

    TEST(AnalyzerRunTest, TheRowsAreExactlyTheLegalActionsAndTheBestAndNormalRowsHaveNoDifferenceToThemselves) {
        for (const TableView& view : {MakeTableView(TurnModel(), kSeat0), MakeTableView(WinModel(), kSeat0), MakeTableView(RobbingModel(), kSeat2)}) {
            const PositionAnalysis analysis = Analyze(view, ConfigOf(kFewWorlds));
            ASSERT_EQ(analysis.rows.size(), view.legal.size());
            EXPECT_EQ(analysis.seat, view.seat);
            EXPECT_EQ(analysis.phase, view.phase);
            EXPECT_EQ(analysis.pendingTile, view.pendingTile);
            EXPECT_EQ(analysis.worldsAsked, kFewWorlds);
            EXPECT_EQ(analysis.worldsPlayed + analysis.worldsDropped, kFewWorlds);
            EXPECT_GT(analysis.worldsPlayed, 0);
            EXPECT_TRUE(Has(view.legal, analysis.normalChoice));

            int best = 0;
            int normal = 0;
            size_t bestIndex = 0;
            for (size_t c = 0; c < analysis.rows.size(); ++c) {
                bestIndex = analysis.rows[c].isBest ? c : bestIndex;
            }
            for (size_t c = 0; c < analysis.rows.size(); ++c) {
                const ActionRow& row = analysis.rows[c];
                EXPECT_EQ(row.action, view.legal[c]);
                EXPECT_EQ(row.ev.n, static_cast<size_t>(analysis.worldsPlayed));
                best += row.isBest ? 1 : 0;
                normal += row.isNormal ? 1 : 0;
                if (row.isNormal) {
                    EXPECT_EQ(row.action, analysis.normalChoice);
                    EXPECT_EQ(row.vsNormal.interval.n, 0U);
                    EXPECT_FALSE(row.vsNormal.significant);
                } else {
                    EXPECT_EQ(row.vsNormal.interval.n, static_cast<size_t>(analysis.worldsPlayed));
                }
                if (row.isBest) {
                    EXPECT_EQ(row.vsBest.interval.n, 0U);
                    EXPECT_FALSE(row.vsBest.significant);
                } else {
                    EXPECT_EQ(row.vsBest.interval.n, static_cast<size_t>(analysis.worldsPlayed));
                    // The difference to the best is the difference of the means, whatever the pairing.
                    EXPECT_NEAR(row.vsBest.interval.mean, row.ev.mean - analysis.rows[bestIndex].ev.mean, kMeanTolerance) << c;
                    EXPECT_NEAR(row.vsNormal.interval.mean, row.ev.mean - NormalRow(analysis).ev.mean, kMeanTolerance) << c;
                }
            }
            EXPECT_EQ(best, 1);
            EXPECT_EQ(normal, 1);
            // The best row has the highest mean, the earliest on a tie.
            for (size_t c = 0; c < analysis.rows.size(); ++c) {
                if (c < bestIndex) {
                    EXPECT_LT(analysis.rows[c].ev.mean, analysis.rows[bestIndex].ev.mean);
                } else {
                    EXPECT_LE(analysis.rows[c].ev.mean, analysis.rows[bestIndex].ev.mean);
                }
            }
        }
    }

    TEST(AnalyzerRunTest, AViewOfNoSeatNoLegalActionNoWorldOrNoSampledWorldIsAnErrorText) {
        const auto failure = [](const TableView& view, const AnalyzerConfig& config) {
            const auto analysis = Analyzer::AnalyzePosition(view, config, PlainLoop);
            EXPECT_FALSE(analysis.has_value());
            return analysis.has_value() ? std::string() : std::string(analysis.error().c_str());
        };
        const TableView view = MakeTableView(TurnModel(), kSeat0);

        EXPECT_FALSE(failure(TableView{}, ConfigOf(kFewWorlds)).empty());
        EXPECT_FALSE(failure(MakeTableView(RobbingModel(), kSeat3), ConfigOf(kFewWorlds)).empty());
        EXPECT_FALSE(failure(view, ConfigOf(0)).empty());
        EXPECT_FALSE(failure(view, ConfigOf(-4)).empty());

        TableView unsampled = view;
        unsampled.liveCount += 5;
        EXPECT_NE(failure(unsampled, ConfigOf(kFewWorlds)).find("sampled"), std::string::npos);
    }

}  // namespace BECore::Mahjong::Tests
