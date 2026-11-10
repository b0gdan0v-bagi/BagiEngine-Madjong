#include "TapSelect.h"

#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/TouchRegime.h>

namespace BECore::Mahjong {

    bool UsesTapToSelect(bool touchSeen, float outputWidth, float outputHeight, float pixelDensity, float minTouchSize) noexcept {
        if (touchSeen) {
            return true;
        }
        const float fit = FitScale(outputWidth, outputHeight);
        if (!(fit > 0.0f) || !(pixelDensity > 0.0f)) {
            return false;
        }
        return kViewerHandTileW * fit / pixelDensity < minTouchSize;
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
