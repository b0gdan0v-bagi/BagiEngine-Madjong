#include "HandShape.h"

namespace BECore::Mahjong {

    namespace {

        /// How far apart (in ranks) two tiles of one set may be: a chow `1 3` reaches over one rank.
        constexpr int kChowReachRanks = 2;

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

        /// The number of different terminals and honours `counts` holds.
        int CountOrphanKinds(const TileCounts& counts) noexcept {
            int kinds = 0;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                kinds += counts[id] > 0 && IsOrphan(Tile{id}) ? 1 : 0;
            }
            return kinds;
        }

        /// Calls `visit(tile)` for each kind that completes the waiting hand `counts` (the rules of `WinningTiles`), in the order of the
        /// ids; `visit` returns true to stop. Returns true when stopped. Only kinds next to a held tile are probed (`HasTileNearby`); a
        /// hand with no melds and 12 or more kinds of orphans can still become thirteen orphans with a kind it does not hold, so
        /// there every orphan is probed as well.
        template <class Visit>
        bool VisitWinningKinds(const TileCounts& counts, int meldCount, const TileCounts& meldTiles, Visit&& visit) {
            if (meldCount < 0 || meldCount > kSetsInHand || TotalTiles(counts) != kWinningHandTiles - 1 - meldCount * kTilesPerSet) {
                return false;
            }
            const bool orphansPossible = meldCount == 0 && CountOrphanKinds(counts) >= kOrphanKinds - 1;
            TileCounts probe = counts;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (counts[id] + meldTiles[id] >= kCopiesPerTile || !(HasTileNearby(counts, id) || (orphansPossible && IsOrphan(Tile{id})))) {
                    continue;
                }
                ++probe[id];
                const bool wins = IsWinningShape(probe, meldCount);
                --probe[id];
                if (wins && visit(Tile{id})) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    bool HasTileNearby(const TileCounts& counts, uint8_t id) noexcept {
        if (counts[id] > 0) {
            return true;
        }
        const Tile tile{id};
        if (!tile.IsSuit()) {
            return false;
        }
        const int rank = tile.Rank();
        for (int distance = -kChowReachRanks; distance <= kChowReachRanks; ++distance) {
            const int other = rank + distance;
            if (distance != 0 && other >= 1 && other <= kRanksPerSuit && counts[id + distance] > 0) {
                return true;
            }
        }
        return false;
    }

    TileCounts CountTiles(const eastl::vector<Tile>& tiles) noexcept {
        TileCounts counts{};
        for (const Tile tile : tiles) {
            if (tile.IsPlayable()) {
                ++counts[tile.Id()];
            }
        }
        return counts;
    }

    TileCounts CountMeldTiles(const eastl::vector<Meld>& melds) noexcept {
        TileCounts counts{};
        for (const Meld& meld : melds) {
            if (!meld.first.IsPlayable()) {
                continue;
            }
            if (meld.kind == MeldKind::Chow) {
                // A chow is three consecutive ranks of one suit, so its lowest tile is a 1..7; anything else is no chow and holds nothing.
                if (!meld.first.IsSuit() || meld.first.Rank() > kLastChowStartRank) {
                    continue;
                }
                for (const Tile member : ChowTiles(meld.first)) {
                    ++counts[member.Id()];
                }
            } else {
                counts[meld.first.Id()] += meld.kind == MeldKind::Kong ? kCopiesPerTile : kTilesPerSet;
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
        int pairs = 0;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            if (counts[id] == 0) {
                continue;
            }
            if (counts[id] != kTilesInPair) {
                return false;
            }
            ++pairs;
            const Tile tile{id};
            allHonours = allHonours && tile.IsHonour();
            if (!tile.IsSuit() || (suit != TileSuit::None && tile.Suit() != suit)) {
                oneSuit = false;
            }
            suit = tile.IsSuit() ? tile.Suit() : suit;
        }
        return pairs == kPairsInSevenPairs && (allHonours || oneSuit);
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
        return WinningTiles(counts, meldCount, TileCounts{});
    }

    eastl::vector<Tile> WinningTiles(const TileCounts& counts, int meldCount, const TileCounts& meldTiles) {
        eastl::vector<Tile> tiles;
        VisitWinningKinds(counts, meldCount, meldTiles, [&](Tile tile) {
            tiles.push_back(tile);
            return false;
        });
        return tiles;
    }

    bool IsWaiting(const TileCounts& counts, int meldCount, const TileCounts& meldTiles) {
        return VisitWinningKinds(counts, meldCount, meldTiles, [](Tile) { return true; });
    }

    eastl::vector<Tile> WinningTiles(const PlayerHand& hand) {
        return WinningTiles(CountTiles(hand.concealed), static_cast<int>(hand.melds.size()), CountMeldTiles(hand.melds));
    }

}  // namespace BECore::Mahjong
