#pragma once

#include "HandTestSupport.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Reflection/XmlDeserializer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/Scene/SceneManager.h>
#include <BECore/Text/FontAtlasLibrary.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Common/EngineEnvironment.h>
#include <Common/RecordingRenderer.h>
#include <Games/GameManager.h>
#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/TileAtlas.h>
#include <gtest/gtest.h>

/**
 * What the tests of the table component share (`MahjongTableUiTest`, `MahjongTableInputTest`, `MahjongTableActionsTest`): the fixture
 * `MahjongTableTestBase` loads the shipped `config/SceneConfig.xml` (read from the source tree, a game's content is not mounted in the
 * test environment), starts the game through `GameManager` and draws the scene into a `RecordingRenderer` as big as the view of the scene,
 * so the view transform is the identity: a call's rect is a slot of the layout and a click at a point of the view is a click at that
 * pixel. Header only; needs the engine services.
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

    inline std::filesystem::path ShippedSceneConfig() {
        return std::filesystem::path(__FILE__).parent_path().parent_path() / "config" / "SceneConfig.xml";
    }

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

    class MahjongTableTestBase : public ::testing::Test {
    protected:
        static void SetUpTestSuite() {
            EngineEnvironment::EnsureCoreServices();
        }

        void SetUp() override {
            CoreManager::GetFontAtlasLibrary().Clear();
            CoreManager::GetUIVars().Clear();
            _renderer = New<RecordingRenderer>();
            _renderer->width = kViewWidth;
            _renderer->height = kViewHeight;
            CoreManager::GetRendererManager().SetRenderer(_renderer);

            XmlDeserializer deserializer;
            ASSERT_TRUE(deserializer.LoadFromFile(ShippedSceneConfig())) << ShippedSceneConfig().string();
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
        }

        void TearDown() override {
            GameManager::GetInstance().Shutdown();
            EngineEnvironment::UseSceneManager({});
            _manager.Reset();
            CoreManager::GetRendererManager().SetRenderer({});
            CoreManager::GetUIVars().Clear();
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

        /// What the model says the slot shows: a tile, or nullopt for a back. `faces` says whose concealed tiles are open.
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
