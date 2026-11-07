#include "HandShape.h"

namespace BECore::Mahjong {

    namespace {

        /// The highest rank a chow may start at (7 8 9).
        constexpr int kLastChowStartRank = kRanksPerSuit - kTilesPerSet + 1;

        bool IsOrphan(Tile tile) noexcept {
            return tile.IsTerminal() || tile.IsHonour();
        }

        /// Walks the splits of `counts` into sets only (nothing may be left over), from the lowest kind upwards. `sets` is the stack of
        /// the sets chosen so far; `visit(sets)` is called for each complete split and returns true to stop the walk. Returns true when
        /// stopped. The lowest tile can only sit in a set that starts at it: at most one pung and, for every other copy, a chow, so each
        /// split is met exactly once.
        template <class Visit>
        bool VisitSplits(TileCounts& counts, uint8_t from, eastl::vector<ShapeSet>& sets, Visit& visit) {
            uint8_t id = from;
            while (id < kKindCount && counts[id] == 0) {
                ++id;
            }
            if (id == kKindCount) {
                return visit(sets);
            }

            const Tile tile{id};
            const int held = counts[id];
            const size_t stackSize = sets.size();
            for (int pung = 0; pung <= 1; ++pung) {
                const int chows = held - pung * kTilesPerSet;
                if (chows < 0) {
                    break;
                }
                const bool chowsFit = chows == 0 || (tile.IsSuit() && tile.Rank() <= kLastChowStartRank && counts[id + 1] >= chows && counts[id + 2] >= chows);
                if (!chowsFit) {
                    continue;
                }

                counts[id] = 0;
                if (chows > 0) {
                    counts[id + 1] -= static_cast<uint8_t>(chows);
                    counts[id + 2] -= static_cast<uint8_t>(chows);
                }
                if (pung == 1) {
                    sets.push_back({MeldKind::Pung, tile});
                }
                for (int i = 0; i < chows; ++i) {
                    sets.push_back({MeldKind::Chow, tile});
                }

                const bool stop = VisitSplits(counts, static_cast<uint8_t>(id + 1), sets, visit);

                sets.resize(stackSize);
                counts[id] = static_cast<uint8_t>(held);
                if (chows > 0) {
                    counts[id + 1] += static_cast<uint8_t>(chows);
                    counts[id + 2] += static_cast<uint8_t>(chows);
                }
                if (stop) {
                    return true;
                }
            }
            return false;
        }

        /// Calls `visit(pair, sets)` for every decomposition of `counts` into `setsNeeded` sets and a pair; `visit` returns true to
        /// stop. Returns true when stopped.
        template <class Visit>
        bool VisitDecompositions(TileCounts counts, int setsNeeded, Visit&& visit) {
            if (setsNeeded < 0 || setsNeeded > kSetsInHand || TotalTiles(counts) != setsNeeded * kTilesPerSet + kTilesInPair) {
                return false;
            }
            eastl::vector<ShapeSet> sets;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (counts[id] < kTilesInPair) {
                    continue;
                }
                counts[id] -= kTilesInPair;
                auto withPair = [&](const eastl::vector<ShapeSet>& found) { return visit(Tile{id}, found); };
                const bool stop = VisitSplits(counts, 0, sets, withPair);
                counts[id] += kTilesInPair;
                if (stop) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    TileCounts CountTiles(const eastl::vector<Tile>& tiles) noexcept {
        TileCounts counts{};
        for (const Tile tile : tiles) {
            if (tile.IsPlayable()) {
                ++counts[tile.Id()];
            }
        }
        return counts;
    }

    int TotalTiles(const TileCounts& counts) noexcept {
        int total = 0;
        for (const uint8_t count : counts) {
            total += count;
        }
        return total;
    }

    eastl::vector<Decomposition> Decompose(const TileCounts& counts, int setsNeeded) {
        eastl::vector<Decomposition> found;
        VisitDecompositions(counts, setsNeeded, [&](Tile pair, const eastl::vector<ShapeSet>& sets) {
            found.push_back({pair, sets});
            return false;
        });
        return found;
    }

    bool IsThirteenOrphans(const TileCounts& counts) noexcept {
        if (TotalTiles(counts) != kWinningHandTiles) {
            return false;
        }
        for (uint8_t id = 0; id < kKindCount; ++id) {
            if (IsOrphan(Tile{id}) != (counts[id] > 0)) {
                return false;
            }
        }
        return true;
    }

    bool IsSevenPairs(const TileCounts& counts) noexcept {
        if (TotalTiles(counts) != kWinningHandTiles) {
            return false;
        }
        bool allHonours = true;
        bool oneSuit = true;
        TileSuit suit = TileSuit::None;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            if (counts[id] == 0) {
                continue;
            }
            if (counts[id] != kTilesInPair) {
                return false;
            }
            const Tile tile{id};
            allHonours = allHonours && tile.IsHonour();
            if (!tile.IsSuit() || (suit != TileSuit::None && tile.Suit() != suit)) {
                oneSuit = false;
            }
            suit = tile.IsSuit() ? tile.Suit() : suit;
        }
        return allHonours || oneSuit;
    }

    bool IsWinningShape(const TileCounts& counts, int meldCount) {
        if (meldCount < 0 || meldCount > kSetsInHand) {
            return false;
        }
        if (meldCount == 0 && (IsThirteenOrphans(counts) || IsSevenPairs(counts))) {
            return true;
        }
        return VisitDecompositions(counts, kSetsInHand - meldCount, [](Tile, const eastl::vector<ShapeSet>&) { return true; });
    }

    eastl::vector<Tile> WinningTiles(const TileCounts& counts, int meldCount) {
        eastl::vector<Tile> tiles;
        if (meldCount < 0 || meldCount > kSetsInHand || TotalTiles(counts) != kWinningHandTiles - 1 - meldCount * kTilesPerSet) {
            return tiles;
        }
        TileCounts probe = counts;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            if (counts[id] >= kCopiesPerTile) {
                continue;
            }
            ++probe[id];
            if (IsWinningShape(probe, meldCount)) {
                tiles.push_back(Tile{id});
            }
            --probe[id];
        }
        return tiles;
    }

}  // namespace BECore::Mahjong
