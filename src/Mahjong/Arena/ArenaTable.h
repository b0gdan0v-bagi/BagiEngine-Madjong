#pragma once

#include <Mahjong/Arena/ArenaRun.h>

namespace BECore::Mahjong::Arena {

    /// The own-draws value reads a handful of draws, so the survival is measured this many rounds ahead.
    inline constexpr int kTableStatsRounds = 4;
    inline constexpr size_t kDeclaredCases = kSeatCount;
    /// The index of `TableStats::winnerScore`: East first.
    inline constexpr size_t kEastCases = 2;
    inline constexpr size_t kWinSourceCount = EnumUtils<WinSource>::Count();

    struct SampleSums {
        int64_t n = 0;
        int64_t sum = 0;
        int64_t sumSquares = 0;

        void Add(int value) noexcept;
    };

    /// For one number of declared opponents and one `i`: the own turns for which the first `i` rounds are known (`observed`), and among
    /// them those in which another seat won within them (`won`).
    struct RoundCount {
        int64_t observed = 0;
        int64_t won = 0;
    };

    /// How often a tile is a winning kind of a declared, waiting seat, against what the unseen copies predict. An exposure is one tile
    /// that a seat could win on (an opponent's discard, or the seat's own draw); the expected hits are the sum over the exposures of
    /// that seat's unseen winning copies / its unseen total, in millionths of a hit, each rounded half up.
    struct HitCount {
        int64_t exposures = 0;
        int64_t expectedMicro = 0;
        int64_t hits = 0;
    };

    /// What `--table-stats` measures over four Normal seats; the source of `kOpponentWinPerRoundPermille` (`docs/own-draws-ev.md`).
    /// Integers only, so the merge of the seeds does not depend on the order or on the threads.
    struct TableStats {
        uint32_t first = 0;
        int count = 0;
        String player;
        int64_t hands = 0;
        int64_t drawn = 0;
        Array<int64_t, kWinSourceCount> wonBySource{};
        /// `[declared opponents][i - 1]`.
        Array<Array<RoundCount, kTableStatsRounds>, kDeclaredCases> rounds{};
        /// By whether the winner was East (index 0) or not (1).
        Array<SampleSums, kEastCases> winnerScore{};
        /// Of each seat that did not win in a won hand, by whether that seat was East (index 0) or not (1).
        Array<SampleSums, kEastCases> standingScore{};
        /// The discards of the other seats, and the seat's own draws (the check: about 1000 per mille by the nature of the wall).
        HitCount discardHits;
        HitCount drawHits;
    };

    /// The hits of one hand, filled by `MakeHitObserver`.
    struct HandHits {
        HitCount discards;
        HitCount draws;
    };

    /// The number of declared opponents of each own turn of a seat, in turn order. An own turn is an action in the `Discard` phase (a
    /// drawn tile, a claimed pung or chow, or the tile after a kong).
    using OwnTurns = Array<Vector<int>, kSeatCount>;

    /// The read-only observer of `PlayArenaHand` that fills `turns` (cleared first); it is outside the timed decision.
    [[nodiscard]] HandObserver MakeOwnTurnObserver(OwnTurns& turns);

    /**
     * @brief The read-only observer that fills `hits` (cleared first); it is outside the timed decision.
     *
     * The state before an action is all it gets, so an exposure is taken from the state before the discard (the seat's view, in which
     * the discarder's concealed tiles are still unseen) and resolved by the discard move that appears in the next call. The last
     * action of a hand has no next call but the one `PlayArenaHand` makes with the hand finished (`actor` is `kNoSeat`), so a discard
     * that ends the hand with no window is counted like any other.
     */
    [[nodiscard]] HandObserver MakeHitObserver(HandHits& hits);

    /**
     * @brief Adds one finished hand to `stats`.
     *
     * A round is the stretch between an own turn of a seat and its next one, in which the three other seats act. For the turn `k` of `n`
     * turns and `i` of 1..`kTableStatsRounds`: the first `i` rounds are known when the turn `k + i` exists, or when another seat won
     * (the win is then in round `n - k + 1`); the turn is then `observed`, and `won` when the win is in a round up to `i`. A hand that
     * ends by a draw or by the seat's own win leaves the rounds that never came out of the count: the chance is that of a round that
     * is played.
     */
    void AddHand(TableStats& stats, const HandRecord& record, const OwnTurns& turns, const HandHits& hits = {});

    /// The seeds `first .. first + count - 1`, one hand each with `player` on all four seats.
    struct TableStatsConfig {
        uint32_t first = kDefaultFirstSeed;
        int count = kDefaultSeedCount;
        ArenaPlayer player;
        int jobs = kDefaultJobs;
    };

    /// The same result at any `jobs` (every seed has its own slot, summed in seed order). The error is that of the lowest seed that
    /// failed, or a bad config.
    [[nodiscard]] std::expected<TableStats, String> RunTableStats(const TableStatsConfig& config);

    /// Fixed width, fixed precision, no locale: the same stats give the same bytes. The survival after `i` rounds is printed next to
    /// `(1 - h)^i`, the value a constant chance `h` would give.
    [[nodiscard]] String FormatTableStats(const TableStats& stats);

}  // namespace BECore::Mahjong::Arena
