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
     * scores. `ScoreSeat` scores each reading of a hand and keeps the best.
     */
    struct Reading {
        Vector<ReadingSet> sets;
        Tile pair;
    };

    /**
     * @brief Every reading of the hand of `input`, never empty.
     *
     * - The winner: the melds are sets as they are; every `Decompose` of the concealed tiles is a candidate. When the win came from a
     *   discard or a robbed kong the set that holds the winning tile is open, and the tile may sit in several sets or the pair of one
     *   decomposition: each placement is a reading of its own. A self-draw gives one reading per decomposition, all concealed. Four
     *   equal concealed tiles that were never declared a kong decompose as a pung and a single.
     * - A winner whose hand is not four sets and a pair (seven pairs, thirteen orphans, or a test input that is no win) is read like a
     *   seat that does not win.
     * - A seat that does not win: one reading from what stands, see `Reading`; chows are not looked for.
     */
    [[nodiscard]] Vector<Reading> BuildReadings(const ScoreInput& input);

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

    [[nodiscard]] inline int CountSets(const Reading& reading, bool (*isWanted)(const ReadingSet&)) noexcept {
        int count = 0;
        for (const ReadingSet& set : reading.sets) {
            count += isWanted(set) ? 1 : 0;
        }
        return count;
    }

    [[nodiscard]] constexpr Tile WindTile(Wind wind) noexcept {
        return Tile::FromWind(static_cast<int>(wind));
    }

    inline void AddLine(ScoreBreakdown& breakdown, ScoreKind kind, Tile tile, int points, int doubles) {
        breakdown.lines.push_back({kind, tile, points, doubles});
    }

    /// How the tiles of a hand lie in the suits (bonus tiles are not looked at): read by the one-suit doubles and the limit hands.
    struct SuitSummary {
        /// The suit when there is just one; with `mixed` the last suit seen; `TileSuit::None` only when there is no suit tile.
        TileSuit suit = TileSuit::None;
        bool mixed = false;
        bool honours = false;

        [[nodiscard]] bool IsOneSuit() const noexcept {
            return suit != TileSuit::None && !mixed;
        }
        [[nodiscard]] bool IsOneSuitNoHonours() const noexcept {
            return IsOneSuit() && !honours;
        }
    };

    /// Over the concealed tiles and the melds (a chow lies in one suit, so its first tile tells).
    [[nodiscard]] SuitSummary SummariseSuits(const PlayerHand& hand) noexcept;

    /// `kPairPoints` for each of dragons, the own wind and the round wind the pair is (0 for any other tile).
    [[nodiscard]] int PairPoints(Tile pair, Wind seatWind, Wind roundWind) noexcept;

}  // namespace BECore::Mahjong
