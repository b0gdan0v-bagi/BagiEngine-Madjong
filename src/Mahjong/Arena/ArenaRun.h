#pragma once

#include <Mahjong/Arena/ArenaHand.h>

namespace BECore::Mahjong::Arena {

    /// The number of kinds of score line; `ArenaTally::winsByKind` has a place for each.
    inline constexpr size_t kScoreKindCount = EnumUtils<ScoreKind>::Count();

    /// The first seed of a run that names none: the first seed of `ComputerStrengthTest`, so the default run is that test's hands.
    inline constexpr uint32_t kDefaultFirstSeed = 4000;
    /// The number of seeds of a run that names none (a seed is four hands, one per seat for the focus player).
    inline constexpr int kDefaultSeedCount = 250;
    /// The number of threads of a run that names none: one, the thread that calls `RunArena`.
    inline constexpr int kDefaultJobs = 1;
    /// The most threads a run may ask for (the front end maps 0 to the hardware concurrency, capped by this).
    inline constexpr int kMaxJobs = 256;

    /// What to play: the seeds `first .. first + count - 1`, and for each player of `focus` and each of the four seats the hand of the
    /// seed with that player on the seat and `others` on the other three. Every focus player plays the same walls.
    struct ArenaConfig {
        uint32_t first = kDefaultFirstSeed;
        int count = kDefaultSeedCount;
        eastl::vector<ArenaPlayer> focus;
        ArenaPlayer others;
        /// Measure the time of the decisions (the one number that is not a function of the config).
        bool timing = false;
        /// The threads in all, 1 to `kMaxJobs`. The seeds come first: `min(jobs, count)` threads each play a seed whole into a slot of its
        /// own, and when there are fewer seeds than `jobs` each of them also owns `jobs / seedThreads` threads (the integer division) that
        /// play the worlds of a search decision, so the total never exceeds `jobs`. The result does not depend on it: the slots are merged
        /// in seed order and a search decision's answer does not depend on the order or the threads of its worlds.
        int jobs = kDefaultJobs;
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
        /// The hands (0..4) another seat won on the focus player's discard or by robbing its kong ("gave the win").
        int gaveWin = 0;
        /// The hands (0..4) another seat won ("lost" hands; a drawn hand is neither won nor lost).
        int lostHands = 0;
        /// The sum of the focus player's rounded scores (`HandSettlement::scores`) over its lost hands.
        int standingScore = 0;
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
        /// The won hands the seat won by drawing the tile itself (`WinSource::SelfDraw`).
        int64_t selfDraws = 0;
        /// The hands another seat won ("lost" by the seat; a drawn hand is neither won nor lost).
        int64_t lost = 0;
        /// The lost hands whose winning tile the seat discarded or whose kong it owned (`source` not `SelfDraw`, `from` the seat).
        int64_t gaveWin = 0;
        /// The sum of the seat's change of points (`deltas`) over its lost hands.
        int64_t lostPoints = 0;
        /// The sum of the seat's rounded scores (`scores`) over its lost hands: the "standing" score of `ArenaTable`.
        int64_t standingScore = 0;
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

    /// Whether `config` can be played: empty when it can, else the text that says why not (no focus player, fewer than one seed, a range
    /// that runs past the largest seed, `jobs` outside 1 to `kMaxJobs`). `RunArena` refuses a config with this text; a front end asks first to tell a bad argument from
    /// a refused action.
    [[nodiscard]] eastl::string ValidateArenaConfig(const ArenaConfig& config);

    /**
     * @brief Plays the config and returns the totals and the per-seed rows of every focus player.
     *
     * Deterministic (the nanoseconds of `timing` apart): the same config gives the same result, whatever `jobs` is, and the result of a
     * focus player does not depend on the others in the list, because every hand is `PlayArenaHand(seed, players)` with the seed and the
     * players alone. With `jobs` above 1 the seeds are taken in order by plain `std::thread`s; a seed (all its focus players and
     * rotations) is played by one thread, writes only its own result slot, and the slots are merged in seed order, so the totals, the
     * rows and the report are the bytes of `jobs` 1 (the timing sums the threads' nanoseconds). A seed thread owns a `WorldThreads` of
     * `jobs / min(jobs, count)` threads (at least 1) for the worlds of its search decisions: the second level appears only when there
     * are fewer seeds than `jobs`, and the time of a decision is then lower than the work in it. A config with no focus player, a `count`
     * below 1, a range that runs past the largest seed or a `jobs` out of range is an error text, and so is a refused action of the
     * model (the error of `PlayArenaHand`, with the name of the focus player in front): the run stops, and the error is that of the
     * lowest seed (then the first focus player in the list) that failed, whatever the number of threads.
     */
    [[nodiscard]] std::expected<ArenaResult, eastl::string> RunArena(const ArenaConfig& config);

    /// How a hand is played: `PlayArenaHand` for the program. `RunArena` calls it from several threads when `jobs` is above 1, so it must
    /// not write anything shared. `worlds` is the loop of the calling seed thread's `WorldThreads`, for the search decisions of the hand
    /// (`PlayArenaHand`'s fourth parameter); it lives for the call.
    using ArenaHandPlayer = std::function<std::expected<HandRecord, eastl::string>(uint32_t seed, const ArenaPlayers& players, bool timing, const WorldLoop& worlds)>;

    /// `RunArena` with another way to play a hand (a seam for the tests: a hand that fails on chosen seeds). The error rules are the same.
    [[nodiscard]] std::expected<ArenaResult, eastl::string> RunArena(const ArenaConfig& config, const ArenaHandPlayer& playHand);

}  // namespace BECore::Mahjong::Arena
