#pragma once

#include <EASTL/array.h>
#include <Mahjong/Arena/ArenaPlayer.h>
#include <Mahjong/Game/ComputerSearch.h>
#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/Score.h>

namespace BECore::Mahjong::Arena {

    /// The seed of the random seats' shared rng is `seed * kRandomSeatSeedFactor + kRandomSeatSeedOffset`: the seeding of
    /// `ComputerStrengthTest`, kept so that the arena plays its hands and reaches its numbers. Changing either moves every random seat.
    inline constexpr uint32_t kRandomSeatSeedFactor = 13;
    inline constexpr uint32_t kRandomSeatSeedOffset = 5;

    /// The four players of a hand, by seat.
    using ArenaPlayers = eastl::array<ArenaPlayer, kSeatCount>;

    /// What happened in one hand of the arena. Plain data.
    struct HandRecord {
        ResultKind kind = ResultKind::Drawn;
        /// `kNoSeat` for a drawn hand.
        Seat winner = kNoSeat;
        /// The rounded score of each seat (`HandSettlement::scores`).
        SeatScores scores{};
        /// The change of each seat's points (`HandSettlement::deltas`; zeros for a drawn hand).
        SeatScores deltas{};
        /// The kinds of the lines of the winner's breakdown, each once, in the order of the breakdown (empty for a drawn hand; a limit
        /// hand is its one line).
        eastl::vector<ScoreKind> winKinds;
        /// Whether the seat declared the calling hand in this hand.
        eastl::array<bool, kSeatCount> declared{};
        /// The actions each seat chose (a seat decides once per action it takes).
        eastl::array<int, kSeatCount> decisions{};
        /// The wall-clock time each seat spent choosing, in nanoseconds; filled only when timing was asked for (it is the one number
        /// that is not a function of the arguments).
        eastl::array<int64_t, kSeatCount> nanoseconds{};
    };

    /**
     * @brief Plays the hand of `seed` with `players` on the four seats and says what happened.
     *
     * The conventions of `ComputerStrengthTest`, so the old numbers come out: the wall is `MakeWall(MahjongRng(seed), MahjongSettings{},
     * east)` with `east = seed % 4`, the round wind is East, the next actor is `ComputerTable::NextActor` (the first pending seat of a
     * window); a computer seat `i` is seeded `SeatSeed(seed, i)` and plays by its profile exactly as `ComputerPlayer` does; the random
     * seats share one `MahjongRng(seed * kRandomSeatSeedFactor + kRandomSeatSeedOffset)` and choose by `ChooseSeekWin`; the results are `SettleHand` of the finished hand.
     * Deterministic: the same arguments give the same record (the nanoseconds apart, which are measured only with `timing`).
     *
     * `worlds` is how a search decision plays its worlds (`SearchRun::RunWorlds`): null plays them at once on the calling thread, a loop
     * (`WorldThreads::AsLoop`) may play them on several. The record is the same either way; the time of a decision (`timing`) is its
     * wall time, its worlds included.
     *
     * A refused action, a computer seat with nothing to choose from or a hand that does not end is a defect and comes back as an error
     * text naming the seed and the seat; it is never swallowed.
     */
    [[nodiscard]] std::expected<HandRecord, eastl::string> PlayArenaHand(uint32_t seed, const ArenaPlayers& players, bool timing = false, const WorldLoop* worlds = nullptr);

    /// The most actions one hand may take before it counts as stuck (a hand is at most about 4 x 70).
    inline constexpr int kArenaStepLimit = 2000;

}  // namespace BECore::Mahjong::Arena
