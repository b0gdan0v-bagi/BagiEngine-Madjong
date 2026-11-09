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
     *   under each tile. A mark on the table is the gold family (`kTurnMark`, `kPendingFrame`, the hints `kDeclareMark`,
     *   `kAdvisedFrame`, and the marks of the last move `kLastDiscardMark`, `kDrawnTileMark`, `kClaimedEdge`, `kClaimHighlight`, the frame of a declared hand `kCallingFrame` and the bar of its safe
     * discards `kKeepsWaitingMark`, and the turn plate of a seat: `kSeatTurnFrame` with the tint `kTurnMark`) and nothing else on the table is gold (the
     * dealer mark `kDealerMark` of a seat plate is red); `kVeil` dims a tile that cannot be discarded now.
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
    /// A tint over the face of the last discard (not a frame, so it stays apart from `kPendingFrame`, which shows besides it).
    inline constexpr Color kLastDiscardMark{255, 214, 80, 110};
    /// A tint over the face of the tile the viewer has just drawn, in its place in the sorted hand (a different alpha from the tint of
    /// the last discard, which lies in the pile and never on the hand).
    inline constexpr Color kDrawnTileMark{255, 214, 80, 130};
    /// The bar along the edge of the claimed tile of an open set that faces the seat it came from, `kClaimedEdgeWidth` units wide.
    inline constexpr Color kClaimedEdge{255, 214, 80, 255};
    inline constexpr float kClaimedEdgeWidth = 4.0f;
    /// Behind the set that was just laid, blinking: it lasts `kClaimHighlightSeconds` and is on for the first half of each
    /// `kClaimBlinkSeconds`.
    inline constexpr Color kClaimHighlight{255, 214, 80, 120};
    inline constexpr float kClaimHighlightSeconds = 2.0f;
    inline constexpr float kClaimBlinkSeconds = 0.5f;
    /// Round the hand of a seat that has declared its calling hand (public), four bars of `kCallingFrameWidth` units on the border of the
    /// padded rectangle of the hand, behind the tiles; it stays while the declaration stands.
    inline constexpr Color kCallingFrame{255, 214, 80, 160};
    inline constexpr float kCallingFrameWidth = 3.0f;
    /// A bar inside the bottom edge of a tile of the viewer's declared hand whose discard leaves the hand waiting (`KeepsWaitingDiscards`), on the
    /// viewer's turn: `kKeepsWaitingBarHeight` units high, the full width of the tile. Inside the tile, so it stays apart from the `kDeclareMark`
    /// bar of the hints (under the tile), which shows only before the declaration.
    inline constexpr Color kKeepsWaitingMark{255, 214, 80, 220};
    inline constexpr float kKeepsWaitingBarHeight = 6.0f;
    /// Over a tile whose declaring discard is not legal while the next discard declares the calling hand.
    inline constexpr Color kVeil{0, 0, 0, 140};
    /// The hints (`MahjongHintsComponent`), marks on the viewer's hand and so in the gold family: a bar under a tile whose discard declares
    /// the calling hand, and the frame on the tile the computer would discard (the two differ in shape and in alpha).
    inline constexpr Color kDeclareMark{255, 214, 80, 255};
    inline constexpr Color kAdvisedFrame{255, 214, 80, 230};

    /// The seat plates (the table paints them; the words on them are labels of the node `SeatPlates`): the base is the `hudPanel` colour
    /// (a test holds them equal), the plate of the seat whose turn it is gets the tint `kTurnMark` over it and a frame
    /// `kSeatTurnFrame` of `kSeatTurnFrameWidth` units on its border (the gold family), the dealer a bar `kDealerMark` of
    /// `kDealerMarkHeight` units along the top edge: the one mark of the table that is neither gold nor a plate.
    inline constexpr Color kSeatPlate{10, 30, 22, 170};
    inline constexpr Color kSeatTurnFrame{255, 214, 80, 255};
    inline constexpr float kSeatTurnFrameWidth = 4.0f;
    inline constexpr Color kDealerMark{200, 70, 60, 255};
    inline constexpr float kDealerMarkHeight = 6.0f;

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
