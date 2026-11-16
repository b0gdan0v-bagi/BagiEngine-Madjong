#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/FixedWall.h>
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
    /// is always exactly `handsPerMatch` hands long; with `false` the number of hands depends on the play (unbounded in theory).
    inline constexpr bool kEastRepeatCountsAsHand = true;

    /// The part of the match the bookkeeping of the rules needs.
    struct MatchProgress {
        int handsPlayed = 0;
        Seat east = 0;

        bool operator==(const MatchProgress&) const = default;
    };

    /**
     * @brief The progress after a hand: pure bookkeeping, so a test covers every case without playing a hand.
     *
     * East wins: East deals again (the hand counts as `kEastRepeatCountsAsHand` says). A drawn hand with `settings.replayDrawnHand`
     * moves nothing and does not count; a drawn hand without it is drawn, not replayed: the hand counts and the deal moves on. `winner`
     * is read only for `ResultKind::Win`.
     */
    [[nodiscard]] MatchProgress NextProgress(MatchProgress progress, ResultKind kind, Seat winner, const MahjongSettings& settings) noexcept;

    /// The round wind of the hand that follows `handsPlayed` counted hands: one wind per quarter of `settings.handsPerMatch`, North
    /// beyond the match, East for a `handsPerMatch` below 4.
    [[nodiscard]] Wind RoundWindOf(int handsPlayed, const MahjongSettings& settings) noexcept;

    [[nodiscard]] bool IsMatchOver(int handsPlayed, const MahjongSettings& settings) noexcept;

    /// `handsPerMatch` a positive multiple of `kRoundsPerMatch`.
    [[nodiscard]] bool IsPlayable(const MahjongSettings& settings) noexcept;

    /// Where a match stands: not started, a hand is in progress (`Hand()` may already be `Finished`, waiting for `EndHand`), or over.
    CORE_ENUM(MatchPhase, uint8_t, NotStarted, InHand, Finished)

    /// Why the match refused something. A refusal changes nothing.
    CORE_ENUM(MatchError, uint8_t,
              BadSettings,      // `IsPlayable` is false
              NotInHand,        // the match is not started or is over: there is no hand to end
              HandNotFinished,  // `EndHand` while the hand in progress has no result yet
              HandRefused,      // the next hand could not begin (cannot happen with winds made by `MakeSeatWinds`)
              BadDeal           // `Start` from a `FixedStart`: the deal, `handsPlayed` or the first hand is refused
    )

    using MatchStatus = std::expected<void, MatchError>;

    /**
     * @brief A match of mahjong over `HandModel`s: the winds, the deal and the scores. Plain C++, no scene. The rules of the match:
     *        `Game/CLAUDE.md` ("Match").
     *
     * `Start(settings, seed)` seeds the match's own `MahjongRng`: the draw for East (`RollSeatWinds`), then one wall per hand
     * (`MakeWall`), so one seed is one match. The caller plays the hand through `Hand()` and calls `EndHand()` once it is
     * `Finished`; the match then deals the next hand, or is `Finished` after the last one (it keeps that last hand, and `EndHand` is
     * refused). `ApplySettlement` is the same step for a settlement made elsewhere, which is how the rules of the match are tested.
     */
    class Match {
    public:
        /// Starts a new match (whatever was going on is dropped) and deals the first hand. `BadSettings`, and nothing changes, unless
        /// `IsPlayable(settings)`.
        MatchStatus Start(const MahjongSettings& settings, uint32_t seed);

        /**
         * @brief Starts a match from a position: the first hand is the fixed wall of `state.deal` (East `state.deal.east`, no draw for
         *        East), `state.handsPlayed` hands already counted (the round wind follows) and `state.scores` as the scores.
         *
         * `seed` seeds the match's own rng, which the **next** hands use (`MakeWall`), so one position and one seed are one match.
         * `BadSettings` unless `IsPlayable(settings)`; `BadDeal` for a deal `TryBuildFixedWall` refuses, `handsPlayed` outside
         * `0..handsPerMatch - 1` or a hand `Begin` refuses. A refusal changes nothing.
         */
        MatchStatus Start(const MahjongSettings& settings, uint32_t seed, const FixedStart& state);

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
        /// The hand in progress, to play it.
        [[nodiscard]] HandModel& Hand() noexcept {
            return _hand;
        }
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
        [[nodiscard]] int HandsPlayed() const noexcept {
            return _progress.handsPlayed;
        }
        [[nodiscard]] int HandsTotal() const noexcept {
            return _settings.handsPerMatch;
        }
        /// Every hand settled so far, a replayed drawn hand too.
        [[nodiscard]] const Vector<HandSettlement>& History() const noexcept {
            return _history;
        }

        /**
         * @brief Settles the finished hand in progress and goes on: returns the settlement that was applied. Errors: `NotInHand`,
         *        `HandNotFinished`.
         */
        std::expected<HandSettlement, MatchError> EndHand();

        /**
         * @brief Applies a settlement to the hand in progress, moves the deal and the count by `NextProgress`, then begins the next
         *        hand or finishes the match.
         *
         * `EndHand` calls it; it does not look at the hand in progress, so a test feeds it made-up settlements. Errors: `NotInHand`.
         */
        MatchStatus ApplySettlement(const HandSettlement& settlement);

        /// The seats by points, best first; equal points in seat order.
        [[nodiscard]] Array<Seat, kSeatCount> Standings() const;

        /**
         * @brief Shows a match that is played elsewhere (an online room): everything is taken over as it is, nothing is checked or
         *        played. For a front end that draws the table of a server; the rules never call it.
         *
         * `hand` is a model made by `HandModel::ForDisplay`; `History()` stays empty. The match is **remote** (`IsRemote()`): `EndHand`
         * and `ApplySettlement` are refused with `NotInHand`, so it can never deal a real hand. `Start` makes a real match again.
         */
        void ShowRemote(const MahjongSettings& settings, MatchPhase phase, int handsPlayed, Seat east, const SeatScores& scores, HandModel hand);
        /// Set by `ShowRemote` and not started since.
        [[nodiscard]] bool IsRemote() const noexcept {
            return _hand.IsDisplayOnly();
        }

    private:
        MatchStatus DealHand(const MatchProgress& progress, HandModel& hand);

        MahjongSettings _settings;
        MahjongRng _rng;
        MatchPhase _phase = MatchPhase::NotStarted;
        MatchProgress _progress;
        SeatScores _scores{};
        HandModel _hand;
        Vector<HandSettlement> _history;
    };

}  // namespace BECore::Mahjong
