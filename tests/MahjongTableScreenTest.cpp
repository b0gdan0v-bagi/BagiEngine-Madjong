#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <BECore/Text/Utf8.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UIImage.h>
#include <BECore/UI/UILabel.h>
#include <BECore/UI/UIToggle.h>
#include <Mahjong/Table/SeatName.h>

/**
 * The canvases of the shipped scene that belong to the table (`Actions`, `Handoff`): the buttons of the component are the buttons of the
 * file with their action and value, the words are keys of the string table, the longest labels fit in every language, both pass the size
 * matrix once per language, and the buttons lie where no tile of any game can.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// The height of an action button in view units: 32 px (the engine's touch minimum) from a window of 731 x 411 px.
        constexpr float kButtonHeight = 56.0f;
        /// The height in pixels of the smallest window the touch minimum is held from; the view is 1280 x 720 units, scaled to fit it.
        constexpr float kSmallestTouchWindowHeight = 411.0f;
        /// The table is a fixed design scaled to the window, so on the smallest windows of the matrix (480 px) a button is 21 px high:
        /// the matrix runs the buttons with this minimum, and the engine's own is held from the 731 px windows (the test of the height).
        constexpr float kSmallestWindowTouchMinimum = 20.0f;
        /// The height of the line of the word at the top of a button; the pictures of tiles lie below it.
        constexpr float kWordLineHeight = 24.0f;
        /// The width of an action button in view units.
        constexpr float kButtonWidth = 132.0f;
        /// The smallest tile picture in view units (the 3:4 of an atlas cell), so a tile can be told at the smallest window of the matrix.
        constexpr float kLeastTileWidth = 20.0f;
        constexpr float kLeastTileHeight = 26.0f;
        /// Output sizes the hint is asked in: the design size, a small window, the narrowest windows.
        struct Output {
            float width;
            float height;
        };
        constexpr Output kOutputs[] = {{1280.0f, 720.0f}, {731.0f, 411.0f}, {853.0f, 480.0f}, {480.0f, 480.0f}, {480.0f, 853.0f}};
        constexpr float kTileShape = 3.0f / 4.0f;
        constexpr float kShapeTolerance = 0.02f;
        /// A pile longer than any hand leaves (136 tiles, 52 in the hands at the least).
        constexpr int kMostDiscards = 100;
        /// The gap between the buttons of a row, and the width of a row of three buttons of 132 with two gaps (the row wraps at it).
        constexpr float kButtonGap = 8.0f;
        constexpr float kRowWidth = 3.0f * kButtonWidth + 2.0f * kButtonGap;

        /// The worst-case layout: every zone at its capacity, so no tile of any game can lie where a control does.
        TableLayout WorstLayout() {
            TableInput worst;
            worst.viewer = 0;
            worst.concealedCount.fill(kMaxConcealedTiles);
            worst.bonusCount.fill(kMaxBonusTiles);
            Meld kong;
            kong.kind = MeldKind::Kong;
            kong.first = T("1p");
            for (auto& melds : worst.melds) {
                melds.assign(kMaxMelds, kong);
            }
            for (int i = 0; i < kMostDiscards; ++i) {
                worst.discardSeats.push_back(i % kSeatCount);
            }
            worst.wallRemaining = 2 * kWallRingCells;
            return MakeTableLayout(worst);
        }

        /// The role of the colour of a button (an entry of `kUiPaletteNames`), by what the button does.
        const char* RoleOf(ButtonKind kind) {
            switch (kind) {
                case ButtonKind::Win:
                case ButtonKind::ClaimMahjong:
                    return "primary";
                case ButtonKind::ClaimPung:
                case ButtonKind::ClaimKong:
                case ButtonKind::ClaimChow0:
                case ButtonKind::ClaimChow1:
                case ButtonKind::ClaimChow2:
                    return "secondary";
                case ButtonKind::Pass:
                    return "danger";
                case ButtonKind::ConcealedKong0:
                case ButtonKind::ConcealedKong1:
                case ButtonKind::ConcealedKong2:
                case ButtonKind::UpgradeKong:
                case ButtonKind::Declare:
                    return "neutral";
            }
            return "";
        }

    }  // namespace

    class MahjongTableScreenTest : public MahjongTableTestBase {
    protected:
        /// Shows or hides a button of the canvas `Actions` with all the pictures of its tiles.
        void SetShown(ButtonKind kind, bool shown) {
            const ButtonSpec& spec = SpecOf(kind);
            auto canvas = Canvas("Actions");
            ASSERT_TRUE(canvas);
            ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(spec.element), shown)) << spec.element;
            for (int t = 0; t < spec.maxTiles; ++t) {
                ASSERT_TRUE(canvas->SetElementVisible(PoolString::Intern(TileImageName(kind, t)), shown)) << spec.element;
            }
        }

        /// The fullest turn (a win, a declaration, three kongs and the upgrade) and the fullest window (all claims, three chows, pass).
        static std::vector<std::vector<ButtonKind>> FullestScreens() {
            return {std::vector<ButtonKind>{ButtonKind::Win, ButtonKind::Declare, ButtonKind::ConcealedKong0, ButtonKind::ConcealedKong1, ButtonKind::ConcealedKong2, ButtonKind::UpgradeKong},
                    std::vector<ButtonKind>{ButtonKind::ClaimMahjong, ButtonKind::ClaimKong, ButtonKind::ClaimPung, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2,
                                            ButtonKind::Pass}};
        }

        /// The longest text (by code points, the measure of a line of text in any language) of `candidates`.
        static eastl::string Longest(const std::vector<eastl::string>& candidates) {
            eastl::string longest;
            size_t most = 0;
            for (const eastl::string& candidate : candidates) {
                if (Utf8::CountCodePoints(candidate) > most) {
                    most = Utf8::CountCodePoints(candidate);
                    longest = candidate;
                }
            }
            return longest;
        }
    };

    TEST_F(MahjongTableScreenTest, EveryButtonOfTheComponentIsInTheSceneFileWithItsActionAndValue) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonKind kind = static_cast<ButtonKind>(i);
            const ButtonSpec& spec = SpecOf(kind);
            const UIElement* element = Element("Actions", spec.element);
            ASSERT_NE(element, nullptr) << spec.element;
            if (kind == ButtonKind::Declare) {
                // The declaration is a toggle: it emits the state it asks for, `on` or `off` (`kDeclareOn`, `kDeclareOff`)
                const auto* toggle = element->Cast<UIToggle>();
                ASSERT_NE(toggle, nullptr);
                EXPECT_EQ(toggle->action, PoolString::Intern(spec.action));
                EXPECT_EQ(toggle->onValue, PoolString::Intern(kDeclareOn.data()));
                EXPECT_EQ(toggle->offValue, PoolString::Intern(kDeclareOff.data()));
                EXPECT_EQ(toggle->bind, "declareNext"_intern);
                EXPECT_EQ(toggle->text, eastl::string("{#") + spec.wordKey + "}") << "the toggle says the word of the string table";
                continue;
            }
            const auto* button = element->Cast<UIButton>();
            ASSERT_NE(button, nullptr) << spec.element;
            EXPECT_EQ(button->action, PoolString::Intern(spec.action)) << spec.element;
            EXPECT_EQ(button->value, PoolString::Intern(spec.value)) << spec.element;
            EXPECT_EQ(button->text, eastl::string("{#") + spec.wordKey + "}") << spec.element << " says a word of the string table";
            // The pictures of its tiles: `maxTiles` hidden images named after the button, and no more
            int images = 0;
            for (const auto& child : button->children) {
                EXPECT_NE(child->Cast<UIImage>(), nullptr) << spec.element << " has only pictures as children";
                ++images;
            }
            EXPECT_EQ(images, spec.maxTiles) << spec.element;
            for (int t = 0; t < kMaxButtonTiles; ++t) {
                const std::string name = TileImageName(kind, t).c_str();
                EXPECT_EQ(Element("Actions", name.c_str()) != nullptr, t < spec.maxTiles) << name;
            }
        }
        EXPECT_NE(Element("Handoff", "Reveal"), nullptr);
        EXPECT_EQ(Element("Handoff", "Reveal")->Cast<UIButton>()->action, "MahjongReveal"_intern);
        EXPECT_EQ(Element("Handoff", "Reveal")->Cast<UIButton>()->text, eastl::string("{#cover.show}"));
        EXPECT_EQ(Element("Handoff", "HandoffText")->Cast<UILabel>()->text, eastl::string("{#cover.text}"));
    }

    TEST_F(MahjongTableScreenTest, EveryActionButtonHasTheColourOfItsRoleAndTheRowKeepsTheGapOfTheGrid) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonKind kind = static_cast<ButtonKind>(i);
            const ButtonSpec& spec = SpecOf(kind);
            const UIElement* element = Element("Actions", spec.element);
            ASSERT_NE(element, nullptr) << spec.element;
            EXPECT_TRUE(SameColor(element->color, PaletteColor(RoleOf(kind)))) << spec.element << " is a " << RoleOf(kind) << " button";
        }

        const UIElement* row = Element("Actions", "ActionRow");
        ASSERT_NE(row, nullptr);
        EXPECT_EQ(row->style.stack.spacing, kButtonGap);
        ASSERT_TRUE(Canvas("Actions")->SetElementVisible("Hint"_intern, true));  // a hidden element has no rect
        Canvas("Actions")->UpdateLayout(kViewWidth, kViewHeight);
        EXPECT_EQ(row->rect.w, kRowWidth) << "three buttons and two gaps fit a row";
        const UIElement* hint = Element("Actions", "Hint");
        ASSERT_NE(hint, nullptr);
        EXPECT_TRUE(SameColor(hint->color, PaletteColor("scrim")));
    }

    TEST_F(MahjongTableScreenTest, EveryElementNameOfTheActionsCanvasIsUniqueAndNoLabelVariableIsLeft) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        std::set<std::string> names;
        std::vector<const UIElement*> pending{&canvas->GetRoot()};
        while (!pending.empty()) {
            const UIElement* element = pending.back();
            pending.pop_back();
            const std::string name = element->name.ToStringView().data();
            if (!name.empty()) {
                EXPECT_TRUE(names.insert(name).second) << "duplicate name " << name;
            }
            if (const auto* label = element->Cast<UILabel>()) {
                // Only the hint follows a variable; the words of the buttons are keys of the string table
                EXPECT_EQ(std::string(label->text.c_str()).find('{') == std::string::npos, name != "Hint") << name << " and its variable";
            }
            for (const auto& child : element->children) {
                pending.push_back(child.Get());
            }
        }
    }

    TEST_F(MahjongTableScreenTest, TheActionButtonsPassTheSizeMatrixInEveryLanguageWithTheirWordsAndPicturesAndEveryButtonIsATouchTarget) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);

        // Every button with all its pictures, once per language of the table (a language added to the file is covered without touching the test)
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            for (const std::vector<ButtonKind>& kinds : FullestScreens()) {
                for (int i = 0; i < kButtonCount; ++i) {
                    SetShown(static_cast<ButtonKind>(i), false);
                }
                for (const ButtonKind kind : kinds) {
                    SetShown(kind, true);
                }
                ExpectPassesTheMatrix("Actions", kSmallestWindowTouchMinimum);
                for (const Output& output : kOutputs) {
                    ExpectWordsFit("Actions", output.width, output.height, 0.0f);
                }
            }
        });
    }

    TEST_F(MahjongTableScreenTest, EveryTilePictureLiesInsideItsButtonBelowTheWordAndKeepsTheShapeOfATile) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        for (int i = 0; i < kButtonCount; ++i) {
            SetShown(static_cast<ButtonKind>(i), true);
        }
        Canvas("Actions")->UpdateLayout(kViewWidth, kViewHeight);

        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonKind kind = static_cast<ButtonKind>(i);
            const ButtonSpec& spec = SpecOf(kind);
            const UIElement* button = Element("Actions", spec.element);
            ASSERT_NE(button, nullptr) << spec.element;
            const Rect& outer = button->rect;
            EXPECT_GE(outer.w, kButtonWidth) << spec.element;
            EXPECT_GE(outer.h, kButtonHeight) << spec.element;
            const UIElement* previous = nullptr;
            for (int t = 0; t < spec.maxTiles; ++t) {
                const std::string name = TileImageName(kind, t).c_str();
                const UIElement* picture = Element("Actions", name.c_str());
                ASSERT_NE(picture, nullptr) << name;
                const Rect& r = picture->rect;
                EXPECT_GE(r.x, outer.x) << name;
                EXPECT_LE(r.x + r.w, outer.x + outer.w) << name;
                EXPECT_GE(r.y, outer.y + kWordLineHeight) << name << " lies below the line of the word";
                EXPECT_LE(r.y + r.h, outer.y + outer.h) << name;
                EXPECT_GE(r.w, kLeastTileWidth) << name;
                EXPECT_GE(r.h, kLeastTileHeight) << name;
                EXPECT_NEAR(r.w / r.h, kTileShape, kShapeTolerance) << name;
                if (previous != nullptr) {
                    EXPECT_GE(r.x, previous->rect.x + previous->rect.w) << name << " does not overlap the tile before it";
                }
                previous = picture;
            }
        }
    }

    TEST_F(MahjongTableScreenTest, EveryActionButtonIsATouchTargetFromAWindowOf731By411) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        for (int i = 0; i < kButtonCount; ++i) {
            canvas->SetElementVisible(PoolString::Intern(SpecOf(static_cast<ButtonKind>(i)).element), true);
        }
        canvas->UpdateLayout(kViewWidth, kViewHeight);

        for (int i = 0; i < kButtonCount; ++i) {
            const UIElement* element = Element("Actions", SpecOf(static_cast<ButtonKind>(i)).element);
            ASSERT_NE(element, nullptr);
            EXPECT_GE(element->rect.h, kButtonHeight) << SpecOf(static_cast<ButtonKind>(i)).element;
            const float fit = kSmallestTouchWindowHeight / kViewHeight;  // the window of 731 x 411 shows the 1280 x 720 view at this scale
            EXPECT_GE(element->rect.h * fit, CoreManager::GetUIEnvironment().GetSettings().minTouchSize - 0.5f) << SpecOf(static_cast<ButtonKind>(i)).element;
        }
    }

    TEST_F(MahjongTableScreenTest, TheHintTakesNoInputLiesInTheViewClearOfTheButtonsAndHoldsTheLongestSentenceOfEveryLanguageInEveryWindow) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        auto canvas = Canvas("Actions");
        ASSERT_TRUE(canvas);
        const UIElement* hint = Element("Actions", "Hint");
        ASSERT_NE(hint, nullptr);
        EXPECT_FALSE(OnScreen("Actions", "Hint")) << "hidden in the file";
        EXPECT_FALSE(hint->IsHitTestable()) << "a tooltip never takes a click";
        EXPECT_TRUE(hint->ignoreSafeArea);
        ASSERT_TRUE(canvas->SetElementVisible("Hint"_intern, true));

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            std::vector<eastl::string> sentences;
            for (int i = 0; i < kButtonCount; ++i) {
                sentences.push_back(HintOf(CoreManager::GetUIStrings(), static_cast<ButtonKind>(i)));
            }
            const eastl::string longest = Longest(sentences);
            EXPECT_LE(Utf8::CountCodePoints(longest), kMaxHintLength);
            CoreManager::GetUIVars().Set("actionHint"_intern, eastl::string_view(longest));
            // With the fullest turn and the fullest window under it, as the matrix test of the buttons has them
            for (const std::vector<ButtonKind>& kinds : FullestScreens()) {
                for (int i = 0; i < kButtonCount; ++i) {
                    SetShown(static_cast<ButtonKind>(i), false);
                }
                for (const ButtonKind kind : kinds) {
                    SetShown(kind, true);
                }
                ExpectPassesTheMatrix("Actions", kSmallestWindowTouchMinimum);
                for (const Output& output : kOutputs) {
                    ExpectWordsFit("Actions", output.width, output.height, 0.0f);
                }
            }
        });

        canvas->UpdateLayout(kViewWidth, kViewHeight);
        const Rect& plate = hint->rect;
        EXPECT_GE(plate.x, 0.0f);
        EXPECT_GE(plate.y, 0.0f);
        EXPECT_LE(plate.x + plate.w, kViewWidth);
        EXPECT_LE(plate.y + plate.h, kViewHeight);
        const Rect& row = Element("Actions", "ActionRow")->rect;
        EXPECT_TRUE(Apart(plate, row)) << "the hint does not lie over the buttons it explains";
        // ... nor over a tile or a plate of the table: a tooltip in the gutter, one grid unit clear of the plates
        const TableLayout layout = WorstLayout();
        for (const TileSlot& slot : layout.slots) {
            EXPECT_TRUE(Apart(slot.rect, plate)) << "the hint covers role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
        }
        const eastl::vector<Rect> plates = ZonePlates(layout);
        for (size_t i = 0; i < plates.size(); ++i) {
            EXPECT_TRUE(Apart(plates[i], Grown(plate, kGridUnit))) << "the hint is less than one grid unit from plate " << i;
        }
    }

    TEST_F(MahjongTableScreenTest, TheCoverPassesTheSizeMatrixInEveryLanguageWithTheLongestSeatName) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "Handoff"_intern, true);

        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            // The longest name any seat can have, from the real function: every seat with every wind
            std::vector<eastl::string> names;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (int wind = 0; wind < kSeatCount; ++wind) {
                    names.push_back(SeatName(CoreManager::GetUIStrings(), seat, static_cast<Wind>(wind)));
                }
            }
            CoreManager::GetUIVars().Set("handoffSeat"_intern, eastl::string_view(Longest(names)));

            ExpectPassesTheMatrix("Handoff", CoreManager::GetUIEnvironment().GetSettings().minTouchSize);
            for (const Output& output : kOutputs) {
                ExpectWordsFit("Handoff", output.width, output.height, 0.0f);
            }
        });
    }

    TEST_F(MahjongTableScreenTest, TheCoverFollowsTheModalAnatomy) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        UICanvasComponent::SetVisibleByName(*_manager->GetSceneByName("MahjongScene"_intern), "Handoff"_intern, true);
        ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
            SCOPED_TRACE(language.ToStringView().data());
            std::vector<eastl::string> names;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (int wind = 0; wind < kSeatCount; ++wind) {
                    names.push_back(SeatName(CoreManager::GetUIStrings(), seat, static_cast<Wind>(wind)));
                }
            }
            CoreManager::GetUIVars().Set("handoffSeat"_intern, eastl::string_view(Longest(names)));
            Draw();
            Canvas("Handoff")->UpdateLayout(kViewWidth, kViewHeight);

            const UIElement* plate = Element("Handoff", "Plate");
            const UIElement* text = Element("Handoff", "HandoffText");
            const UIElement* reveal = Element("Handoff", "Reveal");
            ASSERT_TRUE(plate != nullptr && text != nullptr && reveal != nullptr);
            EXPECT_TRUE(SameColor(plate->color, PaletteColor("surface")));
            EXPECT_TRUE(SameColor(text->color, PaletteColor("header")));
            EXPECT_TRUE(SameColor(reveal->color, PaletteColor("primary")));
            EXPECT_EQ(reveal->rect.h, kMainButtonHeight);
            EXPECT_EQ(text->rect.y, plate->rect.y + kModalPadding);
            EXPECT_EQ(reveal->rect.y - (text->rect.y + text->rect.h), kModalSectionGap);
            EXPECT_EQ(reveal->rect.y + reveal->rect.h, plate->rect.y + plate->rect.h - kModalPadding) << "the plate holds the band and the button and nothing more";
        });
    }

    TEST_F(MahjongTableScreenTest, TheButtonsLieInTheFreeCornerOfTheTableAndCoverNoTile) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        Draw();
        const TableLayout layout = WorstLayout();
        const UIElement* row = Element("Actions", "ActionRow");
        ASSERT_NE(row, nullptr);

        for (const TileSlot& slot : layout.slots) {
            EXPECT_TRUE(Apart(slot.rect, row->rect)) << "role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
        }

        // The row holds what is in it: the buttons of the fullest turn and of the fullest claim window lie inside the row
        // (the gap of the stack is also between its lines) and so clear of every slot
        for (const std::vector<ButtonKind>& kinds :
             {std::vector<ButtonKind>{ButtonKind::Win, ButtonKind::Declare, ButtonKind::ConcealedKong0, ButtonKind::ConcealedKong1, ButtonKind::ConcealedKong2, ButtonKind::UpgradeKong},
              std::vector<ButtonKind>{ButtonKind::ClaimMahjong, ButtonKind::ClaimKong, ButtonKind::ClaimPung, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2,
                                      ButtonKind::Pass}}) {
            for (int i = 0; i < kButtonCount; ++i) {
                SetShown(static_cast<ButtonKind>(i), false);
            }
            for (const ButtonKind kind : kinds) {
                SetShown(kind, true);
            }
            Canvas("Actions")->UpdateLayout(kViewWidth, kViewHeight);
            for (const ButtonKind kind : kinds) {
                const UIElement* button = Element("Actions", SpecOf(kind).element);
                ASSERT_NE(button, nullptr) << SpecOf(kind).element;
                const Rect& r = button->rect;
                EXPECT_GE(r.x, row->rect.x) << SpecOf(kind).element;
                EXPECT_GE(r.y, row->rect.y) << SpecOf(kind).element;
                EXPECT_LE(r.x + r.w, row->rect.x + row->rect.w) << SpecOf(kind).element;
                EXPECT_LE(r.y + r.h, row->rect.y + row->rect.h) << SpecOf(kind).element << " lies below the row";
                for (const TileSlot& slot : layout.slots) {
                    EXPECT_TRUE(Apart(slot.rect, r)) << SpecOf(kind).element << " covers role " << static_cast<int>(slot.role) << " seat " << slot.seat << " index " << slot.index;
                }
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
