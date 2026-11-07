#include "Score.h"

#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/ScoreReading.h>

namespace BECore::Mahjong {

    namespace {

        /// The number of a flower or a season, 0..3 (the wind of the same number is East..North).
        int BonusNumber(Tile bonus) noexcept {
            static_assert(kFlowerCount == kSeasonCount && kFlowerCount == kWindCount, "a bonus tile is numbered like a wind");
            return bonus.Id() < kFirstSeasonId ? bonus.Id() - kFirstFlowerId : bonus.Id() - kFirstSeasonId;
        }

        void AddLine(ScoreBreakdown& breakdown, ScoreKind kind, Tile tile, int points, int doubles) {
            breakdown.lines.push_back({kind, tile, points, doubles});
        }

        // ----- points -----

        /// The points of a pung or kong (a chow scores nothing).
        int SetPoints(const ReadingSet& set) noexcept {
            int points = kOpenSimplePungPoints;
            points *= set.first.IsSimple() ? 1 : kHonourPointsFactor;
            points *= set.concealed ? kConcealedPointsFactor : 1;
            points *= set.kind == MeldKind::Kong ? kKongPointsFactor : 1;
            return points;
        }

        void AddSetPoints(const Reading& reading, ScoreBreakdown& breakdown) {
            for (const ReadingSet& set : reading.sets) {
                if (set.kind != MeldKind::Chow) {
                    AddLine(breakdown, set.kind == MeldKind::Kong ? ScoreKind::Kong : ScoreKind::Pung, set.first, SetPoints(set), 0);
                }
            }
        }

        void AddPairPoints(const Reading& reading, const ScoreInput& input, ScoreBreakdown& breakdown) {
            const int points = PairPoints(reading.pair, input.seatWind, input.roundWind);
            if (points > 0) {
                AddLine(breakdown, ScoreKind::Pair, reading.pair, points, 0);
            }
        }

        void AddBonusPoints(const ScoreInput& input, ScoreBreakdown& breakdown) {
            for (const Tile bonus : input.hand.bonus) {
                AddLine(breakdown, ScoreKind::Bonus, bonus, kBonusPoints, 0);
            }
        }

        // ----- doubles for everyone -----

        /// A pung or a kong of dragons, of the own wind, of the round wind: a line per set and per reason (a set of the East wind of
        /// an East seat in the East round has two).
        void AddSetDoubles(const Reading& reading, const ScoreInput& input, ScoreBreakdown& breakdown) {
            for (const ReadingSet& set : reading.sets) {
                if (set.kind == MeldKind::Chow) {
                    continue;
                }
                if (set.first.IsDragon()) {
                    AddLine(breakdown, ScoreKind::DragonSet, set.first, 0, kDragonSetDoubles);
                }
                if (set.first == WindTile(input.seatWind)) {
                    AddLine(breakdown, ScoreKind::OwnWindSet, set.first, 0, kOwnWindSetDoubles);
                }
                if (set.first == WindTile(input.roundWind)) {
                    AddLine(breakdown, ScoreKind::RoundWindSet, set.first, 0, kRoundWindSetDoubles);
                }
            }
        }

        /// One suit with honours or without: read on every tile the seat holds, the concealed ones and those of the melds. Bonus tiles
        /// are not looked at.
        void AddOneSuitDoubles(const ScoreInput& input, ScoreBreakdown& breakdown) {
            TileSuit suit = TileSuit::None;
            bool oneSuit = true;
            bool honours = false;
            auto look = [&](Tile tile) {
                if (tile.IsHonour()) {
                    honours = true;
                } else if (tile.IsSuit()) {
                    oneSuit = oneSuit && (suit == TileSuit::None || suit == tile.Suit());
                    suit = tile.Suit();
                }
            };
            for (const Tile tile : input.hand.concealed) {
                look(tile);
            }
            for (const Meld& meld : input.hand.melds) {
                look(meld.first);
            }
            if (suit == TileSuit::None || !oneSuit) {
                return;
            }
            if (honours) {
                AddLine(breakdown, ScoreKind::OneSuitWithHonours, Tile{}, 0, kOneSuitWithHonoursDoubles);
            } else {
                AddLine(breakdown, ScoreKind::OneSuitNoHonours, Tile{}, 0, kOneSuitNoHonoursDoubles);
            }
        }

        /// A flower or a season of the own wind's number, of the round wind's number: a line per tile and per reason.
        void AddBonusDoubles(const ScoreInput& input, ScoreBreakdown& breakdown) {
            for (const Tile bonus : input.hand.bonus) {
                if (BonusNumber(bonus) == static_cast<int>(input.seatWind)) {
                    AddLine(breakdown, ScoreKind::OwnBonus, bonus, 0, kOwnBonusDoubles);
                }
                if (BonusNumber(bonus) == static_cast<int>(input.roundWind)) {
                    AddLine(breakdown, ScoreKind::RoundBonus, bonus, 0, kRoundBonusDoubles);
                }
            }
        }

        // ----- totals -----

        /// `points` x 2^`doubles`, 64-bit inside and cut off at the largest `int`.
        int ShiftSaturating(int points, int doubles) noexcept {
            constexpr int64_t kMax = std::numeric_limits<int>::max();
            int64_t value = points;
            for (int step = 0; step < doubles && value < kMax; ++step) {
                value *= 2;
            }
            return static_cast<int>(value < kMax ? value : kMax);
        }

        void Total(const ScoreInput& input, ScoreBreakdown& breakdown) {
            for (const ScoreLine& line : breakdown.lines) {
                breakdown.points += line.points;
                breakdown.doubles += line.doubles;
            }
            breakdown.raw = ShiftSaturating(breakdown.points, breakdown.doubles);
            breakdown.score = RoundUpToTen(breakdown.raw < input.limit ? breakdown.raw : input.limit);
        }

        /// The score of one reading: all its lines.
        ScoreBreakdown ScoreReading(const ScoreInput& input, const Reading& reading) {
            ScoreBreakdown breakdown;
            breakdown.won = input.won;
            AddSetPoints(reading, breakdown);
            AddPairPoints(reading, input, breakdown);
            AddBonusPoints(input, breakdown);
            AddSetDoubles(reading, input, breakdown);
            AddOneSuitDoubles(input, breakdown);
            AddBonusDoubles(input, breakdown);
            Total(input, breakdown);
            return breakdown;
        }

    }  // namespace

    ScoreBreakdown ScoreSeat(const ScoreInput& input) {
        ScoreBreakdown best;
        bool first = true;
        for (const Reading& reading : BuildReadings(input)) {
            ScoreBreakdown breakdown = ScoreReading(input, reading);
            if (first || breakdown.raw > best.raw) {
                best = eastl::move(breakdown);
                first = false;
            }
        }
        return best;
    }

}  // namespace BECore::Mahjong
