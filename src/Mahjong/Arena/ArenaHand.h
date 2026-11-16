#pragma once

#include <EASTL/array.h>
#include <Mahjong/Arena/ArenaPlayer.h>
#include <Mahjong/Game/ComputerSearch.h>
#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/Score.h>

namespace BECore::Mahjong::Arena {

    /// The seeding of the random seats' shared rng, as in `ComputerStrengthTest`; changing either moves every random seat and with it
    /// the reference numbers.
    inline constexpr uint32_t kRandomSeatSeedFactor = 13;
    inline constexpr uint32_t kRandomSeatSeedOffset = 5;

    /// The four players of a hand, by seat.
    using ArenaPlayers = Array<ArenaPlayer, kSeatCount>;

    /// What happened in one hand of the arena.
    struct HandRecord {
        ResultKind kind = ResultKind::Drawn;
        /// `kNoSeat` for a drawn hand.
        Seat winner = kNoSeat;
        /// The rounded score of each seat (`HandSettlement::scores`).
        SeatScores scores{};
        /// The change of each seat's points (`HandSettlement::deltas`; zeros for a drawn hand).
        SeatScores deltas{};
        Seat east = 0;
        /// How the hand was won (meaningful when `winner` is a seat).
        WinSource source = WinSource::SelfDraw;
        /// Who discarded the winning tile (`HandResult::from`): the owner of the kong for a robbed kong, the winner for a self-draw,
        /// `kNoSeat` for a drawn hand.
        Seat from = kNoSeat;
        /// The kinds of the lines of the winner's breakdown, each once, in the order of the breakdown (empty for a drawn hand; a limit
        /// hand is its one line).
        Vector<ScoreKind> winKinds;
        /// Whether the seat declared the calling hand in this hand.
        Array<bool, kSeatCount> declared{};
        /// The actions each seat chose.
        Array<int, kSeatCount> decisions{};
        /// Wall-clock time spent choosing; filled only when timing was asked for, the one field that is not a function of the arguments.
        Array<int64_t, kSeatCount> nanoseconds{};
        /// The slowest single decision of each seat, measured the same way (0 without timing).
        Array<int64_t, kSeatCount> slowestNanoseconds{};
    };

    /// Called before each action with the model as it stands, and once more with the hand finished (`actor` is `kNoSeat`); it reads
    /// only, so the hand and its record are the same with or without one.
    using HandObserver = std::function<void(const HandModel& hand, Seat actor)>;

    /**
     * @brief Plays the hand of `seed` with `players` on the four seats and says what happened.
     *
     * Follows the conventions of `ComputerStrengthTest` (the wall, `east = seed % 4`, East round wind, `ComputerTable::NextActor`, the
     * seat seeds, one shared rng and `ChooseSeekWin` for the random seats), so the old numbers come out: do not "tidy" any of them.
     * Deterministic: the same arguments give the same record (the nanoseconds apart, which are measured only with `timing`).
     *
     * `worlds` plays a search decision's worlds (`SearchRun::RunWorlds`): null on the calling thread, a loop possibly on several. The
     * record is the same either way; the time of a decision is its wall time, its worlds included. The `observer` is outside the
     * timed decision.
     *
     * A refused action, a computer seat with nothing to choose from or a hand that does not end is a defect and comes back as an error
     * text naming the seed and the seat; it is never swallowed.
     */
    [[nodiscard]] std::expected<HandRecord, String> PlayArenaHand(uint32_t seed, const ArenaPlayers& players, bool timing = false, const WorldLoop* worlds = nullptr,
                                                                  const HandObserver* observer = nullptr);

    /// The most actions one hand may take before it counts as stuck.
    inline constexpr int kArenaStepLimit = 2000;

}  // namespace BECore::Mahjong::Arena
