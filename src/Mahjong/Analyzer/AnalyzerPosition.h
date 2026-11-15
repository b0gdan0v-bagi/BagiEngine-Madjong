#pragma once

#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong::Analyzer {

    /**
     * @brief One decision of a seat found in a match log: the seat's view at that moment and what the log's player did.
     *
     * No `HandModel`, on purpose: the analysis gets `view` and nothing else.
     *
     * **Numbering** (the command line uses it too): `hand` is 1-based and hand K follows K-1 `Settlement` events, so a replayed drawn
     * hand has a number of its own. `actionIndex` is 0-based among the `Action` events of that hand, the passes of a claim window
     * included.
     */
    struct LogPosition {
        TableView view;
        Seat seat = kNoSeat;
        /// As the log has it, not checked against `view.legal`.
        HandAction logged;
        int hand = 0;
        int actionIndex = 0;
    };

    /**
     * @brief The position before action `actionIndex` of hand `hand` of `events` (one match): the events before it are replayed and
     * only the view of the seat that acted is kept. An error text for a log that does not replay or a number out of range.
     */
    [[nodiscard]] std::expected<LogPosition, String> PositionFromLog(const Vector<MatchLogEvent>& events, int hand, int actionIndex);

    /// Every decision of `seat` in hand `hand`, in log order, a decision with a single legal action too. An error text for a log that
    /// does not replay, a number out of range, or a seat with no decision in that hand.
    [[nodiscard]] std::expected<Vector<LogPosition>, String> PositionsOfSeat(const Vector<MatchLogEvent>& events, int hand, Seat seat);

    // clang-format off
    /**
     * @brief A hand-written position, as the view of its seat (`docs/ev-analyzer.md`, "Where a position comes from"). Line based; `#`
     *        starts a comment; tiles are the notation of `Tile.h`; seats are 0..3 and run counter-clockwise.
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
     * `discards` tiles, the pending one, and the tiles of it that other seats' melds claim (`from`); it decides `first` of `calling`. The
     * counts are taken as written, not checked against the turn order. Tiles that are not named are unseen: they fill the other hands,
     * then the wall, in a fixed order (the model needs some consistent fill; it never reaches the analysis, which gets
     * `MakeTableView`). `HandModel::Restore` judges what can exist, and `legal` comes from the model, not from the text. Nothing is guessed: an error names the line.
     */
    [[nodiscard]] std::expected<TableView, String> ParsePosition(StringView text);
    // clang-format on

}  // namespace BECore::Mahjong::Analyzer
