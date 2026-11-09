#pragma once

#include <EASTL/array.h>

namespace BECore::Mahjong {

    /**
     * The look of the game in numbers: the palette, the type scale and the spacing grid. `config/SceneConfig.xml` writes the colours of
     * `kUiPalette` as text (`r,g,b,a`) and the sizes of `kFontScale` (`fontSize`), and every position and size on the grid of
     * `kGridUnit`; `MahjongUiStyleTest` holds the file to all three, so a stray colour, size or number fails. The colours of the table
     * (`kFelt` ... `kVeil`) are drawn by `MahjongTableComponent`, the two marks of the hints (`kDeclareMark`, `kAdvisedFrame`) by `MahjongHintsComponent`, and none is in the file; the modal numbers
     * below are the anatomy that the file's screens follow and the tests of the screens check.
     *
     * Roles, not colours, are the rule: a new control takes the role of what it does.
     * - The table: `kFelt` with a `kFeltRim` round the view, a `kZonePlate` under the discard blocks and the wall ring, a `kTileShadow`
     *   under each tile. A mark on the table is the gold family (`kTurnMark`, `kPendingFrame`, `kDeclareMark`, `kAdvisedFrame`) and nothing else on the table is gold;
     *   `kVeil` dims a tile that cannot be discarded now.
     * - The screens: `scrim` dims what lies behind a modal screen (and the hint), `surface` is the plate, `header` its headline band,
     *   `surfaceRaised` the rows on it, `hudPanel` the panels of the HUD over the felt.
     * - The buttons: `primary` confirms (Mahjong!, Start, Next hand), `secondary` claims and the HUD's New game, `neutral` the extras of
     *   an own turn and the segments, `danger` passes.
     * - The text: white, `textMuted` for captions, `accent` for marks and the first place.
     */

    /// The table.
    inline constexpr Color kFelt{22, 92, 62, 255};
    inline constexpr Color kFeltRim{14, 64, 44, 255};
    inline constexpr Color kZonePlate{0, 0, 0, 40};
    inline constexpr Color kTileShadow{0, 0, 0, 70};
    /// Behind the hand of the seat whose turn it is.
    inline constexpr Color kTurnMark{255, 214, 80, 90};
    /// Round the discard that waits for a claim.
    inline constexpr Color kPendingFrame{255, 214, 80, 200};
    /// Over a tile whose declaring discard is not legal while the next discard declares the calling hand.
    inline constexpr Color kVeil{0, 0, 0, 140};
    /// The hints (`MahjongHintsComponent`), marks on the viewer's hand and so in the gold family: a bar under a tile whose discard declares
    /// the calling hand, and the frame on the tile the computer would discard (the two differ in shape and in alpha).
    inline constexpr Color kDeclareMark{255, 214, 80, 255};
    inline constexpr Color kAdvisedFrame{255, 214, 80, 230};

    /// A plate under a discard block or the wall ring reaches this far past its zone, in view units; the HUD keeps clear of the plate.
    inline constexpr float kPlatePadding = 4.0f;

    /// The colours of the scene file, by role (`kUiPaletteNames` holds the role of each, in the same order).
    inline constexpr int kUiPaletteSize = 12;
    inline constexpr eastl::array<Color, kUiPaletteSize> kUiPalette{{
        {0, 0, 0, 200},        // scrim
        {36, 40, 52, 255},     // surface
        {48, 54, 72, 255},     // surfaceRaised
        {58, 66, 92, 255},     // header
        {10, 30, 22, 170},     // hudPanel
        {40, 110, 60, 255},    // primary
        {45, 75, 120, 255},    // secondary
        {70, 70, 100, 255},    // neutral
        {90, 50, 50, 255},     // danger
        {255, 255, 255, 255},  // text
        {190, 196, 190, 255},  // textMuted
        {255, 214, 80, 255},   // accent
    }};
    inline constexpr eastl::array<const char*, kUiPaletteSize> kUiPaletteNames{{
        "scrim",
        "surface",
        "surfaceRaised",
        "header",
        "hudPanel",
        "primary",
        "secondary",
        "neutral",
        "danger",
        "text",
        "textMuted",
        "accent",
    }};

    /// The type scale: small (the hint, the winner's lines, captions), body (the HUD, rows, ordinary buttons), title (the headline of a
    /// screen, its main button). No other `fontSize` in the scene file.
    inline constexpr eastl::array<int, 3> kFontScale{{18, 22, 28}};

    /// The spacing grid: every position, size, spacing and padding of the scene file is a multiple of the unit; the step is the usual
    /// gap between controls and the margin to the edge of the view.
    inline constexpr int kGridUnit = 4;
    inline constexpr int kGridStep = 8;

    /// The anatomy of a modal screen (`Setup`, `Handoff`, `HandOver`, `MatchOver`), in view units: the plate's padding, the gap between
    /// the rows of its stack (the grid step), the gap that sets the main button apart from the rows above it, the headline band of
    /// `Setup`, the main button and the margin of a plate to the edge of the view.
    inline constexpr float kModalPadding = 16.0f;
    inline constexpr float kModalGap = static_cast<float>(kGridStep);
    inline constexpr float kModalSectionGap = 16.0f;
    inline constexpr float kModalTitleHeight = 48.0f;
    inline constexpr float kMainButtonHeight = 64.0f;
    inline constexpr float kViewMargin = 8.0f;

}  // namespace BECore::Mahjong
