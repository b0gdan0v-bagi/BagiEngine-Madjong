#include <Mahjong/Arena/ArenaChartsWindows.h>
#include <Mahjong/Arena/ArenaWorlds.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>

/// A few seconds in Debug: whole hands of four Normal seats, and a few dozen worlds per measured window.
namespace BECore::Mahjong::Arena {

    namespace {

        using namespace Tests;

        constexpr uint32_t kFirstSeed = 1;
        constexpr int kSeeds = 40;
        constexpr int kWorlds = 24;
        constexpr int kThreads = 3;
        constexpr uint32_t kHandSeed = 77;
        constexpr int kWindowIndex = 3;
        constexpr Seat kClaimer = 1;

        ArenaPlayers NormalPlayers() {
            const std::expected<ArenaPlayer, String> normal = ParseArenaPlayer("normal");
            EXPECT_TRUE(normal.has_value());
            ArenaPlayers players;
            players.fill(normal.value_or(ArenaPlayer{}));
            return players;
        }

        ClaimWindows Collect(uint32_t seed) {
            const std::expected<ClaimWindows, String> windows = CollectClaimWindows(seed, NormalPlayers());
            EXPECT_TRUE(windows.has_value()) << (windows.has_value() ? "" : windows.error().c_str());
            return windows.value_or(ClaimWindows{});
        }

        String Describe(const ClaimWindows& collected) {
            String text = Format("{} {} {}", collected.hands, collected.claimWindows, collected.excludedForWin);
            for (const ClaimWindow& window : collected.windows) {
                text += Format("\n{} seat {} live {} pending {} |", window.index, static_cast<int>(window.view.seat), window.view.liveCount, ToString(window.view.pendingTile));
                for (const Tile tile : window.view.own.concealed) {
                    text += Format(" {}", ToString(tile));
                }
                text += Format(" | {}", window.view.legal.size());
            }
            return text;
        }

        /// Seat 1 holds a pair of 5p and 4p 6p: East's 5p can be a chow (4p 5p 6p) or a pung, and no kong.
        TableView ChowAndPungWindow() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = "4p 5p 5p 6p 1s 2s 7s 8s 3m 4m 8m 9m Ww";
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = kQuiet3;
            spec.live = "5p Rd Gd Wd";
            spec.loose = "9m 9s";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return MakeTableView(model, kClaimer);
        }

        Analyzer::PositionAnalysis Analyze(const TableView& view, uint32_t handSeed, int windowIndex) {
            Analyzer::AnalyzerConfig config;
            config.worlds = kWorlds;
            config.seed = AnalysisSeedOf(handSeed, windowIndex);
            const std::expected<Analyzer::PositionAnalysis, String> analysis = Analyzer::AnalyzePosition(view, config, SerialWorldLoop);
            EXPECT_TRUE(analysis.has_value()) << (analysis.has_value() ? "" : analysis.error().c_str());
            return analysis.value_or(Analyzer::PositionAnalysis{});
        }

        WindowRows Measure(const TableView& view, const WorldLoop& loop = SerialWorldLoop) {
            const std::expected<WindowRows, String> measured = MeasureWindow(view, kHandSeed, kWindowIndex, kWorlds, loop);
            EXPECT_TRUE(measured.has_value()) << (measured.has_value() ? "" : measured.error().c_str());
            return measured.value_or(WindowRows{});
        }

