#pragma once

#include <Mahjong/Game/ScoreReading.h>

namespace BECore::Mahjong {

    /**
     * @brief Whether the winner's hand before the winning tile waits on exactly one kind, and that kind is the winning tile.
     *
     * A kind the seat cannot win on (all four copies in its hand and melds) is not a wait (`WinningTiles`). False for a winner
     * without a valid winning tile in the concealed tiles.
     */
    [[nodiscard]] bool WaitsOnlyForTheWinningTile(const ScoreInput& input);

    /// The points only a winner has (`onlyWinning` is `WaitsOnlyForTheWinningTile`). Added first, so they head the lines.
    void AddWinnerPoints(const ScoreInput& input, bool onlyWinning, ScoreBreakdown& breakdown);

    /// The doubles only a winner has, read on `reading` and on the points already in `breakdown` (so it is called last): see
    /// `Game/CLAUDE.md` ("Scoring"). The doubles about the shape of the hand need a reading of four sets and a pair and are skipped for
    /// any other.
    void AddWinnerDoubles(const Reading& reading, const ScoreInput& input, ScoreBreakdown& breakdown);

}  // namespace BECore::Mahjong
