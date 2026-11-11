#pragma once

#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /**
     * The arithmetic under the value of a discard from the seat's own draws (`OwnDraws.h`): what a win pays, what a standing hand costs or
     * earns when another seat wins, and the chance that nobody else has won after some draws. Pure: no `TableView`, no rng.
     *
     * **Fixed point, so one input gives one choice on every platform.** A probability is an integer over `kOwnDrawsProbBase`, a value is
     * an `int64_t` in milli-points (`kMilliPerPoint` to a point). Every division goes through `RoundedDiv` (round half up). No `double`
     * is used in a choice. A score is an `int` up to `LimitCap(INT_MAX)`, a milli-point value of it with the largest factor is far inside
     * an `int64_t`, so a huge limit cannot overflow.
     */

    /// A probability of 1.
    inline constexpr int64_t kOwnDrawsProbBase = 1'000'000;
    /// Milli-points in a point: the unit of every value.
    inline constexpr int64_t kMilliPerPoint = 1000;
    /// The unit of `kOpponentWinPerRoundPermille`.
    inline constexpr int64_t kPermilleBase = 1000;
    /// The most opponents that can have declared the calling hand (the other three seats).
    inline constexpr int kMaxDeclaredOpponents = kSeatCount - 1;

    /**
     * The chance, in thousandths, that another seat wins in one round (the three other seats act once), by the number of the other seats
     * that have declared the calling hand (0..3). Measured by `BagiMahjongArena --table-stats --first 4000 --seeds 1000 --jobs 14` (four Normal
     * seats, Release): the first column of the table, rounded. `docs/own-draws-ev.md` says what the table shows beyond round 1: the number of
     * declared opponents grows during the horizon, so the survival after two rounds is lower than `(1 - h)^2`.
     */
    inline constexpr int kOpponentWinPerRoundPermille[kSeatCount] = {7, 188, 336, 475};

    /// A typical winner's score and a typical score of a seat that stands next to it (the losers' pairwise settlement is by the difference).
    struct TypicalScores {
        int winner = 0;
        int loser = 0;
    };

    /// The winner's score and the standing score of a seat that did not win, measured by the same command (the means of the rounded scores
    /// over the won hands, 45.5 and 9.2 pooled, rounded): East and the other seats do not differ significantly (47.9 +- 4.1 against 44.5 +-
    /// 2.0 for a winner, 9.4 +- 1.0 against 9.1 +- 0.4 standing), so one pair serves both.
    inline constexpr TypicalScores kTypicalScoresEast{45, 9};
    inline constexpr TypicalScores kTypicalScoresOther{45, 9};

    /// `numerator / denominator` rounded half up; both are not negative and `denominator` is positive. The one rounding of this file.
    [[nodiscard]] int64_t RoundedDiv(int64_t numerator, int64_t denominator) noexcept;

    /// `numerator / denominator` rounded half away from zero (`RoundedDiv` of the magnitude); `denominator` is positive.
    [[nodiscard]] int64_t RoundedDivSigned(int64_t numerator, int64_t denominator) noexcept;

    /// What a win of `score` points pays the seat, in milli-points: the winner receives `score` from each of the three others, and a
    /// payment with East is `kEastPaymentFactor` times larger. So East gets three doubled payments and any other seat one doubled (East's)
    /// and two plain ones. Derived from `kEastPaymentFactor`; `Settle` agrees.
    [[nodiscard]] int64_t WinPayoff(int score, bool seatIsEast) noexcept;

    /// The typical scores of a seat that is East or not.
    [[nodiscard]] constexpr TypicalScores TypicalScoresOf(bool seatIsEast) noexcept {
        return seatIsEast ? kTypicalScoresEast : kTypicalScoresOther;
    }

    /**
     * @brief What the seat's standing hand is worth when another seat wins, in milli-points, on average over which seat wins.
     *
     * The seat pays the winner `typical.winner` and settles with each of the two other losers by the difference of the scores,
     * `standingScore - typical.loser` (a negative difference is a payment). A payment that involves East is `kEastPaymentFactor` times
     * larger: an East seat has the factor on all three payments; another seat has it on the payment with East only, and East is the
     * winner with chance 1/3 and one of the two losers with chance 2/3, so each payment carries the expected factor
     * `(kEastPaymentFactor + kSeatCount - 2) / (kSeatCount - 1)`, kept as a fraction of integers. The value is `factor x (losers x
     * (standing - loser) - winner)` points, rounded half away from zero. A drawn hand pays nothing and is not this function's business.
     */
    [[nodiscard]] int64_t NotWinningValue(int standingScore, bool seatIsEast, const TypicalScores& typical) noexcept;

    /// `NotWinningValue` at the typical scores of the table (`TypicalScoresOf`).
    [[nodiscard]] int64_t NotWinningValue(int standingScore, bool seatIsEast) noexcept;

    /**
     * @brief The chance, over `kOwnDrawsProbBase`, that nobody else has won after the seat's `draws` next draws: `(1 - h)^draws`, `h` the
     *        chance of `kOpponentWinPerRoundPermille[declaredOpponents]`. 1 at 0 draws, falling with every draw. `declaredOpponents` is
     *        clamped to 0..`kMaxDeclaredOpponents`, `draws` below 0 is 0.
     *
     * Each step multiplies by the survival of one round and rounds half up (`RoundedDiv`), so the table is monotone and the same
     * everywhere.
     */
    [[nodiscard]] int64_t Alive(int draws, int declaredOpponents) noexcept;

}  // namespace BECore::Mahjong