        bool SameRows(const WindowRows& a, const WindowRows& b) {
            if (a.dropped != b.dropped || a.rows.size() != b.rows.size()) {
                return false;
            }
            for (size_t i = 0; i < a.rows.size(); ++i) {
                if (!(a.rows[i].cls == b.rows[i].cls) || !(a.rows[i].claim == b.rows[i].claim) || a.rows[i].gain != b.rows[i].gain || a.rows[i].normalTakes != b.rows[i].normalTakes) {
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    TEST(ArenaChartsWindowsTest, AHandCollectsTheSameWindowsTwice) {
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + 3; ++seed) {
            EXPECT_EQ(Describe(Collect(seed)), Describe(Collect(seed))) << "seed " << seed;
        }
    }

    TEST(ArenaChartsWindowsTest, EveryCollectedViewIsAClaimWindowWithPassAndNoWin) {
        int windows = 0;
        int excluded = 0;
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + kSeeds; ++seed) {
            const ClaimWindows collected = Collect(seed);
            EXPECT_EQ(collected.hands, 1);
            EXPECT_EQ(collected.claimWindows, collected.excludedForWin + static_cast<int>(collected.windows.size())) << "seed " << seed;
            int previous = -1;
            for (const ClaimWindow& window : collected.windows) {
                EXPECT_GT(window.index, previous);
                previous = window.index;
                EXPECT_EQ(window.view.phase, Phase::Claim);
                EXPECT_TRUE(Has(window.view.legal, HandAction::MakePass()));
                bool charted = false;
                for (const HandAction& action : window.view.legal) {
                    EXPECT_FALSE(action.kind == ActionKind::Claim && action.claim == ClaimKind::Mahjong) << "seed " << seed;
                    charted = charted || IsChartedClaim(action);
                }
                EXPECT_TRUE(charted);
            }
            windows += static_cast<int>(collected.windows.size());
            excluded += collected.excludedForWin;
        }
        EXPECT_GT(windows, 0);
        // A winning discard happens within these hands: such a window is counted and left out.
        EXPECT_GT(excluded, 0);
    }

    TEST(ArenaChartsWindowsTest, AWindowOfAChowAndAPungGivesTwoRowsOfTheAnalyzersDifference) {
        const TableView view = ChowAndPungWindow();
        const WindowRows measured = Measure(view);
        ASSERT_FALSE(measured.dropped);
        ASSERT_EQ(measured.rows.size(), 2U);

        const Analyzer::PositionAnalysis analysis = Analyze(view, kHandSeed, kWindowIndex);
        const auto meanOf = [&analysis](const HandAction& action) {
            for (const Analyzer::ActionRow& row : analysis.rows) {
                if (row.action == action) {
                    return row.ev.mean;
                }
            }
            ADD_FAILURE() << "the analysis has no such row";
            return 0.0;
        };
        const double pass = meanOf(HandAction::MakePass());
        const HandAction chow = HandAction::MakeClaim(ClaimKind::Chow, T("4p"));
        const HandAction pung = HandAction::MakeClaim(ClaimKind::Pung);
        // The rows follow `view.legal`: a row for each claim, none for Pass.
        const auto rowOf = [&measured](const HandAction& claim) {
            for (const ClaimRow& row : measured.rows) {
                if (row.claim == claim) {
                    return row;
                }
            }
            ADD_FAILURE() << "no row for the claim";
            return ClaimRow{};
        };
        EXPECT_EQ(rowOf(chow).gain, meanOf(chow) - pass);
        EXPECT_EQ(rowOf(pung).gain, meanOf(pung) - pass);
        EXPECT_EQ(rowOf(chow).cls, ClassifyClaim(view, chow));
        EXPECT_EQ(rowOf(pung).cls, ClassifyClaim(view, pung));
        EXPECT_EQ(rowOf(chow).cls.kind, ClaimChartKind::Chow);
        EXPECT_EQ(rowOf(pung).cls.kind, ClaimChartKind::Pung);

        int takes = 0;
        for (const ClaimRow& row : measured.rows) {
            takes += row.normalTakes ? 1 : 0;
            for (const Analyzer::ActionRow& analyzed : analysis.rows) {
                if (analyzed.action == row.claim) {
                    EXPECT_EQ(row.normalTakes, analyzed.isNormal);
                }
            }
        }
        EXPECT_LE(takes, 1);
    }

    TEST(ArenaChartsWindowsTest, TheSameWindowGivesTheSameRowsOnAnyLoop) {
        const TableView view = ChowAndPungWindow();
        const WindowRows serial = Measure(view);
        ASSERT_FALSE(serial.rows.empty());
        EXPECT_TRUE(SameRows(serial, Measure(view)));
        WorldThreads threads(kThreads);
        EXPECT_TRUE(SameRows(serial, Measure(view, threads.AsLoop())));
    }

    TEST(ArenaChartsWindowsTest, TheAnalysisSeedIsAFunctionOfTheHandAndTheWindow) {
        EXPECT_EQ(AnalysisSeedOf(kHandSeed, kWindowIndex), AnalysisSeedOf(kHandSeed, kWindowIndex));
        EXPECT_NE(AnalysisSeedOf(kHandSeed, kWindowIndex), AnalysisSeedOf(kHandSeed, kWindowIndex + 1));
        EXPECT_NE(AnalysisSeedOf(kHandSeed, kWindowIndex), AnalysisSeedOf(kHandSeed + 1, kWindowIndex));
        // Two windows of neighbouring hands do not share a seed.
        EXPECT_NE(AnalysisSeedOf(kHandSeed, 1), AnalysisSeedOf(kHandSeed + 1, 0));
    }

    TEST(ArenaChartsWindowsTest, AWindowThatCannotBeSampledIsDroppedAndTold) {
        TableView view = ChowAndPungWindow();
        view.visible.fill(kCopiesPerTile);
        const WindowRows measured = Measure(view);
        EXPECT_TRUE(measured.dropped);
        EXPECT_TRUE(measured.rows.empty());
        EXPECT_FALSE(measured.reason.empty());
    }

    TEST(ArenaChartsWindowsTest, ABadCallIsAnErrorNotADroppedWindow) {
        const TableView view = ChowAndPungWindow();
        EXPECT_FALSE(MeasureWindow(view, kHandSeed, kWindowIndex, 0, SerialWorldLoop).has_value());
        EXPECT_FALSE(MeasureWindow(TableView{}, kHandSeed, kWindowIndex, kWorlds, SerialWorldLoop).has_value());
        TableView turn = view;
        turn.phase = Phase::Discard;
        EXPECT_FALSE(MeasureWindow(turn, kHandSeed, kWindowIndex, kWorlds, SerialWorldLoop).has_value());
        TableView noPass = view;
        noPass.legal.erase(eastl::find(noPass.legal.begin(), noPass.legal.end(), HandAction::MakePass()));
        EXPECT_FALSE(MeasureWindow(noPass, kHandSeed, kWindowIndex, kWorlds, SerialWorldLoop).has_value());
        TableView withWin = view;
        withWin.legal.push_back(HandAction::MakeClaim(ClaimKind::Mahjong));
        EXPECT_FALSE(MeasureWindow(withWin, kHandSeed, kWindowIndex, kWorlds, SerialWorldLoop).has_value());
    }

}  // namespace BECore::Mahjong::Arena
