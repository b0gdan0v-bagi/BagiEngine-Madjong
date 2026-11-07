#include "Payments.h"

namespace BECore::Mahjong {

    namespace {

        /// `payer` gives `payee` `amount` points, doubled when one of them is East.
        void Transfer(SeatScores& change, Seat payer, Seat payee, int amount, Seat east) noexcept {
            const int due = (payer == east || payee == east) ? amount * kEastPaymentFactor : amount;
            change[payer] -= due;
            change[payee] += due;
        }

    }  // namespace

    SeatScores Settle(const SeatScores& rounded, Seat winner, Seat east) {
        SeatScores change{};
        if (!IsSeat(winner)) {
            return change;
        }
        for (Seat loser = 0; loser < kSeatCount; ++loser) {
            if (loser != winner) {
                Transfer(change, loser, winner, rounded[winner], east);
            }
        }
        for (Seat first = 0; first < kSeatCount; ++first) {
            for (Seat second = first + 1; second < kSeatCount; ++second) {
                if (first == winner || second == winner) {
                    continue;
                }
                if (rounded[first] < rounded[second]) {
                    Transfer(change, first, second, rounded[second] - rounded[first], east);
                } else if (rounded[second] < rounded[first]) {
                    Transfer(change, second, first, rounded[first] - rounded[second], east);
                }
            }
        }
        return change;
    }

}  // namespace BECore::Mahjong
