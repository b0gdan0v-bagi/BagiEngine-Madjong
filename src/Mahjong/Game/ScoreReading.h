#pragma once

#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Score.h>

namespace BECore::Mahjong {

    /// One set of a reading: a chow (`first` is its lowest tile), a pung or a kong (`first` is the tile). `concealed` says whether it
    /// was made without a claim: it decides the points of a pung or kong and, for the winner, the hand being fully concealed.
    struct ReadingSet {
        MeldKind kind = MeldKind::Pung;
        Tile first;
        bool concealed = false;
    };

    /**
     * @brief One way to read a hand before it is scored: sets and a pair.
     *
     * The winner's reading has four sets and a pair. For a seat that does not win the sets are the ones that stand (melds, and the
     * kinds held three or four times in the concealed tiles as concealed pungs) and `pair` is the best scoring pair, invalid when none
     * scores. Several readings of one hand are possible; `ScoreSeat` scores each and keeps the best.
     */
    struct Reading {
        eastl::vector<ReadingSet> sets;
        Tile pair;
    };

    /**
     * @brief Every reading of the hand of `input`, never empty.
     *
     * - The winner: the melds are sets as they are (`ReadingSet::concealed` is the meld's flag, so a claimed or an upgraded set is
     *   open); the concealed tiles go through `Decompose` and every decomposition is a candidate. When the win came from a discard or
     *   a robbed kong the set that holds the winning tile is a claimed set, open, and the winning tile may sit in several sets or the
     *   pair of one decomposition: each placement is a reading of its own. A self-draw gives one reading per decomposition, every set
     *   of the concealed tiles concealed. Four equal concealed tiles that were never declared a kong decompose as a pung and a single.
     * - A winner whose hand is not four sets and a pair (seven pairs, thirteen orphans, or a test input that is no win) is read like a
     *   seat that does not win.
     * - A seat that does not win: one reading from what stands, see `Reading`. Chows in the concealed tiles score nothing and are not
     *   looked for.
     */
    [[nodiscard]] eastl::vector<Reading> BuildReadings(const ScoreInput& input);

    /// A reading of four sets and a pair: the shape of a win by sets.
    [[nodiscard]] inline bool IsCompleteReading(const Reading& reading) noexcept {
        return reading.sets.size() == static_cast<size_t>(kSetsInHand) && reading.pair.IsValid();
    }

    [[nodiscard]] inline bool IsGroupSet(const ReadingSet& set) noexcept {
        return set.kind != MeldKind::Chow;
    }

    [[nodiscard]] inline bool IsWindGroup(const ReadingSet& set) noexcept {
        return IsGroupSet(set) && set.first.IsWind();
    }

    [[nodiscard]] inline bool IsDragonGroup(const ReadingSet& set) noexcept {
        return IsGroupSet(set) && set.first.IsDragon();
    }

    [[nodiscard]] inline bool IsKong(const ReadingSet& set) noexcept {
        return set.kind == MeldKind::Kong;
    }

    /// How many sets of `reading` `isWanted` accepts.
    [[nodiscard]] inline int CountSets(const Reading& reading, bool (*isWanted)(const ReadingSet&)) noexcept {
        int count = 0;
        for (const ReadingSet& set : reading.sets) {
            count += isWanted(set) ? 1 : 0;
        }
        return count;
    }

    /// The tile of the wind `wind` (the numbering of `Wind` and of the winds of `Tile` is the same).
    [[nodiscard]] constexpr Tile WindTile(Wind wind) noexcept {
        return Tile::FromWind(static_cast<int>(wind));
    }

    /// Appends a line to `breakdown` (the one way the line families make lines).
    inline void AddLine(ScoreBreakdown& breakdown, ScoreKind kind, Tile tile, int points, int doubles) {
        breakdown.lines.push_back({kind, tile, points, doubles});
    }

    /// The points of a pair: `kPairPoints` for each of dragons, the own wind and the round wind it is (0 for any other tile).
    [[nodiscard]] int PairPoints(Tile pair, Wind seatWind, Wind roundWind) noexcept;

}  // namespace BECore::Mahjong
