#pragma once

#include "HandTestSupport.h"
#include "ShippedScene.h"
#include "StringsTestSupport.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UIButton.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UILabel.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Common/UILayoutMatrix.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Games/GameManager.h>
#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/TileAtlas.h>
#include <gtest/gtest.h>

/**
 * What the tests of the table component share (`MahjongTableUiTest`, `MahjongTableInputTest`, `MahjongTableActionsTest`,
 * `MahjongTableScreenTest`, `MahjongTableWindowTest`, `MahjongHudTest`): the fixture `MahjongTableTestBase` loads the shipped
 * scene (`ShippedScene.h`: `config/SceneConfig.xml` with its node files expanded, read from the source tree, a game's content is not mounted in the test environment), starts the game through
 * `GameManager` and draws the scene into a `RecordingRenderer` as big as the view of the scene, so the view transform is the identity:
 * a call's rect is a slot of the layout and a click at a point of the view is a click at that pixel. Header only; needs the engine
 * services.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;

    inline constexpr uint32_t kSeed = 7;
    inline constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
    inline constexpr SeatSetup kTwoPeople{SeatKind::Person, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
    /// A stand-in atlas: the draw calls carry the source rects, the pixels do not matter.
    inline constexpr uint32_t kTextureSide = 4;
    /// Actions of a hand played by computers; a loop fails fast.
    inline constexpr int kActionLimit = 600;

    /// The tile at `part` of a meld as laid, worked out here and not taken from the component.
    inline Tile LaidTile(const Meld& meld, int part) {
        return meld.kind == MeldKind::Chow ? Tile(static_cast<uint8_t>(meld.first.Id() + part)) : meld.first;
    }

    inline SDL_MouseButtonEvent MouseButton(float x, float y, bool down) {
        SDL_MouseButtonEvent event{};
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button = SDL_BUTTON_LEFT;
        event.down = down;
        event.x = x;
        event.y = y;
        return event;
    }

    /// An `ActionEvent` as a button of a canvas emits it.
    inline void EmitAction(const char* action, const char* value = "") {
        InputEvents::ActionEvent::Emit(PoolString::Intern(action), PoolString::Intern(value));
    }

    /// The text of a `UIVars` variable.
    inline std::string Var(const char* name) {
        const eastl::string_view value = CoreManager::GetUIVars().Get(PoolString::Intern(name));
        return std::string(value.data(), value.size());
    }

    class MahjongTableTestBase : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetUIVars().Clear();
            EXPECT_TRUE(LoadShippedStrings(CoreManager::GetUIStrings()).empty());
            ASSERT_TRUE(CoreManager::GetUIStrings().SetLanguage("en"_intern));  // the expectations of the tests are English
            _renderer = New<RecordingRenderer>();
            _renderer->width = kViewWidth;
            _renderer->height = kViewHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(LoadShippedScene(deserializer)) << ShippedSceneConfig().string();
            _manager = New<SceneManager>();
            _manager->Deserialize(deserializer);
            ASSERT_FALSE(deserializer.HasErrors());
            _manager->Initialize();
            EngineEnvironment::UseSceneManager(_manager);  // GameManager switches to the scene of the game through CoreManager
            GameManager::GetInstance().Initialize();

            _game = MahjongGame::GetActive();
            ASSERT_NE(_game, nullptr);
            _table = MahjongTableComponent::GetActive();
            ASSERT_NE(_table, nullptr);
            const std::vector<uint8_t> pixels(kTextureSide * kTextureSide * 4, 255);
            IntrusivePtr<ITexture> atlas = _renderer->CreateTexture(pixels.data(), kTextureSide, kTextureSide);
            _atlasTexture = atlas.Get();
            _table->SetAtlasTexture(std::move(atlas));
            // Tiles are at their slots in every test but those of the flights (`MahjongTableFlightTest`), which switch them on
            _table->SetTileFlightSeconds(0.0f);
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetUIVars().Clear();
            CoreManager::GetUIStrings().Clear();
            CoreManager::GetFontAtlasLibrary().Clear();
        }

        void Start(const SeatSetup& seats, uint32_t seed = kSeed, bool flowers = false) {
            MahjongSettings settings = _game->GetSettings();
            settings.flowers = flowers;
            _game->SetSettings(settings);
            _game->SetSeats(seats);
            ASSERT_TRUE(_game->StartMatch(seed).has_value());
        }

        void Draw() {
            _renderer->ClearCalls();
            _manager->DrawAll();
        }

        /// Switches the flights of arriving tiles on, with their real duration (`kTileFlightSeconds`): `SetUp` switches them off.
        void UseFlights() {
            _table->SetTileFlightSeconds(kTileFlightSeconds);
        }

        /// Time passes on the clock of the table (the pace of the computers, the highlight and the flights run on it).
        static void AdvanceTime(float seconds) {
            SceneEvents::SceneUpdateEvent::Emit(seconds);
        }

        /// The draw calls of tiles, in the order of the layout's slots.
        std::vector<DrawCall> TileCalls() const {
            std::vector<DrawCall> calls;
            for (const DrawCall& call : _renderer->calls) {
                if (call.kind == DrawKind::Texture && call.texture == _atlasTexture) {
                    calls.push_back(call);
                }
            }
            return calls;
        }

        const HandModel& Hand() const {
            return _game->GetMatch().Hand();
        }

        /// Puts a hand built on a fixed wall in place of the one in progress (the session reads the hand, so it follows).
        void Lay(const HandModel& model) {
            _game->GetMatch().Hand() = model;
        }

        /// A left click (press and release) at a point of the view through the engine's input path: a canvas that takes it consumes
        /// it, otherwise it reaches the table. True when a canvas took the press.
        bool Click(float x, float y) {
            const bool taken = CoreManager::GetUIInput().DispatchMouseButtonDown(MouseButton(x, y, true));
            CoreManager::GetUIInput().DispatchMouseButtonUp(MouseButton(x, y, false));
            return taken;
        }

        /// The slot of the `index`-th concealed tile of the bottom seat in the last layout.
        const TileSlot* HandSlot(int index) const {
            for (const TileSlot& slot : _table->GetLayout().slots) {
                if (slot.role == SlotRole::HandTile && slot.seat == _table->GetLayout().viewer && slot.index == index) {
                    return &slot;
                }
            }
            return nullptr;
        }

        /// Clicks the middle of the `index`-th concealed tile of the bottom seat as the last draw put it.
        bool ClickHandTile(int index) {
            const TileSlot* slot = HandSlot(index);
            EXPECT_NE(slot, nullptr) << "hand tile " << index;
            return slot != nullptr && Click(slot->rect.x + slot->rect.w * 0.5f, slot->rect.y + slot->rect.h * 0.5f);
        }

        /// The index of the first concealed tile of `seat` that is `tile`, -1 when it holds none.
        int IndexOf(Seat seat, Tile tile) const {
            const PlayerHand& held = Hand().GetHand(seat);
            for (size_t i = 0; i < held.concealed.size(); ++i) {
                if (held.concealed[i] == tile) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        /// The canvas on the node `node` of the scene; null when there is none.
        IntrusivePtr<UICanvasComponent> Canvas(const char* node) const {
            for (const auto& child : _manager->GetSceneByName("MahjongScene"_intern)->GetRootNode().GetChildren()) {
                if (child->GetName() == PoolString::Intern(node)) {
                    return child->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        /// The UI element `element` of the canvas on the node `node`, laid out by the last draw; null when there is none.
        UIElement* Element(const char* node, const char* element) const {
            const auto canvas = Canvas(node);
            return canvas ? canvas->GetRoot().FindByName(PoolString::Intern(element)) : nullptr;
        }

        static void CollectControls(const UIElement& element, std::vector<const UIElement*>& found) {
            if (element.Cast<UILabel>() != nullptr || element.Cast<UIButton>() != nullptr) {
                found.push_back(&element);
            }
            for (const auto& child : element.children) {
                CollectControls(*child, found);
            }
        }

        /// Whether the cover of a hot seat is shown: a canvas that is shown takes the input (a hidden one keeps its old rects).
        bool CoverShown() const {
            const auto canvas = Canvas("Handoff");
            return canvas && canvas->TakesInput();
        }

        /// Whether the element is on screen: it has a rect after the last draw (a hidden element or canvas has none).
        bool OnScreen(const char* node, const char* element) const {
            const UIElement* found = Element(node, element);
            return found != nullptr && found->rect.w > 0.0f && found->rect.h > 0.0f;
        }

        /// Clicks the middle of a button of the canvas as the last draw put it.
        bool ClickElement(const char* node, const char* element) {
            const UIElement* found = Element(node, element);
            EXPECT_NE(found, nullptr) << element;
            return found != nullptr && Click(found->rect.x + found->rect.w * 0.5f, found->rect.y + found->rect.h * 0.5f);
        }

        /// The size matrix on a canvas of the scene, as Match3's HUD test does it. `minTouchSize`: the least size of a control as seen.
        void ExpectPassesTheMatrix(const char* node, float minTouchSize) {
            auto canvas = Canvas(node);
            ASSERT_TRUE(canvas) << node;
            canvas->UpdateLayout(kViewWidth, kViewHeight);  // the reflected fields of the component reach the layout on its first update
            UILayoutContext base;
            base.sceneViewWidth = kViewWidth;
            base.sceneViewHeight = kViewHeight;
            UILayoutValidatorOptions options;
            options.minTouchSize = minTouchSize;

            const auto failures = ValidateOnMatrix(canvas->GetLayout(), options, base);

            EXPECT_TRUE(failures.empty()) << node << ": " << DescribeMatrixFailures(failures).c_str();
        }

        /// The validator does not see text that a label cuts with an ellipsis (it can shrink): asks every control of the canvas on the
        /// text it shows, with `air` units kept free each side of an interactive one, once the canvas is laid out for this output size.
        void ExpectWordsFit(const char* node, float outputWidth, float outputHeight, float air) {
            auto canvas = Canvas(node);
            ASSERT_TRUE(canvas) << node;
            canvas->UpdateLayout(outputWidth, outputHeight);
            std::vector<const UIElement*> controls;
            CollectControls(canvas->GetRoot(), controls);
            const float scale = canvas->GetLayout().GetResolved().scale;
            for (const UIElement* control : controls) {
                if (control->rect.IsEmpty()) {
                    continue;  // a hidden element
                }
                const float spare = control->interactive ? air : 0.0f;
                const UIContentFit fit = control->CheckContent(control->rect.w - 2.0f * spare, control->rect.h, scale);
                EXPECT_TRUE(!fit.hasContent || fit.fits) << node << " " << control->name.ToStringView().data() << " cuts its text at " << outputWidth << "x" << outputHeight;
            }
        }

        /// What the model says the slot shows: a tile, or nullopt for a back. `allFaces` says that every concealed tile is open, otherwise
        /// only those of `faceSeat`.
        std::optional<Tile> Expected(const TileSlot& slot, bool allFaces, Seat faceSeat) const {
            const PlayerHand& held = Hand().GetHand(slot.seat);
            const bool open = allFaces || slot.seat == faceSeat;
            switch (slot.role) {
                case SlotRole::HandTile:
                    return open ? std::optional<Tile>(held.concealed[slot.index]) : std::nullopt;
                case SlotRole::MeldTile: {
                    const Meld& meld = held.melds[slot.index];
                    return (!meld.concealed || open) ? std::optional<Tile>(LaidTile(meld, slot.part)) : std::nullopt;
                }
                case SlotRole::BonusTile:
                    return held.bonus[slot.index];
                case SlotRole::DiscardTile:
                    return Hand().Discards()[slot.index].tile;
                case SlotRole::WallStack:
                    break;
            }
            return std::nullopt;
        }

        /// Every slot of the layout was drawn once, in order, at its rect, with the face the model allows; nothing else was a tile.
        void ExpectTableDrawn(bool allFaces, Seat faceSeat) {
            const std::vector<DrawCall> calls = TileCalls();
            const TableLayout& layout = _table->GetLayout();
            ASSERT_EQ(calls.size(), layout.slots.size());
            for (size_t i = 0; i < calls.size(); ++i) {
                const TileSlot& slot = layout.slots[i];
                const std::optional<Tile> tile = Expected(slot, allFaces, faceSeat);
                const Rect expected = tile ? SourceRect(*tile) : BackRect();
                ASSERT_TRUE(calls[i].source.has_value()) << "slot " << i;
                EXPECT_EQ(calls[i].source->x, expected.x) << "slot " << i << " role " << static_cast<int>(slot.role) << " seat " << slot.seat;
                EXPECT_EQ(calls[i].source->y, expected.y) << "slot " << i << " role " << static_cast<int>(slot.role) << " seat " << slot.seat;
                EXPECT_EQ(calls[i].rect.x, slot.rect.x) << "slot " << i;
                EXPECT_EQ(calls[i].rect.y, slot.rect.y) << "slot " << i;
                EXPECT_EQ(calls[i].rect.w, slot.rect.w) << "slot " << i;
                EXPECT_EQ(calls[i].rect.h, slot.rect.h) << "slot " << i;
            }
        }

        /// The number of hand tiles of `seat` in the layout that are drawn face up.
        int FaceUpHandTiles(Seat seat) const {
            const std::vector<DrawCall> calls = TileCalls();
            const TableLayout& layout = _table->GetLayout();
            int faceUp = 0;
            for (size_t i = 0; i < calls.size() && i < layout.slots.size(); ++i) {
                const TileSlot& slot = layout.slots[i];
                const bool isBack = calls[i].source->x == BackRect().x && calls[i].source->y == BackRect().y;
                if (slot.role == SlotRole::HandTile && slot.seat == seat && !isBack) {
                    ++faceUp;
                }
            }
            return faceUp;
        }

        /// Steps the computers (all seats) until `done` or the limit; false when the limit came first.
        template <typename Done>
        bool StepUntil(Done done) {
            for (int step = 0; step < kActionLimit; ++step) {
                if (done()) {
                    return true;
                }
                if (_game->GetSession().GetPhase() != SessionPhase::Playing || !_game->GetSession().StepComputer().has_value()) {
                    return false;
                }
            }
            return false;
        }

        const ITexture* _atlasTexture = nullptr;
        IntrusivePtr<RecordingRenderer> _renderer;
        IntrusivePtrAtomic<SceneManager> _manager;
        MahjongGame* _game = nullptr;
        MahjongTableComponent* _table = nullptr;
    };

}  // namespace BECore::Mahjong::Tests
