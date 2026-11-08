#pragma once

#include <Mahjong/Game/ComputerTurn.h>

namespace BECore::Mahjong {

    /// The shanten at which a hand waits (one tile from complete): a waiting hand claims nothing but mahjong.
    inline constexpr int kWaitingShanten = 0;

    /// What a claim brings, in the order the options are compared at an equal rating: a kong over a pung over a chow.
    inline constexpr int kClaimRankKong = 2;
    inline constexpr int kClaimRankPung = 1;
    inline constexpr int kClaimRankChow = 0;

    /// The tie-break rank of a kind of claim (`kClaimRank...`); 0 for a mahjong claim, which is never compared.
    [[nodiscard]] int ClaimRank(ClaimKind kind) noexcept;

    /// The concealed tiles of the seat after it claims `view.pendingTile` as `claim` (a `HandAction` of kind `Claim`, not mahjong):
    /// the tiles of the new set that the seat holds are out (chow: the other two of the sequence, pung: two, kong: three). The pending
    /// tile itself is not in the hand; the set is one more meld.
    [[nodiscard]] TileCounts ConcealedAfterClaim(const TableView& view, const HandAction& claim);

    /**
     * @brief How good the hand is after the seat makes `claim` (a `HandAction` of kind `Claim`, not mahjong).
     *
     * A chow or a pung leaves 11 tiles (with 3n + 2 tiles) and the seat has to discard: the rating of `BestDiscardRating`. A kong
     * leaves 10, the seat draws a loose tile of unknown kind and then discards: the hand is rated as it stands (a hand of 3n + 1
     * tiles, `tile` invalid). So the shanten of the result is comparable with the shanten of the hand before the claim.
     */
    [[nodiscard]] DiscardRating RateClaim(const TableView& view, const HandAction& claim);

    /**
     * @brief The action of a seat that has to decide a window (`Phase::Claim` or `Phase::RobKong`, `view.legal` holds `Pass`).
     *
     * The first that applies wins:
     * 1. `Mahjong` whenever it is listed (the only claim of `Phase::RobKong`).
     * 2. `Pass` when the hand waits (`Shanten` 0 with the 13 tiles it holds): it stays concealed and takes mahjong only.
     * 3. Otherwise each listed pung, kong and chow is rated with `RateClaim` (shanten `s1`, acceptance `a1`; the shanten of the hand as
     *    it is is `s0`). A claim is acceptable when `s1 < s0`, or when `s1 == s0` for a pung or kong of a value tile (`IsValueTile`:
     *    a dragon, the own wind, the round wind; it scores a double). Of the acceptable ones: the lowest `s1`, then the highest `a1`,
     *    then `ClaimRank` (a kong, a pung, a chow), then `rng`.
     * 4. No acceptable claim: `Pass`.
     *
     * No defence: every loser pays the winner the same, so a discard that wins for another seat costs the discarder nothing extra.
     * Always an element of `view.legal` (`Pass` is, for a seat that has not decided).
     */
    [[nodiscard]] HandAction ChooseClaimAction(const TableView& view, MahjongRng& rng);

}  // namespace BECore::Mahjong
