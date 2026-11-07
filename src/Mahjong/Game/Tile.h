#pragma once

#include <EASTL/array.h>

namespace BECore::Mahjong {

    /// Tile ids: 0..8 dots 1..9, 9..17 bamboo 1..9, 18..26 characters 1..9, 27..30 winds East South West North,
    /// 31..33 dragons Red White Green, 34..37 flowers 1..4, 38..41 seasons 1..4.
    inline constexpr uint8_t kSuitCount = 3;
    inline constexpr uint8_t kRanksPerSuit = 9;
    inline constexpr uint8_t kSuitTileCount = kSuitCount * kRanksPerSuit;
    inline constexpr uint8_t kWindCount = 4;
    inline constexpr uint8_t kDragonCount = 3;
    inline constexpr uint8_t kFlowerCount = 4;
    inline constexpr uint8_t kSeasonCount = 4;

    inline constexpr uint8_t kFirstWindId = kSuitTileCount;
    inline constexpr uint8_t kFirstDragonId = kFirstWindId + kWindCount;
    inline constexpr uint8_t kFirstFlowerId = kFirstDragonId + kDragonCount;
    inline constexpr uint8_t kFirstSeasonId = kFirstFlowerId + kFlowerCount;

    /// Playable kinds (dots, bamboo, characters, winds, dragons): the size of a counting array.
    inline constexpr uint8_t kKindCount = kFirstFlowerId;
    /// Flowers and seasons: laid open when drawn, never part of a hand.
    inline constexpr uint8_t kBonusCount = kFlowerCount + kSeasonCount;
    /// Every id: the playable kinds plus the bonus tiles.
    inline constexpr uint8_t kTileIdCount = kKindCount + kBonusCount;
    /// Copies of each playable kind in the set (a bonus tile has one).
    inline constexpr uint8_t kCopiesPerTile = 4;

    /// Counting array over the playable kinds: counts[tile.Id()] is how many copies of the kind are held.
    using TileCounts = eastl::array<uint8_t, kKindCount>;

    /// The suit of a tile; None for honours and bonus tiles.
    CORE_ENUM(TileSuit, uint8_t, None, Dots, Bamboo, Characters)

    /**
     * @brief One tile, a value type over a single id (layout above).
     *
     * A default constructed tile is invalid (IsValid() is false, every predicate is false); it stands for "no tile".
     * Tiles order by id, which is the order a hand is kept sorted in: suits, winds, dragons, flowers, seasons.
     */
    class Tile {
    public:
        static constexpr uint8_t kInvalidId = 0xFF;

        constexpr Tile() noexcept = default;
        explicit constexpr Tile(uint8_t id) noexcept : _id(id < kTileIdCount ? id : kInvalidId) {}

        /// A suit tile; rank is 1..9. An invalid suit or rank gives an invalid tile.
        [[nodiscard]] static constexpr Tile Suited(TileSuit suit, int rank) noexcept {
            if (suit == TileSuit::None || rank < 1 || rank > kRanksPerSuit) {
                return Tile{};
            }
            return Tile{static_cast<uint8_t>((static_cast<int>(suit) - 1) * kRanksPerSuit + rank - 1)};
        }

        /// By index inside the group: a wind 0..3 (East South West North), a dragon 0..2 (Red White Green), a flower 0..3, a season 0..3.
        /// An index out of range gives an invalid tile.
        [[nodiscard]] static constexpr Tile FromWind(int index) noexcept {
            return index >= 0 && index < kWindCount ? Tile{static_cast<uint8_t>(kFirstWindId + index)} : Tile{};
        }
        [[nodiscard]] static constexpr Tile FromDragon(int index) noexcept {
            return index >= 0 && index < kDragonCount ? Tile{static_cast<uint8_t>(kFirstDragonId + index)} : Tile{};
        }
        [[nodiscard]] static constexpr Tile FromFlower(int index) noexcept {
            return index >= 0 && index < kFlowerCount ? Tile{static_cast<uint8_t>(kFirstFlowerId + index)} : Tile{};
        }
        [[nodiscard]] static constexpr Tile FromSeason(int index) noexcept {
            return index >= 0 && index < kSeasonCount ? Tile{static_cast<uint8_t>(kFirstSeasonId + index)} : Tile{};
        }

