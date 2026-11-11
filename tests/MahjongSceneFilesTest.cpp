#include "MahjongTableTestSupport.h"

#include <Mahjong/Table/TableLayout.h>
#include <iterator>
#include <map>
#include <set>
#include <vector>

/**
 * The scene is split into files: `config/SceneConfig.xml` holds the scene and one `<node ref>` per node, each node lives in `config/nodes/Mahjong<Name>.xml`
 * (the engine's node files, `BECore/Scene/CLAUDE.md`, "Node files"). `kSceneNodes` is the one place a node of the game is added, together with its file and its
 * reference line. The files are read from the source tree (`ShippedScene.h`), the last test loads the scene the way the game does.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        struct SceneNode {
            const char* name;
            int layer;
        };

        /// The nodes of `MahjongScene` in scene order, which is the order of the references, the draw order and the input order.
        constexpr SceneNode kSceneNodes[] = {
            {"Table", 0},     {"Hints", 5},    {"HintsButton", 6}, {"Actions", 10},        {"ActionsCompact", 10}, {"Hud", 10},   {"SeatPlates", 10}, {"CenterPanel", 10}, {"HudMenu", 10},
            {"HintsWhy", 11}, {"Handoff", 20}, {"HandOver", 25},   {"HandOverAction", 26}, {"MatchOver", 25},      {"Setup", 30}, {"Options", 31},    {"Rules", 35},       {"RulesAction", 36},
        };

        std::string NodeFileStem(const char* name) {
            return std::string("Mahjong") + name;
        }

        std::filesystem::path NodeFilePath(const char* name) {
            return ShippedNodesDirectory() / (NodeFileStem(name) + ".xml");
        }

        std::string ReferenceOf(const char* name) {
            return "nodes/" + NodeFileStem(name);
        }

        size_t AttributeCount(const pugi::xml_node& node) {
            const pugi::xml_object_range<pugi::xml_attribute_iterator> attributes = node.attributes();
            return static_cast<size_t>(std::distance(attributes.begin(), attributes.end()));
        }

        std::vector<pugi::xml_node> ElementChildren(const pugi::xml_node& parent) {
            std::vector<pugi::xml_node> elements;
            for (const pugi::xml_node& child : parent.children()) {
                if (child.type() == pugi::node_element) {
                    elements.push_back(child);
                }
            }
            return elements;
        }

    }  // namespace

    TEST(MahjongSceneFilesTest, SceneConfigHoldsOnlyTheSceneAndTheReferences) {
        pugi::xml_document document;
        ASSERT_TRUE(document.load_file(ShippedSceneConfig().c_str())) << ShippedSceneConfig().string();
        const pugi::xml_node scenes = document.child("SceneManager").child("scenes");
        const std::vector<pugi::xml_node> sceneElements = ElementChildren(scenes);
        ASSERT_EQ(sceneElements.size(), 1u) << "exactly one scene";
        const pugi::xml_node scene = sceneElements[0];
        EXPECT_STREQ(scene.name(), "scene");
        EXPECT_STREQ(scene.attribute("type").as_string(), "Scene");
        EXPECT_STREQ(scene.attribute("name").as_string(), "MahjongScene");
        EXPECT_EQ(scene.attribute("viewWidth").as_float(), kViewWidth);
        EXPECT_EQ(scene.attribute("viewHeight").as_float(), kViewHeight);

        // The one other view: the portrait design for an output that is at least as high as it is wide (`ShapeOfOutput`)
        const std::vector<pugi::xml_node> views = ElementChildren(scene.child("viewVariants"));
        ASSERT_EQ(views.size(), 1u);
        EXPECT_STREQ(views[0].name(), "view");
        EXPECT_STREQ(views[0].attribute("name").as_string(), "Portrait");
        EXPECT_STREQ(views[0].attribute("when").as_string(), "orientation:portrait");
        EXPECT_EQ(views[0].attribute("width").as_float(), kPortraitViewWidth);
        EXPECT_EQ(views[0].attribute("height").as_float(), kPortraitViewHeight);

        const std::vector<pugi::xml_node> nodes = ElementChildren(scene.child("nodes"));
        ASSERT_EQ(nodes.size(), std::size(kSceneNodes));
        for (size_t i = 0; i < nodes.size(); ++i) {
            const char* name = kSceneNodes[i].name;
            EXPECT_STREQ(nodes[i].name(), "node") << name;
            EXPECT_EQ(AttributeCount(nodes[i]), 1u) << name << ": a reference carries no other attribute (the layer is in the node file)";
            EXPECT_TRUE(ElementChildren(nodes[i]).empty()) << name << ": a reference has no child";
            EXPECT_EQ(std::string(nodes[i].attribute("ref").as_string()), ReferenceOf(name)) << "position " << i;
        }
    }

    TEST(MahjongSceneFilesTest, EveryNodeIsInAFileOfItsOwn) {
        // Every xml file of config/ is loaded by ConfigManager as a config named by its stem; the `Mahjong` prefix of the node files also keeps them clear of the stems of the engine's config.
        std::map<std::string, int> stems;
        for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(ShippedConfigDirectory())) {
            if (entry.is_regular_file() && entry.path().extension() == ".xml") {
                ++stems[entry.path().stem().string()];
            }
        }
        for (const SceneNode& node : kSceneNodes) {
            const std::filesystem::path path = NodeFilePath(node.name);
            ASSERT_TRUE(std::filesystem::exists(path)) << path.string();
            pugi::xml_document document;
            ASSERT_TRUE(document.load_file(path.c_str())) << path.string();
            const pugi::xml_node root = document.document_element();
            EXPECT_STREQ(root.name(), "node") << path.string();
            EXPECT_STREQ(root.attribute("name").as_string(), node.name) << path.string();
            EXPECT_EQ(root.attribute("layer").as_int(0), node.layer) << path.string();
            EXPECT_FALSE(root.attribute("ref")) << path.string() << ": a node file is not a reference";
            EXPECT_EQ(stems[NodeFileStem(node.name)], 1) << NodeFileStem(node.name) << ": another config has the same name";
        }
    }

    TEST(MahjongSceneFilesTest, NoNodeFileIsLeftOut) {
        std::set<std::string> expected;
        for (const SceneNode& node : kSceneNodes) {
            expected.insert(NodeFilePath(node.name).filename().string());
        }
        std::set<std::string> found;
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(ShippedNodesDirectory())) {
            found.insert(entry.path().filename().string());
        }
        EXPECT_EQ(found, expected) << "config/nodes/ holds exactly the files of kSceneNodes";
    }

    TEST(MahjongSceneFilesTest, TheExpandedSceneIsTheFlatScene) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        const pugi::xml_node scene = document.child("SceneManager").child("scenes").child("scene");
        ASSERT_TRUE(scene);
        const std::vector<pugi::xml_node> nodes = ElementChildren(scene.child("nodes"));
        ASSERT_EQ(nodes.size(), std::size(kSceneNodes));

        std::set<std::string> names;
        for (size_t i = 0; i < nodes.size(); ++i) {
            EXPECT_STREQ(nodes[i].attribute("name").as_string(), kSceneNodes[i].name) << "position " << i;
            EXPECT_EQ(nodes[i].attribute("layer").as_int(0), kSceneNodes[i].layer) << kSceneNodes[i].name;
            EXPECT_TRUE(names.insert(nodes[i].attribute("name").as_string()).second) << "a name twice: " << kSceneNodes[i].name;
        }
        EXPECT_FALSE(document.select_node("//*[@ref]")) << "a reference was left in the expanded document";

        const auto componentOf = [&](const char* node) {
            for (const pugi::xml_node& candidate : nodes) {
                if (std::string(candidate.attribute("name").as_string()) == node) {
                    return std::string(candidate.child("components").child("component").attribute("type").as_string());
                }
            }
            return std::string();
        };
        EXPECT_EQ(componentOf("Table"), "MahjongTableComponent");
        EXPECT_EQ(componentOf("Hud"), "MahjongHudComponent");
        EXPECT_EQ(componentOf("Hints"), "MahjongHintsComponent");
    }

    class MahjongSceneLoadTest : public MahjongTableTestBase {};

    TEST_F(MahjongSceneLoadTest, TheLoadedSceneHasTheNodesInOrder) {
        Scene* scene = _manager->GetSceneByName("MahjongScene"_intern);
        ASSERT_NE(scene, nullptr);
        EXPECT_EQ(scene->GetViewWidth(), kViewWidth);
        EXPECT_EQ(scene->GetViewHeight(), kViewHeight);

        const auto& children = scene->GetRootNode().GetChildren();
        ASSERT_EQ(children.size(), std::size(kSceneNodes));
        for (size_t i = 0; i < children.size(); ++i) {
            EXPECT_EQ(children[i]->GetName(), PoolString::Intern(kSceneNodes[i].name)) << "position " << i;
            EXPECT_EQ(children[i]->GetLayer(), kSceneNodes[i].layer) << kSceneNodes[i].name;
        }
    }

    TEST_F(MahjongSceneLoadTest, TheLoadedViewVariantsAreThePortraitViewTheTestsStandIn) {
        const Scene* scene = _manager->GetSceneByName("MahjongScene"_intern);
        ASSERT_NE(scene, nullptr);
        const auto& variants = scene->GetViewVariants();
        ASSERT_EQ(variants.size(), PortraitViewVariants().size());
        EXPECT_TRUE(variants[0] == PortraitViewVariants()[0]) << "the helper of the tests repeats the file";
        EXPECT_EQ(variants[0].name, "Portrait"_intern);
        EXPECT_EQ(variants[0].width, kPortraitViewWidth);
        EXPECT_EQ(variants[0].height, kPortraitViewHeight);
    }

    TEST_F(MahjongSceneLoadTest, TheViewTheFilePicksIsTheDesignOfTheShapeTheRuleGives) {
        const Scene* scene = _manager->GetSceneByName("MahjongScene"_intern);
        ASSERT_NE(scene, nullptr);
        const auto& variants = scene->GetViewVariants();
        struct Output {
            float width;
            float height;
        };
        // A square and a tall output are portrait, a wide one is not; an output of no size has no shape, so the default view
        for (const Output output : {Output{1280, 720}, Output{720, 1280}, Output{480, 480}, Output{1, 1}, Output{0, 0}, Output{390, 844}, Output{2560, 1080}, Output{1080, 1920}, Output{721, 720}}) {
            const ActiveSceneView picked =
                SceneViewSelector::Pick(scene->GetViewWidth(), scene->GetViewHeight(), eastl::span<const SceneViewVariant>(variants.data(), variants.size()), output.width, output.height);
            const TableShape shape = ShapeOfOutput(output.width, output.height);
            EXPECT_EQ(picked.width, DesignWidthOf(shape)) << output.width << " x " << output.height;
            EXPECT_EQ(picked.height, DesignHeightOf(shape)) << output.width << " x " << output.height;
            EXPECT_EQ(ShapeOfView(picked.width, picked.height), shape) << output.width << " x " << output.height;
        }
    }

}  // namespace BECore::Mahjong::Tests
