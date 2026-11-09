#pragma once

#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongRng.h>

namespace BECore::Mahjong {

    /// How often a declared opponent's hidden tiles are drawn again when they do not wait (`SampleWorld`). A random hand of 13 tiles
    /// almost never waits, so the number is large; measured on playouts at 400 tries: with 4 melds (one hidden tile) and with 3 melds
    /// (four) the sample waits every time, with 2 melds (seven) in about half of the samples, with fewer almost never. After the last
    /// try the last draw is kept.
    inline constexpr int kWorldDeclaredTries = 400;

    /// A declared opponent whose number of discards is not 0 or 1 declared "not on its first discard" in the sample (`SampleWorld`);
    /// this is the number of discards it had made then.
    inline constexpr int kWorldLateDeclarationDiscards = 1;

    /**
     * @brief Builds a whole hand that is consistent with what one seat sees: a determinisation for the search player.
     *
     * Reads nothing but `view` and draws only from `rng` (no `HandModel` of the table, no live wall, no other hand), so two tables that
     * a seat cannot tell apart give the same world and leave `rng` in the same state. The world is a real `HandModel` (`HandModel::Restore`):
     * `MakeTableView(world, view.seat)` equals `view` in every field (the own hand, the public seats, `visible`, `liveCount`, `legal`, the
     * phase, the current seat and the pending tile), every kind has all its copies over the whole world, and a rollout can play it on.
     *
     * How it is made:
     * - The pool is the unseen copies of every kind (`UnseenCopies`). Every other seat gets its concealed kongs first (a kind with all
     *   four copies unseen, picked at random, as many as the view counts), then `13 - 3 * (open melds + concealed kongs)` concealed
     *   tiles drawn at random from the pool (the seat to act holds one more, and so does the owner of a kong that is being robbed: it
     *   holds the pending tile besides). A declared opponent's tiles are drawn again, up to `kWorldDeclaredTries` times, until its 13
     *   tiles wait (`IsWaiting`); after the last try it keeps the last draw.
     * - What is left of the pool goes to the wall with the unseen bonus tiles, shuffled: `view.liveCount` live tiles, the dead wall
     *   (`kDeadWallTiles`) and the rest as the loose reserve (at most `kLooseTiles`). The view does not say whether the set has bonus
     *   tiles: with a bonus tile laid open by anyone it has; otherwise the number of tiles that are left has to fit a wall without them,
     *   or else a wall with all eight. A bonus tile is never in a hidden hand (a drawn one is laid open and replaced at once), only
     *   in the wall. The other settings (`limit`, the starting score) are the defaults: the view does not hold them.
     * - The pile is rebuilt from `visible`: what is visible and neither held by the seat nor in an open meld is in the pile (the kind
     *   counts are exact); how many tiles each seat put in it follows from its `discardCount` less the claimed tiles taken from it;
     *   which kind belongs to which seat and in which order is drawn at random (the pending discard of a claim window is the last).
     * - The drawn tile of the seat to act: the tile of a listed `UpgradeKong`, else, for a listed `DeclareWin`, a tile that completes
     *   the hand, else none (after a claim the tile is not a draw, and nothing tells the two apart). The robbed kong's tile is the
     *   owner's drawn tile.
     * - A claim window is reopened with every seat that could claim undecided (`PendingClaimSeats()` is in seat order; the search lets
     *   the seat of the view decide first): what earlier seats passed before is not in the view, so they decide again.
     *
     * Known approximations, because the view cannot tell: the order and the discarders of the pile; the tiles a declared opponent
     * really holds (its hand need not wait after the declaration, and the sample of a declared hand with fewer than three melds often does not wait); a
     * declared opponent with two or more discards declared on its second discard, never its first (`callingAtDiscardCount`
     * `kWorldLateDeclarationDiscards`, 0 when it has made one), so the double for a declaration on the first discard is never given
     * to it; the move log is empty; the drawn tile is not from the loose reserve.
     *
     * `nullopt` when the view has no legal action (it is not the seat's turn, or the hand is over), is the view of no seat, or the pool
     * does not fit the places (no wall length is possible, a hand too short, a kong with no kind to be, a pile that disagrees with
     * the discard counts, a robbed kong with no pung, or `legal` that the sample cannot reproduce).
     */
    [[nodiscard]] std::optional<HandModel> SampleWorld(const TableView& view, MahjongRng& rng);

}  // namespace BECore::Mahjong
