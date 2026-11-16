#pragma once

#include <Mahjong/Table/HudValues.h>
#include <gtest/gtest.h>

// The game's `config/` is not mounted in the test environment, so the file is read from the source tree, as `ShippedTableLayout.h` does.
namespace BECore::Mahjong::Tests {

    inline constexpr StringView kShippedHudValuesName = "MahjongHudValues";

    inline std::filesystem::path ShippedHudValuesFile() {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "MahjongHudValues.xml";
    }

    /// Loads the shipped file into `document`; false (and a failure of the test) when it cannot be read.
    inline bool LoadShippedHudValuesDocument(pugi::xml_document& document) {
        const pugi::xml_parse_result result = document.load_file(ShippedHudValuesFile().c_str());
        if (!result) {
            ADD_FAILURE() << ShippedHudValuesFile().string() << ": " << result.description();
        }
        return static_cast<bool>(result);
    }

    /// The shipped file read by `ParseHudValues`; the errors are returned, not failures, so a test of the refusal can use it.
    inline HudValuesParse ReadShippedHudValues() {
        pugi::xml_document document;
        HudValuesParse parse;
        if (LoadShippedHudValuesDocument(document)) {
            parse = ParseHudValues(document.document_element(), kShippedHudValuesName);
        }
        return parse;
    }

}  // namespace BECore::Mahjong::Tests
