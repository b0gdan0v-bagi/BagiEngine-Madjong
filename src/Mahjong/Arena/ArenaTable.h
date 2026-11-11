#pragma once

#include <Mahjong/Arena/ArenaRun.h>

namespace BECore::Mahjong::Arena {

    /// How many rounds ahead the survival is measured (`1 - h` to the power of each of 1..this); the own-draws value reads a handful of draws.
    inline constexpr int kTableStatsRounds = 4;
    /// The values of the number of declared opponents: 0 .. `kSeatCount - 1`.
    inline constexpr size_t kDeclaredCases = kSeatCount;
    /// The seats that are East or not (the index of `TableStats::winnerScore`): East first.
    inline constexpr size_t kEastCases = 2;
    inline constexpr size_t kWinSourceCount = EnumUtils<WinSource>::Count();

    /// A count of samples with the sums that give a mean and a standard deviation.
    struct SampleSums {
        int64_t n = 0;
        int64_t sum = 0;
        int64_t sumSquares = 0;

        void Add(int value) noexcept;
    };

    /// For one number of declared opponents and one `i` of 1..`kTableStatsRounds`: the own turns for which the first `i` rounds are known
    /// (`observed`), and among them those in which another seat won within them (`won`).
    struct RoundCount {
        int64_t observed = 0;
        int64_t won = 0;
    };

    /// What `--table-stats` measures over four Normal seats. Plain data, integers only, so the merge of the seeds does not depend on the
    /// order or on the threads.
    struct TableStats {
        uint32_t first = 0;
        int count = 0;
        eastl::string player;
        int64_t hands = 0;
        int64_t drawn = 0;
        /// The won hands by how they were won, indexed by `WinSource`.
        eastl::array<int64_t, kWinSourceCount> wonBySource{};
        /// `[declared opponents][i - 1]`.
        eastl::array<eastl::array<RoundCount, kTableStatsRounds>, kDeclaredCases> rounds{};
        /// The score of the winner of a won hand, by whether the winner was East (index 0) or not (1).
        eastl::array<SampleSums, kEastCases> winnerScore{};
        /// The standing score of each seat that did not win in a won hand, by whether that seat was East (index 0) or not (1).
        eastl::array<SampleSums, kEastCases> standingScore{};
    };

    /// The number of declared opponents of each own turn of a seat, in the order of its turns: what the observer of a hand collects. An
    /// own turn is an action in the `Discard` phase (a drawn tile, a claimed pung or chow, or the tile after a kong).
    using OwnTurns = eastl::array<eastl::vector<int>, kSeatCount>;

    /// The observer that fills `turns` (cleared first) as `PlayArenaHand` calls it: at each action of the `Discard` phase the actor's turn
    /// is appended with the number of the other three seats that have declared the calling hand.
    [[nodiscard]] HandObserver MakeOwnTurnObserver(OwnTurns& turns);

    /**
     * @brief Adds one finished hand to `stats`.
     *
     * A round is the stretch between an own turn of a seat and its next one, in which the three other seats act. For the turn `k` of `n`
     * turns of a seat and `i` of 1..`kTableStatsRounds`: the first `i` rounds are known when the turn `k + i` exists, or when another seat
     * won (the win is then in round `n - k + 1`, because the hand ends after the seat's last turn); the turn is then `observed`, and `won`
     * when the win is in a round up to `i`. A hand that ends by a draw or by the seat's own win leaves the rounds that never came out of
     * the count: the chance is the one of a round that is played.
     */
    void AddHand(TableStats& stats, const HandRecord& record, const OwnTurns& turns);

    /// What `RunTableStats` plays: the seeds `first .. first + count - 1`, one hand each with `player` on all four seats.
    struct TableStatsConfig {
        uint32_t first = kDefaultFirstSeed;
        int count = kDefaultSeedCount;
        ArenaPlayer player;
        int jobs = kDefaultJobs;
    };

    /// Plays the hands and sums them: the same result at any `jobs` (the tallies are integers and every seed has its own slot, summed in
    /// seed order). The error is that of the lowest seed that failed (a refused action of the model), or a bad config.
    [[nodiscard]] std::expected<TableStats, eastl::string> RunTableStats(const TableStatsConfig& config);

    /// The report in fixed text (the numbers by `Format`, a fixed precision, no locale): the same stats give the same bytes. It lists, by the
    /// number of declared opponents, the own turns, the chance that another seat wins in the next round (per mille) and the survival after
    /// 1..`kTableStatsRounds` rounds next to `(1 - h)^i`; the mean score of a winner and the mean standing score of the seats that did not win,
    /// East and not East; the share of the won hands by how they were won.
    [[nodiscard]] eastl::string FormatTableStats(const TableStats& stats);

}  // namespace BECore::Mahjong::Arena
