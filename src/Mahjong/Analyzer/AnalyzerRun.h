#pragma once

#include <Mahjong/Arena/ArenaStats.h>
#include <Mahjong/Game/ComputerSearch.h>

namespace BECore::Mahjong::Analyzer {

    /// The worlds of a run that names none. A placeholder until the measurement of the docs sets it (`docs/ev-analyzer.md`).
    inline constexpr int kDefaultWorlds = 1000;
    /// The seed of a run that names none.
    inline constexpr uint32_t kDefaultSeed = 1;
    /// The seed of the rng that breaks the ties of Normal's choice on the position (a tie of `BestDiscard` is decided by an rng, so
    /// the seed is part of the report).
    inline constexpr uint32_t kNormalChoiceSeed = 1;
    /// Added to the seed of a run before it is turned into the base seed of the worlds, so a small seed does not start the worlds of
    /// a search from the same place a small seed starts anything else.
    inline constexpr uint32_t kBaseSeedSalt = 0x9e3779b9U;

    /// What to play: `worlds` sampled worlds with the seed `seed` (the base seed of the worlds and of their rollouts is a function of
    /// it alone, never the machine's randomness), and Normal's choice with an rng of `normalSeed`.
    struct AnalyzerConfig {
        int worlds = kDefaultWorlds;
        uint32_t seed = kDefaultSeed;
        uint32_t normalSeed = kNormalChoiceSeed;
    };

    /// The mean and the 95 % interval of a per-world difference between two actions, and whether the interval excludes 0. A row has
    /// no difference to itself: `interval` is then empty (`n` 0, mean 0) and `significant` false.
    struct ActionDifference {
        Arena::MeanInterval interval;
        bool significant = false;
    };

    /// One legal action of the position with its value.
    struct ActionRow {
        HandAction action;
        /// The points of the seat at the end of the hand after `action` and Normal play on for all four seats, over the played worlds.
        Arena::MeanInterval ev;
        /// This row minus the best row, paired by world. Empty on the best row. Has the small bias of a best that was picked by the
        /// same worlds.
        ActionDifference vsBest;
        /// This row minus Normal's choice, paired by world. Empty on Normal's row.
        ActionDifference vsNormal;
        /// The highest mean; a tie goes to the earlier row. Exactly one row.
        bool isBest = false;
        /// The row of Normal's choice on the position. Exactly one row.
        bool isNormal = false;
    };

    /// The EV of every legal action of one position of one seat.
    struct PositionAnalysis {
        AnalyzerConfig config;
        /// Who the position is for and what it is: the seat, the phase, and the discard a window is about (invalid in a turn).
        Seat seat = kNoSeat;
        Phase phase = Phase::Finished;
        Tile pendingTile;
        /// The worlds asked, played through and dropped (the hidden tiles could not be sampled, or the model refused a rollout).
        int worldsAsked = 0;
        int worldsPlayed = 0;
        int worldsDropped = 0;
        /// What the Normal player does on the view with `config.normalSeed`; always one of the rows.
        HandAction normalChoice;
        /// One row per element of `view.legal`, in its order (a discard with and without `declareCalling` are two rows).
        eastl::vector<ActionRow> rows;
    };

    /**
     * @brief The expected points of every legal action of `view`'s seat at the end of the hand, with intervals and paired differences.
     *
     * The position is a `TableView` and nothing else, so the analysis sees only what the seat sees. `view.legal` is the list of
     * candidates as it is. The worlds, their rollout seeds and the rollouts are those of the search player (`BeginSearchOver`): each
     * world is a sample of the hidden tiles, every action is applied in it and four Normal players (`kNormalProfile`) play the hand
     * out, the seat's own later plays included; the value is the seat's points (`SettleHand`, a drawn hand pays 0). Every action is
     * played in the same worlds with the same rollout seeds (common random numbers), so the differences are paired by world.
     *
     * The unit of every interval is the world: `Arena::MeanCi` of the per-world values or differences. The answer is a function of the
     * view and the config alone and the same for any `loop` (a plain loop, `WorldThreads::AsLoop()`, any order): the values are
     * summed in world order.
     *
     * An error text for a view of no seat, a view with no legal action, fewer than one world, or no world that could be played.
     */
    [[nodiscard]] std::expected<PositionAnalysis, eastl::string> AnalyzePosition(const TableView& view, const AnalyzerConfig& config, const WorldLoop& loop);

}  // namespace BECore::Mahjong::Analyzer
