#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Values/Context.h>
#include <BECore/Values/Value.h>
#include <Mahjong/Table/HudFacts.h>
#include <pugixml.hpp>

namespace BECore::Mahjong {

    /**
     * @brief The HUD's data (`config/MahjongHudValues.xml`): which element shows when, as a read of the facts of the table
     *
     * The `context` declares the level `view` (`HudFacts`) and is read **before** the values, which link their paths against it while
     * the file loads. A value that was not given stays a `Const` false: an element the file does not name stays hidden.
     */
    struct HudValues {
        BE_CLASS(HudValues)

        BE_REFLECT_FIELD ContextDecl context;
        /// Shows the element `MoveLine`
        BE_REFLECT_FIELD Value<bool> moveLineShown = false;
        /// Shows the canvas `MatchOver`
        BE_REFLECT_FIELD Value<bool> matchOverShown = false;
    };

    /// The document read from a file and what was wrong with it; the values are used only when `Ok()`
    struct HudValuesParse {
        HudValues values;
        /// One line per problem: the document, the path of the field and what is wrong (a link error names the path as well)
        Vector<String> errors;

        [[nodiscard]] bool Ok() const noexcept {
            return errors.empty();
        }
    };

    /// Reads the document from its root element @p root under a link scope named @p documentName. An unknown key is an error too: a typo
    /// would otherwise leave a value false for good. Pure: it asks no service.
    [[nodiscard]] HudValuesParse ParseHudValues(const pugi::xml_node& root, StringView documentName);

}  // namespace BECore::Mahjong
