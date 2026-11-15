#pragma once

#include <EASTL/array.h>

namespace BECore::Mahjong {

    /**
     * The look of the game in numbers: the palette, the type scale and the spacing grid. The scene file writes the colours of `kUiPalette`
     * as text, the sizes of `kFontScale` and every position and size on the grid of `kGridUnit`; `MahjongUiStyleTest` holds it to all three.
     * The roles are in `config/CLAUDE.md` ("UI style"): a new control takes the role of what it does, never a new literal. The colours
     * from `kFelt` to `kAdvisedFrame` are painted by the components (`MahjongTableComponent`, `MahjongHintsComponent`) and are not in the
     * file.
     *
     * **Gold is the table's family of marks**, and nothing else on the table is gold; the wind bars, the danger marks and the dealer mark
     * are roles of their own. Every gold mark has an alpha of its own, because the tests find a mark by its colour.
     */

    /// The table.
    inline constexpr Color kFelt{22, 92, 62, 255};
    inline constexpr Color kFeltRim{10, 46, 32, 255};
    /// The rim is this wide on every side of the view.
    inline constexpr float kRimWidth = 8.0f;
    /// A thin line on the inner side of the rim, under every tile.
    inline constexpr Color kFeltLip{54, 128, 92, 255};
    inline constexpr float kFeltLipWidth = 2.0f;
    /// The two layers of a tile's shadow, both under the tile: the tight one at an offset, the fainter one round it. Neither is a mark.
    inline constexpr Color kTileShadow{0, 0, 0, 70};
    inline constexpr Color kTileShadowSoft{0, 0, 0, 34};
    /// The rack under the viewer's hand (the bottom seat, whoever sits there): the darker lip is the whole plate, the lighter colour lies
    /// on it with the lip left on three sides, so it reads as raised. Wood, nowhere near the gold of a mark; under every tile and mark.
    inline constexpr Color kHandRack{92, 62, 42, 255};
    inline constexpr Color kHandRackLip{52, 34, 24, 255};
    inline constexpr float kRackPadding = 4.0f;
    inline constexpr float kHandRackLipWidth = 2.0f;
    /// Behind the hand of the seat whose turn it is.
    inline constexpr Color kTurnMark{255, 214, 80, 90};
    /// Round the discard that waits for a claim.
    inline constexpr Color kPendingFrame{255, 214, 80, 200};
    /// A tint over the face of the last discard (not a frame, so it stays apart from `kPendingFrame`, which shows besides it).
    inline constexpr Color kLastDiscardMark{255, 214, 80, 110};
    /// A tint over the face of the tile the viewer has just drawn, in its place in the sorted hand.
    inline constexpr Color kDrawnTileMark{255, 214, 80, 130};
    /// The bar along the edge of the claimed tile of an open set that faces the seat it came from.
    inline constexpr Color kClaimedEdge{255, 214, 80, 255};
    inline constexpr float kClaimedEdgeWidth = 4.0f;
    /// Behind the set that was just laid, blinking: on for the first half of each `kClaimBlinkSeconds`, for `kClaimHighlightSeconds`.
    inline constexpr Color kClaimHighlight{255, 214, 80, 120};
    inline constexpr float kClaimHighlightSeconds = 2.0f;
    inline constexpr float kClaimBlinkSeconds = 0.5f;
    /// Round the plate of a seat whose claim lost to a better one in the last claim window; the plate of the seat that won blinks in
    /// `kClaimHighlight`. Both last as long as the set just laid is highlighted, so the two signs of one window end together.
    inline constexpr Color kClaimLostFrame{255, 214, 80, 180};
    inline constexpr float kClaimLostSeconds = kClaimHighlightSeconds;
    /// Round the hand of a seat that has declared its calling hand (public), behind the tiles; it stays while the declaration stands.
    inline constexpr Color kCallingFrame{255, 214, 80, 160};
    inline constexpr float kCallingFrameWidth = 3.0f;
    /// A bar inside the bottom edge of a tile of the viewer's declared hand whose discard leaves the hand waiting (`KeepsWaitingDiscards`).
    /// Inside the tile, so it stays apart from the `kDeclareMark` bar of the hints (under the tile).
    inline constexpr Color kKeepsWaitingMark{255, 214, 80, 220};
    inline constexpr float kKeepsWaitingBarHeight = 6.0f;
    /// A bar along the top edge of a hand tile that is the own seat wind or the round wind (`WindMarkOf`); a tile that is both has the left
    /// half in the first and the right half in the second. At the edge the hints' bars do not use. Not gold: they say what the tile is
    /// worth, not what to do.
    inline constexpr Color kSeatWindMark{90, 180, 255, 255};
    inline constexpr Color kRoundWindMark{200, 140, 255, 255};
    inline constexpr float kWindMarkBarHeight = 6.0f;
    /// Over a tile that cannot be discarded now (the next discard has to declare the calling hand).
    inline constexpr Color kVeil{0, 0, 0, 140};
    /// The danger marks of the hints (`DangerMarks`): a bar inside the left edge of a hand tile while an opponent has declared. Each is a
    /// role of its own, none gold, a wind bar or the dealer's red.
    inline constexpr Color kSafeMark{70, 200, 110, 255};
    inline constexpr Color kRiskyMark{255, 140, 30, 255};
    inline constexpr Color kDangerMark{235, 40, 70, 255};
    inline constexpr float kDangerMarkWidth = 5.0f;
    /// The hints' marks on the viewer's hand, in the gold family: a bar under a tile whose discard declares the calling hand, and the
    /// frame on the tile the computer would discard (they differ in shape and in alpha).
    inline constexpr Color kDeclareMark{255, 214, 80, 255};
    inline constexpr Color kAdvisedFrame{255, 214, 80, 230};

