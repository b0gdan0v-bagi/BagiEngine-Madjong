#pragma once

#include <Mahjong/Game/ComputerTurn.h>

namespace BECore::Mahjong {

    /// The shanten at which a hand waits (one tile from complete): a waiting hand claims nothing but mahjong.
    inline constexpr int kWaitingShanten = 0;

    /// What a claim brings, in the order the options are compared at an equal rating: a kong over a pung over a chow.
    inline constexpr int kClaimRankKong = 2;
    inline constexpr int kClaimRankPung = 1;
    inline constexpr int kClaimRankChow = 0;
    /// A mahjong claim is taken at once and never compared with another claim; it has a rank of its own so that no other number is meant.
    inline constexpr int kClaimRankMahjong = -1;

    /// The tie-break rank of a kind of claim (`kClaimRank...`).
    [[nodiscard]] int ClaimRank(ClaimKind kind) noexcept;

    /// The concealed tiles of the seat after it claims `view.pendingTile` as `claim` (a `HandAction` of kind `Claim`, not mahjong):
    /// the tiles of the new set that the seat holds are out (chow: the other two of the sequence, pung: two, kong: three). The pending
    /// tile itself is not in the hand; the set is one more meld.
    [[nodiscard]] TileCounts ConcealedAfterClaim(const TableView& view, const HandAction& claim);

    /// The loose tile a kong draws: one draw more than the wall leaves the seat.
    inline constexpr int kKongLooseDraws = 1;

    /**
     * @brief The view of the seat after it makes `claim` on `view.pendingTile` (a `HandAction` of kind `Claim`, not mahjong): the tiles
     *        of the set that the seat holds are out of `own.concealed` (`ConcealedAfterClaim`), the set is an open meld.
     *
     * The copies the seat sees do not change (the pending tile lay in the pile, now it lies in the meld). A pung or a chow makes
     * the seat's own discard turn, as a claim does in `HandModel`: `Phase::Discard`, the seat to act, `legal` the plain discards of
     * the hand it holds, no pending tile; nothing is drawn, so `liveCount` stays. A kong is left as the window shows it: the seat
     * still draws its loose tile (`kKongLooseDraws`) before it discards, so the rest of the view is unchanged.
     */
    [[nodiscard]] TableView ViewAfterClaim(const TableView& view, const HandAction& claim);

    /**
     * @brief How good the hand is after the seat makes `claim` (a `HandAction` of kind `Claim`, not mahjong).
     *
     * A chow or a pung leaves 11 tiles (with 3n + 2 tiles) and the seat has to discard: the rating of `BestDiscardRating`. A kong
     * leaves 10, the seat draws a loose tile of unknown kind and then discards: the hand is rated as it stands (a hand of 3n + 1
     * tiles, `tile` invalid). So the shanten of the result is comparable with the shanten of the hand before the claim.
     */
    [[nodiscard]] DiscardRating RateClaim(const TableView& view, const HandAction& claim);

    /// A listed pung, kong or chow as `ChooseClaimAction` sees it: the action, the rating of the hand after it, its tie-break rank
    /// (`ClaimRank`) and whether it is acceptable (`s1 < s0`, or equal for a value pung/kong when the profile takes those).
    struct RatedClaim {
        HandAction action;
        DiscardRating after;
        int rank = 0;
        bool acceptable = false;
    };

    /// Whether claim `a` is better than `b`: the lower shanten after it, then the higher acceptance, then the higher rank.
    [[nodiscard]] bool IsBetterClaim(const RatedClaim& a, const RatedClaim& b) noexcept;

    /// Every pung, kong and chow of `view.legal` (no mahjong, no pass), in that order, rated with `RateClaim`; `shantenBefore` is the
    /// shanten of the hand as it is. `ChooseClaimAction` keeps the best of the acceptable ones.
    [[nodiscard]] Vector<RatedClaim> RateClaims(const TableView& view, int shantenBefore, const LevelProfile& profile);

    /// A kong of the discard offered to a waiting hand that keeps the wait, and what the hand is worth with it and without it.
    struct WaitingKong {
        HandAction kong;
        /// In milli-points (`ValueOfHeldHand`, `OwnDraws.h`): the hand as it is, and after the kong with its loose tile.
        int64_t passValue = 0;
        int64_t kongValue = 0;

        /// Whether the kong is worth more; an equal value passes (the hand stays concealed).
        [[nodiscard]] bool KongIsBetter() const noexcept {
            return kongValue > passValue;
        }
    };

    /// The kong of `view.pendingTile` when it is listed and the hand after it still waits (`RateClaim` at `kWaitingShanten`), rated
    /// by points: the kong gives its points and the loose tile, and the hand loses `FullyConcealed` once it is open. nullopt
    /// otherwise. A pure function of the view, no rng.
    [[nodiscard]] Optional<WaitingKong> RateWaitingKong(const TableView& view);

    /// The rule of the window that decided (the numbers of the list in `ChooseClaimAction`).
    enum class ClaimStep {
        Mahjong,      // 1
        WaitingHand,  // 2: the hand waits and the claim would break the wait (or nothing kept the wait)
        WaitingKong,  // 2: with `waitingKongByPoints`, a kong that keeps the wait is worth more than the pass
        WaitingPass,  // 2: with `waitingKongByPoints`, a kong that keeps the wait is worth no more than the pass
        Claim,        // 3: an acceptable claim was chosen (also when a roll of `skipClaimPercent` passed it, and a handoff placeholder)
        NoGain,       // 4: nothing acceptable
    };

    /// The chosen action and the rule that chose it.
    struct ClaimDecision {
        HandAction action;
        ClaimStep step = ClaimStep::NoGain;
    };

    /// `ChooseClaimAction` with the rule that decided; the same draws of `rng` in the same order.
    [[nodiscard]] ClaimDecision DecideClaim(const TableView& view, MahjongRng& rng, const LevelProfile& profile = kNormalProfile, SearchHandoff* handoff = nullptr);

    /**
     * @brief The action of a seat that has to decide a window (`Phase::Claim` or `Phase::RobKong`, `view.legal` holds `Pass`).
     *
     * The first rule that applies wins (`ClaimStep`; the rules in order: `Game/CLAUDE.md`, "Computer player"). With `profile.searchWorlds`
     * above 0 the search (`ComputerSearch.h`) runs after step 2 and may replace the choice of steps 3 and 4. With `profile.ownDrawsClaimDepth` above 0 the value of each answer (`ValueClaims`,
     * `OwnDraws.h`) decides instead of steps 3 and 4: a claim only when worth strictly more than the pass, no rng. A claim is acceptable when `RateClaim` lowers the shanten, or keeps it for a pung or
     * kong of a value tile (`IsValueTile`) while `profile.takeValueClaimsAtEqualShanten`; with `profile.skipClaimPercent` a roll passes the claim that was chosen.
     *
     * No defence: every loser pays the winner the same, so a discard that wins for another seat costs the discarder nothing extra.
     * Always an element of `view.legal` (`Pass` is, for a seat that has not decided).
     *
     * `handoff` as in `ChooseTurnAction`: a searched decision is handed back as a begun run and `Pass` is a placeholder.
     */
    [[nodiscard]] HandAction ChooseClaimAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile = kNormalProfile, SearchHandoff* handoff = nullptr);

}  // namespace BECore::Mahjong
