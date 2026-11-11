#pragma once

#include <Mahjong/Game/ComputerView.h>

namespace BECore::Mahjong {

    /**
     * @brief How likely a discard is to be the winning tile of a declared opponent, from public data only (`TableView`).
     *
     * `Safe < Low < Medium < High`. A grade is a reading, not a probability: it never says a tile is certain to win or to pass.
     */
    CORE_ENUM(Danger, uint8_t, Safe, Low, Medium, High)

    /// Sequence ways of a kind at which the grade is `High` / `Medium` (fewer live ways: `Low`).
    inline constexpr int kSequenceWaysHigh = 3;
    inline constexpr int kSequenceWaysMedium = 2;

    /// The grade of every playable kind, indexed by `Tile::Id()`.
    using DangerTable = eastl::array<Danger, kKindCount>;

    /**
     * @brief The rules of the reading, for the seat `v = view.seat` that is about to discard, per kind `t`, against every declared
     *        opponent `d` (`view.seats[d].callingDeclared`, `d != v`). The grade of a kind is the worst over the declared opponents; with
     *        none, every kind is `Safe` and `HasDeclaredOpponent` is false (nothing to show, no effect on a choice).
     *
     * `unseen(k)` is `UnseenCopies(view, k)`: what `v` cannot see, so what `d` can hold (`v`'s own copy of `t` is already counted
     * as seen). `d` wins on the discard of `t` only through a live way:
     * - the **pair way**: `unseen(t) >= 1` (`d` holds one copy of `t` and waits on the other); the **pung way**: `unseen(t) >= 2`
     *   (`d` holds two). An honour has only these two, and the pung way needs the pair way's copy, so one test (`>= 1`) covers both;
     * - a **sequence way** (suit kinds only): of the three partner pairs `(r-2, r-1)`, `(r-1, r+1)`, `(r+1, r+2)` of rank `r` that lie
     *   inside 1..9, each one whose two partner kinds both have `unseen >= 1`. A terminal has at most 1, a 2 or an 8 at most 2, ranks
     *   3..7 at most 3.
     *
     * The first rule that applies, against one `d`:
     * 1. `PassedAfterDeclaration` -> `Safe`: another seat discarded this kind after `d`'s declaring discard (`PublicSeat::passedKinds`),
     *    so `d` could have won on it and did not. Caveat: a person may pass a win, the rules do not force it.
     * 2. `NoLiveWay` -> `Safe`: no live way at all (an honour with all four copies seen is the common case). **A suit kind with all four
     *    copies seen is not safe by itself**: `d` holds none of them, and can still wait on the copy `v` throws; it is safe only when
     *    every sequence way is dead too. (Only for an honour is "every copy visible" safe.)
     * 3. By the live sequence ways `s`: `s >= kSequenceWaysHigh` -> `High`, `s == kSequenceWaysMedium` -> `Medium`, else `Low` (an
     *    honour with a live pair way is `Low`, a live terminal `Low`, a live 2 or 8 `Medium`, a live middle tile `High`).
     * 4. `OwnDiscard`: when `d` itself discarded the kind at any time (`PublicSeat::discardedKinds`), the grade is at most `Low`, not
     *    `Safe`: this ruleset has no furiten, so it is a strong habit and not a certainty.
     *
     * Not read: seven pairs and thirteen orphans waits (the computer player never aims at them), the number of melds of `d`, how long
     * `d` has been declared. The reading uses `view.seats`, `view.visible` and `view.seat` only; it is pure (no rng, no static state).
     * A view of a sampled world has no discard history (`TableView`), so there it rests on the counts alone: rule 1 and 4 never apply.
     * Meant for `Phase::Discard` (in `Phase::Claim` the pending discard counts as passed).
     */
    [[nodiscard]] bool HasDeclaredOpponent(const TableView& view) noexcept;

    /// The grade of one tile; `Safe` for a tile that is not playable.
    [[nodiscard]] Danger DangerOf(const TableView& view, Tile tile) noexcept;

    /// The grade of every kind, computed once for the whole table.
    [[nodiscard]] DangerTable DiscardDangers(const TableView& view) noexcept;

}  // namespace BECore::Mahjong
