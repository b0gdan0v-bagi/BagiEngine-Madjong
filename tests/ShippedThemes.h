#pragma once

#include <BECore/Config/XmlConfig.h>
#include <BECore/UI/UIThemes.h>
#include <filesystem>

// The shipped theme table is read from the source tree: a game's content is not mounted in the test environment.
namespace BECore::Mahjong::Tests {

    inline std::filesystem::path ShippedThemesPath() {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "UIThemes.xml";
    }

    /// Replaces the contents of `themes` by the shipped `config/UIThemes.xml` and returns what `LoadFromXml` complained about (empty when the file is sound).
    inline Vector<String> LoadShippedThemes(UIThemes& themes) {
        themes.Clear();
        auto config = XmlConfig::Create();
        if (!config.LoadFromFile(ShippedThemesPath())) {
            return {String("the file did not load")};
        }
        return themes.LoadFromXml(config.GetRoot());
    }

}  // namespace BECore::Mahjong::Tests
