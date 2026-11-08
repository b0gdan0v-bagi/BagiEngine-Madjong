#pragma once

#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    /// The number `Shanten` answers for a complete hand (four sets and a pair).
    inline constexpr int kShantenComplete = -1;

    /**
     * @brief How many tiles a hand is away from four sets and a pair, the standard count.
     *
     * `counts` are the concealed tiles (a kind may be held up to four times; a count above four is the caller's bug), `meldCount` the
     * melds of the seat (0..4, clamped; they are not in `counts` and count as complete sets). With `needed = 4 - meldCount` the number is
     * `2 * needed - 2 * sets - min(partials, needed - sets) - (pair ? 1 : 0)`, the least over the readings of the tiles into complete
     * sets, partial sets (two tiles of a chow, or a pair that is not the pair of the hand) and one pair.
     *
     * The same formula reads for both sizes of hand. A hand of `3n + 1` tiles (13 with no melds) is one tile from complete at 0 (it
     * waits); it can never be -1. A hand of `3n + 2` tiles (14 with no melds) is `kShantenComplete` (-1) when it is four sets and a
     * pair, and 0 when it needs one more draw to be so after the discard. Each step up is one more tile that has to come.
     *
     * Only the shape of four sets and a pair is looked at: seven pairs and thirteen orphans (limit hands only, `IsWinningShape` knows
     * them) are not, so a hand of six pairs has a high number. A copy limit is not applied: a kind held four times can be a pung and a
     * single, and "waiting" can mean waiting on a fifth copy that does not exist (`WinningTiles` knows the copies).
     *
     * Cost: a depth-first search over the readings of the lowest kind held, with no heap allocation, the partial sets cut off at
     * `needed`, and a reading dropped as soon as the tiles that are left cannot take it below the best so far (a tile lowers the number
     * by at most 2/3). Measured per call on 13 and 14 tile hands, 2000 of each kind: random hands 5 microseconds in Debug (-O0) and
     * 1.2 in Release (-O2); mostly one suit 6 and 1.2; one suit 2 and 0.3. Without the cut the same hands took 15, 160 and 270
     * microseconds in Debug.
     */
    [[nodiscard]] int Shanten(const TileCounts& counts, int meldCount);

    /**
     * @brief The kinds whose addition lowers `Shanten(counts, meldCount)` by one, in the order of the tile ids.
     *
     * Meant for a hand of `3n + 1` tiles (the player after a discard): the tiles it is waiting for when it is at 0, the tiles that
     * take it one step nearer otherwise. Empty for a complete hand. Only the kinds next to a held tile (the same kind or the same
     * suit within two ranks) are tried, because no other tile can join a set. A kind of which the caller passes four copies is still
     * listed: the caller subtracts the copies it cannot get (the view of the player knows what lies in the pile).
     */
    [[nodiscard]] eastl::vector<Tile> ImprovingTiles(const TileCounts& counts, int meldCount);

}  // namespace BECore::Mahjong
