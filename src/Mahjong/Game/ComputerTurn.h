#pragma once

#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/ComputerSearch.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/Shanten.h>

namespace BECore::Mahjong {

    /// What `KeepValue` says of a tile: the higher, the more the seat wants to keep it when the hand is no nearer to a win either way.
    inline constexpr int kKeepWorthless = 0;  // an honour that is no dragon, not the own wind and not the round wind
    inline constexpr int kKeepTerminal = 1;
    inline constexpr int kKeepSimple = 2;
    inline constexpr int kKeepValueTile = 3;  // a dragon, the own wind, the round wind: a pung of it scores a double

    /// The copies of `tile` the seat cannot see (`kCopiesPerTile - view.visible`, never below 0); 0 for a tile that is not playable.
    [[nodiscard]] int UnseenCopies(const TableView& view, Tile tile) noexcept;

    /// Whether a pung of `tile` scores a double for the seat: a dragon, the seat's own wind or the round wind. The one place of the rule
    /// (`KeepValue` and the claims use it).
    [[nodiscard]] bool IsValueTile(Tile tile, Wind seatWind, Wind roundWind) noexcept;

    /// The number of melds the seat has (the `meldCount` of the shanten functions).
    [[nodiscard]] int MeldCountOf(const TableView& view) noexcept;

    /// The first of `view.legal` of `kind`, or null.
    [[nodiscard]] const HandAction* FindAction(const TableView& view, ActionKind kind) noexcept;

    /// The first mahjong claim of `view.legal`, or null.
    [[nodiscard]] const HandAction* FindMahjong(const TableView& view) noexcept;

    /// One of non-empty `ties` by `rng` (the first when there is only one: no number is drawn).
    template <typename T>
    const T& PickOne(const eastl::vector<T>& ties, MahjongRng& rng) {
        return ties.size() == 1 ? ties.front() : ties[rng.Below(static_cast<uint32_t>(ties.size()))];
    }

    /// Whether a roll of `percent` comes up. What holds: a knob that is off (`percent` not above 0) draws nothing, so it moves no
    /// stream and Normal's tie-breaks are those of the player before levels existed. A knob that is on draws one number from `rng`,
    /// so it shifts the later tie-breaks of that player (its play is another play, as a level should be).
    [[nodiscard]] inline bool Roll(MahjongRng& rng, int percent) {
        return percent > 0 && static_cast<int>(rng.Below(kPercentBase)) < percent;
    }

    /// How much the seat wants to keep `tile` (`kKeep...`); it only breaks ties, efficiency decides first.
    [[nodiscard]] int KeepValue(Tile tile, Wind seatWind, Wind roundWind) noexcept;

    /// The unseen copies of the kinds that lower `Shanten(counts, meldCount)` (`ImprovingTiles`): how many tiles of the wall (and of the
    /// hands) would take a hand of `3n + 1` tiles one step nearer. At shanten 0 these are the unseen copies of the winning tiles of the
    /// four-sets-and-a-pair shape.
    [[nodiscard]] int AcceptanceOf(const TableView& view, const TileCounts& counts, int meldCount);

    /// A discard that leaves the hand waiting: the tile, the unseen copies of the tiles it waits on, and its `KeepValue`.
    struct WaitingDiscard {
        Tile tile;
        int copies = 0;
        int keep = 0;
    };

    /// The listed discards with `declareCalling == declaring` that leave a waiting hand (steps 2 and 4 of the turn, and the candidates of
    /// the search), in the order of `view.legal`.
    [[nodiscard]] eastl::vector<WaitingDiscard> WaitingDiscards(const TableView& view, bool declaring);

    /// How good the best discard of a hand of `3n + 2` tiles is: the tile, the shanten and the acceptance of the `3n + 1` tiles left.
    struct DiscardRating {
        Tile tile;
        int shanten = 0;
        int acceptance = 0;
    };

    /**
     * @brief The best discard of the concealed tiles `counts` with `meldCount` melds (hand of `3n + 2` tiles).
     *
     * Of every kind held, the discards that leave the lowest `Shanten` are candidates; among them the highest `AcceptanceOf`; among
     * those the lowest `KeepValue`; the rest of the ties go to `rng`. `view` gives the unseen copies and the winds. Used for the own
     * turn and, with the hand after a claim, for the claims. An empty `counts` gives an invalid tile.
     */
    [[nodiscard]] DiscardRating BestDiscard(const TileCounts& counts, int meldCount, const TableView& view, MahjongRng& rng);

    /// The shanten and the acceptance `BestDiscard` finds, with no rng: `tile` is the first of the tied discards (the lowest `KeepValue`,
    /// then the lowest id). For comparing hands.
    [[nodiscard]] DiscardRating BestDiscardRating(const TileCounts& counts, int meldCount, const TableView& view);

    /**
     * @brief The action of a seat that has to discard (`Phase::Discard`, `view.currentSeat == view.seat`, `view.legal` not empty).
     *
     * The steps, the first that applies wins:
     * 1. `DeclareWin` when it is listed.
     * 2. The calling hand: of the discards that declare it, the one that leaves the most unseen winning copies (`WinningTiles` of the
     *    13 tiles that stay); ties by the lowest `KeepValue`, then `rng`. With `profile.skipDeclarationPercent` a roll can skip this
     *    step for the turn.
     * 3. A kong (`ConcealedKong`, `UpgradeKong`) when the hand without it is no worse: the best discard of the hand as it is, against
     *    the hand without the four tiles and one more meld (the fourth tile of an upgrade leaves), the shanten not higher and, for a
     *    waiting hand, the acceptance not lower.
     * 4. After the declaration: the discard that keeps the hand waiting with the most unseen winning copies (the tie-breaks as in 2).
     * 5. Otherwise `BestDiscard` of the concealed tiles; with `profile.skipAcceptancePercent` a roll leaves out the acceptance (the
     *    lowest shanten, then `KeepValue`, then `rng`).
     *
     * `profile` is the level's numbers (`kNormalProfile`: every knob off, no number drawn besides the tie-breaks). With
     * `profile.searchWorlds` above 0 the search (`ComputerSearch.h`) runs after step 1 and may replace the choice of steps 2 to 5. Always an element
     * of `view.legal`. Seven pairs and thirteen orphans are not aimed at (`Shanten` does not look at them), but
     * step 1 to 2 take them whenever the model lists them.
     *
     * With a `handoff`, a decision that is a search is not played: the run (begun, so the one draw is made) goes into `handoff->run`
     * and `Pass` comes back as a placeholder. Any other decision is answered as without a handoff and leaves `handoff->run` empty.
     */
    [[nodiscard]] HandAction ChooseTurnAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile = kNormalProfile, SearchHandoff* handoff = nullptr);

}  // namespace BECore::Mahjong
