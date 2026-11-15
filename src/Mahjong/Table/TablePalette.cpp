#include "TablePalette.h"

#include <BECore/UI/UIThemes.h>

namespace BECore::Mahjong {

    namespace {

        Color ColorOf(const UIThemes& themes, StringView token) {
            return themes.Find(token).value_or(kMissingTokenColor);
        }

    }  // namespace

    TablePalette MakeTablePalette(const UIThemes& themes) {
        TablePalette palette;
        palette.felt = ColorOf(themes, "felt");
        palette.feltRim = ColorOf(themes, "feltRim");
        palette.feltLip = ColorOf(themes, "feltLip");
        palette.handRack = ColorOf(themes, "handRack");
        palette.handRackLip = ColorOf(themes, "handRackLip");
        palette.seatPlate = ColorOf(themes, "hudPanel");
        palette.seatPlateDeep = ColorOf(themes, "hudPanelDeep");
        palette.seatPlateFrame = ColorOf(themes, "plateFrame");
        palette.seatPlateShadow = ColorOf(themes, "plateShadow");
        palette.seatPlateHighlight = ColorOf(themes, "plateHighlight");
        return palette;
    }

}  // namespace BECore::Mahjong
