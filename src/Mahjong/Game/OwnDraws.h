#pragma once

#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/OwnDrawsValue.h>

namespace BECore::Mahjong {

    /// The most draws the search looks ahead (`ValueDiscards`' `depth` is clamped to it).
    inline constexpr int kOwnDrawsMaxDepth = 4;
    /// The most root candidates `ValueDiscards` rates, in the order of `RateDiscards`.
    inline constexpr int kOwnDrawsMaxCandidates = 6;

    /// What the value counts. The default is the value of slice 1: the seat's own draws only, nothing else.
    struct OwnDrawsModel {
        /// The draws the search looks ahead (`ValueDiscards`), clamped to `kOwnDrawsMaxDepth`; the closed-form functions have none.
        int depth = 0;
        /// A discard of another seat can win too (`LevelProfile::ownDrawsDiscardWins`): in each round of the closed form of a hand that
        /// waits, the three opponents discard before the seat draws, and a discard of a winning kind wins with the chance
        /// `kDiscardHitPermille` of the share of the unseen copies; the win is scored as a win on a discard (the set that holds the tile
        /// is open, no self-draw lines). Off: only the draw wins, as before.
        bool discardWins = false;
    };

    /// What one discard is worth from the seat's own draws, as plain data (the hints and the analyzer may show it later).
    struct DiscardValue {
        Tile tile;
        /// The chance, over `kOwnDrawsProbBase`, that the seat wins within the horizon.
        int winChance = 0;
        /// The score of a win, in points (the mean over the winning kinds and the lines, rounded).
        int meanWinScore = 0;
        /// In milli-points, given the seat does not win: another seat wins (the standing hand pays and settles) or the hand is drawn (0).
        int64_t valueWhenNotWinning = 0;
        /// In milli-points: the win payoffs and the not-winning outcomes together. What the discards are ordered by.
        int64_t expected = 0;
    };

    /**
     * @brief Each candidate discard of a seat on its turn, rated in points from the seat's own draws, best expected value first.
     *
     * A pure function of the `TableView` (so no peeking) and of `OwnDrawsValue.h`; no rng, no static state, so the answer does not
     * depend on call order. Empty for `depth` below 1, for a view that is not `Phase::Discard` with the seat to act, and when no plain
     * discard is listed. `depth` is clamped to `kOwnDrawsMaxDepth`.
     *
     * **The candidates** are the listed plain (non-declaring) discards whose hand left has a shanten at most `kSearchShantenSlack` above
     * the best, at most `kOwnDrawsMaxCandidates` of them in the order of `RateDiscards`. Ties of `expected` keep that order.
     *
     * **The search** is an expectimax over the seat's next `depth` draws. A draw of a kind that lowers the shanten (`ImprovingTiles`)
     * branches with the probability `unseen copies / unseen total` and is followed by the discard of the best value among the discards
     * that leave the lowest shanten; every other draw is one branch "the hand is unchanged" that takes the rest of the probability and
     * spends a draw. Lines meeting again are valued once, and so are the shanten, the improving and the winning kinds of one hand (tables of the call). A hand that waits is taken as declared (Normal
     * always declares) and is valued in closed form over all the draws the wall leaves (`liveCount / kSeatCount`): each draw wins with
     * the chance of the unseen winning copies, every win is `ScoreSeat` of the hand with that tile, and the rest is the standing hand
     * (`NotWinningValue`) or a drawn hand (0). Where the search stops with a hand that does not wait, it does not win any more.
     *
     * **What it leaves out**, all on purpose: the opponents are the table `Alive` (their number of declarations, no hand is modelled);
     * a win on a discard only with `OwnDrawsModel::discardWins` (and then without the priority between claims or the discards after
     * the seat's last draw), else only the own draws; the limit is `kDefaultLimit` (the view has none; a lower setting only caps
     * a score); a draw is taken from the whole unseen pool, the opponents' concealed tiles included, as every unseen copy is; the tile
     * of an unchanged draw is not counted as seen or as discarded; a win on the last live tile is not marked.
     */
    [[nodiscard]] Vector<DiscardValue> ValueDiscards(const TableView& view, int depth);

    /// As above with the model: `model.depth` is the depth.
    [[nodiscard]] Vector<DiscardValue> ValueDiscards(const TableView& view, const OwnDrawsModel& model);

    /**
     * @brief The discards that declare the calling hand (the listed ones that leave a waiting hand), rated in the same closed form,
     *        best expected value first. Ties: the most unseen winning copies, the lowest `KeepValue`, the lowest tile id.
     *
     * For step 2 of the turn: the declaration with the wait that is worth the most. Empty when the view is not a seat's own discard turn
     * or no discard declares.
     */
    [[nodiscard]] Vector<DiscardValue> ValueWaits(const TableView& view, const OwnDrawsModel& model = {});

    /**
     * @brief The seat's hand as it is held (3n + 1 tiles: in a claim window, or a view made for a claim), in milli-points, in the
     *        closed form of a waiting hand (taken as declared) over the draws the wall leaves plus `extraDraws` (a kong's loose tile).
     *
     * No search (a hand that does not wait wins nothing and is worth its standing value when another seat wins). For the kong of a
     * waiting hand (`RateWaitingKong`, `ComputerClaim.h`): the same arithmetic as `ValueWaits`, so the two choices agree. 0 for a view
     * of no seat.
     */
    [[nodiscard]] int64_t ValueOfHeldHand(const TableView& view, int extraDraws = 0, const OwnDrawsModel& model = {});

}  // namespace BECore::Mahjong
