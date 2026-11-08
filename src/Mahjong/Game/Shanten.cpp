#include "Shanten.h"

namespace BECore::Mahjong {

    namespace {

        /// The roles the lowest tile of the search can play, in the order they are tried. The copies of one kind take roles in
        /// non-decreasing order, so two orders of the same roles are not walked twice.
        enum class Role : uint8_t { Pung, Chow, Pair, PartialPair, PartialNeighbour, PartialGap, Isolated };

        constexpr int kMostSetsNeeded = kSetsInHand;
        /// Two tiles that reach a chow over one rank (1 3, 5 7) and two that are next to each other (4 5).
        constexpr int kGapRanks = 2;
        constexpr int kNeighbourRanks = 1;

        /// The search over the readings of `held` into sets, partial sets and a pair. Works on its own copy of the counts.
        class Searcher {
        public:
            Searcher(const TileCounts& counts, int needed) : _needed(needed) {
                for (uint8_t id = 0; id < kKindCount; ++id) {
                    _held[id] = static_cast<int8_t>(counts[id]);
                }
            }

            int Run() {
                Walk(0, Role::Pung, 0, 0, 0);
                return _best;
            }

        private:
            /// Whether the search can stop: nothing is lower than a complete hand.
            [[nodiscard]] bool Done() const noexcept {
                return _best <= kShantenComplete;
            }

            /// The lowest kind that is still held, from `id` on (kKindCount when none); the role restarts when the kind changes.
            [[nodiscard]] int NextKind(int id, Role& role) const noexcept {
                while (id < kKindCount && _held[id] == 0) {
                    ++id;
                    role = Role::Pung;
                }
                return id;
            }

            [[nodiscard]] bool CanStartChow(int id, int span) const noexcept {
                const Tile tile{static_cast<uint8_t>(id)};
                return tile.IsSuit() && tile.Rank() + span <= kRanksPerSuit;
            }

            void Walk(int id, Role role, int sets, int partials, int pair) {
                if (Done()) {
                    return;
                }
                id = NextKind(id, role);
                if (id == kKindCount) {
                    // A set found after the partial sets can leave more of them than the hand has room for: only the room counts.
                    const int usefulPartials = partials < _needed - sets ? partials : _needed - sets;
                    const int value = 2 * _needed - 2 * sets - usefulPartials - pair;
                    _best = value < _best ? value : _best;
                    return;
                }
                const int blocks = sets + partials;
                const int8_t copies = _held[id];

                if (role <= Role::Pung && copies >= kTilesPerSet && sets < _needed) {
                    _held[id] -= kTilesPerSet;
                    Walk(id, Role::Pung, sets + 1, partials, pair);
                    _held[id] += kTilesPerSet;
                }
                const bool chowFits = CanStartChow(id, kTilesPerSet - 1) && _held[id + 1] > 0 && _held[id + kGapRanks] > 0;
                if (role <= Role::Chow && sets < _needed && chowFits) {
                    TakeChow(id, -1);
                    Walk(id, Role::Chow, sets + 1, partials, pair);
                    TakeChow(id, 1);
                }
                if (role <= Role::Pair && pair == 0 && copies >= kTilesInPair) {
                    _held[id] -= kTilesInPair;
                    Walk(id, Role::Pair, sets, partials, 1);
                    _held[id] += kTilesInPair;
                }
                if (blocks < _needed) {
                    WalkPartials(id, role, sets, partials, pair);
                }
                if (role <= Role::Isolated) {
                    --_held[id];
                    Walk(id, Role::Isolated, sets, partials, pair);
                    ++_held[id];
                }
            }

            /// The branches that take a partial set; the caller has checked that one more block is wanted.
            void WalkPartials(int id, Role role, int sets, int partials, int pair) {
                if (role <= Role::PartialPair && _held[id] >= kTilesInPair) {
                    _held[id] -= kTilesInPair;
                    Walk(id, Role::PartialPair, sets, partials + 1, pair);
                    _held[id] += kTilesInPair;
                }
                if (role <= Role::PartialNeighbour && CanStartChow(id, kNeighbourRanks) && _held[id + kNeighbourRanks] > 0) {
                    TakePartial(id, kNeighbourRanks, -1);
                    Walk(id, Role::PartialNeighbour, sets, partials + 1, pair);
                    TakePartial(id, kNeighbourRanks, 1);
                }
                if (role <= Role::PartialGap && CanStartChow(id, kGapRanks) && _held[id + kGapRanks] > 0) {
                    TakePartial(id, kGapRanks, -1);
                    Walk(id, Role::PartialGap, sets, partials + 1, pair);
                    TakePartial(id, kGapRanks, 1);
                }
            }

            /// Takes (`delta` -1) or puts back (+1) the three tiles of the chow that starts at `id`.
            void TakeChow(int id, int delta) noexcept {
                for (int step = 0; step < kTilesPerSet; ++step) {
                    _held[id + step] = static_cast<int8_t>(_held[id + step] + delta);
                }
            }

            /// Takes or puts back the tile `id` and the one `distance` ranks above it.
            void TakePartial(int id, int distance, int delta) noexcept {
                _held[id] = static_cast<int8_t>(_held[id] + delta);
                _held[id + distance] = static_cast<int8_t>(_held[id + distance] + delta);
            }

            eastl::array<int8_t, kKindCount> _held{};
            int _needed = 0;
            int _best = 2 * kMostSetsNeeded;
        };

        int ClampMeldCount(int meldCount) noexcept {
            return meldCount < 0 ? 0 : (meldCount > kSetsInHand ? kSetsInHand : meldCount);
        }

        /// Whether a tile of kind `id` could join a set or a pair of the held tiles: the same kind, or the same suit within two ranks.
        bool TouchesHeldTile(const TileCounts& counts, uint8_t id) noexcept {
            if (counts[id] > 0) {
                return true;
            }
            const Tile tile{id};
            if (!tile.IsSuit()) {
                return false;
            }
            const int rank = tile.Rank();
            for (int distance = -kGapRanks; distance <= kGapRanks; ++distance) {
                const int other = rank + distance;
                if (distance != 0 && other >= 1 && other <= kRanksPerSuit && counts[id + distance] > 0) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    int Shanten(const TileCounts& counts, int meldCount) {
        const int needed = kSetsInHand - ClampMeldCount(meldCount);
        return Searcher(counts, needed).Run();
    }

    eastl::vector<Tile> ImprovingTiles(const TileCounts& counts, int meldCount) {
        eastl::vector<Tile> improving;
        const int current = Shanten(counts, meldCount);
        if (current <= kShantenComplete) {
            return improving;
        }
        TileCounts probe = counts;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            if (!TouchesHeldTile(counts, id)) {
                continue;
            }
            ++probe[id];
            if (Shanten(probe, meldCount) < current) {
                improving.push_back(Tile{id});
            }
            --probe[id];
        }
        return improving;
    }

}  // namespace BECore::Mahjong
