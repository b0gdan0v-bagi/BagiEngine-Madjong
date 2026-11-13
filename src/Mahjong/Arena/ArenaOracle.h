#pragma once

#include <Mahjong/Arena/ArenaPlayer.h>
#include <Mahjong/Game/ComputerSearch.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongRng.h>

/**
 * The oracle players: seats that know something the rules do not let a computer player see, so the arena can measure how much that
 * knowledge is worth (`docs/bot-search.md`, "The oracle ladder"). **They exist only here.** None is a level or a `LevelProfile` field,
 * none is reachable from `Game/`, the table session, the control API or a hint: the arena's `PlayArenaHand` hands the real `HandModel`
 * to `ChooseOracle` for a seat whose `ArenaPlayer::oracle` is set, and nothing else does. Every oracle plays Normal's choosers
 * (the spec's knobs on the Normal profile) and differs from Normal only by what it is told.
 *
 * - `Acceptance` (`oracle-acceptance`): Normal on `TrueAcceptanceView`, whose unseen copies are exactly the copies still in the live wall
 *   and the loose reserve (the dead wall and the opponents' concealed tiles count as seen): the ceiling of reading the hands for the
 *   acceptance and the waits of a hand-building player.
 * - `Defence` (`oracle-defence`): Normal's action, except that a discard (or a kong upgrade, which can be robbed) of a tile that completes
 *   a declared opponent's concealed tiles is replaced by the best discard of equal shanten that does not.
 */
namespace BECore::Mahjong::Arena {

    /**
     * @brief The view of `seat` on `hand` with only `visible` changed: `visible[id]` is `kCopiesPerTile` minus the copies of the kind in
     *        the live wall and the loose reserve, so `UnseenCopies(view, tile)` is the number of copies that can still be drawn.
     *
     * The dead wall is never drawn and the tiles in the opponents' concealed hands are held, so both count as seen. Everything else of
     * the view (`MakeTableView`) is as it was.
     */
    [[nodiscard]] TableView TrueAcceptanceView(const HandModel& hand, Seat seat);

    /**
     * @brief The action of the oracle seat `seat` (`player.oracle` is not `None`) on the real hand `hand`; `seat` is the actor and has
     *        a legal action (the caller checks).
     *
     * The rng is the seat's own and is drawn exactly as Normal draws it on the same state: an oracle adds no draw. With `worlds` a
     * search decision (a profile with `searchWorlds`) plays its worlds through that loop, otherwise at once on the calling thread; the
     * answer is the same either way. Always an element of the view's `legal`.
     */
    [[nodiscard]] HandAction ChooseOracle(const HandModel& hand, Seat seat, const ArenaPlayer& player, MahjongRng& rng, const WorldLoop* worlds);

}  // namespace BECore::Mahjong::Arena
