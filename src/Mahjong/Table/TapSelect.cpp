#include "TapSelect.h"

#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/TouchRegime.h>

namespace BECore::Mahjong {

    bool UsesTapToSelect(bool touchSeen, float outputWidth, float outputHeight, float pixelDensity, float minTouchSize, float handTileW, float viewWidth, float viewHeight) noexcept {
        if (touchSeen) {
            return true;
        }
        const float fit = FitScale(outputWidth, outputHeight, viewWidth, viewHeight);
        if (!(fit > 0.0f) || !(pixelDensity > 0.0f)) {
            return false;
        }
        // The same expression `TableProfileFor` sizes the phone tile with, so a tile made just wide enough is not under the minimum
        return handTileW < minTouchSize * pixelDensity / fit;
    }

    TapOutcome StepTap(const std::optional<int>& selected, const std::optional<int>& hit, bool tapToSelect) noexcept {
        if (!tapToSelect) {
            return hit ? TapOutcome{TapAction::Discard, *hit} : TapOutcome{};
        }
        if (!hit) {
            return selected ? TapOutcome{TapAction::Clear, 0} : TapOutcome{};
        }
        return selected && *selected == *hit ? TapOutcome{TapAction::Discard, *hit} : TapOutcome{TapAction::Select, *hit};
    }

    Rect RaisedRect(const Rect& slotRect) noexcept {
        Rect raised = slotRect;
        raised.y -= kSelectedTileRise * (slotRect.h / kViewerHandTileH);
        return raised;
    }

    eastl::string SelectedTileText(const UIStrings& strings, Tile tile) {
        return Tr(strings, "table.selectedTile", {{"tile", TileName(strings, tile)}});
    }

}  // namespace BECore::Mahjong
