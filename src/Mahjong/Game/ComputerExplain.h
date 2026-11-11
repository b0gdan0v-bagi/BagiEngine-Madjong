#pragma once

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/DiscardDanger.h>

namespace BECore::Mahjong {

    /// The seed of the computer player that gives the advice: fixed, so the same view always gives the same advice and no rng of the game is touched.
    inline constexpr uint32_t kAdviceSeed = 1;

    /// How many alternatives an explanation lists beside the chosen action.
    inline constexpr int kExplainAlternatives = 2;

    /// A tile and the copies of it the seat cannot see (`UnseenCopies`).
    struct TileCopies {
        Tile tile;
        int unseen = 0;
    };

    /// One discard with the numbers the chooser rated it by (`DiscardOption`) and, when the 13 tiles that stay are waiting, the number of
    /// kinds they wait on and the unseen copies of those (both 0 otherwise). `danger` is the reading of the tile (`DiscardDangers`,
    /// `Safe` with no declared opponent), shown whatever the advice player's `readDanger` knob says; the knob only decides whether the
    /// advice (`RateDiscards`' order) uses it as a tie-break.
    struct TurnOption {
        Tile tile;
        int shanten = 0;
        int acceptance = 0;
        int keep = 0;
        int waitKinds = 0;
        int waitCopies = 0;
        Danger danger = Danger::Safe;
    };

    /**
     * @brief Why the advice of a turn is what it is: the numbers of the rule that decided.
     *
     * - `Discard`: `chosen` is the advised kind, `equalChoices` the number of other kinds with its shanten and acceptance (the keep
     *   value, and then chance, settles those), `alternatives` the next entries of `RateDiscards` that are not the advised kind.
     * - `Declare` / `KeepWaiting`: the options are the discards `WaitingDiscards` lists, best first (the most unseen winning copies, then
     *   the lowest keep value); `equalChoices` counts the others with the same copies; `waits` are the winning tiles of the 13 that stay.
     * - `Kong`: `shantenBefore` and `kongAcceptanceBefore` are those of the best discard of the hand as it is, `kongShantenAfter` and
     *   `kongAcceptanceAfter` those of the hand without the kong (`RateKong`); `chosen.tile` is the kong's tile, the other numbers of
     *   `chosen` and `alternatives` are not used.
     * - `DeclareWin`: only the step.
     *
     * `declaredOpponents` counts the opponents whose calling hand is declared (0: the `danger` of every option is `Safe` and means nothing);
     * `readDanger` is the knob of the profile the advice was made with, so whether the danger was part of the order (`TurnOption::danger`
     * decided between equal options only with it on).
     */
    struct TurnExplanation {
        HandAction action;
        TurnStep step = TurnStep::Discard;
        int shantenBefore = 0;
        TurnOption chosen;
        int equalChoices = 0;
        eastl::vector<TurnOption> alternatives;
        eastl::vector<TileCopies> waits;
        int kongShantenAfter = 0;
        int kongAcceptanceBefore = 0;
        int kongAcceptanceAfter = 0;
        int declaredOpponents = 0;
        bool readDanger = false;
    };

    /**
     * @brief Why the advice of a claim window is what it is.
     *
     * - `Claim`: `chosen` is the advised claim; `alternatives` the other listed claims, the acceptable ones first (best first by
     *   `IsBetterClaim`), then the unacceptable ones, at most `kExplainAlternatives`.
     * - `NoGain`: `chosen` is empty (the advice is `Pass`); `alternatives` the best listed claims, all unacceptable.
     * - `WaitingHand`: `waits` are the winning tiles of the held tiles.
     * - `Mahjong`: only the step.
     */
    struct ClaimExplanation {
        HandAction action;
        ClaimStep step = ClaimStep::NoGain;
        int shantenBefore = 0;
        std::optional<RatedClaim> chosen;
        eastl::vector<RatedClaim> alternatives;
        eastl::vector<TileCopies> waits;
    };

    /// The explanation of one advice: the member of the phase of the view is set.
    struct Explanation {
        std::optional<TurnExplanation> turn;
        std::optional<ClaimExplanation> claim;

        /// The advised action.
        [[nodiscard]] const HandAction& Action() const noexcept {
            return turn.has_value() ? turn->action : claim->action;
        }
    };

    /**
     * @brief The advice `ComputerPlayer(kAdviceSeed, profile).Choose(view)` gives, with the numbers it was chosen by (`profile`: the
     *        default level's, the advice player; a test passes another).
     *
     * Built only from the public rating functions of the chooser (`DecideTurn` / `DecideClaim`, `RateDiscards`, `RateKong`,
     * `RateClaims`, `WaitingDiscards`), so the numbers are the chooser's and there is no second rule: a new rule of the chooser has to be
     * reflected in `TurnStep` / `ClaimStep` and here. Dispatches on `view.phase` as `ComputerPlayer::Choose` does; nullopt when
     * `view.legal` is empty or the phase is `Finished`. Sees only the view, so it cannot peek.
     */
    [[nodiscard]] std::optional<Explanation> ExplainAdvice(const TableView& view, const LevelProfile& profile = ProfileOf(kDefaultLevel));

}  // namespace BECore::Mahjong