        [[nodiscard]] constexpr uint8_t Id() const noexcept {
            return _id;
        }
        [[nodiscard]] constexpr bool IsValid() const noexcept {
            return _id < kTileIdCount;
        }

        /// Dots, bamboo or characters.
        [[nodiscard]] constexpr bool IsSuit() const noexcept {
            return _id < kSuitTileCount;
        }
        /// Winds and dragons.
        [[nodiscard]] constexpr bool IsHonour() const noexcept {
            return _id >= kFirstWindId && _id < kFirstFlowerId;
        }
        [[nodiscard]] constexpr bool IsWind() const noexcept {
            return _id >= kFirstWindId && _id < kFirstDragonId;
        }
        [[nodiscard]] constexpr bool IsDragon() const noexcept {
            return _id >= kFirstDragonId && _id < kFirstFlowerId;
        }
        /// Flowers and seasons.
        [[nodiscard]] constexpr bool IsBonus() const noexcept {
            return _id >= kFirstFlowerId && _id < kTileIdCount;
        }
        /// A playable kind: a suit tile or an honour (the ids below kKindCount).
        [[nodiscard]] constexpr bool IsPlayable() const noexcept {
            return _id < kKindCount;
        }
        /// A 1 or a 9 of a suit.
        [[nodiscard]] constexpr bool IsTerminal() const noexcept {
            return IsSuit() && (Rank() == 1 || Rank() == kRanksPerSuit);
        }
        /// A 2..8 of a suit.
        [[nodiscard]] constexpr bool IsSimple() const noexcept {
            return IsSuit() && !IsTerminal();
        }
        /// Bamboo 2, 3, 4, 6, 8 and the green dragon.
        [[nodiscard]] constexpr bool IsGreen() const noexcept {
            if (IsDragon()) {
                return *this == FromDragon(kGreenDragonIndex);
            }
            if (Suit() != TileSuit::Bamboo) {
                return false;
            }
            const int rank = Rank();
            return rank == 2 || rank == 3 || rank == 4 || rank == 6 || rank == 8;
        }

        [[nodiscard]] constexpr TileSuit Suit() const noexcept {
            return IsSuit() ? static_cast<TileSuit>(_id / kRanksPerSuit + 1) : TileSuit::None;
        }
        /// 1..9 for a suit tile, 0 for any other.
        [[nodiscard]] constexpr int Rank() const noexcept {
            return IsSuit() ? _id % kRanksPerSuit + 1 : 0;
        }

        constexpr bool operator==(const Tile&) const noexcept = default;
        constexpr bool operator<(const Tile& other) const noexcept {
            return _id < other._id;
        }

    private:
        static constexpr int kGreenDragonIndex = 2;

        uint8_t _id = kInvalidId;
    };

    /// The notation of a tile: 1p..9p dots, 1s..9s bamboo, 1m..9m characters, Ew Sw Ww Nw winds, Rd Wd Gd dragons,
    /// F1..F4 flowers, T1..T4 seasons. "??" for an invalid tile.
    [[nodiscard]] std::string_view ToString(Tile tile) noexcept;

    /// Reads one tile in the notation above (case sensitive); nullopt for anything else.
    [[nodiscard]] std::optional<Tile> TryParse(std::string_view text) noexcept;

    /// Reads tiles separated by spaces ("1p 2p 3p"); an empty text gives an empty list, a bad token or a doubled space gives nullopt.
    [[nodiscard]] std::optional<eastl::vector<Tile>> ParseTiles(std::string_view text);

}  // namespace BECore::Mahjong
