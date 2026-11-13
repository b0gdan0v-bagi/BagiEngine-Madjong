#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "ShippedTableLayout.h"
#include "UiStyleTestSupport.h"

#include <Common/ContentFilesCheck.h>
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
            {"Table", 0},        {"Hints", 5},      {"HintsButton", 6}, {"Actions", 10},     {"ActionsCompact", 10}, {"Hud", 10},       {"SeatPlates", 10}, {"CenterPanel", 10}, {"HudMenu", 10},
            {"HintsWhy", 11},    {"TileHover", 12}, {"Handoff", 20},    {"HandOver", 25},    {"HandOverAction", 26}, {"MatchOver", 25}, {"Setup", 30},      {"Options", 31},     {"Online", 32},
            {"OnlineEntry", 33}, {"Consent", 34},   {"Rules", 35},      {"RulesAction", 36},
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

        /// The root elements of the `SceneView` canvas of the node `node` of the expanded scene (the elements that lie against the canvas, not those
        /// inside a stack).
        std::vector<pugi::xml_node> RootElementsOf(const pugi::xml_document& document, const char* node) {
            std::vector<pugi::xml_node> found;
            const std::string path = std::string("//node[@name='") + node + "']//component[@space='SceneView']/elements/element";
            for (const pugi::xpath_node& element : document.select_nodes(path.c_str())) {
                found.push_back(element.node());
            }
            return found;
        }

        /// The root elements of a table canvas that do not name a region, each with the reason: the one place that lists a deliberate exception.
        struct UnanchoredElement {
            const char* node;
            const char* element;
            const char* reason;
        };
        constexpr UnanchoredElement kUnanchoredElements[] = {
            {"HandOver", "Plate", "the score table is a modal plate over the discards, the wall and the plates on purpose: it lies in percents of the whole view, clear of the hands at the edges"},
        };

        bool IsListedUnanchored(const char* node, const char* element) {
            for (const UnanchoredElement& listed : kUnanchoredElements) {
                if (std::string(listed.node) == node && std::string(listed.element) == element) {
                    return true;
                }
            }
            return false;
        }

        bool IsPercent(const char* value) {
            const size_t length = std::strlen(value);
            return length > 1 && value[length - 1] == '%';
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

        // The other views, in the order of the file: the portrait design for an output that is at least as high as it is wide, the narrow one for
        // a landscape output at most kNarrowMaxAspect times as wide as high, the wide one for an output kWideMinAspect times as wide as high or
        // more (`ShapeOfOutput`)
        const std::vector<pugi::xml_node> views = ElementChildren(scene.child("viewVariants"));
        ASSERT_EQ(views.size(), 3u);
        EXPECT_STREQ(views[0].name(), "view");
        EXPECT_STREQ(views[0].attribute("name").as_string(), "Portrait");
        EXPECT_STREQ(views[0].attribute("when").as_string(), "orientation:portrait");
        EXPECT_EQ(views[0].attribute("width").as_float(), kPortraitViewWidth);
        EXPECT_EQ(views[0].attribute("height").as_float(), kPortraitViewHeight);
        EXPECT_STREQ(views[1].name(), "view");
        EXPECT_STREQ(views[1].attribute("name").as_string(), "Narrow");
        EXPECT_STREQ(views[1].attribute("when").as_string(), "orientation:landscape;maxAspect:1.55");
        EXPECT_EQ(views[1].attribute("width").as_float(), kNarrowViewWidth);
        EXPECT_EQ(views[1].attribute("height").as_float(), kNarrowViewHeight);
        EXPECT_STREQ(views[2].name(), "view");
        EXPECT_STREQ(views[2].attribute("name").as_string(), "Wide");
        EXPECT_STREQ(views[2].attribute("when").as_string(), "minAspect:2");
        EXPECT_EQ(views[2].attribute("width").as_float(), kWideViewWidth);
        EXPECT_EQ(views[2].attribute("height").as_float(), kViewHeight);

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

    /// The game's content files are at the version the engine's loaders read: an engine update that bumps it is followed by
    /// `python3 engine/tools/migrate/migrate.py .` in this repository (engine/tools/migrate/CLAUDE.md), or the scene loads empty
    TEST(MahjongSceneFilesTest, TheContentFilesAreAtTheVersionOfTheEngine) {
        ExpectContentFilesAtCurrentVersion(ShippedConfigDirectory());
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

    TEST(MahjongSceneFilesTest, TheVariantsOfTheCanvasesThatFollowTheViewUseTheConditionOfAView) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        // A canvas on the table is laid out against the view, whose aspect is the view's own, so each shape's variant carries the text of the view's
        // `when` (the narrow one also says `orientation:landscape`: a portrait view is under the narrow aspect too) and the three exclude each other
        std::set<std::string> allowed;
        for (const SceneViewVariant& view : SceneViewVariants()) {
            allowed.insert(view.when.ToText().c_str());
        }
        ASSERT_EQ(allowed.size(), 3u);
        size_t found = 0;
        // (a `Screen` canvas sees the window, not the view: `HudMenu` splits the narrow windows at an aspect of its own, so its variants are not held to it)
        for (const pugi::xpath_node& variant : document.select_nodes("//component[@space='SceneView']//variant[@when]")) {
            const std::string when = variant.node().attribute("when").as_string();
            EXPECT_EQ(allowed.count(when), 1u) << "a variant that is not the condition of a view: " << when;
            ++found;
        }
        EXPECT_GT(found, 0u);
    }

    /// The contract of the canvases on the table: an element lies in a region the table publishes, not at a pair of numbers that repeats the layout.
    TEST(MahjongSceneFilesTest, EveryElementOfATableCanvasNamesARegionOrIsAListedException) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        size_t named = 0;
        for (const char* node : kRegionNodes) {
            const std::vector<pugi::xml_node> elements = RootElementsOf(document, node);
            ASSERT_FALSE(elements.empty()) << node << " has a SceneView canvas";
            for (const pugi::xml_node& element : elements) {
                const char* name = element.attribute("name").as_string();
                if (*element.attribute("region").as_string() != '\0') {
                    EXPECT_FALSE(IsListedUnanchored(node, name)) << node << "/" << name << " names a region and is listed as an exception";
                    ++named;
                } else {
                    EXPECT_TRUE(IsListedUnanchored(node, name)) << node << "/" << name << " names no region and is not a listed exception";
                }
            }
        }
        for (const UnanchoredElement& listed : kUnanchoredElements) {
            EXPECT_TRUE(document.select_node((std::string("//node[@name='") + listed.node + "']//element[@name='" + listed.element + "']").c_str())) << "an exception that is gone: " << listed.element;
            EXPECT_GT(std::strlen(listed.reason), 0u);
        }
        EXPECT_GT(named, 0u);
    }

    TEST(MahjongSceneFilesTest, NothingOnATableCanvasIsPlacedByAPairOfNumbersAndTheVariantsMoveNothingByPixels) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        for (const char* node : kRegionNodes) {
            for (const pugi::xml_node& element : RootElementsOf(document, node)) {
                for (const char* axis : {"x", "y"}) {
                    const char* value = element.attribute(axis).as_string();
                    EXPECT_TRUE(*value == '\0' || IsPercent(value)) << node << "/" << element.attribute("name").as_string() << " " << axis << "=" << value << ": an offset in a region or in percents";
                }
            }
            // A variant of such a canvas is a real difference (a size class, an arrangement), so it sets no offset in units
            const std::string sets = std::string("//node[@name='") + node + "']//variants//set";
            for (const pugi::xpath_node& set : document.select_nodes(sets.c_str())) {
                for (const char* axis : {"x", "y"}) {
                    const char* value = set.node().attribute(axis).as_string();
                    EXPECT_TRUE(*value == '\0' || IsPercent(value)) << node << " variant sets " << set.node().attribute("element").as_string() << " " << axis << "=" << value;
                }
            }
        }
    }

    TEST(MahjongSceneFilesTest, EveryRegionNameOfTheTableNodesExistsInTheSpecOfEveryShape) {
        pugi::xml_document document;
        ASSERT_TRUE(LoadShippedSceneDocument(document));
        std::set<std::string> used;
        for (const char* node : kRegionNodes) {
            const std::string attributes = std::string("//node[@name='") + node + "']//@region";
            for (const pugi::xpath_node& region : document.select_nodes(attributes.c_str())) {
                used.insert(region.attribute().as_string());
            }
        }
        ASSERT_GT(used.size(), 4u);
        const TableLayoutSpec& spec = ShippedTableLayoutSpec();
        for (const TableShape shape : {TableShape::Landscape, TableShape::Portrait, TableShape::Wide, TableShape::Narrow}) {
            const eastl::vector<TableRegion> regions = MakeTableRegions(MakeTableLayout(WorstCaseTableInput(0), spec, DesignWidthOf(shape), DesignHeightOf(shape)));
            for (const std::string& name : used) {
                bool found = false;
                for (const TableRegion& region : regions) {
                    found = found || name == region.name.c_str();
                }
                EXPECT_TRUE(found) << "the region " << name << " of the node files is not in the layout of " << EnumUtils<TableShape>::ToString(shape).data();
            }
        }
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

    /// The canvases of the table whose elements lie in the free ground the layout gives them (not on tiles, plates or the panel by design), with the
    /// regions the rule is asked of: every region an element names is looked up by the element, so a new element is covered without a line here.
    TEST_F(MahjongSceneLoadTest, EveryTableCanvasPassesTheMatrixAgainstItsRegionsAndTheElementsOfTheFreeGroundLieInsideAndClearOfTheWorstCaseTable) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        for (const char* node : kRegionNodes) {
            const auto canvas = Canvas(node);
            ASSERT_TRUE(canvas) << node;
            std::vector<UIElement*> pending{&canvas->GetRoot()};
            while (!pending.empty()) {
                UIElement* element = pending.back();
                pending.pop_back();
                canvas->SetElementVisible(element->name, true);
                for (const auto& child : element->children) {
                    pending.push_back(child.Get());
                }
            }
        }

        // The size matrix with the regions of the worst-case table of each case's view (`OutsideRegion`, `MissingRegion`, `OutsideCanvas` and the
        // overlaps of siblings), once per language; the touch size is the business of `MahjongTouchSizeTest`
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            for (const char* node : kRegionNodes) {
                ExpectPassesTheMatrix(node, 0.0f);
            }
        });

        // The elements that lie in free ground are inside their region and clear of every tile of the worst-case table, in every window of the table
        // (the phone profile included), and a grid unit off every plate
        const std::vector<const char*> nodes(std::begin(kRegionNodes), std::end(kRegionNodes));
        const char* const freeGroundNodes[] = {"Hud", "Hints", "HintsButton", "Actions"};
        ForEachTableWindow([&](const MatrixWindow& window) {
            SCOPED_TRACE(window.label.c_str());
            const TableLayout layout = MakeWorstCaseTableLayout(0, window);
            const eastl::vector<TableRegion> regions = MakeTableRegions(layout);
            LayOutOnTheTable(nodes, window);
            for (const char* node : freeGroundNodes) {
                const auto canvas = Canvas(node);
                ASSERT_TRUE(canvas) << node;
                for (const auto& child : canvas->GetRoot().children) {
                    const UIElement& element = *child;
                    const char* name = element.name.ToStringView().data();
                    ASSERT_FALSE(element.rect.IsEmpty()) << node << "/" << name;
                    const Rect* region = nullptr;
                    for (const TableRegion& candidate : regions) {
                        if (PoolString::Intern(candidate.name.c_str()) == element.style.rect.region) {
                            region = &candidate.rect;
                        }
                    }
                    ASSERT_NE(region, nullptr) << node << "/" << name << " names a region the table publishes";
                    EXPECT_GE(element.rect.x, region->x - 0.01f) << node << "/" << name;
                    EXPECT_GE(element.rect.y, region->y - 0.01f) << node << "/" << name;
                    EXPECT_LE(element.rect.x + element.rect.w, region->x + region->w + 0.01f) << node << "/" << name;
                    EXPECT_LE(element.rect.y + element.rect.h, region->y + region->h + 0.01f) << node << "/" << name;
                    ExpectClearOfTable(element.rect, layout, name);
                    for (const Rect& plate : layout.zones.seatPlates) {
                        EXPECT_TRUE(Apart(plate, element.rect)) << node << "/" << name << " lies over a plate";
                    }
                }
            }
        });
    }

    TEST_F(MahjongSceneLoadTest, TheLoadedViewVariantsArePortraitNarrowAndWideInThatOrder) {
        const Scene* scene = _manager->GetSceneByName("MahjongScene"_intern);
        ASSERT_NE(scene, nullptr);
        const auto& variants = scene->GetViewVariants();
        ASSERT_EQ(variants.size(), 3u);
        ASSERT_EQ(variants.size(), SceneViewVariants().size());
        for (size_t i = 0; i < variants.size(); ++i) {
            EXPECT_TRUE(variants[i] == SceneViewVariants()[i]) << "the helper of the tests repeats the file: view " << i;
        }
        EXPECT_EQ(variants[0].name, "Portrait"_intern);
        EXPECT_EQ(variants[0].when.ToText(), "orientation:portrait");
        EXPECT_EQ(variants[0].width, kPortraitViewWidth);
        EXPECT_EQ(variants[0].height, kPortraitViewHeight);
        EXPECT_EQ(variants[1].name, "Narrow"_intern);
        EXPECT_EQ(variants[1].when.ToText(), "orientation:landscape;maxAspect:1.55");
        EXPECT_EQ(variants[1].width, kNarrowViewWidth);
        EXPECT_EQ(variants[1].height, kNarrowViewHeight);
        EXPECT_EQ(variants[2].name, "Wide"_intern);
        EXPECT_EQ(variants[2].when.ToText(), "minAspect:2");
        EXPECT_EQ(variants[2].width, kWideViewWidth);
        EXPECT_EQ(variants[2].height, kViewHeight);
    }

    TEST_F(MahjongSceneLoadTest, TheViewTheFilePicksIsTheDesignOfTheShapeTheRuleGives) {
        const Scene* scene = _manager->GetSceneByName("MahjongScene"_intern);
        ASSERT_NE(scene, nullptr);
        const auto& variants = scene->GetViewVariants();
        struct Output {
            float width;
            float height;
        };
        // A square and a tall output are portrait, a landscape one up to 1.55 is narrow, one of aspect 2 or more is wide (a landscape phone too), the rest
        // is landscape; an output of no size has no shape, so the default view. The exact narrow boundary (1550 x 1000) is not asserted: it is a
        // float comparison of the engine
        for (const Output output : {Output{1280, 720}, Output{1600, 1000}, Output{1920, 1080}, Output{2000, 1000}, Output{2560, 1080}, Output{3440, 1440}, Output{720, 1280},
                                    Output{480, 480},  Output{1, 1},       Output{0, 0},       Output{390, 844},   Output{915, 330},   Output{1080, 1920}, Output{1024, 768},
                                    Output{1080, 768}, Output{1200, 800},  Output{1500, 1000}, Output{1999, 1000}, Output{1400, 1000}, Output{640, 480}}) {
            const ActiveSceneView picked =
                SceneViewSelector::Pick(scene->GetViewWidth(), scene->GetViewHeight(), eastl::span<const SceneViewVariant>(variants.data(), variants.size()), output.width, output.height);
            const TableShape shape = ShapeOfOutput(output.width, output.height);
            EXPECT_EQ(picked.width, DesignWidthOf(shape)) << output.width << " x " << output.height;
            EXPECT_EQ(picked.height, DesignHeightOf(shape)) << output.width << " x " << output.height;
            EXPECT_EQ(ShapeOfView(picked.width, picked.height), shape) << output.width << " x " << output.height;
        }
    }

}  // namespace BECore::Mahjong::Tests
