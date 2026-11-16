#pragma once

#include <Mahjong/Arena/ArenaHand.h>

namespace BECore::Mahjong::Arena {

    /// One place in `ArenaTally::winsByKind` per kind of score line.
    inline constexpr size_t kScoreKindCount = EnumUtils<ScoreKind>::Count();

    /// The first seed of `ComputerStrengthTest`, so a default run plays that test's hands.
    inline constexpr uint32_t kDefaultFirstSeed = 4000;
    /// A seed is four hands, one per seat for the focus player.
    inline constexpr int kDefaultSeedCount = 250;
    /// The thread that calls `RunArena`.
    inline constexpr int kDefaultJobs = 1;
    /// The front end maps `--jobs 0` to the hardware concurrency, capped by this.
    inline constexpr int kMaxJobs = 256;

    /// The seeds `first .. first + count - 1`; every focus player plays each seed on each of the four seats against `others`, on the same
    /// walls as every other focus player.
    struct ArenaConfig {
        uint32_t first = kDefaultFirstSeed;
        int count = kDefaultSeedCount;
        Vector<ArenaPlayer> focus;
        ArenaPlayer others;
        /// The decision times are the one result that is not a function of the config.
        bool timing = false;
        /// The threads in all, 1 to `kMaxJobs`: `min(jobs, count)` seed threads, and with fewer seeds than `jobs` each also owns
        /// `jobs / seedThreads` threads for the worlds of a search decision, so the total never exceeds `jobs`. The result does not
        /// depend on it.
        int jobs = kDefaultJobs;
    };

    /// One seed of one focus player, summed over its four rotations. The four hands share a wall, so the seed is the independent unit
    /// of the statistics; a per-hand value is the number divided by `kSeatCount`.
    struct SeedRow {
        uint32_t seed = 0;
        /// `HandSettlement::deltas` of the focus player, summed over the four hands.
        int pointsOfFocus = 0;
        /// The hands (0..4) the focus player won.
        int winsOfFocus = 0;
        /// The hands (0..4) that were drawn.
        int drawn = 0;
        /// The hands in which the focus player declared the calling hand.
        int declared = 0;
        /// The sum of the rounded scores (`HandSettlement::scores`) of the hands the focus player won.
        int winScore = 0;
        /// The hands (0..4) another seat won on the focus player's discard or by robbing its kong ("gave the win").
        int gaveWin = 0;
        /// The hands (0..4) another seat won; a drawn hand is neither won nor lost.
        int lostHands = 0;
        /// The sum of the focus player's rounded scores over its lost hands.
        int standingScore = 0;
    };

    /// Counts over seat-hands: one hand played by a group of seats counts once per seat of the group.
    struct ArenaTally {
        int64_t hands = 0;
        int64_t wins = 0;
        int64_t points = 0;
        int64_t drawn = 0;
        int64_t declared = 0;
        /// The sum of the rounded scores of the won hands.
        int64_t winScore = 0;
        /// The won hands with `WinSource::SelfDraw`.
        int64_t selfDraws = 0;
        /// The hands another seat won; a drawn hand is neither won nor lost.
        int64_t lost = 0;
        /// The lost hands whose winning tile the seat discarded or whose kong it owned (`source` not `SelfDraw`, `from` the seat).
        int64_t gaveWin = 0;
        /// The sum of the seat's `deltas` over its lost hands.
        int64_t lostPoints = 0;
        /// The sum of the seat's rounded scores over its lost hands: the "standing" score of `ArenaTable`.
        int64_t standingScore = 0;
        /// A kind counts once per won hand whose breakdown has it.
        Array<int64_t, kScoreKindCount> winsByKind{};
        /// `nanoseconds` is filled only with `ArenaConfig::timing`.
        int64_t decisions = 0;
        int64_t nanoseconds = 0;
        /// The slowest single decision: merged as a maximum, so any `jobs` gives the maximum of the same measurements.
        int64_t slowestNanoseconds = 0;
    };

    /// The result of one focus player.
    struct FocusResult {
        ArenaPlayer player;
        /// One row per seed, in seed order.
        Vector<SeedRow> rows;
        /// The focus player on its four seats.
        ArenaTally focus;
        /// The three other seats summed over the seats (a drawn hand counts three times in `drawn`): divide by `kSeatCount - 1` for a
        /// seat's average.
        ArenaTally others;
        /// The hands the focus player won while sitting on each seat.
        Array<int64_t, kSeatCount> winsBySeat{};
    };

    struct ArenaResult {
        uint32_t first = 0;
        int count = 0;
        ArenaPlayer others;
        bool timing = false;
        /// In the order of `ArenaConfig::focus`.
        Vector<FocusResult> focus;
    };

    /// Empty when `config` can be played, else the reason. `RunArena` refuses a config with this text; a front end asks first to tell a
    /// bad argument from a refused action.
    [[nodiscard]] String ValidateArenaConfig(const ArenaConfig& config);

    /**
     * @brief Plays the config and returns the totals and the per-seed rows of every focus player.
     *
     * Deterministic (the nanoseconds of `timing` apart): the same config gives the same bytes at any `jobs`, and a focus player's
     * result does not depend on the others in the list. A seed (all its focus players and rotations) is played by one thread into a
     * result slot of its own, and the slots are merged in seed order. The second level of threads (`WorldThreads`, the worlds of a
     * search decision) appears only with fewer seeds than `jobs`; the time of a decision is then lower than the work in it.
     *
     * An invalid config and a refused action of the model are an error text; the run stops, and the error is that of the lowest seed
     * (then the first focus player) that failed, whatever the number of threads.
     */
    [[nodiscard]] std::expected<ArenaResult, String> RunArena(const ArenaConfig& config);

    /// How a hand is played; `PlayArenaHand` for the program. It is called from several threads, so it must not write anything shared.
    /// `worlds` is the loop of the calling seed thread's `WorldThreads` and lives for the call.
    using ArenaHandPlayer = std::function<std::expected<HandRecord, String>(uint32_t seed, const ArenaPlayers& players, bool timing, const WorldLoop& worlds)>;

    /// The seam for a test that makes a hand fail on chosen seeds; the error rules are those of `RunArena(config)`.
    [[nodiscard]] std::expected<ArenaResult, String> RunArena(const ArenaConfig& config, const ArenaHandPlayer& playHand);

}  // namespace BECore::Mahjong::Arena
