#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UISegmented.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The page `Options` of the shipped scene (`config/nodes/MahjongOptions.xml`): its shape on the whole size matrix in every language, the
 * words, the touch size, that nothing behind it takes input, the effect of each row on the game, and that `Back` and `CloseSetup`
 * close it with `Setup` and its pending choice left as they were. The rows of segments are one list (an advisor or a coach row is one
 * entry more); the level is the dropdown of the ten levels, whose open list is checked on the matrix too.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// A row of segments of the page: the names in the file, the action and variable it carries and a segment (not the chosen one)
        /// with the value that segment sets.
        struct PageRow {
            const char* row;
            const char* caption;
            const char* group;
            const char* variable;
            const char* action;
            const char* firstSegment;
            const char* firstValue;
            const char* secondSegment;
            const char* secondValue;
        };

        constexpr PageRow kRows[] = {
            {"LanguageRow", "LanguageCaption", "Language", "language", "MahjongSetLanguage", "LanguageEn", "en", "LanguageRu", "ru"},
            {"HintsRow", "HintsCaption", "HintsChoice", "hintsChoice", "MahjongSetHints", "HintsOn", "on", "HintsOff", "off"},
        };
        constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

        /// The row of the level: a caption and the dropdown `LevelChoice` of the options `Level1` .. `Level10`, under the rows of segments.
        constexpr const char* kLevelRow = "LevelRow";
        constexpr const char* kLevelCaption = "LevelCaption";
        constexpr const char* kLevelChoice = "LevelChoice";
        /// The row of the speed: a caption and the three segments `SpeedSlow`, `SpeedNormal`, `SpeedFast` of the group `SpeedChoice`, between hints and level.
        constexpr const char* kSpeedRow = "SpeedRow";
        constexpr const char* kSpeedCaption = "SpeedCaption";
        constexpr const char* kSpeedChoice = "SpeedChoice";
        constexpr size_t kPageRowCount = kRowCount + 2;

        /// The segments in the order of `EnumUtils<AnimationSpeed>::Values()`.
        constexpr const char* kSpeedSegments[] = {"SpeedSlow", "SpeedNormal", "SpeedFast"};
        static_assert(sizeof(kSpeedSegments) / sizeof(kSpeedSegments[0]) == EnumUtils<AnimationSpeed>::Count());

        std::string LevelOption(ComputerLevel level) {
            return "Level" + std::string(LevelText(level));
        }

        /// Everything that takes input or shows a word on the page, for the matrix tests.
        std::vector<const char*> Controls() {
            std::vector<const char*> names;
            for (const PageRow& row : kRows) {
                names.push_back(row.firstSegment);
                names.push_back(row.secondSegment);
            }
            for (const char* segment : kSpeedSegments) {
                names.push_back(segment);
            }
            names.push_back(kLevelChoice);
            names.push_back("Back");
            return names;
        }

        constexpr float kButtonAir = 8.0f;
        constexpr float kRowPadding = 4.0f;
        constexpr float kRoomyPlateHeight = kModalPadding * 2.0f + kModalTitleHeight + 64.0f * kPageRowCount + kMainButtonHeight + kModalGap * (kPageRowCount + 2);
        constexpr float kMinControlHeight = 48.0f;

        constexpr struct {
            float width;
            float height;
        } kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}, {1707.0f, 480.0f}};

        MatrixWindow DesignWindow() {
            MatrixWindow window;
            window.label = "design 1280x720";
            window.outputWidth = kViewWidth;
            window.outputHeight = kViewHeight;
            return window;
        }

        MatrixWindow WindowOf(float width, float height) {
            MatrixWindow window = DesignWindow();
            window.label = (std::to_string(static_cast<int>(width)) + "x" + std::to_string(static_cast<int>(height))).c_str();
            window.outputWidth = width;
            window.outputHeight = height;
            return window;
        }

    }  // namespace

    class MahjongOptionsTest : public MahjongTableTestBase {
    protected:
        /// A started game with `Setup` and the page over it, drawn.
        void OpenPage() {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            _game->OpenSetup();
            Draw();
            _game->OpenOptions();
            Draw();
            ASSERT_TRUE(PageShown());
        }

        bool PageShown() const {
            const auto canvas = Canvas("Options");
            return canvas && canvas->TakesInput();
        }

        UIDropdown* Dropdown() const {
            UIElement* element = const_cast<UIElement*>(Element("Options", kLevelChoice));
            return element != nullptr ? element->Cast<UIDropdown>() : nullptr;
        }

        /// Opens the list by a click on the header, then clicks the option of `level`, as a person does.
        void ChooseLevel(ComputerLevel level) {
            ASSERT_TRUE(ClickElement("Options", kLevelChoice));
            Draw();
            ASSERT_NE(Dropdown(), nullptr);
            ASSERT_TRUE(Dropdown()->IsOpen()) << "a click on the header opens the list";
            ASSERT_TRUE(ClickElement("Options", LevelOption(level).c_str()));
            Draw();
            EXPECT_FALSE(Dropdown()->IsOpen()) << "a choice closes the list";
        }

        bool SetupShown() const {
            const auto canvas = Canvas("Setup");
            return canvas && canvas->TakesInput();
        }

        bool Selected(const char* segment) const {
            const UIElement* element = Element("Options", segment);
            const auto* button = element != nullptr ? element->Cast<UIButton>() : nullptr;
            return button != nullptr && button->selected;
        }

        std::string Text(const char* element) const {
            const UIElement* found = Element("Options", element);
            const UILabel* label = found != nullptr ? found->Cast<UILabel>() : nullptr;
            if (label == nullptr && found != nullptr) {
                label = found->Cast<UIButton>();  // a button is a label by inheritance, but `Cast` is exact
            }
            if (found != nullptr && found->Cast<UIDropdown>() != nullptr) {
                const UIButton* chosen = found->Cast<UIDropdown>()->FindChosenOption();  // the header shows the chosen option
                return chosen != nullptr ? std::string(chosen->GetDisplayText().data(), chosen->GetDisplayText().size()) : std::string("<none chosen>");
            }
            return label != nullptr ? std::string(label->GetDisplayText().data(), label->GetDisplayText().size()) : std::string("<no label>");
        }
    };

    // ----- the shape -----

    TEST_F(MahjongOptionsTest, ThePageIsHiddenUntilItIsOpenAndShownExactlyWhileTheGameSaysSo) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        EXPECT_FALSE(PageShown());
        _game->OpenSetup();
        Draw();
        EXPECT_FALSE(PageShown()) << "Setup alone does not show the page";
        _game->OpenOptions();
        Draw();
        EXPECT_TRUE(PageShown());
        EXPECT_TRUE(SetupShown()) << "Setup stays up under the page";
        _game->CloseOptions();
        Draw();
        EXPECT_FALSE(PageShown());
        EXPECT_TRUE(SetupShown());
    }

    TEST_F(MahjongOptionsTest, ThePageIsAModalScreenCanvasOverSetupAndHasEveryRowOfTheList) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        const auto page = Canvas("Options");
        const auto setup = Canvas("Setup");
        ASSERT_TRUE(page && setup);
        EXPECT_EQ(page->GetLayout().params.space, CanvasSpace::Screen);
        EXPECT_EQ(page->GetLayout().params.scaleMode, CanvasScaleMode::ScaleWithScreen);
        EXPECT_GE(page->GetLayout().params.minScale, 0.6f) << "a floor keeps a 56 unit control above 32 px";
        EXPECT_EQ(page->GetLayout().params.maxScale, UICanvasScaleParams{}.maxScale);
        for (const PageRow& row : kRows) {
            for (const char* name : {row.row, row.caption, row.group, row.firstSegment, row.secondSegment}) {
                EXPECT_NE(Element("Options", name), nullptr) << name;
            }
            const auto* group = Element("Options", row.group) != nullptr ? Element("Options", row.group)->Cast<UISegmented>() : nullptr;
            ASSERT_NE(group, nullptr) << row.group;
            EXPECT_EQ(group->bind, PoolString::Intern(row.variable)) << row.group;
            for (const auto* segment : {row.firstSegment, row.secondSegment}) {
                const UIElement* element = Element("Options", segment);
                const auto* button = element != nullptr ? element->Cast<UIButton>() : nullptr;
                ASSERT_NE(button, nullptr) << segment;
                EXPECT_EQ(button->action, PoolString::Intern(row.action)) << segment;
            }
        }
        for (const char* name : {kSpeedRow, kSpeedCaption, kLevelRow, kLevelCaption}) {
            EXPECT_NE(Element("Options", name), nullptr) << name;
        }
        const auto* speedGroup = Element("Options", kSpeedChoice) != nullptr ? Element("Options", kSpeedChoice)->Cast<UISegmented>() : nullptr;
        ASSERT_NE(speedGroup, nullptr);
        EXPECT_EQ(speedGroup->bind, "speedChoice"_intern);
        ASSERT_EQ(speedGroup->children.size(), EnumUtils<AnimationSpeed>::Count()) << "one segment per speed, slowest first";
        for (size_t index = 0; index < EnumUtils<AnimationSpeed>::Count(); ++index) {
            const auto* segment = speedGroup->children[index]->Cast<UIButton>();
            ASSERT_NE(segment, nullptr) << index;
            EXPECT_EQ(segment->name.ToStringView(), kSpeedSegments[index]);
            EXPECT_EQ(segment->action, "MahjongSetSpeed"_intern);
            EXPECT_EQ(segment->value.ToStringView(), SpeedText(EnumUtils<AnimationSpeed>::Values()[index]).data());
        }
        const UIDropdown* level = Dropdown();
        ASSERT_NE(level, nullptr);
        EXPECT_EQ(level->bind, "levelChoice"_intern);
        EXPECT_EQ(level->action, "MahjongSetLevel"_intern);
        ASSERT_EQ(level->children.size(), static_cast<size_t>(kLevelCount)) << "one option per level, nothing else";
        for (size_t index = 0; index < level->children.size(); ++index) {
            const ComputerLevel expected = kAllLevels[index];
            const auto* option = level->children[index]->Cast<UIButton>();
            ASSERT_NE(option, nullptr) << index;
            EXPECT_EQ(option->name.ToStringView(), LevelOption(expected).c_str());
            EXPECT_EQ(option->value.ToStringView(), LevelText(expected).data()) << "weakest first, the value is the number";
        }
        const auto* back = Element("Options", "Back") != nullptr ? Element("Options", "Back")->Cast<UIButton>() : nullptr;
        ASSERT_NE(back, nullptr);
        EXPECT_EQ(back->action, "MahjongOptionsBack"_intern);
    }

    TEST_F(MahjongOptionsTest, ThePagePassesTheSizeMatrixInEveryLanguageAndEveryWordFitsAndEveryControlIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            Draw();
            Canvas("Options")->UpdateLayout(kViewWidth, kViewHeight);
            for (const char* name : Controls()) {
                const UIElement* control = Element("Options", name);
                ASSERT_NE(control, nullptr) << name;
                EXPECT_GE(control->rect.h, kMinControlHeight) << name;
            }
            ExpectPassesTheMatrix("Options", EngineTouchMinimum());
            for (const auto& output : kOutputs) {
                ExpectWordsFit("Options", output.width, output.height, kButtonAir);
            }
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("Options"), kButtonAir);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
    }

    TEST_F(MahjongOptionsTest, ThePageShowsTheWordsOfTheTableInTheLanguageOfTheGame) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            const UIStrings& strings = CoreManager::GetUIStrings();
            Draw();
            EXPECT_EQ(Text("Title"), Tr(strings, "options.title").c_str());
            EXPECT_EQ(Text("Back"), Tr(strings, "options.back").c_str());
            EXPECT_EQ(Text("LanguageCaption"), Tr(strings, "setup.language").c_str());
            EXPECT_EQ(Text("HintsCaption"), Tr(strings, "setup.hints").c_str());
            EXPECT_EQ(Text(kSpeedCaption), Tr(strings, "options.speed").c_str());
            EXPECT_EQ(Text("SpeedSlow"), Tr(strings, "options.speedSlow").c_str());
            EXPECT_EQ(Text("SpeedNormal"), Tr(strings, "options.speedNormal").c_str());
            EXPECT_EQ(Text("SpeedFast"), Tr(strings, "options.speedFast").c_str());
            EXPECT_EQ(Text("LevelCaption"), Tr(strings, "setup.level").c_str());
            EXPECT_EQ(Text(kLevelChoice), LevelText(_game->GetLevel())) << "the header shows the level of the game";
        });
    }

    TEST_F(MahjongOptionsTest, ThePlateLiesInTheSafeAreaAndTheRowsInThePlateOneUnderTheOtherAndEveryControlIsATouchTargetInEveryWindow) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        auto page = Canvas("Options");
        const float minTouch = EngineTouchMinimum();
        std::vector<const char*> bands{"Title"};
        for (const PageRow& row : kRows) {
            bands.push_back(row.row);
        }
        bands.insert(bands.begin() + 1 + kRowCount, kSpeedRow);
        bands.push_back(kLevelRow);
        bands.push_back("Back");

        const CanvasScaleMode modes[] = {CanvasScaleMode::ConstantPixelSize, CanvasScaleMode::ScaleWithScreen, CanvasScaleMode::ConstantPhysicalSize};
        size_t cases = 0;
        ForEachMatrixWindow([&](const MatrixWindow& window) {
            for (const CanvasScaleMode mode : modes) {
                for (const float userScale : {1.0f, 1.5f}) {
                    const std::string label = Format("{} {} x{}", window.label.c_str(), EnumUtils<CanvasScaleMode>::ToString(mode).data(), userScale).c_str();
                    const UICanvasResolved resolved = LayOutInWindow(*page, window, mode, userScale);
                    const UIElement* plate = Element("Options", "Plate");
                    ASSERT_NE(plate, nullptr);
                    ASSERT_FALSE(plate->rect.IsEmpty()) << label;
                    const Rect& safe = resolved.contentBounds;
                    EXPECT_GE(plate->rect.x, safe.x - 0.01f) << label;
                    EXPECT_GE(plate->rect.y, safe.y - 0.01f) << label;
                    EXPECT_LE(plate->rect.x + plate->rect.w, safe.x + safe.w + 0.01f) << label;
                    EXPECT_LE(plate->rect.y + plate->rect.h, safe.y + safe.h + 0.01f) << label;

                    const UIElement* previous = nullptr;
                    for (const char* name : bands) {
                        const UIElement* band = Element("Options", name);
                        ASSERT_NE(band, nullptr) << name;
                        if (band->rect.IsEmpty()) {
                            EXPECT_STREQ(name, "Title") << label << ": only the title may give way";
                            continue;
                        }
                        EXPECT_GE(band->rect.y, plate->rect.y - 0.01f) << label << " " << name;
                        EXPECT_LE(band->rect.y + band->rect.h, plate->rect.y + plate->rect.h + 0.01f) << label << " " << name;
                        if (previous != nullptr) {
                            EXPECT_GE(band->rect.y, previous->rect.y + previous->rect.h - 0.01f) << label << " " << name << " overlaps the one above";
                        }
                        previous = band;
                    }
                    for (const char* name : Controls()) {
                        const UIElement* control = Element("Options", name);
                        ASSERT_NE(control, nullptr) << name;
                        ASSERT_FALSE(control->rect.IsEmpty()) << label << " " << name;
                        EXPECT_GE(control->rect.x, plate->rect.x - 0.01f) << label << " " << name;
                        EXPECT_LE(control->rect.x + control->rect.w, plate->rect.x + plate->rect.w + 0.01f) << label << " " << name;
                        EXPECT_GE(std::min(control->rect.w, control->rect.h) * resolved.scale / window.pixelDensity, minTouch - 0.01f) << label << " " << name;
                    }
                    ++cases;
                }
            }
        });
        EXPECT_EQ(cases, BECore::Tests::UILayoutMatrix().size() * 6);
    }

    TEST_F(MahjongOptionsTest, ThePageIsTheModalAnatomyWhereThereIsRoomAndTheTwoVariantsGiveWayInASmallCanvas) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        auto page = Canvas("Options");

        // The design size: the plate of 448, the title band of 48, the rows of 64, Back of 64 set 16 below the rows
        LayOutInWindow(*page, DesignWindow());
        const Rect plate = Element("Options", "Plate")->rect;
        EXPECT_NEAR(plate.h, kRoomyPlateHeight, 0.01f);
        EXPECT_NEAR(plate.h, 448.0f, 0.01f);
        EXPECT_TRUE(SameColor(Element("Options", "Plate")->color, PaletteColor("surface")));
        EXPECT_TRUE(SameColor(Element("Options", "Dim")->color, PaletteColor("scrim")));
        const UIElement* title = Element("Options", "Title");
        ASSERT_NE(title, nullptr);
        EXPECT_NEAR(title->rect.h, kModalTitleHeight, 0.01f);
        EXPECT_TRUE(SameColor(title->color, PaletteColor("header")));
        EXPECT_EQ(title->rect.x, plate.x + kModalPadding);
        EXPECT_EQ(title->rect.y, plate.y + kModalPadding);

        const Rect* above = &title->rect;
        for (const PageRow& entry : kRows) {
            SCOPED_TRACE(entry.row);
            const UIElement* row = Element("Options", entry.row);
            ASSERT_NE(row, nullptr);
            EXPECT_NEAR(row->rect.h, 64.0f, 0.01f);
            EXPECT_TRUE(SameColor(row->color, PaletteColor("surfaceRaised")));
            EXPECT_EQ(GapBelow(*above, row->rect), kModalGap);
            above = &row->rect;
            const UIElement* caption = Element("Options", entry.caption);
            const UIElement* group = Element("Options", entry.group);
            ASSERT_TRUE(caption != nullptr && group != nullptr);
            EXPECT_NEAR(caption->rect.w, 112.0f, 0.01f);
            EXPECT_TRUE(SameColor(caption->Cast<UILabel>()->textColor, PaletteColor("textMuted")));
            EXPECT_EQ(caption->rect.x, row->rect.x + kRowPadding);
            EXPECT_EQ(group->rect.x, caption->rect.x + caption->rect.w + kModalGap);
            EXPECT_EQ(group->rect.x + group->rect.w, row->rect.x + row->rect.w - kRowPadding);
            EXPECT_EQ(group->rect.y, row->rect.y + kRowPadding);
            EXPECT_EQ(group->rect.y + group->rect.h, row->rect.y + row->rect.h - kRowPadding);
            for (const char* name : {entry.firstSegment, entry.secondSegment}) {
                const auto* segment = Element("Options", name)->Cast<UIButton>();
                ASSERT_NE(segment, nullptr) << name;
                EXPECT_TRUE(SameColor(segment->selectedColor, PaletteColor("primary"))) << name;
                EXPECT_TRUE(SameColor(segment->color, PaletteColor("neutral"))) << name;
                EXPECT_EQ(segment->rect.h, group->rect.h) << name;
            }
        }
        {
            SCOPED_TRACE(kSpeedRow);
            const UIElement* row = Element("Options", kSpeedRow);
            ASSERT_NE(row, nullptr);
            EXPECT_NEAR(row->rect.h, 64.0f, 0.01f);
            EXPECT_TRUE(SameColor(row->color, PaletteColor("surfaceRaised")));
            EXPECT_EQ(GapBelow(*above, row->rect), kModalGap);
            above = &row->rect;
            const UIElement* caption = Element("Options", kSpeedCaption);
            const UIElement* group = Element("Options", kSpeedChoice);
            ASSERT_TRUE(caption != nullptr && group != nullptr);
            EXPECT_NEAR(caption->rect.w, 96.0f, 0.01f) << "narrower than the others: the three words need the room";
            EXPECT_TRUE(SameColor(caption->Cast<UILabel>()->textColor, PaletteColor("textMuted")));
            EXPECT_EQ(group->rect.x, caption->rect.x + caption->rect.w + kModalGap);
            EXPECT_EQ(group->rect.x + group->rect.w, row->rect.x + row->rect.w - kRowPadding);
            EXPECT_EQ(group->rect.y, row->rect.y + kRowPadding);
            EXPECT_EQ(group->rect.y + group->rect.h, row->rect.y + row->rect.h - kRowPadding);
            for (const char* name : kSpeedSegments) {
                const auto* segment = Element("Options", name)->Cast<UIButton>();
                ASSERT_NE(segment, nullptr) << name;
                EXPECT_TRUE(SameColor(segment->selectedColor, PaletteColor("primary"))) << name;
                EXPECT_TRUE(SameColor(segment->color, PaletteColor("neutral"))) << name;
                EXPECT_EQ(segment->rect.h, group->rect.h) << name;
            }
        }
        {
            SCOPED_TRACE(kLevelRow);
            const UIElement* row = Element("Options", kLevelRow);
            ASSERT_NE(row, nullptr);
            EXPECT_NEAR(row->rect.h, 64.0f, 0.01f);
            EXPECT_TRUE(SameColor(row->color, PaletteColor("surfaceRaised")));
            EXPECT_EQ(GapBelow(*above, row->rect), kModalGap);
            above = &row->rect;
            const UIElement* caption = Element("Options", kLevelCaption);
            const UIDropdown* group = Dropdown();
            ASSERT_TRUE(caption != nullptr && group != nullptr);
            EXPECT_NEAR(caption->rect.w, 112.0f, 0.01f);
            EXPECT_TRUE(SameColor(caption->Cast<UILabel>()->textColor, PaletteColor("textMuted")));
            EXPECT_EQ(group->rect.x, caption->rect.x + caption->rect.w + kModalGap);
            EXPECT_EQ(group->rect.x + group->rect.w, row->rect.x + row->rect.w - kRowPadding);
            EXPECT_EQ(group->rect.y, row->rect.y + kRowPadding);
            EXPECT_EQ(group->rect.y + group->rect.h, row->rect.y + row->rect.h - kRowPadding);
            EXPECT_TRUE(SameColor(group->color, PaletteColor("neutral")));
        }
        const UIElement* back = Element("Options", "Back");
        ASSERT_NE(back, nullptr);
        EXPECT_TRUE(SameColor(back->color, PaletteColor("primary")));
        EXPECT_EQ(back->rect.h, kMainButtonHeight);
        EXPECT_EQ(GapBelow(*above, back->rect), kModalSectionGap);
        EXPECT_EQ(back->rect.x, plate.x + kModalPadding);
        EXPECT_EQ(back->rect.y + back->rect.h, plate.y + plate.h - kModalPadding) << "the plate holds Back and nothing more";

        // A canvas of 320 units (a 480 px window at the user scale 1.5): no title, narrower captions, the plate the whole safe width
        LayOutInWindow(*page, WindowOf(480.0f, 480.0f), CanvasScaleMode::ConstantPixelSize, 1.5f);
        EXPECT_TRUE(Element("Options", "Title")->rect.IsEmpty());
        EXPECT_LT(Element("Options", kLevelCaption)->rect.w, 112.0f);
        EXPECT_TRUE(Element("Options", kSpeedCaption)->rect.IsEmpty()) << "the caption of the speed gives its room to the three words";
        EXPECT_NEAR(Element("Options", "Plate")->rect.w, page->GetLayout().GetResolved().contentBounds.w, 0.01f);
        for (const PageRow& row : kRows) {
            EXPECT_GT(Element("Options", row.row)->rect.h, 0.0f) << row.row << ": nothing but the title is dropped";
        }
        EXPECT_GT(Element("Options", kSpeedRow)->rect.h, 0.0f) << "nothing but the title is dropped";
        EXPECT_GT(Element("Options", kLevelRow)->rect.h, 0.0f) << "nothing but the title is dropped";
        EXPECT_GT(Element("Options", "Back")->rect.h, 0.0f);

        // A canvas that is only narrow keeps the title
        LayOutInWindow(*page, WindowOf(480.0f, 853.0f), CanvasScaleMode::ConstantPixelSize, 1.5f);
        EXPECT_FALSE(Element("Options", "Title")->rect.IsEmpty());
        EXPECT_LT(Element("Options", "HintsCaption")->rect.w, 112.0f);
    }

    // ----- nothing behind the page takes input -----

    TEST_F(MahjongOptionsTest, TheDimCoversTheWindowSoTheRowsOfSetupAndTheNewGameButtonUnderItTakeNoClick) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        auto page = Canvas("Options");
        auto setup = Canvas("Setup");
        auto menu = Canvas("HudMenu");
        ASSERT_TRUE(page && setup && menu);

        ForEachMatrixWindow([&](const MatrixWindow& window) {
            LayOutInWindow(*page, window);
            const UIElement* dim = Element("Options", "Dim");
            ASSERT_NE(dim, nullptr);
            const Rect dimOutput = page->GetLayout().GetResolved().transform.ToOutput(dim->rect.x, dim->rect.y, dim->rect.w, dim->rect.h);
            EXPECT_LE(dimOutput.x, 0.01f) << window.label.c_str();
            EXPECT_LE(dimOutput.y, 0.01f) << window.label.c_str();
            EXPECT_GE(dimOutput.x + dimOutput.w, window.outputWidth - 0.01f) << window.label.c_str();
            EXPECT_GE(dimOutput.y + dimOutput.h, window.outputHeight - 0.01f) << window.label.c_str();
        });

        // In the design window a click on the place of a control of Setup is the dim's: the choice, the seats and the match stay
        for (const auto& canvas : {setup, page, menu}) {
            LayOutInWindow(*canvas, DesignWindow());
        }
        const SeatSetup seats = MahjongHudComponent::GetActive()->PendingSeats();
        const int hands = MahjongHudComponent::GetActive()->PendingHands();
        for (const char* name : {"Seat2Person", "Seat2Computer", "Hands8", "Start", "Options"}) {
            const UIElement* under = Element("Setup", name);
            if (under == nullptr) {
                continue;  // the button of Setup that leads here comes with the next work package
            }
            ASSERT_TRUE(Click(under->rect.x + under->rect.w * 0.5f, under->rect.y + under->rect.h * 0.5f)) << name;
        }
        EXPECT_TRUE(PageShown());
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_EQ(MahjongHudComponent::GetActive()->PendingSeats(), seats);
        EXPECT_EQ(MahjongHudComponent::GetActive()->PendingHands(), hands);
        ASSERT_TRUE(menu->TakesInput());
        ClickElement("HudMenu", "NewGame");
        EXPECT_TRUE(PageShown());
    }

    // ----- what the rows do -----

    TEST_F(MahjongOptionsTest, EachRowChangesTheGameAndItsVariableAndSelectsItsSegment) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        ASSERT_EQ(_game->GetLevel(), ComputerLevel::Normal);
        ASSERT_TRUE(_game->HintsOn());
        ASSERT_EQ(_game->GetLanguage(), "en"_intern);

        ASSERT_NO_FATAL_FAILURE(ChooseLevel(ComputerLevel::Easy));
        EXPECT_EQ(_game->GetLevel(), ComputerLevel::Easy);
        EXPECT_EQ(Var("levelChoice"), "1");
        EXPECT_EQ(Text(kLevelChoice), "1");

        ASSERT_TRUE(ClickElement("Options", "HintsOff"));
        Draw();
        EXPECT_FALSE(_game->HintsOn());
        EXPECT_EQ(Var("hintsChoice"), "off");
        EXPECT_TRUE(Selected("HintsOff"));
        EXPECT_FALSE(Selected("HintsOn"));

        ASSERT_EQ(_game->GetAnimationSpeed(), AnimationSpeed::Normal);
        EXPECT_TRUE(Selected("SpeedNormal"));
        for (size_t index = 0; index < EnumUtils<AnimationSpeed>::Count(); ++index) {
            const AnimationSpeed speed = EnumUtils<AnimationSpeed>::Values()[index];
            SCOPED_TRACE(SpeedText(speed).data());
            ASSERT_TRUE(ClickElement("Options", kSpeedSegments[index]));
            Draw();
            EXPECT_EQ(_game->GetAnimationSpeed(), speed);
            EXPECT_EQ(Var("speedChoice"), SpeedText(speed).data());
            EXPECT_TRUE(Selected(kSpeedSegments[index]));
        }
        EXPECT_FALSE(Selected("SpeedNormal"));
        EXPECT_EQ(_game->GetAnimationSpeed(), AnimationSpeed::Fast);

        ASSERT_TRUE(ClickElement("Options", "LanguageRu"));
        Draw();
        EXPECT_EQ(_game->GetLanguage(), "ru"_intern);
        EXPECT_EQ(Var("language"), "ru");
        EXPECT_TRUE(Selected("LanguageRu"));
        EXPECT_EQ(Text("Back"), Tr(CoreManager::GetUIStrings(), "options.back").c_str()) << "the page is in the new language at once";

        // And back: every row, both ways, goes through the one list
        for (const PageRow& row : kRows) {
            ASSERT_TRUE(ClickElement("Options", row.firstSegment)) << row.firstSegment;
            Draw();
            EXPECT_TRUE(Selected(row.firstSegment)) << row.firstSegment;
            EXPECT_EQ(Var(row.variable), row.firstValue) << row.variable;
        }
        EXPECT_EQ(_game->GetLevel(), ComputerLevel::Easy);
        EXPECT_TRUE(_game->HintsOn());
        EXPECT_EQ(_game->GetLanguage(), "en"_intern);
    }

    TEST_F(MahjongOptionsTest, EveryLevelOfTheListIsChosenByAClickAndShownInTheHeader) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        for (const ComputerLevel level : kAllLevels) {
            SCOPED_TRACE(LevelText(level).data());
            ASSERT_NO_FATAL_FAILURE(ChooseLevel(level));
            EXPECT_EQ(_game->GetLevel(), level);
            EXPECT_EQ(Var("levelChoice"), LevelText(level).data());
            EXPECT_EQ(Text(kLevelChoice), LevelText(level).data());
        }
    }

    TEST_F(MahjongOptionsTest, TheOpenListOfTheLevelsPassesTheSizeMatrixInEveryLanguage) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        ASSERT_TRUE(ClickElement("Options", kLevelChoice));
        Draw();
        ASSERT_TRUE(Dropdown()->IsOpen());
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            ExpectPassesTheMatrix("Options", EngineTouchMinimum());
            const std::vector<std::string> cut = WordsThatDoNotFitOnTheMatrix(*Canvas("Options"), kButtonAir);
            EXPECT_TRUE(cut.empty()) << cut.size() << " controls cut their text, the first: " << (cut.empty() ? "" : cut.front());
        });
        // In the design window the options are the full touch size and the list lies in the safe area
        Canvas("Options")->UpdateLayout(kViewWidth, kViewHeight);
        for (const ComputerLevel level : kAllLevels) {
            const UIElement* option = Element("Options", LevelOption(level).c_str());
            ASSERT_NE(option, nullptr);
            EXPECT_NEAR(option->rect.h, 56.0f, 0.01f) << LevelText(level);
        }
    }

    TEST_F(MahjongOptionsTest, TheLevelOfThePageIsThatOfTheNextMatch) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        ASSERT_NO_FATAL_FAILURE(ChooseLevel(ComputerLevel{3}));
        ASSERT_TRUE(ClickElement("Options", "Back"));
        EmitAction("MahjongStart");
        ASSERT_TRUE(_game->GetMatch().GetPhase() != MatchPhase::NotStarted);
        EXPECT_EQ(_game->GetSession().Levels(), UniformLevels(ComputerLevel{3}));
    }

    TEST_F(MahjongOptionsTest, TheSpeedTakesEffectAtOnceIsKeptByANewMatchAndAnUnknownSpeedIsIgnored) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        EmitAction("MahjongSetSpeed", "slow");
        EXPECT_EQ(_game->GetAnimationSpeed(), AnimationSpeed::Slow);
        EmitAction("MahjongSetSpeed", "warp");
        EmitAction("MahjongSetSpeed", "");
        EXPECT_EQ(_game->GetAnimationSpeed(), AnimationSpeed::Slow) << "text that is no speed changes nothing";
        ASSERT_TRUE(ClickElement("Options", "Back"));
        EmitAction("MahjongSetSpeed", "fast");
        EXPECT_EQ(_game->GetAnimationSpeed(), AnimationSpeed::Slow) << "the page is closed: the event is out of date";
        EmitAction("MahjongStart");
        ASSERT_TRUE(_game->GetMatch().GetPhase() != MatchPhase::NotStarted);
        EXPECT_EQ(_game->GetAnimationSpeed(), AnimationSpeed::Slow);
        ASSERT_TRUE(_game->StartMatch(7).has_value());
        EXPECT_EQ(_game->GetAnimationSpeed(), AnimationSpeed::Slow) << "a match does not reset the speed";
    }

    TEST_F(MahjongOptionsTest, ThePageOpensOnWhatTheGameHasNow) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        _game->SetLevel(ComputerLevel::Easy);
        _game->SetAnimationSpeed(AnimationSpeed::Slow);
        CoreManager::GetUIVars().Set("levelChoice"_intern, "5");  // a variable that is out of date
        CoreManager::GetUIVars().Set("speedChoice"_intern, "fast");
        CoreManager::GetUIVars().Set("language"_intern, "ru");
        _game->OpenOptions();
        Draw();
        EXPECT_EQ(Var("levelChoice"), "1");
        EXPECT_EQ(Var("language"), "en");
        EXPECT_EQ(Var("speedChoice"), "slow");
        EXPECT_TRUE(Selected("SpeedSlow"));
        EXPECT_EQ(Text(kLevelChoice), "1");
        EXPECT_TRUE(Selected("LanguageEn"));
        EXPECT_TRUE(Selected("HintsOn")) << "the hints component publishes while Setup is open";
    }

    // ----- Back and the end of Setup -----

    TEST_F(MahjongOptionsTest, BackLeavesSetupOpenWithThePendingSeatsAndHandsAsTheyWere) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->OpenSetup();
        Draw();
        EmitAction("MahjongSetSeat2", "person");
        EmitAction("MahjongSetSeat3", "person");
        EmitAction("MahjongSetHands", "8");
        Draw();
        const SeatSetup seats = MahjongHudComponent::GetActive()->PendingSeats();
        ASSERT_EQ(seats[2], SeatKind::Person);

        _game->OpenOptions();
        Draw();
        ASSERT_TRUE(PageShown());
        ASSERT_NO_FATAL_FAILURE(ChooseLevel(ComputerLevel::Easy));
        ASSERT_TRUE(ClickElement("Options", "Back"));
        Draw();

        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_FALSE(PageShown());
        EXPECT_TRUE(_game->IsSetupOpen());
        EXPECT_TRUE(SetupShown());
        EXPECT_EQ(MahjongHudComponent::GetActive()->PendingSeats(), seats);
        EXPECT_EQ(MahjongHudComponent::GetActive()->PendingHands(), kShortMatchHands);
        EXPECT_EQ(Var("seatKind2"), "person");
        EXPECT_EQ(Var("handsChoice"), "8");
    }

    TEST_F(MahjongOptionsTest, TheTablePausesWhileThePageIsOpenAndThePageClosesWithSetup) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        EXPECT_TRUE(_game->IsTablePaused());
        _game->CloseSetup();
        Draw();
        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_FALSE(PageShown());
        EXPECT_FALSE(SetupShown());
        EXPECT_FALSE(_game->IsTablePaused());
    }

    TEST_F(MahjongOptionsTest, AStartWhileThePageIsOpenClosesBothAndAClickOfTheClosedPageChangesNothing) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        EmitAction("MahjongStart");  // a control client or a late event: the match begins and both are closed
        Draw();
        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(PageShown());
        const ComputerLevel level = _game->GetLevel();
        const PoolString language = _game->GetLanguage();
        EmitAction("MahjongSetLevel", level == ComputerLevel::Easy ? "5" : "1");
        EmitAction("MahjongSetLanguage", language == "ru"_intern ? "en" : "ru");
        EXPECT_EQ(_game->GetLevel(), level);
        EXPECT_EQ(_game->GetLanguage(), language);
    }

}  // namespace BECore::Mahjong::Tests
