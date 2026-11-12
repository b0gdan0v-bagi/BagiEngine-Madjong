#pragma once

#include <Mahjong/Table/TableLayoutSpecXml.h>
#include <gtest/gtest.h>

/**
 * Where the shipped table layout lives and how the tests read it: the one place that knows. The game's `config/` is not mounted in the test
 * environment, so the file is read from the source tree (like `ShippedScene.h` does for the scene). Header only, pure pugixml and the reader
 * of `Table/TableLayoutSpecXml.h`: no engine services. A file that does not load or does not parse is a failure of the test that asks.
 */
namespace BECore::Mahjong::Tests {

    inline std::filesystem::path ShippedTableLayoutFile() {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "TableLayout.xml";
    }

    /// Loads the shipped file into `document`; false (and a failure of the test) when it cannot be read.
    inline bool LoadShippedTableLayoutDocument(pugi::xml_document& document) {
        const pugi::xml_parse_result result = document.load_file(ShippedTableLayoutFile().c_str());
        if (!result) {
            ADD_FAILURE() << ShippedTableLayoutFile().string() << ": " << result.description();
        }
        return static_cast<bool>(result);
    }

    /// The shipped file read by `ParseTableLayoutSpec`, each error a failure of the test that asks.
    inline TableLayoutSpecParse ReadShippedTableLayout() {
        pugi::xml_document document;
        TableLayoutSpecParse parse;
        if (LoadShippedTableLayoutDocument(document)) {
            parse = ParseTableLayoutSpec(document.document_element());
            for (const eastl::string& error : parse.errors) {
                ADD_FAILURE() << error.c_str();
            }
        }
        return parse;
    }

    /// The shipped spec, read once for the process.
    inline const TableLayoutSpec& ShippedTableLayoutSpec() {
        static const TableLayoutSpec spec = ReadShippedTableLayout().spec;
        return spec;
    }

    /// The shipped spec with two more free regions in each shape, `test.first` and `test.second`, on ground no tile of the worst-case table
    /// reaches (a spec that passes `CheckTableLayoutSpec`): the pure tests of how regions are fitted and published do not depend on which
    /// regions the shipped file has.
    inline TableLayoutSpec ShippedSpecWithTestRegions() {
        TableLayoutSpec spec = ShippedTableLayoutSpec();
        ShapeSpec& landscape = spec.Shape(TableShape::Landscape);
        landscape.freeRegions.push_back(FreeRegion{"test.first", ZoneRect{0.01f, 0.01f, 0.2f, 0.1f}});
        landscape.freeRegions.push_back(FreeRegion{"test.second", ZoneRect{0.25f, 0.01f, 0.05f, 0.05f}});
        ShapeSpec& portrait = spec.Shape(TableShape::Portrait);
        portrait.freeRegions.push_back(FreeRegion{"test.first", ZoneRect{0.02f, 0.5f, 0.2f, 0.1f}});
        portrait.freeRegions.push_back(FreeRegion{"test.second", ZoneRect{0.7f, 0.5f, 0.2f, 0.1f}});
        ShapeSpec& narrow = spec.Shape(TableShape::Narrow);
        narrow.freeRegions.push_back(FreeRegion{"test.first", ZoneRect{0.01f, 0.01f, 0.1f, 0.08f}});
        narrow.freeRegions.push_back(FreeRegion{"test.second", ZoneRect{0.15f, 0.01f, 0.05f, 0.05f}});
        return spec;
    }

}  // namespace BECore::Mahjong::Tests
