#pragma once

#include <Mahjong/Table/TableLayoutSpec.h>

namespace BECore::Mahjong {

    /**
     * The reader of `config/TableLayout.xml` (the format is explained in the comments of the file). Checked: every problem is one message
     * that names the shape, the seat, the zone and the attribute, and a problem is never a silent default (a number that is no number, a
     * fraction outside 0..1, an unknown shape, element or attribute, a zone that is missing or given twice). The reader finds what is wrong
     * with the file's text; whether the table the zones make is sound is the check of `TableLayout.h` (`CheckTableLayoutSpec`).
     */
    struct TableLayoutSpecParse {
        TableLayoutSpec spec;
        eastl::vector<eastl::string> errors;

        [[nodiscard]] bool Ok() const noexcept {
            return errors.empty();
        }
    };

    /// Reads the spec from the `TableLayout` element, or from a node that has one as a child (a document). The spec is only meant to be used
    /// when `Ok()`; what could be read is in it otherwise, for a message to point at.
    [[nodiscard]] TableLayoutSpecParse ParseTableLayoutSpec(const pugi::xml_node& root);

    // Applied: the file is sound and differs from the active spec, it is the active spec now.
    // Unchanged: the file is sound and says what the active spec says.
    // Rejected: the file does not read or is not a sound table, the active spec stays.
    CORE_ENUM(TableLayoutOffer, uint8_t, Applied, Unchanged, Rejected)

    struct TableLayoutOfferResult {
        TableLayoutOffer outcome = TableLayoutOffer::Unchanged;
        eastl::vector<eastl::string> errors;  // only when `Rejected`: what `ParseTableLayoutSpec` or `CheckTableLayoutSpec` found
    };

    /// Reads the file as `ParseTableLayoutSpec` does and makes it the active spec (`SetActiveTableLayoutSpec`) only when it reads with no
    /// error, differs from the active spec and passes `CheckTableLayoutSpec`: a bad edit never replaces the last good spec. The equal file
    /// is answered before the check, so offering the same document again and again (the poll of `MahjongTableComponent`) costs a parse and a
    /// comparison. The spec that is set lives in this module until the next one (main thread only, like `SetActiveTableLayoutSpec`).
    [[nodiscard]] TableLayoutOfferResult OfferTableLayoutSpec(const pugi::xml_node& root);

}  // namespace BECore::Mahjong
