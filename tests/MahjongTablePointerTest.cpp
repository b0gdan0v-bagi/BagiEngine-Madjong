#include <BECore/Input/InputControl.h>
#include <BECore/Input/PointerId.h>
#include <BECore/Scene/Components/ClickableComponent.h>
#include <BECore/Scene/Components/TransformComponent.h>
#include <BECore/Scene/SceneNode.h>
#include <tests/ComputerTestSupport.h>
#include <tests/MahjongTableTestSupport.h>
#include <tests/MahjongTouchTestSupport.h>

/**
 * How the table and the hints take the pointer through the engine's one hit test (`UIInput`, `Input/CLAUDE.md`): their place in the draw
 * order, the pointers that reach them (a mouse, the control API's click, a finger), the hover that a canvas ends, the bars beside the view and
 * the scene that is not active. What a click on a tile does is held by `MahjongTableInputTest`, the name on hover by `MahjongTableHoverTest`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        /// Seat 0 holds a hand that is far from waiting; as East it draws 9p.
        constexpr const char* kPlainHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        /// 123s 456s 789m 5p 5p and three single honours: with a drawn north wind a 5p leaves shanten 2, so the coach makes a note of it.
        constexpr const char* kPairedHand = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew";
        constexpr int kShieldLayer = 6;  // above the node `Hints` (5), below the canvases
        /// A window wider than the design view, which fits by height: a bar of the same width on each side.
        constexpr float kWideOutputWidth = 1400.0f;
        constexpr float kWideOutputHeight = 720.0f;
        constexpr float kBarProbe = 5.0f;  // a pixel inside the bar
        constexpr const char* kOtherScene = R"(<scene type="Scene" name="Other"><nodes/></scene>)";

        struct Point {
            float x = 0.0f;
            float y = 0.0f;
        };

        Point CenterOf(const Rect& rect) {
            return Point{rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f};
        }

    }  // namespace

    class MahjongTablePointerTest : public MahjongTableTestBase {
    protected:
        /// A match with East (the person, seat 0) holding `hand` and drawing 9p; the screens that open with it are closed.
        void StartWith(const char* hand = kPlainHand, const SeatSetup& seats = kDefaultSeatSetup) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            _game->CloseSetup();
            Lay(MakeModel(SpecOf(hand, "9p"), kSeat0));
            Draw();
        }

        const IUIInputHandler* PressTaker() const {
            return CoreManager::GetUIInput().LastPressTaker();
        }

        bool TableTookThePress() const {
            return PressTaker() == static_cast<const IUIInputHandler*>(_table);
        }

        /// The middle of the `index`-th hand tile in the view's units.
        Point TileCenter(int index) const {
            const TileSlot* slot = HandSlot(index);
            EXPECT_NE(slot, nullptr) << "hand tile " << index;
            return slot != nullptr ? CenterOf(slot->rect) : Point{};
        }

        /// A node of the scene above the table whose rect takes every press and motion over it: a canvas drawn over a hand tile, which the
        /// shipped canvases never are.
        IntrusivePtr<SceneNode> AddShieldOver(const Rect& rect) {
            Scene* scene = _manager->GetSceneByName("MahjongScene"_intern);
            EXPECT_NE(scene, nullptr);
            IntrusivePtr<SceneNode> node = scene->GetRootNode().AddChild("PointerShield"_intern);
            node->SetLayer(kShieldLayer);
            auto transform = New<TransformComponent>();
            transform->_x = rect.x;
            transform->_y = rect.y;
            transform->_width = rect.w;
            transform->_height = rect.h;
            node->AddComponent(transform);
            auto shield = New<ClickableComponent>();
            shield->_blocksInput = true;
            node->AddComponent(shield);
            return node;
        }

        /// The whole scene made again with a second scene `Other` in the manager, the game's scene `MahjongScene` still the active one.
        void UseSceneManagerWithAnotherScene() {
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            String xml = ShippedSceneXml();
            const size_t end = xml.rfind("</scenes>");
            ASSERT_NE(end, String::npos);
            xml.insert(end, kOtherScene);
            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromString(xml));
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);
            GameManager::GetInstance().Initialize();
            _game = MahjongGame::GetActive();
            ASSERT_NE(_game, nullptr);
            _table = MahjongTableComponent::GetActive();
            ASSERT_NE(_table, nullptr);
        }

        /// The middle of a rectangle of the view in output pixels, as the last draw fitted the view to the window.
        Point OutputCenterOf(const Rect& viewRect) const {
            const Scene* scene = _manager->GetSceneByName("MahjongScene"_intern);
            return CenterOf(scene->GetViewTransform().ToOutput(viewRect.x, viewRect.y, viewRect.w, viewRect.h));
        }
    };

    // ----- the draw order -----

    TEST_F(MahjongTablePointerTest, APressOnATileUnderANodeDrawnAboveTheTableGoesToThatNodeAndDiscardsNothing) {
        ASSERT_NO_FATAL_FAILURE(StartWith());
        const TileSlot* slot = HandSlot(0);
        ASSERT_NE(slot, nullptr);
        IntrusivePtr<SceneNode> shield = AddShieldOver(slot->rect);
        Draw();
        const String before = Snapshot(Hand());
        const uint64_t version = _game->GetSession().Version();

        EXPECT_TRUE(ClickHandTile(0)) << "the node above the table takes the press";

        EXPECT_NE(PressTaker(), nullptr);
        EXPECT_FALSE(TableTookThePress());
        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_EQ(_game->GetSession().Version(), version);

        shield->SetEditorFlags(EditorFlags::Hidden);
        Draw();
        EXPECT_FALSE(ClickHandTile(0)) << "the same point with the node hidden is the table's";

        EXPECT_TRUE(TableTookThePress());
        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("1p"));
    }

    TEST_F(MahjongTablePointerTest, APressOnAButtonOfACanvasGoesToTheCanvasNotToTheTableUnderIt) {
        ASSERT_NO_FATAL_FAILURE(StartWith());
        ASSERT_FALSE(_game->IsRulesOpen());

        EXPECT_TRUE(ClickElement("HudMenu", "Rules"));

        EXPECT_NE(PressTaker(), nullptr);
        EXPECT_FALSE(TableTookThePress()) << "the window-wide shape of the table is under every canvas";
        EXPECT_TRUE(_game->IsRulesOpen());
    }

    TEST_F(MahjongTablePointerTest, AClickOnATileThatNoNodeCoversIsTheTables) {
        ASSERT_NO_FATAL_FAILURE(StartWith());

        EXPECT_FALSE(ClickHandTile(0));

        EXPECT_TRUE(TableTookThePress());
        ASSERT_FALSE(Hand().Discards().empty());
    }

    // ----- the pointers -----

    TEST_F(MahjongTablePointerTest, AClickOfTheControlApiOnATilePlaysLikeAMouseClick) {
        ASSERT_NO_FATAL_FAILURE(StartWith());
        const Point at = TileCenter(0);

        const PointClick click = ClickOutputPoint(CoreManager::GetUIInput(), at.x, at.y, SDL_BUTTON_LEFT, kControlPointerId);

        EXPECT_TRUE(click.consumed) << "the table takes the press, so click_at says it was consumed";
        EXPECT_EQ(click.taker, static_cast<const IUIInputHandler*>(_table));
        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("1p"));
    }

    TEST_F(MahjongTablePointerTest, AFingerSelectsOnTheFirstTapAndDiscardsOnTheSecond) {
        ASSERT_NO_FATAL_FAILURE(StartWith());
        const Point at = TileCenter(0);
        const String before = Snapshot(Hand());

        EXPECT_FALSE(Click(at.x, at.y, kFingerPointerBase));
        Draw();
        EXPECT_EQ(Snapshot(Hand()), before) << "the first tap discards nothing";
        EXPECT_EQ(_table->SelectedHandIndex(), 0);

        EXPECT_FALSE(Click(at.x, at.y, kFingerPointerBase));
        ASSERT_FALSE(Hand().Discards().empty());
        EXPECT_EQ(Hand().Discards().front().tile, T("1p"));
    }

    TEST_F(MahjongTablePointerTest, TheTouchRegimeOfAFingerStaysOnForALaterMouseClick) {
        ASSERT_NO_FATAL_FAILURE(StartWith());
        const Point first = TileCenter(0);
        const Point second = TileCenter(1);
        Click(first.x, first.y, kFingerPointerBase);
        Draw();
        ASSERT_EQ(_table->SelectedHandIndex(), 0);

        Click(second.x, second.y);  // the mouse
        Draw();

        EXPECT_TRUE(Hand().Discards().empty()) << "a click that is not on the selected tile only moves the selection";
        EXPECT_EQ(_table->SelectedHandIndex(), 1);
    }

    // ----- the hover -----

    TEST_F(MahjongTablePointerTest, ThePointerThatMovesFromATileOntoACanvasEndsTheNameAndMovingBackBringsItBack) {
        ASSERT_NO_FATAL_FAILURE(StartWith());
        SettleTransitions();
        const Point tile = TileCenter(0);
        const UIElement* rules = Element("HudMenu", "Rules");
        ASSERT_NE(rules, nullptr);
        const Point button = CenterOf(rules->rect);

        EXPECT_FALSE(HoverAt(tile.x, tile.y)) << "no canvas: the table is told";
        Draw();
        EXPECT_NE(Var("tileHoverText"), "");
        EXPECT_TRUE(OnScreen("TileHover", "TileHoverPlate"));

        const uint64_t motions = _table->PointerMotions();
        EXPECT_TRUE(HoverAt(button.x, button.y)) << "the canvas takes the motion";
        EXPECT_EQ(_table->PointerMotions(), motions) << "the table is not given it, it is told the pointer left";
        Draw();
        EXPECT_EQ(Var("tileHoverText"), "");
        EXPECT_FALSE(OnScreen("TileHover", "TileHoverPlate"));

        EXPECT_FALSE(HoverAt(tile.x, tile.y));
        Draw();
        EXPECT_NE(Var("tileHoverText"), "");
        EXPECT_TRUE(OnScreen("TileHover", "TileHoverPlate"));
    }

    TEST_F(MahjongTablePointerTest, AFingerNeverShowsTheNameOfATile) {
        ASSERT_NO_FATAL_FAILURE(StartWith());
        const Point tile = TileCenter(0);

        HoverAt(tile.x, tile.y, kFingerPointerBase);
        Draw();
        EXPECT_EQ(Var("tileHoverText"), "");

        HoverAt(tile.x, tile.y);
        Draw();
        ASSERT_NE(Var("tileHoverText"), "");
        HoverAt(tile.x, tile.y, kFingerPointerBase);
        Draw();
        EXPECT_EQ(Var("tileHoverText"), "") << "a finger after the mouse: there is no pointer to rest on the tile";
    }

    // ----- beside the view -----

    TEST_F(MahjongTablePointerTest, ATapInTheBarBesideTheViewClearsTheSelection) {
        ASSERT_NO_FATAL_FAILURE(StartWith());
        SetOutputSize(*_renderer, kWideOutputWidth, kWideOutputHeight);
        Draw();
        const Scene* scene = _manager->GetSceneByName("MahjongScene"_intern);
        ASSERT_NE(scene, nullptr);
        ASSERT_LT(scene->ScreenToSceneX(kBarProbe), 0.0f) << "the probe is in the bar, outside the view";
        const TileSlot* slot = HandSlot(0);
        ASSERT_NE(slot, nullptr);
        const Point tile = OutputCenterOf(slot->rect);
        Tap(tile.x, tile.y);
        Draw();
        ASSERT_EQ(_table->SelectedHandIndex(), 0);

        Tap(kBarProbe, kWideOutputHeight * 0.5f);
        Draw();

        EXPECT_FALSE(_table->SelectedHandIndex().has_value());
        EXPECT_TRUE(Hand().Discards().empty());
    }

    // ----- the hints -----

    TEST_F(MahjongTablePointerTest, AShownNoteIsClearedByAPressThatTheTableThenTakesAndDiscardsWith) {
        _game->SetHintsMode(HintsMode::Coach);
        ASSERT_NO_FATAL_FAILURE(Start(kDefaultSeatSetup));
        _game->CloseSetup();
        Lay(MakeModel(SpecOf(kPairedHand, "Nw")));
        Draw();
        ASSERT_TRUE(_game->GetSession().PerformPerson(HandAction::MakeDiscard(T("5p"), false)).has_value());
        Draw();
        ASSERT_TRUE(_game->GetCoach().Note().has_value()) << "a differing discard puts the note up";
        ASSERT_TRUE(StepUntil([&] { return _game->GetSession().ActorIsPerson(); }));
        Draw();
        const CoachNote shown = *_game->GetCoach().Note();
        const size_t discards = Hand().Discards().size();

        EXPECT_FALSE(ClickHandTile(0)) << "the hints watch the press and take none";

        EXPECT_TRUE(TableTookThePress());
        EXPECT_GT(Hand().Discards().size(), discards) << "the press went on to the table";
        const Optional<CoachNote>& now = _game->GetCoach().Note();
        EXPECT_TRUE(!now.has_value() || *now != shown) << "the note that was up is gone: at most the discard made the next one";
    }

    // ----- another scene -----

    TEST_F(MahjongTablePointerTest, WithAnotherSceneActiveTheTableTakesNoPress) {
        ASSERT_NO_FATAL_FAILURE(UseSceneManagerWithAnotherScene());
        ASSERT_NO_FATAL_FAILURE(StartWith());
        const Point at = TileCenter(0);
        _manager->SetActiveScene("Other"_intern);
        Draw();
        const String before = Snapshot(Hand());

        EXPECT_FALSE(Click(at.x, at.y));

        EXPECT_FALSE(TableTookThePress());
        EXPECT_EQ(Snapshot(Hand()), before);

        _manager->SetActiveScene("MahjongScene"_intern);
        Draw();
        Click(at.x, at.y);
        EXPECT_TRUE(TableTookThePress()) << "the scene was the reason";
        EXPECT_FALSE(Hand().Discards().empty());
    }

}  // namespace BECore::Mahjong::Tests
