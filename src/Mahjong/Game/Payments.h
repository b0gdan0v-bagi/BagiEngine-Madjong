#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /// A number per seat (indexed by seat): the rounded scores of a hand, or the change of each seat's points.
    using SeatScores = Array<int, kSeatCount>;

    /// Every payment between two seats of which one is East is this times larger: East pays and receives double.
    inline constexpr int kEastPaymentFactor = 2;

    /**
     * @brief The change of every seat's points after a hand won by `winner`.
     *
     * `rounded` are the scores of the four seats (`ScoreBreakdown::score`). Every loser pays the winner the winner's score, whatever
     * the loser's own score is (also when it is larger). The losers settle pairwise: the one with the lower score pays the other the
     * difference. A payment involving `east` is multiplied by `kEastPaymentFactor`. The result is zero-sum. A `winner` that is no seat
     * (`kNoSeat`, a drawn hand) gives all zeros.
     *
     * The amounts are `int`: a score is at most `LimitCap(limit)` and a playable match has `limit <= kMaxLimit` (`IsPlayable`), so a
     * seat's change is at most (`kSeatCount` - 1) x `kEastPaymentFactor` x `LimitCap(kMaxLimit)`; a larger limit is not checked here.
     */
    [[nodiscard]] SeatScores Settle(const SeatScores& rounded, Seat winner, Seat east);

}  // namespace BECore::Mahjong
