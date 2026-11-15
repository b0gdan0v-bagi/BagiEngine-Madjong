#include "FixedWall.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/TileSet.h>

namespace BECore::Mahjong {

    namespace {

        /// Collects the problems and the count of every kind still unused.
        class SetCounter {
        public:
            SetCounter(bool flowers, Vector<FixedDealProblem>& problems) : _flowers(flowers), _problems(problems) {
                for (uint8_t id = 0; id < kTileIdCount; ++id) {
                    const Tile tile{id};
                    _left[id] = tile.IsBonus() ? (flowers ? 1 : 0) : kCopiesPerTile;
                }
            }

            void Take(Tile tile, const String& member) {
                if (--_left[tile.Id()] < 0) {
                    _problems.push_back(
                        {member, Format("uses more copies of {} than the set has{}", ToString(tile), tile.IsBonus() && !_flowers ? " (the set has no bonus tiles: flowers is off)" : "")});
                    _left[tile.Id()] = 0;
                }
            }

            [[nodiscard]] int Left(uint8_t id) const {
                return _left[id];
            }

        private:
            bool _flowers;
            Vector<FixedDealProblem>& _problems;
            Array<int, kTileIdCount> _left{};
        };

        /// The playable tiles of a list; an invalid tile is a problem and is left out.
        Vector<Tile> ValidTiles(const Vector<Tile>& tiles, const String& member, Vector<FixedDealProblem>& problems) {
            Vector<Tile> valid;
            valid.reserve(tiles.size());
            for (size_t i = 0; i < tiles.size(); ++i) {
                if (tiles[i].IsValid()) {
                    valid.push_back(tiles[i]);
                } else {
                    problems.push_back({member, Format("tile {} is not a tile", i)});
                }
            }
            return valid;
        }

        String HandMember(Seat seat) {
            return Format("hands[{}]", seat);
        }

    }  // namespace

    FixedWallBuild BuildFixedWall(const FixedDeal& deal, bool flowers) {
        FixedWallBuild build;
        Vector<FixedDealProblem>& problems = build.problems;

        Seat east = deal.east;
        if (!IsSeat(east)) {
            problems.push_back({"east", Format("{} is not a seat", east)});
            east = 0;
        }

        SetCounter set(flowers, problems);
        Array<Vector<Tile>, kSeatCount> hands;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const String member = HandMember(seat);
            if (deal.hands[seat].size() != static_cast<size_t>(kDealtTiles)) {
                problems.push_back({member, Format("needs {} tiles, has {}", kDealtTiles, deal.hands[seat].size())});
            }
            hands[seat] = ValidTiles(deal.hands[seat], member, problems);
            for (const Tile tile : hands[seat]) {
                set.Take(tile, member);
            }
        }
        const Vector<Tile> live = ValidTiles(deal.live, "live", problems);
        const Vector<Tile> loose = ValidTiles(deal.loose, "loose", problems);
        for (const Tile tile : live) {
            set.Take(tile, "live");
        }
        for (const Tile tile : loose) {
            set.Take(tile, "loose");
        }

        // Unused tiles in the order of the set: the front pads, the last 14 are the dead wall.
        Vector<Tile> pool;
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            pool.insert(pool.end(), static_cast<size_t>(set.Left(id)), Tile{id});
        }
        size_t nextPad = 0;
        bool padRanOut = false;
        const auto pad = [&pool, &nextPad, &padRanOut, &problems]() {
            if (nextPad >= pool.size()) {
                if (!padRanOut) {
                    problems.push_back({"", "the set has no unused tiles left to pad with"});
                    padRanOut = true;
                }
                return Tile{static_cast<uint8_t>(0)};
            }
            return pool[nextPad++];
        };

        Vector<Tile> order;
        // The reserve; the tiles after the first two refill it from the live tail.
        Vector<Tile> refills;
        for (size_t i = 0; i < loose.size(); ++i) {
            if (i < kLooseTiles) {
                order.push_back(loose[i]);
            } else {
                refills.push_back(loose[i]);
            }
        }
        while (order.size() < kLooseTiles) {
            order.push_back(pad());
        }

        // The deal, in the order DealHands takes the tiles: four at a time from East, three times, then one each.
        const auto dealt = [&](Seat seat, int from, int count) {
            for (int i = from; i < from + count; ++i) {
                const size_t index = static_cast<size_t>(i);
                order.push_back(index < hands[seat].size() ? hands[seat][index] : pad());
            }
        };
        for (int round = 0; round < kDealBlockRounds; ++round) {
            for (int step = 0; step < kSeatCount; ++step) {
                dealt((east + step) % kSeatCount, round * kDealBlock, kDealBlock);
            }
        }
        for (int step = 0; step < kSeatCount; ++step) {
            dealt((east + step) % kSeatCount, kDealBlockRounds * kDealBlock, 1);
        }

        // The live wall after the deal: `live`, padding, and the refills at the very end.
        const size_t ringSize = static_cast<size_t>(RingSize(StacksPerSide(flowers)));
        const size_t dealtSize = order.size();
        const size_t liveNeeded = live.size() + refills.size();
        const size_t liveAvailable = ringSize > dealtSize + kDeadWallTiles ? ringSize - dealtSize - kDeadWallTiles : 0;
        size_t liveWanted = liveAvailable;
        if (deal.liveAfterDeal.has_value()) {
            const int cut = *deal.liveAfterDeal;
            if (cut < 0 || static_cast<size_t>(cut) < liveNeeded || static_cast<size_t>(cut) > liveAvailable) {
                problems.push_back(
                    {"liveAfterDeal", Format("{} is outside {}..{} (`live` and the refills at least, the ring after the deal and the dead wall at most)", cut, liveNeeded, liveAvailable)});
                const size_t cutSize = cut < 0 ? 0 : static_cast<size_t>(cut);
                liveWanted = eastl::max(liveNeeded, eastl::min(cutSize, liveAvailable));
            } else {
                liveWanted = static_cast<size_t>(cut);
            }
        } else if (liveWanted < liveNeeded) {
            problems.push_back({"live", "`live` and the refills do not fit the live wall"});
        }
        for (const Tile tile : live) {
            order.push_back(tile);
        }
        const size_t padTo = dealtSize + eastl::max(liveWanted, liveNeeded) - refills.size();
        while (order.size() < padTo) {
            order.push_back(pad());
        }
        for (auto it = refills.rbegin(); it != refills.rend(); ++it) {
            order.push_back(*it);
        }

        if (pool.size() - nextPad < kDeadWallTiles) {
            problems.push_back({"", "the set has too few unused tiles for the dead wall"});
        }
        const size_t deadBegin = pool.size() >= kDeadWallTiles ? pool.size() - kDeadWallTiles : 0;
        for (size_t i = deadBegin; i < pool.size(); ++i) {
            order.push_back(pool[i]);
        }
        build.wall = Wall::FromOrder(eastl::move(order));
        return build;
    }

    std::expected<Wall, String> TryBuildFixedWall(const FixedDeal& deal, bool flowers) {
        FixedWallBuild build = BuildFixedWall(deal, flowers);
        if (!build.problems.empty()) {
            const FixedDealProblem& first = build.problems.front();
            return std::unexpected(first.member.empty() ? Format("deal: {}", first.what) : Format("deal.{}: {}", first.member, first.what));
        }
        return eastl::move(build.wall);
    }

}  // namespace BECore::Mahjong
