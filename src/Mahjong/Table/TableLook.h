#pragma once

namespace BECore::Mahjong {

    /// The size of the felt texture in pixels (the view is 1280 x 720: the texture is stretched over it, so the grain is a few units wide).
    inline constexpr uint32_t kFeltTextureWidth = 320;
    inline constexpr uint32_t kFeltTextureHeight = 180;
    /// The felt colour (`kFelt`) is multiplied by `kFeltCentreGain` in the middle and by `kFeltEdgeGain` at the corners, between them by the
    /// square of the distance from the middle (the vignette).
    inline constexpr float kFeltCentreGain = 1.12f;
    inline constexpr float kFeltEdgeGain = 0.72f;
    /// The grain adds at most this many levels to each channel, the same to all three.
    inline constexpr int kFeltGrainLevels = 3;

    /// Pixels of an image: RGBA8 (red first), straight alpha, rows from the top, `width * height * 4` bytes (empty when a side is 0).
    struct FeltPixels {
        uint32_t width = 0;
        uint32_t height = 0;
        eastl::vector<uint8_t> rgba;
    };

    /**
     * The felt of the table for `width` x `height` pixels: `kFelt` lighter in the middle and darker toward the edges, with a faint grain.
     * Pure and deterministic (the same size gives the same bytes, no random generator) and symmetric left to right and top to bottom, the
     * grain too (it is a function of the distance to the nearest edge on each axis). Every pixel is opaque.
     */
    [[nodiscard]] FeltPixels MakeFeltPixels(uint32_t width, uint32_t height);

}  // namespace BECore::Mahjong
