#include "ShippedScene.h"
#include "UiStyleTestSupport.h"

#include <BECore/UI/UIValueText.h>
#include <cmath>

/**
 * The look of the shipped scene file held to the style of `Table/UiPalette.h` (pure logic: the files are read from the source tree and parsed
 * with pugixml, no engine services). Every `element` of the shipped scene (`config/SceneConfig.xml` with its node files expanded, `ShippedScene.h`) is walked: each colour (also the `textColor` of a
 * `UITextComponent`) is an entry of the palette, each `fontSize` is on the type scale, each position and size is a multiple of the grid unit (a percent and the tile pictures of the buttons excepted),
 * and each button (a `UIInputClicker`) and toggle row has an action (a segment of a choice group may take its action from the group). A later entry that adds a control with a stray colour, size or
 * number fails here; a deliberately broken copy of the text shows that each check can fail. The skin of a box (`Table/UiPalette.h`) is held too: the colours of `borderColor`, `gradientColor`,
 * `highlight`, of `shadow` and `glow` (`offsetX,offsetY,blur,spread,color`), of `textOutline` / `textShadow` and the tints of a picture are palette entries, a `cornerRadius` is on the scale of the
 * skin (or half the side of a round button), a `borderWidth` is the one width, the numbers of a shadow are on the grid, and a font family and weight are the two of the game. The `set` nodes of a
 * variant (`<set element=".." fontSize/x/y/width/height=..>`) are held to the font and grid rules too.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        CORE_ENUM(Rule, uint8_t, Colour, FontSize, Grid, Action, Scale, Family)

        struct Violation {
            Rule rule;
            std::string element;
            std::string attribute;
            std::string value;
        };

        constexpr const char* kColourAttributes[] = {"color", "hoverColor", "pressedColor", "disabledColor", "selectedColor", "trackColor", "fillColor", "thumbColor"};
        /// ... and the colour of a text, which sits on its `UITextComponent` (the tints of a picture are white by design: not a look).
        constexpr const char* kComponentColourAttributes[] = {"textColor"};
        /// The skin of a box (`Table/UiPalette.h`): the colours of the border, of the end of the gradient and of the top line, the tints of a picture (the icon of a button; a hover tint
        /// of the palette's white stays white) ...
        constexpr const char* kSkinColourAttributes[] = {"borderColor", "gradientColor", "highlight"};
        constexpr const char* kTintAttributes[] = {"tint", "iconTint", "hoverTint", "pressedTint", "disabledTint", "spriteTint"};
        /// ... and the shadow and the glow, `offsetX,offsetY,blur,spread,color`.
        constexpr const char* kEffectAttributes[] = {"shadow", "glow"};
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

        bool OnTheCornerScale(float radius) {
            return radius == 0.0f || std::any_of(kCornerScale.begin(), kCornerScale.end(), [&](int entry) { return static_cast<float>(entry) == radius; });
        }

        bool IsFontFamily(const std::string& family) {
            return family == kFontFamilyDisplay || family == kFontFamilyText;
        }

        bool IsFontWeight(const std::string& weight) {
            const std::optional<float> value = UIText::ParseFiniteFloat(weight.c_str());
            return value && (*value == static_cast<float>(kFontWeightRegular) || *value == static_cast<float>(kFontWeightBold));
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

        /// A round button has a radius of half its side: width and height are one number of pixels and the radius is half of it.
        bool IsRoundBox(const pugi::xml_node& node, float radius) {
            const std::optional<UILength> width = UIText::ParseLength(node.attribute("width").as_string());
            const std::optional<UILength> height = UIText::ParseLength(node.attribute("height").as_string());
            return width && height && width->unit == UIUnit::Pixels && height->unit == UIUnit::Pixels && width->value == height->value && width->value == 2.0f * radius;
        }

        /// The corner radius and the border width of an `element` or of a `set` of a variant: the radii are on the scale of the skin (or round), the border is one width.
        void CheckSkinScale(const pugi::xml_node& node, const std::string& name, std::vector<Violation>& found) {
            if (node.attribute("cornerRadius")) {
                const std::optional<UICorners> corners = UIText::ParseCorners(node.attribute("cornerRadius").as_string());
                const auto fits = [&](float radius) { return OnTheCornerScale(radius) || IsRoundBox(node, radius); };
                if (!corners || !fits(corners->topLeft) || !fits(corners->topRight) || !fits(corners->bottomRight) || !fits(corners->bottomLeft)) {
                    found.push_back({Rule::Scale, name, "cornerRadius", AttributeOf(node, "cornerRadius")});
                }
            }
            if (node.attribute("borderWidth")) {
                const std::optional<float> width = UIText::ParseFiniteFloat(node.attribute("borderWidth").as_string());
                if (!width || (*width != 0.0f && *width != static_cast<float>(kBorderWidth))) {
                    found.push_back({Rule::Scale, name, "borderWidth", AttributeOf(node, "borderWidth")});
                }
            }
        }

        /// The colours of the skin of an element and its shadow and glow (colour in the palette, numbers on the grid).
        void CheckSkinPaint(const pugi::xml_node& element, const std::string& name, std::vector<Violation>& found) {
            for (const char* attribute : kSkinColourAttributes) {
                if (element.attribute(attribute)) {
                    const std::optional<Color> color = UIText::ParseColor(element.attribute(attribute).as_string());
                    if (!color || !OnThePalette(*color)) {
                        found.push_back({Rule::Colour, name, attribute, AttributeOf(element, attribute)});
                    }
                }
            }
            for (const char* attribute : kEffectAttributes) {
                if (!element.attribute(attribute)) {
                    continue;
                }
                const std::optional<UIShadowSpec> spec = UIText::ParseShadow(element.attribute(attribute).as_string());
                if (!spec || !OnThePalette(spec->color)) {
                    found.push_back({Rule::Colour, name, attribute, AttributeOf(element, attribute)});
                }
                if (!spec || !OnTheGrid(spec->offsetX) || !OnTheGrid(spec->offsetY) || !OnTheGrid(spec->blur) || !OnTheGrid(spec->spread)) {
                    found.push_back({Rule::Grid, name, attribute, AttributeOf(element, attribute)});
                }
            }
        }

        /// What a `UITextComponent` and a `UIImageComponent` bring: the outline and shadow colours of a text, the tints of a picture, the font family and weight.
        void CheckSkinComponent(const pugi::xml_node& component, const std::string& name, std::vector<Violation>& found) {
            const auto flag = [&](Rule rule, const char* attribute) { found.push_back({rule, name, attribute, AttributeOf(component, attribute)}); };
            for (const char* attribute : kTintAttributes) {
                if (component.attribute(attribute)) {
                    const std::optional<Color> color = UIText::ParseColor(component.attribute(attribute).as_string());
                    if (!color || !OnThePalette(*color)) {
                        flag(Rule::Colour, attribute);
                    }
                }
            }
            if (component.attribute("textOutline")) {
                float width = 0.0f;
                Color color;
                if (!UIText::ParseTextOutline(component.attribute("textOutline").as_string(), width, color) || !OnThePalette(color)) {
                    flag(Rule::Colour, "textOutline");
                }
            }
            if (component.attribute("textShadow")) {
                float offsetX = 0.0f;
                float offsetY = 0.0f;
                Color color;
                if (!UIText::ParseTextShadow(component.attribute("textShadow").as_string(), offsetX, offsetY, color) || !OnThePalette(color)) {
                    flag(Rule::Colour, "textShadow");
                }
            }
            if (component.attribute("fontFamily") && !IsFontFamily(AttributeOf(component, "fontFamily"))) {
                flag(Rule::Family, "fontFamily");
            }
            if (component.attribute("fontWeight") && !IsFontWeight(AttributeOf(component, "fontWeight"))) {
                flag(Rule::Family, "fontWeight");
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
                CheckSkinComponent(component, name, found);
            }

            CheckSkinPaint(element, name, found);
            CheckSkinScale(element, name, found);
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
                    CheckSkinScale(child, name, found);
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
        /// A button with the whole skin, every number and colour of it on the palette and the scale.
        constexpr const char* kSkinAttributes =
            "type=\"UIElement\" name=\"Skin\" cornerRadius=\"12\" borderWidth=\"2\" borderColor=\"140,150,180,255\" gradientDirection=\"Vertical\" gradientColor=\"26,78,40,255\" "
            "shadow=\"0,4,8,0,0,0,0,90\" glow=\"0,0,8,0,90,200,120,160\" highlight=\"255,255,255,60\"";
        constexpr const char* kSkinText = "<component type=\"UITextComponent\" fontFamily=\"Russo One\" fontWeight=\"700\" textOutline=\"2,16,18,26,255\" textShadow=\"0,2,0,0,0,90\" />"
                                          "<component type=\"UIImageComponent\" tint=\"255,255,255,255\" hoverTint=\"255,255,255,255\" />";

        /// The skin of `kSkinAttributes` with the first `from` of it replaced by `to`, in an element with the text and picture of `kSkinText`.
        Report SkinWith(const std::string& from, const std::string& to, const std::string& components = kSkinText) {
            std::string attributes = kSkinAttributes;
            const size_t at = attributes.find(from);
            EXPECT_NE(at, std::string::npos) << from << " is not in the skin";
            if (at != std::string::npos) {
                attributes.replace(at, from.size(), to);
            }
            return CheckStyle(OneElement(attributes, components));
        }

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

    TEST_F(MahjongUiStyleTest, EveryCornerBorderAndFontFamilyOfTheFileIsOnTheScaleOfTheSkin) {
        EXPECT_TRUE(Of(_report, Rule::Scale).empty()) << Describe(Of(_report, Rule::Scale));
        EXPECT_TRUE(Of(_report, Rule::Family).empty()) << Describe(Of(_report, Rule::Family));
    }

    TEST_F(MahjongUiStyleTest, TheWholeSkinOnThePaletteAndTheScaleIsNoViolation) {
        EXPECT_TRUE(CheckStyle(OneElement(kSkinAttributes, kSkinText)).violations.empty());
        // four radii (a header band: round on top only), the zero border of a row, a state tint, a hard shadow, the other family
        EXPECT_TRUE(SkinWith("cornerRadius=\"12\"", "cornerRadius=\"16,16,0,0\"").violations.empty());
        EXPECT_TRUE(SkinWith("borderWidth=\"2\"", "borderWidth=\"0\"").violations.empty());
        EXPECT_TRUE(CheckStyle(OneElement(kSkinAttributes, Broken(kSkinText, "Russo One", "PT Sans"))).violations.empty());
        EXPECT_TRUE(SkinWith("shadow=\"0,4,8,0,0,0,0,90\"", "shadow=\"0,4,0,0,0,0,0,90\"").violations.empty());
    }

    TEST_F(MahjongUiStyleTest, ASkinColourOffThePaletteIsCaught) {
        for (const auto& [from, to] : {std::pair<const char*, const char*>{"borderColor=\"140,150,180,255\"", "borderColor=\"140,150,181,255\""},
                                       {"gradientColor=\"26,78,40,255\"", "gradientColor=\"26,78,41,255\""},
                                       {"highlight=\"255,255,255,60\"", "highlight=\"255,255,255,61\""},
                                       {"shadow=\"0,4,8,0,0,0,0,90\"", "shadow=\"0,4,8,0,0,0,0,91\""},
                                       {"glow=\"0,0,8,0,90,200,120,160\"", "glow=\"0,0,8,0,90,200,120,161\""},
                                       {"borderColor=\"140,150,180,255\"", "borderColor=\"grey\""},
                                       {"shadow=\"0,4,8,0,0,0,0,90\"", "shadow=\"soft\""}}) {
            const Report report = SkinWith(from, to);
            EXPECT_FALSE(Of(report, Rule::Colour).empty()) << to;
            EXPECT_TRUE(Of(report, Rule::Scale).empty()) << to;
            EXPECT_TRUE(Of(report, Rule::Family).empty()) << to;
        }
    }

    TEST_F(MahjongUiStyleTest, ATextOutlineShadowOrTintOffThePaletteIsCaught) {
        for (const auto& [from, to] : {std::pair<const char*, const char*>{"textOutline=\"2,16,18,26,255\"", "textOutline=\"2,16,18,26,254\""},
                                       {"textShadow=\"0,2,0,0,0,90\"", "textShadow=\"0,2,0,0,0,91\""},
                                       {"tint=\"255,255,255,255\" hoverTint", "tint=\"255,255,254,255\" hoverTint"},
                                       {"hoverTint=\"255,255,255,255\"", "hoverTint=\"250,250,250,255\""}}) {
            std::string components = kSkinText;
            const size_t at = components.find(from);
            ASSERT_NE(at, std::string::npos) << from;
            components.replace(at, std::string(from).size(), to);
            const Report report = CheckStyle(OneElement(kSkinAttributes, components));
            EXPECT_EQ(Of(report, Rule::Colour).size(), 1u) << to;
            EXPECT_EQ(report.violations.size(), 1u) << to;
        }
    }

    TEST_F(MahjongUiStyleTest, ACornerRadiusOrBorderWidthOffTheScaleIsCaught) {
        for (const auto& [from, to] : {std::pair<const char*, const char*>{"cornerRadius=\"12\"", "cornerRadius=\"10\""},
                                       {"cornerRadius=\"12\"", "cornerRadius=\"16,16,16,9\""},
                                       {"cornerRadius=\"12\"", "cornerRadius=\"big\""},
                                       {"borderWidth=\"2\"", "borderWidth=\"3\""}}) {
            const Report report = SkinWith(from, to);
            EXPECT_EQ(Of(report, Rule::Scale).size(), 1u) << to;
            EXPECT_EQ(report.violations.size(), 1u) << to;
        }
        // a round button has half its side, and only when it is a square
        EXPECT_TRUE(CheckStyle(OneElement("type=\"UIElement\" name=\"Round\" width=\"56\" height=\"56\" cornerRadius=\"28\"", kClicker)).violations.empty());
        EXPECT_FALSE(CheckStyle(OneElement("type=\"UIElement\" name=\"Round\" width=\"56\" height=\"48\" cornerRadius=\"28\"", kClicker)).violations.empty());
        EXPECT_FALSE(CheckStyle(OneElement("type=\"UIElement\" name=\"Round\" width=\"56\" height=\"56\" cornerRadius=\"24\"", kClicker)).violations.empty());
        // the `set` of a variant is held to the same scale
        const std::string variant = "<root><element type=\"UIElement\" name=\"A\"><components><component type=\"UITextComponent\" /></components></element><variant><set element=\"A\" "
                                    "cornerRadius=\"12\" /></variant></root>";
        EXPECT_TRUE(CheckStyle(variant).violations.empty());
        EXPECT_FALSE(Of(CheckStyle(Broken(variant, "cornerRadius=\"12\"", "cornerRadius=\"13\"")), Rule::Scale).empty());
    }

    TEST_F(MahjongUiStyleTest, TheNumbersOfAShadowOrGlowOffTheGridAreCaught) {
        for (const auto& [from, to] : {std::pair<const char*, const char*>{"shadow=\"0,4,8,0,0,0,0,90\"", "shadow=\"0,3,8,0,0,0,0,90\""},
                                       {"shadow=\"0,4,8,0,0,0,0,90\"", "shadow=\"2,4,8,0,0,0,0,90\""},
                                       {"shadow=\"0,4,8,0,0,0,0,90\"", "shadow=\"0,4,6,0,0,0,0,90\""},
                                       {"shadow=\"0,4,8,0,0,0,0,90\"", "shadow=\"0,4,8,1,0,0,0,90\""},
                                       {"glow=\"0,0,8,0,90,200,120,160\"", "glow=\"0,0,10,0,90,200,120,160\""}}) {
            const Report report = SkinWith(from, to);
            EXPECT_EQ(Of(report, Rule::Grid).size(), 1u) << to;
            EXPECT_EQ(report.violations.size(), 1u) << to;
        }
    }

    TEST_F(MahjongUiStyleTest, AFontFamilyOrWeightOfNoneOfTheGameIsCaught) {
        for (const auto& [from, to] :
             {std::pair<const char*, const char*>{"fontFamily=\"Russo One\"", "fontFamily=\"Arial\""}, {"fontWeight=\"700\"", "fontWeight=\"600\""}, {"fontWeight=\"700\"", "fontWeight=\"bold\""}}) {
            std::string components = kSkinText;
            const size_t at = components.find(from);
            ASSERT_NE(at, std::string::npos) << from;
            components.replace(at, std::string(from).size(), to);
            const Report report = CheckStyle(OneElement(kSkinAttributes, components));
            EXPECT_EQ(Of(report, Rule::Family).size(), 1u) << to;
            EXPECT_EQ(report.violations.size(), 1u) << to;
        }
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
