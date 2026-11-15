#pragma once

namespace BECore::Mahjong {

    /// The size of the felt texture in pixels; it is stretched over the view, so the grain is a few view units wide.
    inline constexpr uint32_t kFeltTextureWidth = 320;
    inline constexpr uint32_t kFeltTextureHeight = 180;
    /// The vignette: `kFelt` times the centre gain in the middle and the edge gain at the corners, between them by the square of the
    /// distance from the middle.
    inline constexpr float kFeltCentreGain = 1.12f;
    inline constexpr float kFeltEdgeGain = 0.72f;
    /// The grain adds at most this many levels, the same to all three channels.
    inline constexpr int kFeltGrainLevels = 3;

    /// Pixels of an image: RGBA8 (red first), straight alpha, rows from the top, `width * height * 4` bytes (empty when a side is 0).
    struct FeltPixels {
        uint32_t width = 0;
        uint32_t height = 0;
        Vector<uint8_t> rgba;
    };

    /**
     * The felt of the table for `width` x `height` pixels. Deterministic with no random generator, and symmetric left to right and top
     * to bottom, the grain too. Every pixel is opaque.
     */
    [[nodiscard]] FeltPixels MakeFeltPixels(uint32_t width, uint32_t height);

}  // namespace BECore::Mahjong
