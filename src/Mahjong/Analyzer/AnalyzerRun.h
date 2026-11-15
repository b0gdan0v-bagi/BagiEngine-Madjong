#pragma once

#include <Mahjong/Arena/ArenaStats.h>
#include <Mahjong/Game/ComputerSearch.h>

namespace BECore::Mahjong::Analyzer {

    /// The half-width falls with the square root of the worlds, so a scan wants fewer (`docs/ev-analyzer.md`, "How many worlds").
    inline constexpr int kDefaultWorlds = 16000;
    inline constexpr uint32_t kDefaultSeed = 1;
    /// A tie of `BestDiscard` is decided by an rng, so its seed is part of the report.
    inline constexpr uint32_t kNormalChoiceSeed = 1;
    /// So a small seed does not start the worlds from the same place a small seed starts anything else.
    inline constexpr uint32_t kBaseSeedSalt = 0x9e3779b9U;

    /// The base seed of the worlds and of their rollouts is a function of `seed` alone, never the machine's randomness.
    struct AnalyzerConfig {
        int worlds = kDefaultWorlds;
        uint32_t seed = kDefaultSeed;
        uint32_t normalSeed = kNormalChoiceSeed;
    };

    /// A per-world difference between two actions; a row has none to itself: `interval` is empty and `significant` false.
    struct ActionDifference {
        Arena::MeanInterval interval;
        bool significant = false;
    };

    struct ActionRow {
        HandAction action;
        /// The seat's points at the end of the hand, Normal play on for all four seats.
        Arena::MeanInterval ev;
        /// Paired by world; empty on the best row. Slightly biased: the best row was picked by the same worlds.
        ActionDifference vsBest;
        /// Paired by world; empty on Normal's row.
        ActionDifference vsNormal;
        /// A tie goes to the earlier row.
        bool isBest = false;
        bool isNormal = false;
    };

    struct PositionAnalysis {
        AnalyzerConfig config;
        /// `pendingTile` is invalid in a turn.
        Seat seat = kNoSeat;
        Phase phase = Phase::Finished;
        Tile pendingTile;
        /// Dropped: the hidden tiles could not be sampled, or the model refused a rollout.
        int worldsAsked = 0;
        int worldsPlayed = 0;
        int worldsDropped = 0;
        /// Always one of the rows.
        HandAction normalChoice;
        /// One per element of `view.legal`, in its order (a discard with and without `declareCalling` are two).
        Vector<ActionRow> rows;
    };

    /**
     * @brief The expected points of every legal action of `view`'s seat at the end of the hand.
     *
     * The position is a `TableView` and nothing else, so the analysis sees only what the seat sees. Worlds and rollouts are those of
     * the search player (`BeginSearchOver`), four Normal players play each hand out. Every action is played in the same worlds with
     * the same seeds, so the differences are paired by world; the unit of every interval is the world. The answer depends on the
     * view and the config alone, the same for any `loop`: values are summed in world order.
     *
     * An error text for a view of no seat or no legal action, or no world that could be played.
     */
    [[nodiscard]] std::expected<PositionAnalysis, String> AnalyzePosition(const TableView& view, const AnalyzerConfig& config, const WorldLoop& loop);

}  // namespace BECore::Mahjong::Analyzer
