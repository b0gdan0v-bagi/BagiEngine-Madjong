#pragma once

#include <BECore/Math/Color.h>

namespace BECore {
    class UIThemes;
}

namespace BECore::Mahjong {

    /**
     * The colours the table paints itself (the felt and what frames it, the rack under the viewer's hand, the seat plates and the centre
     * panel), read from the current theme of `config/UIThemes.xml`. A plain value: it knows no scene, so it is made again when the version
     * of the table moves. The marks of the table are not here: they sit on tiles no theme changes (`UiPalette.h`).
     */
    struct TablePalette {
        Color felt;
        Color feltRim;
        Color feltLip;
        Color handRack;
        Color handRackLip;
        /// The fill of a plate and its deeper end (`hudPanel` and `hudPanelDeep`, as the HUD's own panels).
        Color seatPlate;
        Color seatPlateDeep;
        Color seatPlateFrame;
        Color seatPlateShadow;
        Color seatPlateHighlight;
    };

    /// The palette of the current theme of `themes`; a token no theme defines is `kMissingTokenColor`, as for an element of the scene.
    [[nodiscard]] TablePalette MakeTablePalette(const UIThemes& themes);

}  // namespace BECore::Mahjong