    /// The seat plates (the table paints them; the words are labels of the node `SeatPlates`): the fill starts at the `hudPanel` colour
    /// (a test holds them equal); the plate of the seat whose turn it is gets the tint `kTurnMark` and the frame `kSeatTurnFrame`, the emblem's
    /// turn and calling rings are tinted `kSeatTurnFrame` and `kCallingFrame` (no new gold), the dealer gets a red bar along the top edge.
    inline constexpr Color kSeatPlate{10, 30, 22, 170};
    inline constexpr Color kSeatTurnFrame{255, 214, 80, 240};
    inline constexpr float kSeatTurnFrameWidth = 4.0f;
    inline constexpr Color kDealerMark{200, 70, 60, 255};
    inline constexpr float kDealerMarkHeight = 6.0f;

    /// The centre panel (the table paints it; the words are labels of the node `CenterPanel`): its ground is the plates' panel, the turn
    /// light a gold bar along the edge that faces the seat to play, the dealer a red bar on the inner boundary of the dealer's cell.
    inline constexpr Color kPanelTurnLight{255, 214, 80, 250};
    inline constexpr float kPanelTurnLightWidth = 4.0f;
    inline constexpr Color kPanelDealerMark{200, 70, 60, 250};

    /// The HUD and the seat plates keep this far clear of a discard block and of the wall ring, in view units.
    inline constexpr float kPlatePadding = 4.0f;

    /// The colours of the scene file, by role (`kUiPaletteNames` holds the role of each, in the same order).
    inline constexpr int kUiPaletteSize = 28;
    inline constexpr Array<Color, kUiPaletteSize> kUiPalette{{
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
        {26, 78, 40, 255},     // primaryDeep
        {30, 52, 86, 255},     // secondaryDeep
        {48, 48, 72, 255},     // neutralDeep
        {62, 34, 34, 255},     // dangerDeep
        {26, 29, 38, 255},     // surfaceDeep
        {34, 38, 52, 255},     // surfaceRaisedDeep
        {42, 48, 68, 255},     // headerDeep
        {6, 20, 14, 170},      // hudPanelDeep
        {78, 86, 116, 255},    // frame
        {140, 150, 180, 255},  // frameLight
        {0, 0, 0, 90},         // shadow
        {255, 255, 255, 60},   // highlight
        {16, 18, 26, 255},     // outline
        {90, 200, 120, 160},   // glowPrimary
        {84, 74, 36, 255},     // winnerRow
        {70, 84, 128, 255},    // placeBadge
    }};
    // clang-format off
    inline constexpr Array<const char*, kUiPaletteSize> kUiPaletteNames{{
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
        "primaryDeep",
        "secondaryDeep",
        "neutralDeep",
        "dangerDeep",
        "surfaceDeep",
        "surfaceRaisedDeep",
        "headerDeep",
        "hudPanelDeep",
        "frame",
        "frameLight",
        "shadow",
        "highlight",
        "outline",
        "glowPrimary",
        "winnerRow",
        "placeBadge",
    }};
    // clang-format on

