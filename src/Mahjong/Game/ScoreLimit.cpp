#include "ScoreLimit.h"

namespace BECore::Mahjong {

    namespace {

        using TilePredicate = bool (*)(Tile);

        /// The rank of the winning tile of the two limit hands that name a tile: 1 dot as the last tile, 5 dots as a loose tile.
        constexpr int kLastTileLimitRank = 1;
        constexpr int kLooseTileLimitRank = 5;

        /// Every tile the seat holds, per kind: the concealed tiles and the tiles of the melds (a chow holds its three, a kong four).
        TileCounts HeldTiles(const PlayerHand& hand) noexcept {
            TileCounts held = CountTiles(hand.concealed);
            const TileCounts inMelds = CountMeldTiles(hand.melds);
            for (uint8_t id = 0; id < kKindCount; ++id) {
                held[id] = static_cast<uint8_t>(held[id] + inMelds[id]);
            }
            return held;
        }

        bool EveryHeldTile(const TileCounts& held, TilePredicate isWanted) noexcept {
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (held[id] > 0 && !isWanted(Tile{id})) {
                    return false;
                }
            }
            return true;
        }

        bool AnyCompleteReading(const eastl::vector<Reading>& readings) noexcept {
            return eastl::any_of(readings.begin(), readings.end(), IsCompleteReading);
        }

        bool IsSevenPairsHand(const PlayerHand& hand) noexcept {
            return hand.melds.empty() && IsSevenPairs(CountTiles(hand.concealed));
        }

        bool IsThirteenOrphansHand(const PlayerHand& hand) noexcept {
            return hand.melds.empty() && IsThirteenOrphans(CountTiles(hand.concealed));
        }

        bool HasGroupOf(const Reading& reading, Tile tile) noexcept {
            return eastl::any_of(reading.sets.begin(), reading.sets.end(), [tile](const ReadingSet& set) { return IsGroupSet(set) && set.first == tile; });
        }

        // ----- hands made of sets: any complete reading that has them -----

        /// Four pungs or kongs, none open, of one suit (no honours, so the pair is of the suit too).
        bool HasConcealedPungsOneSuit(const eastl::vector<Reading>& readings, const PlayerHand& hand) {
            if (!SummariseSuits(hand).IsOneSuitNoHonours()) {
                return false;
            }
            return eastl::any_of(readings.begin(), readings.end(), [](const Reading& reading) {
                const bool allConcealed = eastl::all_of(reading.sets.begin(), reading.sets.end(), [](const ReadingSet& set) { return set.concealed; });
                return IsCompleteReading(reading) && CountSets(reading, IsGroupSet) == kSetsInHand && allConcealed;
            });
        }

        /// Only green tiles in pungs or kongs and a pair: a chow `2 3 4` of bamboo is green but not allowed, so the readings decide.
        bool HasAllGreen(const eastl::vector<Reading>& readings, const TileCounts& held) {
            if (!EveryHeldTile(held, [](Tile tile) { return tile.IsGreen(); })) {
                return false;
            }
            return eastl::any_of(readings.begin(), readings.end(), [](const Reading& reading) { return IsCompleteReading(reading) && CountSets(reading, IsGroupSet) == kSetsInHand; });
        }

        /// A pung or kong of each dragon, and a pung or kong and the pair of one suit.
        bool IsBigThreeDragons(const Reading& reading) noexcept {
            if (!IsCompleteReading(reading) || CountSets(reading, IsDragonGroup) != kDragonCount) {
                return false;
            }
            for (int dragon = 0; dragon < kDragonCount; ++dragon) {
                if (!HasGroupOf(reading, Tile::FromDragon(dragon))) {
                    return false;
                }
            }
            for (const ReadingSet& set : reading.sets) {
                if (!IsDragonGroup(set)) {
                    return IsGroupSet(set) && set.first.IsSuit() && reading.pair.IsSuit() && set.first.Suit() == reading.pair.Suit();
                }
            }
            return false;
        }

        /// A pung or kong of each wind and a pair.
        bool IsBigFourWinds(const Reading& reading) noexcept {
            if (!IsCompleteReading(reading)) {
                return false;
            }
            for (int wind = 0; wind < kWindCount; ++wind) {
                if (!HasGroupOf(reading, Tile::FromWind(wind))) {
                    return false;
                }
            }
            return true;
        }

        bool IsFourKongs(const Reading& reading) noexcept {
            return IsCompleteReading(reading) && CountSets(reading, IsKong) == kSetsInHand;
        }

        bool AnyReading(const eastl::vector<Reading>& readings, bool (*hasHand)(const Reading&) noexcept) noexcept {
            return eastl::any_of(readings.begin(), readings.end(), hasHand);
        }

    }  // namespace

    std::optional<ScoreKind> FindLimitHand(const eastl::vector<Reading>& readings, const ScoreInput& input) {
        if (!input.won) {
            return std::nullopt;
        }
        const PlayerHand& hand = input.hand;
        const TileCounts held = HeldTiles(hand);
        const bool sevenPairs = IsSevenPairsHand(hand);
        const bool honoursOnly = EveryHeldTile(held, [](Tile tile) { return tile.IsHonour(); });

        if (sevenPairs) {
            return honoursOnly ? ScoreKind::SevenPairsHonours : ScoreKind::SevenPairsOneSuit;
        }
        if (HasConcealedPungsOneSuit(readings, hand)) {
            return ScoreKind::ConcealedPungsOneSuit;
        }
        if (honoursOnly && AnyCompleteReading(readings)) {
            return ScoreKind::HonoursOnly;
        }
        if (EveryHeldTile(held, [](Tile tile) { return tile.IsTerminal(); }) && AnyCompleteReading(readings)) {
            return ScoreKind::TerminalsOnly;
        }
        if (HasAllGreen(readings, held)) {
            return ScoreKind::AllGreen;
        }
        if (AnyReading(readings, IsBigThreeDragons)) {
            return ScoreKind::BigThreeDragons;
        }
        if (AnyReading(readings, IsBigFourWinds)) {
            return ScoreKind::BigFourWinds;
        }
        if (AnyReading(readings, IsFourKongs)) {
            return ScoreKind::FourKongs;
        }
        if (IsThirteenOrphansHand(hand)) {
            return ScoreKind::ThirteenOrphans;
        }
        // These two read the circumstances, but only of a hand that is complete (the shapes of the limit hands above returned already).
        if (!AnyCompleteReading(readings)) {
            return std::nullopt;
        }
        if (input.result.lastLiveTile && input.result.winningTile == Tile::Suited(TileSuit::Dots, kLastTileLimitRank)) {
            return ScoreKind::LastTileOneDot;
        }
        if (input.result.loose && input.result.winningTile == Tile::Suited(TileSuit::Dots, kLooseTileLimitRank)) {
            return ScoreKind::LooseTileFiveDots;
        }
        return std::nullopt;
    }

}  // namespace BECore::Mahjong
