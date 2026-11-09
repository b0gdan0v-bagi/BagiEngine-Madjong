#pragma once

#include <Mahjong/Arena/ArenaHand.h>

namespace BECore::Mahjong::Arena {

    /// The number of kinds of score line; `ArenaTally::winsByKind` has a place for each.
    inline constexpr size_t kScoreKindCount = EnumUtils<ScoreKind>::Count();

    /// What to play: the seeds `first .. first + count - 1`, and for each player of `focus` and each of the four seats the hand of the
    /// seed with that player on the seat and `others` on the other three. Every focus player plays the same walls.
    struct ArenaConfig {
        uint32_t first = 4000;
        int count = 250;
        eastl::vector<ArenaPlayer> focus;
        ArenaPlayer others;
        /// Measure the time of the decisions (the one number that is not a function of the config).
        bool timing = false;
    };

    /// One seed of one focus player, summed over its four rotations (the focus player on each seat in turn). The four hands share a wall,
    /// so the seed is the independent unit of the statistics; a per-hand value is the number divided by `kSeatCount`.
    struct SeedRow {
        uint32_t seed = 0;
        /// The change of the focus player's points (`HandSettlement::deltas`) over the four hands.
        int pointsOfFocus = 0;
        /// The hands (0..4) the focus player won.
        int winsOfFocus = 0;
        /// The hands (0..4) that were drawn.
        int drawn = 0;
        /// The hands in which the focus player declared the calling hand.
        int declared = 0;
        /// The sum of the focus player's winning scores (the rounded score of the winner, `HandSettlement::scores`).
        int winScore = 0;
    };

    /// Counts over seat-hands: one hand played by a group of seats counts once per seat of the group.
    struct ArenaTally {
        int64_t hands = 0;
        int64_t wins = 0;
        int64_t points = 0;
        int64_t drawn = 0;
        int64_t declared = 0;
        /// The sum of the winning scores of the won hands.
        int64_t winScore = 0;
        /// The won hands whose winner's breakdown has the kind (a kind once per won hand).
        eastl::array<int64_t, kScoreKindCount> winsByKind{};
        /// The actions chosen, and (only with timing) the nanoseconds spent choosing them.
        int64_t decisions = 0;
        int64_t nanoseconds = 0;
    };

    /// The result of one focus player.
    struct FocusResult {
        ArenaPlayer player;
        /// One row per seed, in seed order.
        eastl::vector<SeedRow> rows;
        /// The focus player on its four seats: `hands` is `kSeatCount * count`.
        ArenaTally focus;
        /// The three other seats together, summed over the seats: `hands` is `(kSeatCount - 1) * kSeatCount * count`; a drawn hand counts
        /// three times in `drawn`. Divide by `kSeatCount - 1` for a seat's average.
        ArenaTally others;
        /// The hands the focus player won while sitting on the seat.
        eastl::array<int64_t, kSeatCount> winsBySeat{};
    };

    struct ArenaResult {
        uint32_t first = 0;
        int count = 0;
        ArenaPlayer others;
        bool timing = false;
        /// In the order of `ArenaConfig::focus`.
        eastl::vector<FocusResult> focus;
    };

    /**
     * @brief Plays the config and returns the totals and the per-seed rows of every focus player.
     *
     * Single-threaded and deterministic (the nanoseconds of `timing` apart): the same config gives the same result, and the result of a
     * focus player does not depend on the others in the list, because every hand is `PlayArenaHand(seed, players)` with the seed and the
     * players alone. A config with no focus player, a `count` below 1 or a range that runs past the largest seed is an error text, and
     * so is a refused action of the model (the error of `PlayArenaHand`, with the name of the focus player in front): the run stops.
     */
    [[nodiscard]] std::expected<ArenaResult, eastl::string> RunArena(const ArenaConfig& config);

}  // namespace BECore::Mahjong::Arena
