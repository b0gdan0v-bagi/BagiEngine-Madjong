#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/Seats.h>

namespace BECore::Mahjong {

    /// The rounds of a match, one per wind: the round wind of the hand number `n` is `Wind(n / handsPerRound)`.
    inline constexpr int kRoundsPerMatch = 4;

    /// Whether a hand that East wins (and so deals again) counts towards `handsPerMatch`. The one place of this rule: with `true` a match
    /// is always exactly `handsPerMatch` hands long; with `false` a repeat does not count, so a round ends after `handsPerMatch / 4`
    /// deals (in the full game of 16 that is the deal going round once) and the number of hands depends on the play (unbounded in theory).
    inline constexpr bool kEastRepeatCountsAsHand = true;

    /// Where the match stands, the part the bookkeeping of the rules needs: how many hands counted and who is East.
    struct MatchProgress {
        int handsPlayed = 0;
        Seat east = 0;

        bool operator==(const MatchProgress&) const = default;
    };

    /**
     * @brief The progress after a hand: pure bookkeeping, so a test covers every case without playing a hand.
     *
     * - East wins: East deals again; the hand counts (`kEastRepeatCountsAsHand`).
     * - Another seat wins: the hand counts and the deal moves to `NextSeat(east)`.
     * - Drawn, `settings.replayDrawnHand`: nothing moves, the hand does not count, the same East deals again.
     * - Drawn, not replayed: the hand counts and the deal moves on (East did not win).
     *
     * `winner` is read only for `ResultKind::Win`.
     */
    [[nodiscard]] MatchProgress NextProgress(MatchProgress progress, ResultKind kind, Seat winner, const MahjongSettings& settings) noexcept;

    /// The round wind of the hand that follows `handsPlayed` counted hands: East for the first quarter of `settings.handsPerMatch`,
    /// then South, West, North (and North for a number of hands beyond the match). East for a `handsPerMatch` below 4.
    [[nodiscard]] Wind RoundWindOf(int handsPlayed, const MahjongSettings& settings) noexcept;

    /// Whether `handsPlayed` counted hands finish the match.
    [[nodiscard]] bool IsMatchOver(int handsPlayed, const MahjongSettings& settings) noexcept;

    /// Whether `settings` describe a match that can be played: `handsPerMatch` a positive multiple of `kRoundsPerMatch`.
    [[nodiscard]] bool IsPlayable(const MahjongSettings& settings) noexcept;

    /// Where a match stands: not started, a hand is in progress (`Hand()` may already be `Finished`, waiting for `EndHand`), or over.
    CORE_ENUM(MatchPhase, uint8_t, NotStarted, InHand, Finished)

    /// Why the match refused something. A refusal changes nothing.
    CORE_ENUM(MatchError, uint8_t,
              BadSettings,      // `handsPerMatch` is not a positive multiple of 4
              NotInHand,        // the match is not started or is over: there is no hand to end
              HandNotFinished,  // `EndHand` while the hand in progress has no result yet
              HandRefused       // the next hand could not begin (cannot happen with winds made by `MakeSeatWinds`)
    )

    using MatchStatus = std::expected<void, MatchError>;

    /**
     * @brief A match of mahjong over `HandModel`s: 2000 points each, 16 hands (8 for the short game), the winds, the deal and the
     *        scores. Plain C++, no scene.
     *
     * `Start(settings, seed)` seeds the match's own `MahjongRng`: the draw for East (`RollSeatWinds`), then one wall per hand
     * (`MakeWall`), so one seed is one match. The first hand begins at once. The caller plays the hand through `Hand()` and calls
     * `EndHand()` once it is `Finished`: the hand is scored and settled (`SettleHand`), the points change by its `deltas`, and
     * `NextProgress` moves the deal and the count. Then the next hand begins, or the match is `Finished` after the last one (it
     * keeps that last hand, and `EndHand` is refused). `ApplySettlement` is the same step for a settlement made elsewhere, which
     * is how the rules of the match are tested without playing a hand.
     *
     * The round wind follows the number of counted hands (`RoundWindOf`); a drawn hand is replayed by the same East when
     * `settings.replayDrawnHand` and does not count; a hand East wins counts and East deals again (`kEastRepeatCountsAsHand`).
     */
    class Match {
    public:
        /// Starts a new match (whatever was going on is dropped): `settings.startingScore` for each seat, East by the draw, the first hand
        /// dealt. `BadSettings`, and nothing changes, unless `IsPlayable(settings)`.
        MatchStatus Start(const MahjongSettings& settings, uint32_t seed);

