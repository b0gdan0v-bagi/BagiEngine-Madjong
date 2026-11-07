#pragma once

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/Score.h>

namespace BECore::Mahjong {

    /// What a finished hand comes to: the score of every seat and the change of points. Plain data, so the match keeps a history of them
    /// and the score table prints the breakdowns.
    struct HandSettlement {
        ResultKind kind = ResultKind::Drawn;
        /// `kNoSeat` for a drawn hand.
        Seat winner = kNoSeat;
        Seat east = 0;
        Wind roundWind = Wind::East;
        /// Every seat's score, also in a drawn hand (a seat that does not win scores the sets and the pair that stand).
        eastl::array<ScoreBreakdown, kSeatCount> breakdowns;
        /// The rounded score of each seat (`breakdowns[seat].score`).
        SeatScores scores{};
        /// What `Settle` makes of `scores`: the change of each seat's points; all zeros for a drawn hand.
        SeatScores deltas{};
    };

    /**
     * @brief The copies of the winning kind `winningTile` that lie open on the table: in the shared pile and in the open melds
     *        (`!concealed`) of all four seats, the winner's too.
     *
     * The winning tile itself is not counted: a win on a discard has taken it out of the pile, a robbed kong has taken it out of the
     * kong (so the owner's pung of three counts three), a self-draw holds it among the concealed tiles. Concealed kongs, concealed
     * tiles and bonus tiles are not visible. This is the one place of the reading "obviously last of four" (`CLAUDE.md`).
     */
    [[nodiscard]] int CountVisibleCopies(const HandModel& model, Tile winningTile) noexcept;

    /**
     * @brief Scores the four seats of a finished hand and settles the payments.
     *
     * Precondition: `model.GetPhase() == Phase::Finished`. Each seat is scored from its own hand with the winds and the limit of the
     * model (`ScoreSeat`); the winner gets `result` and the visible copies of its winning tile. A hand that is not finished is
     * settled like a drawn one: scores for everyone, no payments.
     */
    [[nodiscard]] HandSettlement SettleHand(const HandModel& finished);

}  // namespace BECore::Mahjong
