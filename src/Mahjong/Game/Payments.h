#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /// A number per seat (indexed by seat): the rounded scores of a hand, or the change of each seat's points.
    using SeatScores = eastl::array<int, kSeatCount>;

    /// Every payment between two seats of which one is East is this times larger: East pays and receives double.
    inline constexpr int kEastPaymentFactor = 2;

    /**
     * @brief The change of every seat's points after a hand won by `winner`.
     *
     * `rounded` are the rounded scores of the four seats (`ScoreBreakdown::score`, multiples of 10, at most the limit). Every loser
     * pays the winner the winner's score, whatever the loser's own score is (also when it is larger). The losers settle among
     * themselves pairwise: of two losers the one with the lower score pays the other the difference (equal scores: nothing). A
     * payment between two seats of which one is `east` is multiplied by `kEastPaymentFactor`; a payment between two others is not.
     * The result is zero-sum. `winner` that is no seat (`kNoSeat`, a drawn hand) gives all zeros.
     *
     * The amounts are `int`: with a limit that is a sane number of points (`LimitCap`) they do not overflow.
     */
    [[nodiscard]] SeatScores Settle(const SeatScores& rounded, Seat winner, Seat east);

}  // namespace BECore::Mahjong
