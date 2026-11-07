#include "Tile.h"

namespace BECore::Mahjong {

    namespace {

        constexpr std::string_view kInvalidName = "??";
        constexpr char kTokenSeparator = ' ';
        constexpr size_t kTokenLength = 2;

        constexpr char kSuitLetters[] = {'p', 's', 'm'};  // TileSuit::Dots, Bamboo, Characters
        constexpr char kWindLetter = 'w';
        constexpr char kDragonLetter = 'd';
        constexpr char kFlowerLetter = 'F';
        constexpr char kSeasonLetter = 'T';
        constexpr char kWindNames[kWindCount] = {'E', 'S', 'W', 'N'};
        constexpr char kDragonNames[kDragonCount] = {'R', 'W', 'G'};

        /// The two characters of every id, built once: ids are few and the table makes ToString a lookup returning a view of static storage.
        struct NameTable {
            char text[kTileIdCount][kTokenLength + 1] = {};

            constexpr NameTable() {
                for (uint8_t id = 0; id < kTileIdCount; ++id) {
                    const Tile tile{id};
                    char* name = text[id];
                    if (tile.IsSuit()) {
                        name[0] = static_cast<char>('0' + tile.Rank());
                        name[1] = kSuitLetters[static_cast<int>(tile.Suit()) - 1];
                    } else if (tile.IsWind()) {
                        name[0] = kWindNames[id - kFirstWindId];
                        name[1] = kWindLetter;
                    } else if (tile.IsDragon()) {
                        name[0] = kDragonNames[id - kFirstDragonId];
                        name[1] = kDragonLetter;
                    } else if (id < kFirstSeasonId) {
                        name[0] = kFlowerLetter;
                        name[1] = static_cast<char>('1' + id - kFirstFlowerId);
                    } else {
                        name[0] = kSeasonLetter;
                        name[1] = static_cast<char>('1' + id - kFirstSeasonId);
                    }
                }
            }
        };

        constexpr NameTable kNames;

    }  // namespace

    std::string_view ToString(Tile tile) noexcept {
        if (!tile.IsValid()) {
            return kInvalidName;
        }
        return std::string_view(kNames.text[tile.Id()], kTokenLength);
    }

    std::optional<Tile> TryParse(std::string_view text) noexcept {
        if (text.size() != kTokenLength) {
            return std::nullopt;
        }
        for (uint8_t id = 0; id < kTileIdCount; ++id) {
            if (text == std::string_view(kNames.text[id], kTokenLength)) {
                return Tile{id};
            }
        }
        return std::nullopt;
    }

    std::optional<eastl::vector<Tile>> ParseTiles(std::string_view text) {
        eastl::vector<Tile> tiles;
        if (text.empty()) {
            return tiles;
        }
        size_t start = 0;
        while (true) {
            const size_t end = text.find(kTokenSeparator, start);
            const auto tile = TryParse(text.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start));
            if (!tile) {
                return std::nullopt;
            }
            tiles.push_back(*tile);
            if (end == std::string_view::npos) {
                return tiles;
            }
            start = end + 1;
        }
    }

}  // namespace BECore::Mahjong
