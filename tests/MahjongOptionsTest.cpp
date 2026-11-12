#include "MahjongButtonSupport.h"
#include "MahjongTableTestSupport.h"
#include "MahjongTouchTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/UI/UIDropdown.h>
#include <BECore/UI/UISegmented.h>
#include <Mahjong/Components/MahjongHudComponent.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * The page `Options` of the shipped scene (`config/nodes/MahjongOptions.xml`): its shape on the whole size matrix in every language, the
 * words, the touch size, that nothing behind it takes input, the effect of each row on the game, and that `Back` and `CloseSetup`
 * close it with `Setup` and its pending choice left as they were. The rows of segments are one list (the hints row has three segments); the level is the dropdown of the ten levels, whose open list is
 * checked on the matrix too.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// A row of segments of the page: the names in the file, the action and variable it carries, its segments with the value each one
        /// sets, and the segment a fresh game starts on.
        struct PageRow {
            static constexpr size_t kMaxSegments = 3;
            const char* row;
            const char* caption;
            const char* group;
            const char* variable;
            const char* action;
            size_t count;
            const char* segments[kMaxSegments];
            const char* values[kMaxSegments];
            size_t defaultSegment;
        };

        constexpr PageRow kRows[] = {
            {"LanguageRow", "LanguageCaption", "Language", "language", "MahjongSetLanguage", 2, {"LanguageEn", "LanguageRu"}, {"en", "ru"}, 0},
            {"HintsRow", "HintsCaption", "HintsChoice", "hintsChoice", "MahjongSetHints", 3, {"HintsOff", "HintsHints", "HintsCoach"}, {"off", "hints", "coach"}, 1},
        };
        constexpr size_t kRowCount = sizeof(kRows) / sizeof(kRows[0]);

        /// The row of the speed: a caption and the three segments `SpeedSlow`, `SpeedNormal`, `SpeedFast` of the group `SpeedChoice`.
        constexpr const char* kSpeedRow = "SpeedRow";
        constexpr const char* kSpeedCaption = "SpeedCaption";
        constexpr const char* kSpeedChoice = "SpeedChoice";
        constexpr size_t kPageRowCount = kRowCount + 1;

        /// The segments in the order of `EnumUtils<AnimationSpeed>::Values()`.
        constexpr const char* kSpeedSegments[] = {"SpeedSlow", "SpeedNormal", "SpeedFast"};
        static_assert(sizeof(kSpeedSegments) / sizeof(kSpeedSegments[0]) == EnumUtils<AnimationSpeed>::Count());

        /// Everything that takes input or shows a word on the page, for the matrix tests.
        std::vector<const char*> Controls() {
            std::vector<const char*> names;
            for (const PageRow& row : kRows) {
                names.insert(names.end(), row.segments, row.segments + row.count);
            }
            for (const char* segment : kSpeedSegments) {
                names.push_back(segment);
            }
            names.push_back("OptionsRules");
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
        /// A started game with the page opened by the HUD button over the table, drawn.
        void OpenPage() {
            ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
            _game->CloseSetup();
            Draw();
            ASSERT_TRUE(ClickElement("HudMenu", "Options"));
            Draw();
            ASSERT_TRUE(PageShown());
        }

        bool PageShown() const {
            const auto canvas = Canvas("Options");
            return canvas && canvas->TakesInput();
        }

        bool SetupShown() const {
            const auto canvas = Canvas("Setup");
            return canvas && canvas->TakesInput();
        }

        bool Selected(const char* segment) const {
            const UIElement* element = Element("Options", segment);
            const auto* button = element != nullptr ? element : nullptr;
            return button != nullptr && button->selected;
        }

        std::string Text(const char* element) const {
            const UIElement* found = Element("Options", element);
            if (found != nullptr && found->Cast<UIDropdown>() != nullptr) {
                const UIElement* chosen = found->Cast<UIDropdown>()->FindChosenOption();  // the header shows the chosen option
                return chosen != nullptr ? DisplayText(chosen) : std::string("<none chosen>");
            }
            return DisplayText(found);
        }
    };

    // ----- the shape -----

    TEST_F(MahjongOptionsTest, ThePageIsHiddenUntilItIsOpenAndShownExactlyWhileTheGameSaysSo) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        EXPECT_FALSE(PageShown());
        _game->OpenSetup();
        Draw();
        EXPECT_FALSE(PageShown()) << "Setup does not show the page";
        EXPECT_EQ(Element("Setup", "OptionsRow"), nullptr) << "nor leads to it";
        _game->CloseSetup();
        _game->OpenOptions();
        Draw();
        EXPECT_TRUE(PageShown());
        EXPECT_FALSE(SetupShown());
        _game->CloseOptions();
        Draw();
        EXPECT_FALSE(PageShown());
    }

    TEST_F(MahjongOptionsTest, ThePageIsAModalScreenCanvasOverTheTableAndHasEveryRowOfTheList) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        const auto page = Canvas("Options");
        ASSERT_TRUE(page);
        EXPECT_EQ(page->GetLayout().params.space, CanvasSpace::Screen);
        EXPECT_EQ(page->GetLayout().params.scaleMode, CanvasScaleMode::ScaleWithScreen);
        EXPECT_GE(page->GetLayout().params.minScale, 0.6f) << "a floor keeps a 56 unit control above 32 px";
        EXPECT_EQ(page->GetLayout().params.maxScale, UICanvasScaleParams{}.maxScale);
        for (const PageRow& row : kRows) {
            for (const char* name : {row.row, row.caption, row.group}) {
                EXPECT_NE(Element("Options", name), nullptr) << name;
            }
            const auto* group = Element("Options", row.group) != nullptr ? Element("Options", row.group)->Cast<UISegmented>() : nullptr;
            ASSERT_NE(group, nullptr) << row.group;
            EXPECT_EQ(group->bind, PoolString::Intern(row.variable)) << row.group;
            ASSERT_EQ(group->children.size(), row.count) << row.group;
            for (size_t index = 0; index < row.count; ++index) {
                const UIElement* button = group->children[index].Get();
                ASSERT_NE(button, nullptr) << row.segments[index];
                EXPECT_EQ(button->name.ToStringView(), row.segments[index]);
                EXPECT_EQ(ButtonAction(button), PoolString::Intern(row.action)) << row.segments[index];
                EXPECT_EQ(ButtonValue(button).ToStringView(), row.values[index]) << row.segments[index];
            }
        }
        for (const char* name : {kSpeedRow, kSpeedCaption}) {
            EXPECT_NE(Element("Options", name), nullptr) << name;
        }
        const auto* speedGroup = Element("Options", kSpeedChoice) != nullptr ? Element("Options", kSpeedChoice)->Cast<UISegmented>() : nullptr;
        ASSERT_NE(speedGroup, nullptr);
        EXPECT_EQ(speedGroup->bind, "speedChoice"_intern);
        ASSERT_EQ(speedGroup->children.size(), EnumUtils<AnimationSpeed>::Count()) << "one segment per speed, slowest first";
        for (size_t index = 0; index < EnumUtils<AnimationSpeed>::Count(); ++index) {
            const UIElement* segment = speedGroup->children[index].Get();
            ASSERT_NE(segment, nullptr) << index;
            EXPECT_EQ(segment->name.ToStringView(), kSpeedSegments[index]);
            EXPECT_EQ(ButtonAction(segment), "MahjongSetSpeed"_intern);
            EXPECT_EQ(ButtonValue(segment).ToStringView(), SpeedText(EnumUtils<AnimationSpeed>::Values()[index]).data());
        }
        ASSERT_TRUE(IsButton(Element("Options", "Back")));
        EXPECT_EQ(ButtonAction(Element("Options", "Back")), "MahjongOptionsBack"_intern);
        ASSERT_TRUE(IsButton(Element("Options", "OptionsRules")));
        EXPECT_EQ(ButtonAction(Element("Options", "OptionsRules")), "MahjongRules"_intern);
        EXPECT_EQ(ButtonAction(Element("HudMenu", "Options")), "MahjongOptions"_intern);
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
            EXPECT_EQ(Text("HintsOff"), Tr(strings, "setup.off").c_str());
            EXPECT_EQ(Text("HintsHints"), Tr(strings, "options.hintsHints").c_str());
            EXPECT_EQ(Text("HintsCoach"), Tr(strings, "options.hintsCoach").c_str());
            EXPECT_EQ(Text(kSpeedCaption), Tr(strings, "options.speed").c_str());
            EXPECT_EQ(Text("SpeedSlow"), Tr(strings, "options.speedSlow").c_str());
            EXPECT_EQ(Text("SpeedNormal"), Tr(strings, "options.speedNormal").c_str());
            EXPECT_EQ(Text("SpeedFast"), Tr(strings, "options.speedFast").c_str());
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
        bands.push_back("ButtonRow");

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

        // The design size: the plate of 376, the title band of 48, the rows of 64, Rules and Back of 64 set 16 below the rows
        LayOutInWindow(*page, DesignWindow());
        const Rect plate = Element("Options", "Plate")->rect;
        EXPECT_NEAR(plate.h, kRoomyPlateHeight, 0.01f);
        EXPECT_NEAR(plate.h, 376.0f, 0.01f);
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
            EXPECT_TRUE(SameColor(caption->GetComponent<UITextComponent>()->textColor, PaletteColor("textMuted")));
            EXPECT_EQ(caption->rect.x, row->rect.x + kRowPadding);
            EXPECT_EQ(group->rect.x, caption->rect.x + caption->rect.w + kModalGap);
            EXPECT_EQ(group->rect.x + group->rect.w, row->rect.x + row->rect.w - kRowPadding);
            EXPECT_EQ(group->rect.y, row->rect.y + kRowPadding);
            EXPECT_EQ(group->rect.y + group->rect.h, row->rect.y + row->rect.h - kRowPadding);
            for (const char* name : std::span<const char* const>(entry.segments, entry.count)) {
                const auto* segment = Element("Options", name);
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
            EXPECT_TRUE(SameColor(caption->GetComponent<UITextComponent>()->textColor, PaletteColor("textMuted")));
            EXPECT_EQ(group->rect.x, caption->rect.x + caption->rect.w + kModalGap);
            EXPECT_EQ(group->rect.x + group->rect.w, row->rect.x + row->rect.w - kRowPadding);
            EXPECT_EQ(group->rect.y, row->rect.y + kRowPadding);
            EXPECT_EQ(group->rect.y + group->rect.h, row->rect.y + row->rect.h - kRowPadding);
            for (const char* name : kSpeedSegments) {
                const auto* segment = Element("Options", name);
                ASSERT_NE(segment, nullptr) << name;
                EXPECT_TRUE(SameColor(segment->selectedColor, PaletteColor("primary"))) << name;
                EXPECT_TRUE(SameColor(segment->color, PaletteColor("neutral"))) << name;
                EXPECT_EQ(segment->rect.h, group->rect.h) << name;
            }
        }
        const UIElement* back = Element("Options", "Back");
        ASSERT_NE(back, nullptr);
        EXPECT_TRUE(SameColor(back->color, PaletteColor("primary")));
        EXPECT_EQ(back->rect.h, kMainButtonHeight);
        EXPECT_EQ(GapBelow(*above, back->rect), kModalSectionGap);
        const UIElement* rules = Element("Options", "OptionsRules");
        ASSERT_NE(rules, nullptr);
        EXPECT_EQ(rules->rect.h, kMainButtonHeight);
        EXPECT_EQ(rules->rect.y, back->rect.y) << "Rules and Back are one row";
        EXPECT_EQ(rules->rect.x, plate.x + kModalPadding);
        EXPECT_EQ(back->rect.x, rules->rect.x + rules->rect.w + kModalGap);
        EXPECT_EQ(back->rect.x + back->rect.w, plate.x + plate.w - kModalPadding);
        EXPECT_EQ(back->rect.y + back->rect.h, plate.y + plate.h - kModalPadding) << "the plate holds the row of Rules and Back and nothing more";

        // A canvas of 320 units (a 480 px window at the user scale 1.5): no title, narrower captions, the plate the whole safe width
        LayOutInWindow(*page, WindowOf(480.0f, 480.0f), CanvasScaleMode::ConstantPixelSize, 1.5f);
        EXPECT_TRUE(Element("Options", "Title")->rect.IsEmpty());
        EXPECT_TRUE(Element("Options", kSpeedCaption)->rect.IsEmpty()) << "the caption of the speed gives its room to the three words";
        EXPECT_NEAR(Element("Options", "Plate")->rect.w, page->GetLayout().GetResolved().contentBounds.w, 0.01f);
        for (const PageRow& row : kRows) {
            EXPECT_GT(Element("Options", row.row)->rect.h, 0.0f) << row.row << ": nothing but the title is dropped";
        }
        EXPECT_GT(Element("Options", kSpeedRow)->rect.h, 0.0f) << "nothing but the title is dropped";
        EXPECT_GT(Element("Options", "Back")->rect.h, 0.0f);

        // A canvas that is only narrow keeps the title
        LayOutInWindow(*page, WindowOf(480.0f, 853.0f), CanvasScaleMode::ConstantPixelSize, 1.5f);
        EXPECT_FALSE(Element("Options", "Title")->rect.IsEmpty());
        EXPECT_TRUE(Element("Options", "HintsCaption")->rect.IsEmpty()) << "the three words of the hints need the room of the caption";
        EXPECT_LT(Element("Options", "LanguageCaption")->rect.w, 112.0f);
    }

    // ----- nothing behind the page takes input -----

    TEST_F(MahjongOptionsTest, TheDimCoversTheWindowSoTheButtonsOfTheHudUnderItTakeNoClick) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        auto page = Canvas("Options");
        auto menu = Canvas("HudMenu");
        ASSERT_TRUE(page && menu);

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

        // In the design window a click on the place of a HUD button is the dim's
        for (const auto& canvas : {page, menu}) {
            LayOutInWindow(*canvas, DesignWindow());
        }
        ASSERT_TRUE(menu->TakesInput());
        ClickElement("HudMenu", "NewGame");
        ClickElement("HudMenu", "Options");
        EXPECT_TRUE(PageShown());
        EXPECT_FALSE(_game->IsSetupOpen());
    }

    // ----- what the rows do -----

    TEST_F(MahjongOptionsTest, EachRowChangesTheGameAndItsVariableAndSelectsItsSegment) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        ASSERT_TRUE(_game->HintsOn());
        ASSERT_EQ(_game->GetLanguage(), "en"_intern);

        ASSERT_EQ(_game->GetHintsMode(), HintsMode::Hints);
        EXPECT_TRUE(Selected("HintsHints"));
        for (const HintsMode mode : {HintsMode::Off, HintsMode::Coach, HintsMode::Hints, HintsMode::Off}) {
            SCOPED_TRACE(HintsModeText(mode).data());
            const char* segment = mode == HintsMode::Off ? "HintsOff" : (mode == HintsMode::Hints ? "HintsHints" : "HintsCoach");
            ASSERT_TRUE(ClickElement("Options", segment));
            Draw();
            EXPECT_EQ(_game->GetHintsMode(), mode);
            EXPECT_EQ(Var("hintsChoice"), HintsModeText(mode).data());
            for (const char* other : {"HintsOff", "HintsHints", "HintsCoach"}) {
                EXPECT_EQ(Selected(other), other == segment) << other;
            }
        }
        EXPECT_FALSE(_game->HintsOn());

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
            const char* segment = row.segments[row.defaultSegment];
            ASSERT_TRUE(ClickElement("Options", segment)) << segment;
            Draw();
            EXPECT_TRUE(Selected(segment)) << segment;
            EXPECT_EQ(Var(row.variable), row.values[row.defaultSegment]) << row.variable;
        }
        EXPECT_TRUE(_game->HintsOn());
        EXPECT_EQ(_game->GetLanguage(), "en"_intern);
    }

    TEST_F(MahjongOptionsTest, TheHintsRowTakesOnlyItsThreeIdsAndOnlyWhileThePageIsOpen) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        for (const char* value : {"", "on", "coach ", "Coach", "HINTS", "1", "warp"}) {
            EmitAction("MahjongSetHints", value);
            EXPECT_EQ(_game->GetHintsMode(), HintsMode::Hints) << "'" << value << "'";
        }
        EmitAction("MahjongSetHints", "coach");
        EXPECT_EQ(_game->GetHintsMode(), HintsMode::Coach);
        EXPECT_TRUE(_game->HintsOn()) << "the coach includes the hints";
        ASSERT_TRUE(ClickElement("Options", "Back"));
        EmitAction("MahjongSetHints", "off");
        EXPECT_EQ(_game->GetHintsMode(), HintsMode::Coach) << "the page is closed: the event is out of date";
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
        _game->CloseSetup();
        Draw();
        _game->SetAnimationSpeed(AnimationSpeed::Slow);
        CoreManager::GetUIVars().Set("speedChoice"_intern, "fast");
        CoreManager::GetUIVars().Set("language"_intern, "ru");
        _game->OpenOptions();
        Draw();
        EXPECT_EQ(Var("language"), "en");
        EXPECT_EQ(Var("speedChoice"), "slow");
        EXPECT_TRUE(Selected("SpeedSlow"));
        EXPECT_TRUE(Selected("LanguageEn"));
        EXPECT_TRUE(Selected("HintsHints")) << "the hints component publishes while the page is open";
    }

    // ----- Back and the end of Setup -----

    TEST_F(MahjongOptionsTest, BackGoesBackToTheTableAndOpensNoSetup) {
        ASSERT_NO_FATAL_FAILURE(OpenPage());
        EXPECT_FALSE(_game->IsSetupOpen()) << "the page lies over the table, not over the seat choice";
        ASSERT_TRUE(ClickElement("Options", "Back"));
        Draw();

        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_FALSE(PageShown());
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(SetupShown());
        EXPECT_FALSE(_game->IsTablePaused());
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
        ASSERT_TRUE(_game->StartMatch(7).has_value());  // a control client: the match begins and every page is closed
        Draw();
        EXPECT_FALSE(_game->IsOptionsOpen());
        EXPECT_FALSE(_game->IsSetupOpen());
        EXPECT_FALSE(PageShown());
        const PoolString language = _game->GetLanguage();
        EmitAction("MahjongSetLanguage", language == "ru"_intern ? "en" : "ru");
        EXPECT_EQ(_game->GetLanguage(), language);
    }

}  // namespace BECore::Mahjong::Tests
