#pragma once

#include <BECore/UI/UIStrings.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore::Mahjong {

    /// How far a selected hand tile rises above its slot, in design units (a quarter of `kViewerHandTileH`); `RaisedRect` scales it with
    /// the layout.
    inline constexpr float kSelectedTileRise = 16.0f;

    /**
     * Whether a tap on a hand tile only selects it (a second tap on the same tile discards) instead of discarding at once: when a touch
     * input was seen (`touchSeen`: a mouse event SDL made from a touch or a pen, remembered by the component) **or** a hand tile of the
     * viewer (`handTileW` view units, the `TableProfile::handTileW` of the window, times `FitScale`, over the pixel density) is smaller
     * than `minTouchSize`, the measure of `NeedsCompactActions`. A phone profile whose tile reaches the minimum keeps the one-click
     * discard. False for a size or a density that is not positive when no touch was seen.
     */
    [[nodiscard]] bool UsesTapToSelect(bool touchSeen, float outputWidth, float outputHeight, float pixelDensity, float minTouchSize, float handTileW = kViewerHandTileW) noexcept;

    /// What a click on the hand does to the table.
    enum class TapAction : uint8_t {
        Nothing,
        /// Select hand tile `TapOutcome::index`.
        Select,
        /// Discard hand tile `TapOutcome::index`.
        Discard,
        /// Drop the selection.
        Clear,
    };

    struct TapOutcome {
        TapAction action = TapAction::Nothing;
        int index = 0;

        friend bool operator==(const TapOutcome&, const TapOutcome&) = default;
    };

    /**
     * The tap rule, over the indexes of the concealed tiles: `selected` the tile selected now, `hit` the tile the click landed on
     * (`HitHandTile`). With `tapToSelect` off a hit discards at once and no hit does nothing. With it on, a hit on the selected tile
     * discards it, a hit on another tile moves the selection there, and no hit clears the selection (`Nothing` when there was none).
     */
    [[nodiscard]] TapOutcome StepTap(const std::optional<int>& selected, const std::optional<int>& hit, bool tapToSelect) noexcept;

    /// The screen rectangle of a selected tile whose slot is `slotRect`: the slot moved up by `kSelectedTileRise` design units at the
    /// scale of the layout (`slotRect.h / kViewerHandTileH`). The hit region does not follow it.
    [[nodiscard]] Rect RaisedRect(const Rect& slotRect) noexcept;

    /// The sentence that names the selected hand tile ("Selected: 5 Dots"): the key `table.selectedTile` of the current language, with the
    /// name of `tile` (`TileName`).
    [[nodiscard]] eastl::string SelectedTileText(const UIStrings& strings, Tile tile);

}  // namespace BECore::Mahjong
