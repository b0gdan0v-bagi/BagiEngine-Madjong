#pragma once

#include <BECore/UI/UIColor.h>
#include <BECore/UI/UIElement.h>

namespace BECore::Mahjong {

    /// The theme tokens more than one component sets by name (keys of `config/UIThemes.xml`).
    struct ColorTokenIds {
        static constexpr auto text = "text"_intern;
    };

    /**
     * Gives `color`, a colour field of `owner` or of a component of it, the theme token `role` (a key of `config/UIThemes.xml`). It writes
     * only when the role changed, so a frame that changes nothing writes nothing; the new token is resolved at the next draw
     * (`UIElement::ResetTheme`). A colour set in code is a token like a colour of the scene file, or it keeps the old theme after a switch.
     */
    inline void SetColorToken(UIElement& owner, UIColor& color, PoolString role) {
        if (color.token == role) {
            return;
        }
        color = UIColor::FromToken(role);
        owner.ResetTheme();
    }

    /// Gives `color` the plain colour `value` (a mark, which no theme changes) and drops a token it had.
    inline void SetPlainColor(UIColor& color, const Color& value) {
        if (!color.token.Empty() || !ColorsEqual(color, value)) {
            color = value;
        }
    }

}  // namespace BECore::Mahjong
