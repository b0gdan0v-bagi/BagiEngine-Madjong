#pragma once

#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong::Analyzer {

    /**
     * @brief One decision of a seat found in a match log: the view of the seat at that moment and what the log's player did.
     *
     * There is no `HandModel` here, on purpose: the analysis gets `view` and nothing else, so what it can know is the seat's own hand
     * and the public table by the types.
     *
     * **Numbering** (the command line uses it too): `hand` is 1-based and hand K is what happens after K-1 `Settlement` events, so a
     * replayed drawn hand has a number of its own and a log without `Hand` events is numbered the same. `actionIndex` is 0-based
     * among the `Action` events of that hand, in log order, the passes of a claim window included.
     */
    struct LogPosition {
        TableView view;
        Seat seat = kNoSeat;
        /// The action the log's player took in this position (as the log has it: not checked against `view.legal`).
        HandAction logged;
        int hand = 0;
        int actionIndex = 0;
    };

    /**
     * @brief The position before action number `actionIndex` of hand number `hand` of `events` (one match).
     *
     * The events before the action are replayed with `ReplayMatchLog` on a fresh `Match`; the view is `MakeTableView` of the hand at
     * that point for the seat that acted, and the model is dropped. An error text for a log that does not replay, a hand or an index
     * out of range.
     */
    [[nodiscard]] std::expected<LogPosition, eastl::string> PositionFromLog(const eastl::vector<MatchLogEvent>& events, int hand, int actionIndex);

    /// Every decision of `seat` in hand number `hand`, in log order (see `LogPosition` for the numbering). An error text for a log that
    /// does not replay, a hand out of range, a number that is no seat, or a seat that has no decision in that hand. A decision with a
    /// single legal action is listed too: the caller decides what to do with it.
    [[nodiscard]] std::expected<eastl::vector<LogPosition>, eastl::string> PositionsOfSeat(const eastl::vector<MatchLogEvent>& events, int hand, Seat seat);

    // clang-format off
    /**
     * @brief A hand-written position, as the view of its seat. Line based; `#` starts a comment; words are separated by spaces; tiles are
     *        the notation of `Tile.h` (`5p 3s Ew Rd F1 T4`); seats are 0..3 and run counter-clockwise.
     *
     * | line | meaning |
     * |------|---------|
     * | `seat S` | the seat the position is for (default 0) |
     * | `east S` | the dealer; the seat winds follow (default: the seat itself) |
     * | `round East\|South\|West\|North` | the round wind (default East) |
     * | `phase discard\|claim` | the seat has to discard (default), or answers a discard (a robbed-kong window cannot be written) |
     * | `hand <tiles>` | the concealed tiles of the seat; melds count three each: 14 in `discard`, 13 in `claim` |
     * | `drew <tile>` / `drew none` | `discard` only: the tile just drawn (a self-draw win and an upgraded kong need it). Default: the last tile of the `hand` line; `none` for a hand that holds its extra tile from a claim |
     * | `meld S chow\|pung\|kong\|ckong <tile> [from T]` | an open chow (its lowest tile), pung or kong, or a concealed kong of seat S; `from T`: claimed from the discard of seat T |
     * | `discards S <tiles>` | the tiles of seat S still in the pile, in the order discarded (not the pending one) |
     * | `pending <tile> from S` | `claim` only, required: the discard to answer, the last of the pile, by seat S |
     * | `calling S [first]` | seat S has declared its calling hand; `first` = with its first discard |
     * | `bonus S <tiles>` | flowers and seasons laid open by seat S; any such line plays with the 144-tile set |
     * | `wall N` | the live tiles left (default: what the named tiles leave, with the loose reserve full) |
     *
     * Every key but `meld` is given once (`discards`, `calling` and `bonus` once per seat). The number of discards of a seat is its
     * `discards` tiles, the pending one, and the tiles of it that other seats' melds claim (`from`); the counts are taken as written, not checked against the turn order. Tiles that are not named are
     * unseen: they fill the other hands, then the wall, in a fixed order (the model needs an arbitrary consistent fill, and it never
     * reaches the analysis: the result is `MakeTableView`). `HandModel::Restore` is the judge of what can exist, and `legal` comes
     * from the model, not from the text.
     *
     * An error text names the line (`line 5: ...`) where one is to blame.
     */
    [[nodiscard]] std::expected<TableView, eastl::string> ParsePosition(std::string_view text);
    // clang-format on

}  // namespace BECore::Mahjong::Analyzer
