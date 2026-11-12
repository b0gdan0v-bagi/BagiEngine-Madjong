#include "ShippedScene.h"
#include "UiStyleTestSupport.h"

#include <BECore/UI/UIValueText.h>
#include <cmath>

/**
 * The look of the shipped scene file held to the style of `Table/UiPalette.h` (pure logic: the files are read from the source tree and parsed
 * with pugixml, no engine services). Every `element` of the shipped scene (`config/SceneConfig.xml` with its node files expanded, `ShippedScene.h`) is walked: each colour (also the `textColor` of a
 * `UITextComponent`) is an entry of the palette, each `fontSize` is on the type scale, each position and size is a multiple of the grid unit (a percent and the tile pictures of the buttons excepted),
 * and each button (a `UIInputClicker`) and toggle row has an action (a segment of a choice group may take its action from the group). A later entry that adds a control with a stray colour, size or
 * number fails here; a deliberately broken copy of the text shows that each check can fail. The `set` nodes of a variant (`<set element=".." fontSize/x/y/width/height=..>`) are held to the font and
 * grid rules too.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        CORE_ENUM(Rule, uint8_t, Colour, FontSize, Grid, Action)

        struct Violation {
            Rule rule;
            std::string element;
            std::string attribute;
            std::string value;
        };

        constexpr const char* kColourAttributes[] = {"color", "hoverColor", "pressedColor", "disabledColor", "selectedColor", "trackColor", "fillColor", "thumbColor"};
        /// ... and the colour of a text, which sits on its `UITextComponent` (the tints of a picture are white by design: not a look).
        constexpr const char* kComponentColourAttributes[] = {"textColor"};
        /// The positions and sizes that are a plain number or a length (a percent is not on the grid; an offset may be one too) ...
        constexpr const char* kLengthAttributes[] = {"x", "y", "width", "height", "minWidth", "maxWidth", "minHeight", "maxHeight"};
        constexpr const char* kNumberAttributes[] = {"spacing"};
        /// ... and the four edges in one attribute.
        constexpr const char* kEdgeAttributes[] = {"padding", "margin"};

        std::string AttributeOf(const pugi::xml_node& node, const char* name) {
            return node.attribute(name).as_string("");
        }

        bool OnTheGrid(float value) {
            return std::fmod(value, static_cast<float>(kGridUnit)) == 0.0f;
        }

        bool OnThePalette(const Color& color) {
            return std::any_of(kUiPalette.begin(), kUiPalette.end(), [&](const Color& entry) { return SameColor(entry, color); });
        }

        bool OnTheFontScale(float size) {
            return std::any_of(kFontScale.begin(), kFontScale.end(), [&](int entry) { return static_cast<float>(entry) == size; });
        }

        /// The tile pictures of the action buttons (`Chow0Tile1`) keep the 3:4 of an atlas cell and are the one exception to the grid.
        bool IsTilePictureName(const std::string& name) {
            const size_t at = name.rfind("Tile");
            return at != std::string::npos && at + 4 < name.size() && std::all_of(name.begin() + static_cast<std::ptrdiff_t>(at) + 4, name.end(), [](char c) { return c >= '0' && c <= '9'; });
        }

        bool HasComponent(const pugi::xml_node& element, const char* type) {
            return static_cast<bool>(element.child("components").find_child_by_attribute("component", "type", type));
        }

        /// A picture is an element with a `UIImageComponent`.
        bool IsTilePicture(const pugi::xml_node& element) {
            return HasComponent(element, "UIImageComponent") && IsTilePictureName(AttributeOf(element, "name"));
        }

        /// The font size and the position and size attributes of an `element` or of a `set` of a variant (`name` is the element it speaks of).
        void CheckFontAndGrid(const pugi::xml_node& node, const std::string& name, bool tilePicture, std::vector<Violation>& found) {
            const auto flag = [&](Rule rule, const char* attribute) { found.push_back({rule, name, attribute, AttributeOf(node, attribute)}); };

            if (node.attribute("fontSize")) {
                const std::optional<float> size = UIText::ParseFiniteFloat(node.attribute("fontSize").as_string());
                if (!size || !OnTheFontScale(*size)) {
                    flag(Rule::FontSize, "fontSize");
                }
            }

            if (tilePicture) {
                return;
            }
            for (const char* attribute : kLengthAttributes) {
                if (node.attribute(attribute)) {
                    const std::optional<UILength> length = UIText::ParseLength(node.attribute(attribute).as_string());
                    if (!length || (length->unit == UIUnit::Pixels && !OnTheGrid(length->value))) {
                        flag(Rule::Grid, attribute);
                    }
                }
            }
            for (const char* attribute : kNumberAttributes) {
                if (node.attribute(attribute)) {
                    const std::optional<float> number = UIText::ParseFiniteFloat(node.attribute(attribute).as_string());
                    if (!number || !OnTheGrid(*number)) {
                        flag(Rule::Grid, attribute);
                    }
                }
            }
            for (const char* attribute : kEdgeAttributes) {
                if (node.attribute(attribute)) {
                    const std::optional<UIEdges> edges = UIText::ParseEdges(node.attribute(attribute).as_string());
                    if (!edges || !OnTheGrid(edges->left) || !OnTheGrid(edges->top) || !OnTheGrid(edges->right) || !OnTheGrid(edges->bottom)) {
                        flag(Rule::Grid, attribute);
                    }
                }
            }
        }

        void CheckElement(const pugi::xml_node& element, std::vector<Violation>& found) {
            const std::string name = AttributeOf(element, "name");
            const auto flag = [&](Rule rule, const char* attribute) { found.push_back({rule, name, attribute, AttributeOf(element, attribute)}); };

            for (const char* attribute : kColourAttributes) {
                if (!element.attribute(attribute)) {
                    continue;
                }
                const std::optional<Color> color = UIText::ParseColor(element.attribute(attribute).as_string());
                if (!color || !OnThePalette(*color)) {
                    flag(Rule::Colour, attribute);
                }
            }

            for (const pugi::xml_node component : element.child("components").children("component")) {
                for (const char* attribute : kComponentColourAttributes) {
                    if (!component.attribute(attribute)) {
                        continue;
                    }
                    const std::optional<Color> color = UIText::ParseColor(component.attribute(attribute).as_string());
                    if (!color || !OnThePalette(*color)) {
                        found.push_back({Rule::Colour, name, attribute, AttributeOf(component, attribute)});
                    }
                }
            }

            CheckFontAndGrid(element, name, IsTilePicture(element), found);

            // A button (an element with a `UIInputClicker`) and a toggle row do something and keep the action on the clicker or on the
            // `UIToggleComponent`; a segment of a choice group may leave it to the group, and an option of a dropdown always does (a
            // choice emits the dropdown's action with the option's value)
            const std::string parentType = AttributeOf(element.parent().parent(), "type");  // element -> children -> element
            const bool groupChild = parentType == "UISegmented" || parentType == "UIDropdown";
            for (const pugi::xml_node component : element.child("components").children("component")) {
                // A toggle row's clicker has no action: its `UIToggleComponent` decides what a click emits and holds the action
                const std::string componentType = AttributeOf(component, "type");
                const bool toggleRow = HasComponent(element, "UIToggleComponent");
                if (componentType == "UIInputClicker" && AttributeOf(component, "action").empty() && !groupChild && !toggleRow) {
                    flag(Rule::Action, "action");
                }
                if (componentType == "UIToggleComponent" && AttributeOf(component, "action").empty()) {
                    flag(Rule::Action, "action");
                }
            }
        }

        /// Every `element` of the document, at any depth (they sit in `elements` and `children`, inside components, inside nodes).
        void Walk(const pugi::xml_node& node, std::vector<Violation>& found, int& count) {
            for (const pugi::xml_node child : node.children()) {
                if (std::string_view(child.name()) == "element") {
                    CheckElement(child, found);
                    ++count;
                } else if (std::string_view(child.name()) == "set") {
                    // a variant sets font and geometry (no colour) of the element it names; a tile picture is told by the name alone
                    const std::string name = AttributeOf(child, "element");
                    CheckFontAndGrid(child, name, IsTilePictureName(name), found);
                    ++count;
                }
                Walk(child, found, count);
            }
        }

        struct Report {
            bool parsed = false;
            int elements = 0;
            std::vector<Violation> violations;
        };

        Report CheckStyle(const std::string& xml) {
            Report report;
            pugi::xml_document document;
            report.parsed = document.load_string(xml.c_str());
            Walk(document, report.violations, report.elements);
            return report;
        }

        std::string ShippedText() {
            return ShippedSceneXml();
        }

        std::vector<Violation> Of(const Report& report, Rule rule) {
            std::vector<Violation> found;
            std::copy_if(report.violations.begin(), report.violations.end(), std::back_inserter(found), [rule](const Violation& v) { return v.rule == rule; });
            return found;
        }

        std::string Describe(const std::vector<Violation>& violations) {
            std::string text;
            for (const Violation& v : violations) {
                text += "\n  " + v.element + " " + v.attribute + "=\"" + v.value + "\"";
            }
            return text;
        }

        /// `text` with the first `from` replaced by `to`; the test fails when there is none (a copy that was not broken proves nothing).
        std::string Broken(std::string text, const std::string& from, const std::string& to) {
            const size_t at = text.find(from);
            EXPECT_NE(at, std::string::npos) << from << " is not in the file";
            if (at != std::string::npos) {
                text.replace(at, from.size(), to);
            }
            return text;
        }

        /// A scene with one element, for the rules at their edges; `components` are the `component` nodes of the element.
        std::string OneElement(const std::string& attributes, const std::string& components = "") {
            return "<SceneManager><scenes><scene><nodes><node><components><component><elements><element " + attributes + "><components>" + components +
                   "</components></element></elements></component></components></node></nodes></scene></scenes></SceneManager>";
        }

        /// The component nodes of a text, a picture and a button (without or with an action).
        constexpr const char* kTextComponent = "<component type=\"UITextComponent\" />";
        constexpr const char* kImageComponent = "<component type=\"UIImageComponent\" />";
        constexpr const char* kSilentClicker = "<component type=\"UIInputClicker\" />";
        constexpr const char* kClicker = "<component type=\"UIInputClicker\" action=\"A\" />";

    }  // namespace

    class MahjongUiStyleTest : public ::testing::Test {
    protected:
        void SetUp() override {
            _text = ShippedText();
            _report = CheckStyle(_text);
        }

        std::string _text;
        Report _report;
    };

    TEST_F(MahjongUiStyleTest, TheFileIsReadAndHoldsTheElementsOfEveryCanvas) {
        ASSERT_TRUE(_report.parsed);
        EXPECT_GT(_report.elements, 100) << "an empty walk would pass every rule";
    }

    TEST_F(MahjongUiStyleTest, EveryColourOfTheFileIsAnEntryOfThePalette) {
        EXPECT_TRUE(Of(_report, Rule::Colour).empty()) << Describe(Of(_report, Rule::Colour));
    }

    TEST_F(MahjongUiStyleTest, EveryFontSizeOfTheFileIsOnTheTypeScale) {
        EXPECT_TRUE(Of(_report, Rule::FontSize).empty()) << Describe(Of(_report, Rule::FontSize));
    }

    TEST_F(MahjongUiStyleTest, EveryPositionAndSizeOfTheFileIsAMultipleOfTheGridUnit) {
        EXPECT_TRUE(Of(_report, Rule::Grid).empty()) << Describe(Of(_report, Rule::Grid));
    }

    TEST_F(MahjongUiStyleTest, EveryButtonAndToggleOfTheFileHasAnAction) {
        EXPECT_TRUE(Of(_report, Rule::Action).empty()) << Describe(Of(_report, Rule::Action));
    }

    TEST_F(MahjongUiStyleTest, AColourOneOffThePaletteIsCaught) {
        const Report report = CheckStyle(Broken(_text, "40,110,60,255", "40,110,61,255"));
        EXPECT_FALSE(Of(report, Rule::Colour).empty());
        EXPECT_TRUE(Of(report, Rule::FontSize).empty());
        EXPECT_TRUE(Of(report, Rule::Grid).empty());
        // ... also in a text colour and as text that is no colour; the other text form of a palette colour is the same colour
        EXPECT_FALSE(Of(CheckStyle(Broken(_text, "textColor=\"190,196,190,255\"", "textColor=\"190,196,190,254\"")), Rule::Colour).empty());
        EXPECT_TRUE(Of(CheckStyle(Broken(_text, "selectedColor=\"40,110,60,255\"", "selectedColor=\"#286E3C\"")), Rule::Colour).empty());
        EXPECT_FALSE(Of(CheckStyle(Broken(_text, "color=\"0,0,0,200\"", "color=\"dark\"")), Rule::Colour).empty());
    }

    TEST_F(MahjongUiStyleTest, AFontSizeOffTheScaleIsCaught) {
        const Report report = CheckStyle(Broken(_text, "fontSize=\"22\"", "fontSize=\"19\""));
        EXPECT_FALSE(Of(report, Rule::FontSize).empty());
        EXPECT_TRUE(Of(report, Rule::Colour).empty());
        EXPECT_TRUE(Of(report, Rule::Grid).empty());
    }

    TEST_F(MahjongUiStyleTest, ASizeOffTheGridIsCaught) {
        for (const auto& [from, to] : {std::pair<const char*, const char*>{"width=\"104\"", "width=\"102\""},
                                       {"spacing=\"8\"", "spacing=\"6\""},
                                       {"padding=\"16\"", "padding=\"15\""},
                                       {"y=\"72\"", "y=\"73\""},
                                       {"maxWidth=\"640\"", "maxWidth=\"642\""}}) {
            const Report report = CheckStyle(Broken(_text, from, to));
            EXPECT_FALSE(Of(report, Rule::Grid).empty()) << to;
            EXPECT_TRUE(Of(report, Rule::Colour).empty()) << to;
            EXPECT_TRUE(Of(report, Rule::FontSize).empty()) << to;
        }
    }

    TEST_F(MahjongUiStyleTest, TheSetNodesOfAVariantAreHeldToTheFontAndGridRules) {
        const std::string variant = "<root><element type=\"UIElement\" name=\"A\"><components><component type=\"UITextComponent\" /></components></element><variant><set element=\"A\" x=\"16\" "
                                    "fontSize=\"18\" width=\"50%\" /></variant></root>";
        EXPECT_TRUE(CheckStyle(variant).violations.empty());
        const Report offGrid = CheckStyle(Broken(variant, "x=\"16\"", "x=\"17\""));
        ASSERT_EQ(Of(offGrid, Rule::Grid).size(), 1u);
        EXPECT_EQ(Of(offGrid, Rule::Grid).front().element, "A");
        EXPECT_FALSE(Of(CheckStyle(Broken(variant, "fontSize=\"18\"", "fontSize=\"19\"")), Rule::FontSize).empty());
        EXPECT_FALSE(Of(CheckStyle(Broken(variant, "width=\"50%\"", "width=\"51\"")), Rule::Grid).empty());
    }

    TEST_F(MahjongUiStyleTest, AButtonWithoutAnActionIsCaught) {
        const Report report = CheckStyle(Broken(_text, " action=\"MahjongPass\"", ""));
        ASSERT_EQ(Of(report, Rule::Action).size(), 1u);
        EXPECT_EQ(Of(report, Rule::Action).front().element, "Pass");
        EXPECT_TRUE(Of(report, Rule::Grid).empty());
    }

    TEST_F(MahjongUiStyleTest, ThePercentTheTilePicturesAndTheSegmentsAreTheExceptions) {
        // A percent is not on the grid; the tile pictures of the buttons are 21 x 28, the 3:4 of an atlas cell
        EXPECT_TRUE(CheckStyle(OneElement("type=\"UIElement\" name=\"A\" width=\"94%\" height=\"96\"", kTextComponent)).violations.empty());
        EXPECT_TRUE(CheckStyle(OneElement("type=\"UIElement\" name=\"Chow0Tile1\" x=\"-23\" y=\"-3\" width=\"21\" height=\"28\"", kImageComponent)).violations.empty());
        EXPECT_FALSE(CheckStyle(OneElement("type=\"UIElement\" name=\"Picture\" width=\"21\" height=\"28\"", kImageComponent)).violations.empty());
        EXPECT_FALSE(CheckStyle(OneElement("type=\"UIElement\" name=\"Chow0Tile1\" width=\"21\"", kTextComponent)).violations.empty()) << "only a picture is exempt";
        // A negative offset is fine on the grid; an edge list is checked in each of its four numbers
        EXPECT_TRUE(CheckStyle(OneElement("type=\"UIElement\" name=\"B\" x=\"-8\" padding=\"8,4,8,4\"", kClicker)).violations.empty());
        EXPECT_FALSE(CheckStyle(OneElement("type=\"UIElement\" name=\"B\" padding=\"8,4,8,5\"", kClicker)).violations.empty());
        // A button of a choice group may take its action from the group; a button anywhere else may not go without
        const Report clicker = CheckStyle(OneElement("type=\"UIElement\" name=\"B\"", kSilentClicker));
        EXPECT_EQ(Of(clicker, Rule::Action).size(), 1u) << "a button without an action";
        const Report toggle = CheckStyle(
            "<root><element type=\"UIElement\" name=\"T\"><components><component type=\"UIInputClicker\" /><component type=\"UIToggleComponent\" action=\"A\" /></components></element></root>");
        EXPECT_TRUE(Of(toggle, Rule::Action).empty()) << "a toggle row keeps its action on the toggle, not on the clicker";
        const Report silentToggle =
            CheckStyle("<root><element type=\"UIElement\" name=\"T\"><components><component type=\"UIInputClicker\" /><component type=\"UIToggleComponent\" /></components></element></root>");
        EXPECT_EQ(Of(silentToggle, Rule::Action).size(), 1u) << "a toggle row without an action";
        const Report segment = CheckStyle("<root><element type=\"UISegmented\" name=\"G\"><children><element type=\"UIElement\" name=\"S\"><components><component type=\"UIInputClicker\" value=\"1\" "
                                          "/></components></element></children></element></root>");
        EXPECT_TRUE(segment.violations.empty());
    }

    TEST_F(MahjongUiStyleTest, TheNamesOfThePaletteAreUniqueAndItsColoursAreDifferent) {
        for (size_t i = 0; i < kUiPalette.size(); ++i) {
            for (size_t j = i + 1; j < kUiPalette.size(); ++j) {
                EXPECT_STRNE(kUiPaletteNames[i], kUiPaletteNames[j]);
                EXPECT_FALSE(SameColor(kUiPalette[i], kUiPalette[j])) << kUiPaletteNames[i] << " and " << kUiPaletteNames[j] << " are one colour";
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
