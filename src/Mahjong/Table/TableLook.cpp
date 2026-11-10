#include "TableLook.h"

#include <Mahjong/Table/UiPalette.h>

namespace BECore::Mahjong {

    namespace {

        constexpr uint8_t kOpaque = 255;
        constexpr uint32_t kHashMultiplierX = 0x9E3779B1u;
        constexpr uint32_t kHashMultiplierY = 0x85EBCA6Bu;
        constexpr uint32_t kHashMixShift = 15;
        constexpr uint32_t kHashByteMask = 0xFFu;

        /// A hash of two numbers, spread over a byte (a plain integer mix: the grain must not depend on a random generator).
        constexpr uint32_t HashByte(uint32_t x, uint32_t y) {
            uint32_t h = (x * kHashMultiplierX) ^ (y * kHashMultiplierY);
            h ^= h >> kHashMixShift;
            h *= kHashMultiplierX;
            h ^= h >> kHashMixShift;
            return h & kHashByteMask;
        }

        /// How far from the middle `nearest` of `size` pixels is, 0 in the middle and nearly 1 at the edge; the same for a pixel and its mirror.
        float DistanceFromMiddle(uint32_t nearest, uint32_t size) {
            return 1.0f - static_cast<float>(2 * nearest + 1) / static_cast<float>(size);
        }

        uint8_t Channel(uint8_t base, float gain, int grain) {
            const float level = static_cast<float>(base) * gain + static_cast<float>(grain);
            return static_cast<uint8_t>(eastl::clamp(level + 0.5f, 0.0f, 255.0f));
        }

    }  // namespace

    FeltPixels MakeFeltPixels(uint32_t width, uint32_t height) {
        FeltPixels felt;
        if (width == 0 || height == 0) {
            return felt;
        }
        felt.width = width;
        felt.height = height;
        felt.rgba.reserve(static_cast<size_t>(width) * height * 4);
        for (uint32_t y = 0; y < height; ++y) {
            const uint32_t nearestY = eastl::min(y, height - 1 - y);
            const float dy = DistanceFromMiddle(nearestY, height);
            for (uint32_t x = 0; x < width; ++x) {
                const uint32_t nearestX = eastl::min(x, width - 1 - x);
                const float dx = DistanceFromMiddle(nearestX, width);
                const float away = 0.5f * (dx * dx + dy * dy);  // 0 in the middle, 1 at a corner
                const float gain = kFeltCentreGain + (kFeltEdgeGain - kFeltCentreGain) * away;
                // -kFeltGrainLevels .. +kFeltGrainLevels, from the pixel nearest to the same place of the other half
                const int grain = static_cast<int>(HashByte(nearestX, nearestY) * (2 * kFeltGrainLevels + 1) / (kHashByteMask + 1)) - kFeltGrainLevels;
                felt.rgba.push_back(Channel(kFelt.r, gain, grain));
                felt.rgba.push_back(Channel(kFelt.g, gain, grain));
                felt.rgba.push_back(Channel(kFelt.b, gain, grain));
                felt.rgba.push_back(kOpaque);
            }
        }
        return felt;
    }

}  // namespace BECore::Mahjong
