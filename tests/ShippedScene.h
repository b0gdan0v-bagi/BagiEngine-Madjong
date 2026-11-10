#pragma once

#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/SceneNodeFiles.h>
#include <gtest/gtest.h>

/**
 * Where the shipped scene lives and how the tests read it: the one place that knows. A game's content is not mounted in the test environment, so the
 * files are read from the source tree (`config/SceneConfig.xml` and the node files it references, `config/nodes/`), and what a test sees is the
 * expanded document, the scene as the engine loads it (every `<node ref>` replaced by the node of its file, in the same place). Header only, pure
 * pugixml: `LoadShippedSceneDocument` and `ShippedSceneXml` need no engine services.
 */
namespace BECore::Mahjong::Tests {

    inline std::filesystem::path ShippedConfigDirectory() {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "config";
    }

    inline std::filesystem::path ShippedSceneConfig() {
        return ShippedConfigDirectory() / "SceneConfig.xml";
    }

    inline std::filesystem::path ShippedNodesDirectory() {
        return ShippedConfigDirectory() / "nodes";
    }

    /// Parses the shipped scene file and expands its node references; every error of the expansion is a failure of the test. The document element is the `SceneManager` root.
    inline bool LoadShippedSceneDocument(pugi::xml_document& document) {
        const NodeExpansion expansion = LoadSceneDocument(ShippedSceneConfig(), document);
        for (const eastl::string& error : expansion.errors) {
            ADD_FAILURE() << error.c_str();
        }
        return expansion.Ok();
    }

    /// The expanded scene document as text (the text-based tests search it, so they see the node files too); empty when it could not be read.
    inline std::string ShippedSceneXml() {
        pugi::xml_document document;
        if (!LoadShippedSceneDocument(document)) {
            return {};
        }
        std::ostringstream text;
        document.save(text, "  ", pugi::format_default);
        return text.str();
    }

    /// Loads the expanded scene into @p deserializer, ready for `SceneManager::Deserialize`.
    inline bool LoadShippedScene(XmlDeserializer& deserializer) {
        const std::string xml = ShippedSceneXml();
        return !xml.empty() && deserializer.LoadFromString(eastl::string_view(xml.data(), xml.size()));
    }

}  // namespace BECore::Mahjong::Tests
