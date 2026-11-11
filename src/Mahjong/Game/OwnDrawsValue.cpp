#include "OwnDrawsValue.h"

#include <EASTL/algorithm.h>

namespace BECore::Mahjong {

    namespace {

        /// The other losers a standing hand settles with when one seat wins.
        constexpr int64_t kOtherLosers = kSeatCount - 2;
        /// The survival of one round in `kOwnDrawsProbBase` per thousandth of the win chance.
        constexpr int64_t kProbPerPermille = kOwnDrawsProbBase / kPermilleBase;

        /// `numerator / denominator` rounded half away from zero (`RoundedDiv` of the magnitude); `denominator` is positive.
        int64_t RoundedDivSigned(int64_t numerator, int64_t denominator) noexcept {
            return numerator < 0 ? -RoundedDiv(-numerator, denominator) : RoundedDiv(numerator, denominator);
        }

    }  // namespace

    int64_t RoundedDiv(int64_t numerator, int64_t denominator) noexcept {
        return (numerator + denominator / 2) / denominator;
    }

    int64_t WinPayoff(int score, bool seatIsEast) noexcept {
        // East: all kSeatCount - 1 payments are doubled. Another seat: East's payment is doubled, the other kSeatCount - 2 are not.
        const int64_t payments = seatIsEast ? (kSeatCount - 1) * kEastPaymentFactor : kEastPaymentFactor + kOtherLosers;
        return payments * score * kMilliPerPoint;
    }

    int64_t NotWinningValue(int standingScore, bool seatIsEast, const TypicalScores& typical) noexcept {
        const int64_t points = kOtherLosers * (static_cast<int64_t>(standingScore) - typical.loser) - typical.winner;
        // The factor of every payment as numerator / denominator: East has `kEastPaymentFactor` on all of them. Another seat has it on
        // the payment with East only, which is the winner with chance 1/(kSeatCount - 1) and one of the losers otherwise, so each of
        // its payments has the expected factor (kEastPaymentFactor + kSeatCount - 2) / (kSeatCount - 1).
        const int64_t numerator = seatIsEast ? kEastPaymentFactor : kEastPaymentFactor + kOtherLosers;
        const int64_t denominator = seatIsEast ? 1 : kSeatCount - 1;
        return RoundedDivSigned(numerator * points * kMilliPerPoint, denominator);
    }

    int64_t NotWinningValue(int standingScore, bool seatIsEast) noexcept {
        return NotWinningValue(standingScore, seatIsEast, TypicalScoresOf(seatIsEast));
    }

    int64_t Alive(int draws, int declaredOpponents) noexcept {
        const int opponents = eastl::clamp(declaredOpponents, 0, kMaxDeclaredOpponents);
        const int64_t survival = kOwnDrawsProbBase - kOpponentWinPerRoundPermille[opponents] * kProbPerPermille;
        int64_t alive = kOwnDrawsProbBase;
        for (int draw = 0; draw < draws; ++draw) {
            alive = RoundedDiv(alive * survival, kOwnDrawsProbBase);
        }
        return alive;
    }

}  // namespace BECore::Mahjong
