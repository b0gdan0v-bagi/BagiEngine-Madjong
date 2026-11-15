#pragma once

#include <Mahjong/Table/TableLayoutSpec.h>

namespace BECore::Mahjong {

    /**
     * The reader of `config/TableLayout.xml` (the format is explained in the comments of the file). Every problem is one message that
     * names the shape, the seat, the zone and the attribute, never a silent default. It finds what is wrong with the file's text;
     * whether the table the zones make is sound is `CheckTableLayoutSpec`.
     */
    struct TableLayoutSpecParse {
        TableLayoutSpec spec;
        Vector<String> errors;

        [[nodiscard]] bool Ok() const noexcept {
            return errors.empty();
        }
    };

    /// Reads the spec from the `TableLayout` element, or from a node that has one as a child. Use the spec only when `Ok()`.
    [[nodiscard]] TableLayoutSpecParse ParseTableLayoutSpec(const pugi::xml_node& root);

    // Applied: the file is sound and differs from the active spec, it is the active spec now.
    // Unchanged: the file is sound and says what the active spec says.
    // Rejected: the file does not read or is not a sound table, the active spec stays.
    CORE_ENUM(TableLayoutOffer, uint8_t, Applied, Unchanged, Rejected)

    struct TableLayoutOfferResult {
        TableLayoutOffer outcome = TableLayoutOffer::Unchanged;
        Vector<String> errors;  // only when `Rejected`: what `ParseTableLayoutSpec` or `CheckTableLayoutSpec` found
    };

    /// Makes the file the active spec (`SetActiveTableLayoutSpec`) only when it reads with no error, differs from the active spec and
    /// passes `CheckTableLayoutSpec`: a bad edit never replaces the last good spec. The equal file is answered before the check, so a
    /// poll that offers the same document again costs a parse and a comparison. The spec set lives in this module until the next one;
    /// main thread only.
    [[nodiscard]] TableLayoutOfferResult OfferTableLayoutSpec(const pugi::xml_node& root);

}  // namespace BECore::Mahjong