        [[nodiscard]] MatchPhase GetPhase() const noexcept {
            return _phase;
        }
        [[nodiscard]] const MahjongSettings& GetSettings() const noexcept {
            return _settings;
        }
        /// The hand in progress, or the last hand once the match is over. A default `HandModel` (a finished, drawn hand) before `Start`.
        [[nodiscard]] const HandModel& Hand() const noexcept {
            return _hand;
        }
        /// The hand in progress, to play it (`Apply`, `Discard`, ...).
        [[nodiscard]] HandModel& Hand() noexcept {
            return _hand;
        }
        /// The points of each seat.
        [[nodiscard]] const SeatScores& Scores() const noexcept {
            return _scores;
        }
        /// The dealer of the hand in progress (of the next hand once the match is over).
        [[nodiscard]] Seat East() const noexcept {
            return _progress.east;
        }
        /// The wind of each seat in the hand in progress (of the next hand once the match is over, like `East()`).
        [[nodiscard]] SeatWinds Winds() const noexcept {
            return MakeSeatWinds(_progress.east);
        }
        /// The round wind of the hand in progress (of the next hand once the match is over: North, `RoundWindOf` stops there).
        [[nodiscard]] Wind RoundWind() const noexcept {
            return RoundWindOf(_progress.handsPlayed, _settings);
        }
        /// Hands that counted so far.
        [[nodiscard]] int HandsPlayed() const noexcept {
            return _progress.handsPlayed;
        }
        /// Hands the match counts: `handsPerMatch`.
        [[nodiscard]] int HandsTotal() const noexcept {
            return _settings.handsPerMatch;
        }
        /// Every hand settled so far, in order, a replayed drawn hand too.
        [[nodiscard]] const eastl::vector<HandSettlement>& History() const noexcept {
            return _history;
        }

        /**
         * @brief Settles the finished hand in progress and goes on: returns the settlement that was applied.
         *
         * Errors: `NotInHand` (not started, or over), `HandNotFinished`.
         */
        std::expected<HandSettlement, MatchError> EndHand();

        /**
         * @brief Applies a settlement to the hand in progress: adds its `deltas` to the scores, keeps it in the history, moves the deal
         *        and the count by `NextProgress(kind, winner)`, then begins the next hand or finishes the match.
         *
         * `EndHand` calls it with the settlement of the hand; it does not look at the hand in progress, so a test feeds it made-up
         * settlements. Errors: `NotInHand`.
         */
        MatchStatus ApplySettlement(const HandSettlement& settlement);

        /// The seats by points, best first; equal points in seat order.
        [[nodiscard]] eastl::array<Seat, kSeatCount> Standings() const;

        /**
         * @brief Shows a match that is played elsewhere (an online room): everything is taken over as it is, nothing is checked or
         *        played. For a front end that draws the table of a server; the rules never call it.
         *
         * `phase` is `InHand` (the hand is in progress or finished, waiting for the next) or `Finished` (the match is over, `hand` is
         * the last hand). `handsPlayed` and `east` are the progress, `scores` the points of the seats, `hand` a model made by
         * `HandModel::ForDisplay`. `History()` stays empty. The match is **remote** (`IsRemote()`): `EndHand` and `ApplySettlement` are
         * refused with `NotInHand`, so it can never deal a real hand. `Start` makes a real match again.
         */
        void ShowRemote(const MahjongSettings& settings, MatchPhase phase, int handsPlayed, Seat east, const SeatScores& scores, HandModel hand);
        /// Whether the match was set by `ShowRemote` (its hand is display only) and not started.
        [[nodiscard]] bool IsRemote() const noexcept {
            return _hand.IsDisplayOnly();
        }

    private:
        /// Deals the hand of `progress` into `hand` from the next wall of the match.
        MatchStatus DealHand(const MatchProgress& progress, HandModel& hand);

        MahjongSettings _settings;
        MahjongRng _rng;
        MatchPhase _phase = MatchPhase::NotStarted;
        MatchProgress _progress;
        SeatScores _scores{};
        HandModel _hand;
        eastl::vector<HandSettlement> _history;
    };

}  // namespace BECore::Mahjong
