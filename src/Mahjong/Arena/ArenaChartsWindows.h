#pragma once

#include <Mahjong/Analyzer/AnalyzerRun.h>
#include <Mahjong/Arena/ArenaChartsClasses.h>
#include <Mahjong/Arena/ArenaHand.h>

namespace BECore::Mahjong::Arena {

    /// One claim window of a hand as the seat that decides it sees it, and its place among the windows of the hand.
    struct ClaimWindow {
        /// The number of the window among every window of the hand that lists a claim (the excluded ones too), from 0: stable whatever
        /// is excluded, so the analysis seed of a window does not move when the exclusion rule does.
        int index = 0;
        TableView view;
    };

    /// The windows of one hand that the charts measure, and how many were left out.
    struct ClaimWindows {
        /// Hands played: 1 here, summed by the run.
        int hands = 1;
        /// Windows (of any seat) that list at least one claim, a mahjong claim included.
        int claimWindows = 0;
        /// Of those, the ones that list a mahjong claim: a win is taken at once by every level, so there is nothing to chart.
        int excludedForWin = 0;
        /// `claimWindows - excludedForWin`, in the order the hand played them.
        Vector<ClaimWindow> windows;
    };

    /**
     * @brief Plays the hand of `seed` and keeps every claim window the charts measure.
     *
     * A window qualifies when the seat's `TableView` lists `Pass`, at least one chow, pung or kong claim and no mahjong claim. The
     * observer of `PlayArenaHand` reads `MakeTableView(hand, actor)` and nothing else of the model, so the hand is played exactly as
     * without it. A window that lists a claim but not `Pass` is a defect and comes back as an error, never counted away.
     */
    [[nodiscard]] std::expected<ClaimWindows, String> CollectClaimWindows(uint32_t seed, const ArenaPlayers& players);

    /// One claim of a window against `Pass`, in the same worlds.
    struct ClaimRow {
        ClaimClass cls;
        HandAction claim;
        /// The mean points of the claim minus the mean points of `Pass`.
        double gain = 0.0;
        /// The claim is the one Normal's rule takes in this window (`Analyzer::ActionRow::isNormal`); at most one row per window.
        bool normalTakes = false;
    };

    /// What measuring a window gave.
    struct WindowRows {
        /// One per claim of the window, in the order of `view.legal`; empty when the window was dropped.
        Vector<ClaimRow> rows;
        /// No world could be sampled for the position (the only analysis failure that is not an error); `reason` is its text. Counted by the caller.
        bool dropped = false;
        String reason;
    };

    /// The analysis seed of a window: a function of the hand's seed and the window's index alone, never of the machine.
    [[nodiscard]] uint32_t AnalysisSeedOf(uint32_t handSeed, int windowIndex) noexcept;

    /// A `WorldLoop` that plays the indexes on the calling thread, in order.
    void SerialWorldLoop(int count, const std::function<void(int index)>& body);  //-NoStd: the body type of `WorldLoop`

    /**
     * @brief The rows of one window: `Analyzer::AnalyzePosition` over `view.legal`, unchanged, with `worlds` worlds and the seed
     *        `AnalysisSeedOf(handSeed, windowIndex)`.
     *
     * The answer is the same for any `loop`. A `view` that is not a claim window with `Pass` and a charted claim, or `worlds` below 1,
     * is an error of the caller, and so is any other error of `AnalyzePosition`; only a position for which no world can be sampled
     * (`Analyzer::kNoWorldReason`) is a `dropped` window.
     */
    [[nodiscard]] std::expected<WindowRows, String> MeasureWindow(const TableView& view, uint32_t handSeed, int windowIndex, int worlds, const WorldLoop& loop);

}  // namespace BECore::Mahjong::Arena
