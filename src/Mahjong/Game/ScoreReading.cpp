#include "ScoreReading.h"

#include <Mahjong/Game/HandShape.h>

namespace BECore::Mahjong {

    namespace {

        bool HoldsTile(const ReadingSet& set, Tile tile) noexcept {
            if (set.kind != MeldKind::Chow) {
                return set.first == tile;
            }
            for (const Tile member : ChowTiles(set.first)) {
                if (member == tile) {
                    return true;
                }
            }
            return false;
        }

        eastl::vector<ReadingSet> MeldSets(const PlayerHand& hand) {
            eastl::vector<ReadingSet> sets;
            for (const Meld& meld : hand.melds) {
                sets.push_back({meld.kind, meld.first, meld.concealed});
            }
            return sets;
        }

        /// The readings of a decomposition of the winner's concealed tiles: one per place the winning tile may sit when a claim made the
        /// win (that set is open), one for a self-draw. A winning tile found nowhere (it is not in the hand) gives the plain reading.
        void AddDecompositionReadings(const ScoreInput& input, const eastl::vector<ReadingSet>& melds, const Decomposition& found, eastl::vector<Reading>& readings) {
            Reading base;
            base.sets = melds;
            for (const ShapeSet& set : found.sets) {
                base.sets.push_back({set.kind, set.first, true});
            }
            base.pair = found.pair;

            const Tile winning = input.result.winningTile;
            const bool claimed = input.result.source != WinSource::SelfDraw;
            const size_t firstConcealedSet = melds.size();
            bool placed = false;
            if (claimed && winning.IsValid()) {
                for (size_t index = firstConcealedSet; index < base.sets.size(); ++index) {
                    if (HoldsTile(base.sets[index], winning)) {
                        Reading reading = base;
                        reading.sets[index].concealed = false;
                        readings.push_back(eastl::move(reading));
                        placed = true;
                    }
                }
                // The winning tile as the pair completes no set: every set stays concealed.
                if (found.pair == winning) {
                    readings.push_back(base);
                    placed = true;
                }
            }
            if (!placed) {
                readings.push_back(eastl::move(base));
            }
        }

        /// The reading of a hand as it stands (a seat that does not win): see `Reading`.
        Reading ReadStandingHand(const ScoreInput& input) {
            Reading reading;
            reading.sets = MeldSets(input.hand);
            const TileCounts counts = CountTiles(input.hand.concealed);
            int bestPairPoints = 0;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                const Tile tile{id};
                if (counts[id] >= kTilesPerSet) {
                    reading.sets.push_back({MeldKind::Pung, tile, true});
                } else if (counts[id] == kTilesInPair) {
                    const int points = PairPoints(tile, input.seatWind, input.roundWind);
                    if (points > bestPairPoints) {
                        bestPairPoints = points;
                        reading.pair = tile;
                    }
                }
            }
            return reading;
        }

    }  // namespace

    SuitSummary SummariseSuits(const PlayerHand& hand) noexcept {
        SuitSummary summary;
        const auto look = [&summary](Tile tile) {
            if (tile.IsHonour()) {
                summary.honours = true;
            } else if (tile.IsSuit()) {
                summary.mixed = summary.mixed || (summary.suit != TileSuit::None && summary.suit != tile.Suit());
                summary.suit = tile.Suit();
            }
        };
        for (const Tile tile : hand.concealed) {
            look(tile);
        }
        for (const Meld& meld : hand.melds) {
            look(meld.first);
        }
        return summary;
    }

    int PairPoints(Tile pair, Wind seatWind, Wind roundWind) noexcept {
        int points = 0;
        if (pair.IsDragon()) {
            points += kPairPoints;
        }
        if (pair.IsWind()) {
            points += pair == WindTile(seatWind) ? kPairPoints : 0;
            points += pair == WindTile(roundWind) ? kPairPoints : 0;
        }
        return points;
    }

    eastl::vector<Reading> BuildReadings(const ScoreInput& input) {
        eastl::vector<Reading> readings;
        if (input.won) {
            const eastl::vector<ReadingSet> melds = MeldSets(input.hand);
            const int setsNeeded = kSetsInHand - static_cast<int>(melds.size());
            for (const Decomposition& found : Decompose(CountTiles(input.hand.concealed), setsNeeded)) {
                AddDecompositionReadings(input, melds, found, readings);
            }
        }
        if (readings.empty()) {
            readings.push_back(ReadStandingHand(input));
        }
        return readings;
    }

}  // namespace BECore::Mahjong