    /// The type scale: small, body, title. No other `fontSize` in the scene file.
    inline constexpr Array<int, 3> kFontScale{{18, 22, 28}};

    /// The families of the text (`TextConfig.xml` of the engine): the display font of headlines and scores (outlined in the `outline`
    /// role) and the text font, whose bold is the text of a button. A family is not a size: `kFontScale` stays the only sizes.
    inline constexpr const char* kFontFamilyDisplay = "Russo One";
    inline constexpr const char* kFontFamilyText = "PT Sans";
    inline constexpr int kFontWeightRegular = 400;
    inline constexpr int kFontWeightBold = 700;
    inline constexpr int kTextOutlineWidth = 2;

    /// The scale of the skin (`config/CLAUDE.md`, "UI style"): three corner radii by kind of box, one border width, one soft shadow for a
    /// raised thing and none for a row. `MahjongUiStyleTest` holds the scene file to them.
    inline constexpr int kCornerSmall = 8;
    inline constexpr int kCornerMiddle = 12;
    inline constexpr int kCornerLarge = 16;
    inline constexpr Array<int, 3> kCornerScale{{kCornerSmall, kCornerMiddle, kCornerLarge}};
    inline constexpr int kBorderWidth = 2;
    inline constexpr int kShadowOffsetY = 4;
    inline constexpr int kShadowBlur = 8;

    /// The motion of a modal screen (`transition*` fields of its canvas; `config/CLAUDE.md`, "UI style"): a fade, and for a card in the
    /// middle of the window also a rise about the centre. A plate that must keep its rect for the cover test fades only; state-bound
    /// plates have none, since a fade there is latency. `MahjongUiStyleTest` holds the scene file to these numbers.
    inline constexpr float kMotionShowSeconds = 0.18f;
    inline constexpr float kMotionHideSeconds = 0.12f;
    inline constexpr float kMotionScaleFrom = 0.96f;
    inline constexpr const char* kMotionEase = "QuadOut";

    /// The table's own panels, the four seat plates and the centre panel, painted by `MahjongTableComponent` as one rounded framed box
    /// (the order of `UIElement::DrawBoxIn`). The radius and the border are those of the scale above times the table's unit; a mark's bar
    /// along an edge is inset by the radius at both ends so it stays on the straight part. The roles are the table's, outside `kUiPalette`:
    /// `kSeatPlateDeep` is `hudPanelDeep`, and `SeatPlatesTest` holds the numbers equal to the scale.
    inline constexpr float kSeatPlateCorner = static_cast<float>(kCornerMiddle);
    inline constexpr float kSeatPlateBorder = static_cast<float>(kBorderWidth);
    inline constexpr Color kSeatPlateDeep{6, 20, 14, 170};
    inline constexpr Color kSeatPlateFrame{84, 138, 106, 200};
    inline constexpr Color kSeatPlateShadow{0, 0, 0, 60};
    inline constexpr Color kSeatPlateHighlight{255, 255, 255, 36};

    /// The spacing grid: every position, size, spacing and padding of the scene file is a multiple of the unit; the step is the usual gap
    /// between controls and the margin to the edge of the view.
    inline constexpr int kGridUnit = 4;
    inline constexpr int kGridStep = 8;

    /// The anatomy of a modal screen (`Setup`, `Handoff`, `HandOver`, `MatchOver`), in view units; the tests of the screens check it.
    inline constexpr float kModalPadding = 16.0f;
    inline constexpr float kModalGap = static_cast<float>(kGridStep);
    inline constexpr float kModalSectionGap = 16.0f;
    inline constexpr float kModalTitleHeight = 48.0f;
    inline constexpr float kMainButtonHeight = 64.0f;
    inline constexpr float kViewMargin = 8.0f;

}  // namespace BECore::Mahjong
