#pragma once

#include <Mahjong/Arena/ArenaPlayer.h>
#include <Mahjong/Game/ComputerSearch.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongRng.h>

/**
 * The oracle players: seats that know something the rules do not let a computer player see, so the arena can measure what that
 * knowledge is worth (`docs/bot-search.md`, "The oracle ladder"; the four kinds: `docs/bot-arena.md`). **They exist only here.** None
 * is a level or a `LevelProfile` field, none is reachable from `Game/`, the table session, the control API or a hint: `PlayArenaHand`
 * hands the real `HandModel` to `ChooseOracle` for a seat whose `ArenaPlayer::oracle` is set, and nothing else does. Every oracle
 * plays Normal's choosers and differs from Normal only by what it is told.
 *
 * `WholeHand` knows the order of the wall, its own coming draws included: a loose bound, not a player any honest one can approach.
 */
namespace BECore::Mahjong::Arena {

    /**
     * @brief The view of `seat` on `hand` with only `visible` changed: `UnseenCopies(view, tile)` is the number of copies that can still
     *        be drawn.
     *
     * The dead wall is never drawn and the opponents' concealed tiles are held, so both count as seen; the rest is `MakeTableView`.
     */
    [[nodiscard]] TableView TrueAcceptanceView(const HandModel& hand, Seat seat);

    /**
     * @brief The worlds of `oracle-hands` (`wallOrderKnown` false) and `oracle-whole` (true) for the real hand `hand`: a `WorldSource` of
     *        a search run.
     *
     * Every world restores one shared, never written capture of `hand`. With the wall order unknown, the loose reserve, the live wall
     * and the dead wall are shuffled together by the world's rng and cut back into lists of the same three sizes, so the multiset of
     * the wall is the real one; with it known, the rng is not drawn from. The view a world gives for the seat of the decision equals
     * the real view. Safe to call from several threads (the source's contract); the source keeps no reference to `hand`.
     */
    [[nodiscard]] WorldSource MakeTrueWorlds(const HandModel& hand, bool wallOrderKnown);

    /**
     * @brief The action of the oracle seat `seat` (`player.oracle` is not `None`) on the real hand `hand`; `seat` is the actor and has
     *        a legal action (the caller checks).
     *
     * The rng is drawn exactly as Normal draws it on the same state: an oracle adds no draw. A search decision plays its worlds
     * through `worlds`, or one by one on the calling thread when it is null; the answer is the same either way. Always an element of
     * the view's `legal`.
     */
    [[nodiscard]] HandAction ChooseOracle(const HandModel& hand, Seat seat, const ArenaPlayer& player, MahjongRng& rng, const WorldLoop* worlds);

}  // namespace BECore::Mahjong::Arena
