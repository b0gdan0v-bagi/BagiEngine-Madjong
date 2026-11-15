#include <BECore/UI/UIElement.h>
#include <BECore/UI/UITextComponent.h>
#include <Common/Printers.h>
#include <Common/UIThemeTestSupport.h>
#include <Mahjong/Table/TableLook.h>
#include <tests/MahjongTableTestSupport.h>
#include <tests/ShippedThemes.h>
#include <tests/UiStyleTestSupport.h>

/**
 * Every screen of the shipped scene once per theme (`ForEachTheme`) and once per language: no theme token is missing after a draw, every text is legible on the plate it is drawn on, and
 * the table in play shows its marks apart from the felt, with the theme's own palette. The contrast of a screen is worked out from the resolved colours of the elements, not from the
 * theme file alone (`MahjongThemesTest` holds the file's roles): a plate may be laid on another one, translucent over the felt, or drawn in a state of its own.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Far under the number of texts the scene has: a count below it means the walk lost the screens.
        constexpr size_t kLeastJudgedTexts = 100;

        /// What a text of an element lies on: the fill under it in each state it can be drawn in, and the token of the element the fill comes from (the one of the innermost element that
        /// has a colour; the felt where none has). A gradient makes two of them.
        struct Ground {
            String token;
            Color normal;
            Color hover;
            Color selected;
        };

        Color Lay(const Color& base, const Color& fill) {
            return fill.a == 0 ? base : Over(fill, base);
        }

        String NameOf(const UIColor& color) {
            return color.HasToken() ? String(color.token.ToStringView()) : String("(literal)");
        }

        /// The grounds under the element's own content: the parent's, with the element's fills laid over them. A state with no fill of its own keeps the normal one (a derived shift of it
        /// does not change the contrast to speak of).
        Vector<Ground> GroundsOf(const UIElement& element, const Vector<Ground>& parents) {
            Vector<Ground> grounds;
            for (const Ground& parent : parents) {
                Ground own = parent;
                own.normal = Lay(parent.normal, element.color);
                own.hover = Lay(parent.hover, element.hoverColor.a != 0 ? element.hoverColor : element.color);
                own.selected = Lay(parent.selected, element.selectedColor.a != 0 ? element.selectedColor : element.color);
                if (element.color.a != 0) {
                    own.token = NameOf(element.color);
                }
                grounds.push_back(own);
                if (element.color.a != 0 && element.box.gradientDirection != UIGradientDirection::None && element.box.gradientColor.a != 0) {
                    Ground end = parent;
                    end.normal = Lay(parent.normal, element.box.gradientColor);
                    end.hover = Lay(parent.hover, element.box.gradientColor);
                    end.selected = Lay(parent.selected, element.box.gradientColor);
                    end.token = NameOf(element.box.gradientColor);
                    grounds.push_back(end);
                }
            }
            return grounds;
        }

        /// One line per text of the tree under `element` that is under `kMinTextContrast` on a ground it lies on, in the current theme of `themes` (a known pair of `classic` is skipped).
        /// The element's colours are resolved first, so a token that fell to magenta is judged as magenta.
        void JudgeTree(UIElement& element, UIThemes& themes, const Vector<Ground>& parents, const String& path, Set<String>& problems, size_t& judged) {
            element.ResolveTheme(themes);
            const Vector<Ground> grounds = GroundsOf(element, parents);
            if (const UITextComponent* text = element.GetComponent<UITextComponent>()) {
                const struct {
                    const char* state;
                    const UIColor& ink;
                    Color Ground::* plate;
                } states[] = {{"normal", text->textColor, &Ground::normal}, {"hover", text->hoverTextColor, &Ground::hover}, {"selected", text->selectedTextColor, &Ground::selected}};
                for (const Ground& ground : grounds) {
                    for (const auto& state : states) {
                        const UIColor& ink = state.ink.a != 0 ? state.ink : text->textColor;
                        const Color plate = ground.*(state.plate);
                        if (state.plate != &Ground::normal && SameColor(ink, text->textColor) && SameColor(plate, ground.normal)) {
                            continue;  // the state looks as the normal one does
                        }
                        ++judged;
                        const float ratio = ContrastRatio(Over(ink, plate), plate);
                        const String pair = NameOf(ink) + " on " + ground.token;
                        if (ratio < kMinTextContrast && !IsKnownLowContrastPair(themes.GetTheme().ToStringView(), pair)) {
                            problems.insert(
                                Format("theme '{}': {} ({}) has the contrast {:.2f}, under {:.1f}: {}", themes.GetTheme().ToStringView(), path, state.state, ratio, kMinTextContrast, pair));
                        }
                    }
                }
            }
            for (const auto& child : element.children) {
                JudgeTree(*child, themes, grounds, path + "/" + String(child->name.ToStringView()), problems, judged);
            }
        }

        /// The texts of a tree that are illegible on their plates, in the current theme, one problem to a line; `judged` (when given) counts the pairs of text and plate looked at, so that
        /// a check that looks at nothing is seen.
        Vector<String> ContrastProblems(UIElement& root, UIThemes& themes, const String& path, size_t* judged = nullptr) {
            const Color felt = Token(themes, "felt");
            Set<String> problems;
            size_t count = 0;
            JudgeTree(root, themes, {Ground{"felt", felt, felt, felt}}, path, problems, count);
            if (judged != nullptr) {
                *judged += count;
            }
            return Vector<String>(problems.begin(), problems.end());
        }

        /// The tokens no theme defined that the last draws and resolves met, one to a line.
        Vector<String> MissingTokenProblems(const UIThemes& themes) {
            Vector<String> problems;
            for (const PoolString token : themes.GetMissing()) {
                problems.push_back(Format("the token '{}' is in no theme", token.ToStringView()));
            }
            return problems;
        }

        String Joined(const Vector<String>& lines) {
            String text;
            for (const String& line : lines) {
                text += line + "\n";
            }
            return text;
        }

        /// A card of two plates, a text on each, with the colours named by tokens of `themes`.
        IntrusivePtr<UIElement> MakeCard(const char* plateToken, const char* inkToken) {
            auto card = New<UIElement>();
            card->name = PoolString::Intern("Card");
            card->color = UIColor::FromToken(PoolString::Intern(plateToken));
            auto caption = New<UIElement>();
            caption->name = PoolString::Intern("Caption");
            caption->AddComponent<UITextComponent>().textColor = UIColor::FromToken(PoolString::Intern(inkToken));
            card->children.push_back(caption);
            return card;
        }

    }  // namespace

    class MahjongThemeScreensTest : public MahjongTableTestBase {
    protected:
        /// The node names of every canvas of the scene, in the order of the file.
        Vector<String> CanvasNodes() const {
            Vector<String> nodes;
            for (const auto& child : _manager->GetSceneByName("MahjongScene"_intern)->GetRootNode().GetChildren()) {
                if (child->GetComponent<UICanvasComponent>()) {
                    nodes.push_back(String(child->GetName().ToStringView()));
                }
            }
            return nodes;
        }

        static void ShowAll(UICanvasComponent& canvas, UIElement& element) {
            canvas.SetElementVisible(element.name, true);
            for (const auto& child : element.children) {
                ShowAll(canvas, *child);
            }
        }

        /// Every canvas of the scene and every element of it shown, so that every token and every text is asked for.
        void ShowEveryScreen() {
            Scene& scene = *_manager->GetSceneByName("MahjongScene"_intern);
            for (const String& node : CanvasNodes()) {
                UICanvasComponent::SetVisibleByName(scene, PoolString::Intern(node), true);
                const auto canvas = Canvas(node.c_str());
                ASSERT_TRUE(canvas) << node.c_str();
                ShowAll(*canvas, canvas->GetRoot());
            }
        }

        Vector<String> ContrastOfEveryScreen(size_t* judged = nullptr) {
            Vector<String> problems;
            for (const String& node : CanvasNodes()) {
                const auto canvas = Canvas(node.c_str());
                const Vector<String> found = ContrastProblems(canvas->GetRoot(), CoreManager::GetUIThemes(), node, judged);
                problems.insert(problems.end(), found.begin(), found.end());
            }
            return problems;
        }

        /// Draws the scene in every theme and every language, and says what was missing in each.
        template <typename Each>
        void DrawInEveryThemeAndLanguage(Each&& each) {
            UIThemes& themes = CoreManager::GetUIThemes();
            ForEachTheme(themes, [&](PoolString theme) {
                ForEachLanguage(CoreManager::GetUIStrings(), [&](PoolString language) {
                    SCOPED_TRACE(Format("{} / {}", theme.ToStringView(), language.ToStringView()).c_str());
                    Draw();
                    Draw();
                    each(theme);
                    EXPECT_TRUE(themes.GetMissing().empty()) << Joined(MissingTokenProblems(themes)).c_str();
                });
            });
        }

        bool Shown(const char* node) const {
            const auto canvas = Canvas(node);
            return canvas && canvas->TakesInput();
        }

        /// Plays the match to its end between computers.
        void PlayMatchToTheEnd() {
            constexpr int kHandsToTry = 40;
            for (int hand = 0; hand < kHandsToTry && _game->GetMatch().GetPhase() != MatchPhase::Finished; ++hand) {
                _game->GetSession().RunComputers();
                ASSERT_TRUE(_game->GetSession().NextHand().has_value());
            }
            ASSERT_EQ(_game->GetMatch().GetPhase(), MatchPhase::Finished);
        }
    };

    // ----- no token is missing -----

    TEST_F(MahjongThemeScreensTest, EveryCanvasOfTheSceneDrawsWithNoMissingTokenInEveryThemeAndLanguage) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        ASSERT_NO_FATAL_FAILURE(ShowEveryScreen());
        DrawInEveryThemeAndLanguage([&](PoolString) {});
    }

    TEST_F(MahjongThemeScreensTest, TheScreensTheGameOpensDrawWithNoMissingTokenInEveryTheme) {
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->CloseSetup();

        _game->OpenSetup();
        DrawInEveryThemeAndLanguage([&](PoolString) { EXPECT_TRUE(Shown("Setup")); });

        _game->CloseSetup();
        _game->OpenOptions();
        DrawInEveryThemeAndLanguage([&](PoolString) { EXPECT_TRUE(Shown("Options")); });

        _game->CloseOptions();
        _game->OpenRules();
        DrawInEveryThemeAndLanguage([&](PoolString) { EXPECT_TRUE(Shown("Rules")); });

        _game->CloseRules();
        _game->OpenSetup();
        _game->OpenOnline();
        DrawInEveryThemeAndLanguage([&](PoolString) { EXPECT_TRUE(Shown("Online")); });
    }

    TEST_F(MahjongThemeScreensTest, TheScoreTableAndTheStandingsDrawWithNoMissingTokenInEveryTheme) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        _game->CloseSetup();
        _game->GetSession().RunComputers();
        DrawInEveryThemeAndLanguage([&](PoolString) { EXPECT_TRUE(Shown("HandOver")); });

        ASSERT_NO_FATAL_FAILURE(PlayMatchToTheEnd());
        DrawInEveryThemeAndLanguage([&](PoolString) { EXPECT_TRUE(Shown("MatchOver")); });
    }

    // ----- legible text -----

    TEST_F(MahjongThemeScreensTest, EveryTextOfTheSceneIsLegibleOnItsPlateInEveryTheme) {
        ASSERT_NO_FATAL_FAILURE(Start(kTwoPeople));
        Draw();
        UIThemes& themes = CoreManager::GetUIThemes();
        size_t visited = 0;
        ForEachTheme(themes, [&](PoolString theme) {
            ++visited;
            size_t judged = 0;
            const Vector<String> problems = ContrastOfEveryScreen(&judged);
            EXPECT_TRUE(problems.empty()) << theme.ToStringView().data() << ":\n" << Joined(problems).c_str();
            EXPECT_GT(judged, kLeastJudgedTexts) << "the check looked at too few texts of " << theme.ToStringView().data();
            EXPECT_TRUE(themes.GetMissing().empty()) << Joined(MissingTokenProblems(themes)).c_str();
        });
        EXPECT_EQ(visited, kShippedThemeIds.size());
    }

    // ----- the table in play -----

    TEST_F(MahjongThemeScreensTest, TheTableInPlayShowsItsMarksApartFromTheFeltWithThePaletteOfTheTheme) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        _game->CloseSetup();
        constexpr size_t kDiscardsToSee = 12;
        ASSERT_TRUE(StepUntil([&] { return Hand().Discards().size() >= kDiscardsToSee; }));
        UIThemes& themes = CoreManager::GetUIThemes();
        Vector<Pair<PoolString, Color>> rims;
        ForEachTheme(themes, [&](PoolString theme) { rims.push_back({theme, Palette().feltRim}); });
        size_t visited = 0;
        ForEachTheme(themes, [&](PoolString theme) {
            ++visited;
            SCOPED_TRACE(theme.ToStringView().data());
            Draw();
            const TablePalette palette = Palette();
            const TableLayout& layout = _table->GetLayout();

            // The wall, the discards and the hand are on screen: every slot of the layout is one tile call
            EXPECT_EQ(TileCalls().size(), layout.slots.size());
            int walls = 0;
            int discards = 0;
            int hands = 0;
            for (const TileSlot& slot : layout.slots) {
                walls += slot.role == SlotRole::WallStack ? 1 : 0;
                discards += slot.role == SlotRole::DiscardTile ? 1 : 0;
                hands += slot.role == SlotRole::HandTile ? 1 : 0;
            }
            EXPECT_GT(walls, 0);
            EXPECT_GT(discards, 0);
            EXPECT_GT(hands, 0);

            // ... on the felt, the rim and the rack of this theme and not of another
            EXPECT_FALSE(MarksOf(_renderer->calls, palette.feltRim).empty());
            EXPECT_FALSE(MarksOf(_renderer->calls, palette.feltLip).empty());
            EXPECT_FALSE(MarksOf(_renderer->calls, palette.handRack).empty());
            EXPECT_FALSE(MarksOf(_renderer->calls, palette.handRackLip).empty());
            for (const auto& [other, otherRim] : rims) {
                if (other != theme) {
                    EXPECT_TRUE(MarksOf(_renderer->calls, otherRim).empty()) << "the rim of " << other.ToStringView().data();
                }
            }

            // The marks the table draws now lie apart from the felt, as the constants say of every mark
            size_t drawn = 0;
            for (const Mark& mark : TableMarks()) {
                if (MarksOf(_renderer->calls, mark.color).empty()) {
                    continue;
                }
                ++drawn;
                EXPECT_GE(Distance(Over(mark.color, palette.felt), palette.felt), kMinMarkDistance) << mark.name;
            }
            EXPECT_GT(drawn, 0u) << "no mark of the table was drawn: the check looks at nothing";
        });
        EXPECT_EQ(visited, kShippedThemeIds.size());
    }

    // ----- the checks are held to account -----

    TEST(MahjongThemeScreensChecksTest, ATextColourThatIsThePlatesColourIsFoundAndAnUnknownTokenIsMissing) {
        UIThemes themes;
        const PoolString legible = PoolString::Intern("legible");
        const PoolString washed = PoolString::Intern("washed");
        for (const PoolString theme : {legible, washed}) {
            themes.Set(theme, "felt", Color{18, 34, 68, 255});
            themes.Set(theme, "panel", Color{240, 240, 230, 255});
        }
        themes.Set(legible, "ink", Color{20, 24, 32, 255});
        themes.Set(washed, "ink", Color{235, 235, 225, 255});
        themes.SetDefaultTheme(legible);

        const auto card = MakeCard("panel", "ink");
        ASSERT_TRUE(themes.SetTheme(legible));
        EXPECT_TRUE(ContrastProblems(*card, themes, "Card").empty());

        card->ResetTheme();
        card->children.front()->ResetTheme();
        ASSERT_TRUE(themes.SetTheme(washed));
        const Vector<String> problems = ContrastProblems(*card, themes, "Card");
        ASSERT_EQ(problems.size(), 1u) << Joined(problems).c_str();
        EXPECT_NE(problems.front().find("Card/Caption"), String::npos) << problems.front().c_str();

        const auto stray = MakeCard("panels", "ink");
        ASSERT_TRUE(themes.SetTheme(legible));
        (void)ContrastProblems(*stray, themes, "Stray");
        EXPECT_EQ(MissingTokenProblems(themes).size(), 1u) << "a token that no theme has is reported";
    }

    TEST(MahjongThemeScreensChecksTest, ATranslucentPlateIsJudgedOverTheFeltAndAKnownPairOfClassicIsSkippedOnlyThere) {
        UIThemes themes;
        const PoolString classic = PoolString::Intern("classic");
        const PoolString other = PoolString::Intern("other");
        for (const PoolString theme : {classic, other}) {
            themes.Set(theme, "felt", Color{20, 30, 60, 255});
            themes.Set(theme, "glass", Color{0, 0, 0, 40});
            themes.Set(theme, "textMuted", Color{90, 100, 110, 255});
            themes.Set(theme, "primary", Color{90, 100, 110, 255});
        }
        themes.SetDefaultTheme(classic);

        const auto glass = MakeCard("glass", "textMuted");
        ASSERT_TRUE(themes.SetTheme(classic));
        EXPECT_FALSE(ContrastProblems(*glass, themes, "Glass").empty()) << "a text of the felt's own brightness on a faint plate over the felt";

        const auto known = MakeCard("primary", "textMuted");
        EXPECT_TRUE(ContrastProblems(*known, themes, "Known").empty()) << "classic's own pair 'textMuted on primary'";
        known->ResetTheme();
        known->children.front()->ResetTheme();
        ASSERT_TRUE(themes.SetTheme(other));
        EXPECT_FALSE(ContrastProblems(*known, themes, "Known").empty()) << "no other theme has the exception";
    }

}  // namespace BECore::Mahjong::Tests
