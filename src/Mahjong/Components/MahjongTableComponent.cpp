#include "MahjongTableComponent.h"

#include <BECore/Config/ConfigManager.h>
#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UITextComponent.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/ApplicationEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/MahjongTableComponent.gen.hpp>
#include <Mahjong/Components/MahjongHintsComponent.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/CallingMarks.h>
#include <Mahjong/Table/CenterPanel.h>
#include <Mahjong/Table/EmblemAtlas.h>
#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/SeatName.h>
#include <Mahjong/Table/SeatPlates.h>
#include <Mahjong/Table/TableLayoutSpecXml.h>
#include <Mahjong/Table/TableLook.h>
#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/TileAtlas.h>
#include <Mahjong/Table/TileHover.h>
#include <Mahjong/Table/TouchRegime.h>
#include <Mahjong/Table/UiPalette.h>
#include <Mahjong/Table/WindMarks.h>

namespace BECore::Mahjong {

    namespace {

        /// The height of the plate `TileHoverPlate` with the name alone and with the line of the danger bar under it (`MahjongTileHover.xml`)
        constexpr float kHoverPlateHeight = 48.0f;
        constexpr float kHoverPlateTallHeight = 92.0f;

        constexpr eastl::string_view kRevealAction = "MahjongReveal";
        /// The tint of a turned tile: white leaves the atlas as it is (the plain `DrawTexture` has the same default).
        constexpr Color kUntinted{255, 255, 255, 255};
        constexpr Color kPlainFace{238, 230, 205, 255};
        constexpr Color kPlainBack{36, 120, 118, 255};
        constexpr Color kPlainSuit{70, 110, 200, 255};
        constexpr Color kPlainHonour{200, 60, 50, 255};
        constexpr Color kPlainBonus{60, 160, 90, 255};
        /// The highlight of the acting seat reaches this far past the hand; the pending discard's frame this far past the tile.
        constexpr float kActingMargin = 4.0f;
        constexpr float kPendingMargin = 3.0f;
        /// A tile's shadow falls this far down and right.
        constexpr float kShadowOffset = 2.0f;
        /// How far the soft layer of a tile's shadow reaches past the tight one on every side, in view units.
        constexpr float kShadowSoftMargin = 2.0f;
        /// The inner mark of a tile drawn without the atlas, as a fraction of the tile.
        constexpr float kPlainMarkInset = 0.25f;

        /// Whose concealed tiles are drawn face up: all of them once the hand is over and while no seat is a person (a watched match),
        /// otherwise those of the viewer (or of none). `seat` is the viewer alone, whom every mark that follows the viewer is for: a
        /// spectator and the cover have none, so no such mark is drawn then, however many hands are face up.
        struct Faces {
            bool all = false;
            Seat seat = kNoSeat;

            [[nodiscard]] bool Shows(Seat other) const noexcept {
                return all || other == seat;
            }
        };

        /// The two nodes of the action buttons (the names of the scene file).
        PoolString ActionsNode() {
            return "Actions"_intern;
        }
        PoolString ActionsCompactNode() {
            return "ActionsCompact"_intern;
        }
        /// The node of the button `HintButton` of the hints on the table: shown with `Actions`, hidden where `ActionsCompact` is shown (which
        /// holds a `HintButton` of its own, the last button of its row)
        PoolString HintsButtonNode() {
            return "HintsButton"_intern;
        }

        /// The canvas on the node `nodeName`; null when the scene has none. The action buttons are on two nodes, `Actions` on the table and
        /// `ActionsCompact` on the bar under it, where the first would be too small to touch and the bar holds the row at its height there
        /// (`NeedsCompactActions`); they hold the same elements by name, and exactly one is shown. A frame looks each canvas up once.
        IntrusivePtr<UICanvasComponent> FindCanvasOf(Scene& scene, PoolString nodeName) {
            for (const auto& child : scene.GetRootNode().GetChildren()) {
                if (child->GetName() == nodeName) {
                    return child->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

        /// Whether the two lists name the same regions at the same rectangles, in the same order.
        bool SameRegions(const eastl::vector<TableRegion>& a, const eastl::vector<TableRegion>& b) {
            if (a.size() != b.size()) {
                return false;
            }
            for (size_t i = 0; i < a.size(); ++i) {
                const Rect& left = a[i].rect;
                const Rect& right = b[i].rect;
                if (a[i].name != b[i].name || left.x != right.x || left.y != right.y || left.w != right.w || left.h != right.h) {
                    return false;
                }
            }
            return true;
        }

        /// The element `name` of `canvas`; null when there is no canvas or no such element.
        UIElement* FindElementIn(const IntrusivePtr<UICanvasComponent>& canvas, PoolString name) {
            return canvas ? canvas->GetRoot().FindByName(name) : nullptr;
        }

        /// The shown button of `Actions` under the pointer (the canvas sets `UIElement::hovered` from the mouse); none when there is none.
        /// Only the buttons of `Actions` are hovered: a touch screen has no hover, and `ActionsCompact` has no label for the hint.
        std::optional<ButtonKind> HoveredButton(const IntrusivePtr<UICanvasComponent>& actions, const ActionButtons& buttons) {
            for (int i = 0; i < kButtonCount; ++i) {
                const ButtonKind kind = static_cast<ButtonKind>(i);
                if (!buttons[kind].shown) {
                    continue;  // a hidden button may still carry the flag of the last motion
                }
                const UIElement* element = FindElementIn(actions, PoolString::Intern(SpecOf(kind).element));
                if (element != nullptr && element->hovered) {
                    return kind;
                }
            }
            return std::nullopt;
        }

        /// The window as the touch rules see it: the layout context of the main window and the touch minimum of the UI settings.
        struct WindowNumbers {
            UILayoutContext context;
            float minTouchSize = 0.0f;
        };
        WindowNumbers ReadWindow() {
            const IRenderer& renderer = *CoreManager::GetRenderer();
            const UIEnvironment& environment = CoreManager::GetUIEnvironment();
            return WindowNumbers{environment.MakeContext(CoreManager::GetMainWindow().Get(), renderer.GetOutputWidth(), renderer.GetOutputHeight()), environment.GetSettings().minTouchSize};
        }

        /// Whether SDL made an event from a touch or a pen (a real mouse never has these ids).
        bool IsTouchDevice(SDL_MouseID which) {
            return which == SDL_TOUCH_MOUSEID || which == SDL_PEN_MOUSEID;
        }

        bool IsTouchClick(const SDL_MouseButtonEvent& button) {
            return IsTouchDevice(button.which);
        }

        /// Whether `element` or a shown element below it reports the pointer over it (the canvas sets `UIElement::hovered` from the motion it
        /// consumed; a hidden element may still carry the flag of its last motion).
        bool HasHoveredElement(const UIElement& element) {
            if (!element.active.visible) {
                return false;
            }
            if (element.hovered) {
                return true;
            }
            for (const auto& child : element.children) {
                if (HasHoveredElement(*child)) {
                    return true;
                }
            }
            return false;
        }

        /// Whether a canvas of the scene has the pointer: one that takes input and is modal (it consumes every motion) or has a shown
        /// element under the pointer. A canvas that consumes a motion keeps it from the table, so the table cannot see the pointer move onto
        /// it from a tile; this is how it finds out.
        bool CanvasHasPointer(Scene& scene) {
            for (const auto& child : scene.GetRootNode().GetChildren()) {
                const IntrusivePtr<UICanvasComponent> canvas = child->GetComponent<UICanvasComponent>();
                if (canvas && canvas->TakesInput() && (canvas->_modal || HasHoveredElement(canvas->GetRoot()))) {
                    return true;
                }
            }
            return false;
        }

        Faces FacesOf(const TableSession& session) {
            BE_PROFILE_SCOPE("MahjongTable::FacesOf");
            Faces faces;
            faces.all = session.GetPhase() != SessionPhase::Playing || session.PersonCount() == 0;
            faces.seat = session.HandoffPending() ? kNoSeat : session.Viewer();
            return faces;
        }

        /// Whether the table is played now: it is not played while the seat choice (`Setup`) or the rules are open over it.
        bool TableIsLive(const MahjongGame& game) {
            return !game.IsTablePaused();
        }

        /// In a room the person to act is the viewer, and only while the server lists an action for it (a frozen table, in `Reconnecting`, lists none that can be sent).
        bool ServerListsActions(const MahjongGame& game) {
            const OnlineClient* client = game.FindOnline();
            const TableSession& session = game.GetSession();
            return client != nullptr && client->Stage() == OnlineStage::Game && !client->Table().actions.empty() && session.Actor() == session.Viewer();
        }

        /// Whether a person is to act and may: the table is being played, the actor is a person and no cover is up. In a room: the server lists an action for the viewer.
        bool PersonActs(const MahjongGame& game) {
            const TableSession& session = game.GetSession();
            if (!TableIsLive(game) || session.GetPhase() != SessionPhase::Playing || session.HandoffPending()) {
                return false;
            }
            return game.IsOnlineTable() ? ServerListsActions(game) : session.ActorIsPerson();
        }

        /// Whether a computer is to act and may: the table is being played and the actor is a computer. Never in a room: the server plays its computers.
        bool ComputerActs(const MahjongGame& game) {
            const TableSession& session = game.GetSession();
            return !game.IsOnlineTable() && TableIsLive(game) && session.GetPhase() == SessionPhase::Playing && !session.ActorIsPerson();
        }

        /// A claim window: a discard or an upgraded kong waits for the other seats to decide.
        bool IsClaimWindow(Phase phase) noexcept {
            return phase == Phase::Claim || phase == Phase::RobKong;
        }

        /// The index of the last copy of `tile` in the sorted `concealed` tiles (`AddConcealed` sorts equal tiles together and the tile that
        /// was added last, a drawn or a won one, is the last of them).
        int LastCopyIndex(const eastl::vector<Tile>& concealed, Tile tile) {
            for (int i = static_cast<int>(concealed.size()) - 1; i >= 0; --i) {
                if (concealed[static_cast<size_t>(i)] == tile) {
                    return i;
                }
            }
            ASSERT(false, "LastCopyIndex: the tile is not in the concealed tiles");
            return -1;
        }

        /// The index, in the concealed tiles of `seat`, of the tile it has just drawn; -1 when it drew none.
        int DrawnIndexOf(const HandModel& hand, Seat seat) {
            const Tile drawn = hand.DrawnTile(seat);
            return drawn.IsValid() ? LastCopyIndex(hand.GetHand(seat).concealed, drawn) : -1;
        }

        /// The entry of the pile that is the last move (a discard nobody has claimed yet); -1 when the last move is something else.
        int LastDiscardEntry(const HandModel& hand) {
            if (hand.Moves().empty() || hand.Discards().empty()) {
                return -1;
            }
            // A discard that took a win left the pile: the entry at the end is an earlier one, even when it has the same seat and tile
            if (hand.Result() && hand.Result()->source == WinSource::Discard) {
                return -1;
            }
            const HandMove& move = hand.Moves().back();
            const DiscardEntry& last = hand.Discards().back();
            if (move.kind != MoveKind::Discard || last.seat != move.seat || last.tile != move.tile) {
                return -1;
            }
            return static_cast<int>(hand.Discards().size()) - 1;
        }

        /// The blink of `kClaimHighlight`: on for the first half of each period `blinkSeconds` of the `elapsed` time of a mark.
        bool BlinkIsOn(float elapsed, float blinkSeconds) {
            return std::fmod(elapsed, blinkSeconds) < blinkSeconds * 0.5f;
        }

        /// The serial of the last claim window of the hand, 0 for none (`ClaimWindowResult::serial`).
        int WindowSerial(const HandModel& hand) {
            return hand.LastClaimWindow() ? hand.LastClaimWindow()->serial : 0;
        }

        /// The bar of `kClaimedEdge` along the edge `edge` of `rect`, inside the tile.
        Rect EdgeBar(const Rect& rect, Edge edge, float width) {
            switch (edge) {
                case Edge::Left:
                    return Rect(rect.x, rect.y, width, rect.h);
                case Edge::Right:
                    return Rect(rect.x + rect.w - width, rect.y, width, rect.h);
                case Edge::Top:
                    return Rect(rect.x, rect.y, rect.w, width);
                case Edge::Bottom:
                    return Rect(rect.x, rect.y + rect.h - width, rect.w, width);
                case Edge::None:
                    break;
            }
            return Rect();
        }

        /// What the frame shows besides its layout, as far as the flights need it (`TableSnapshot`): the face-up hands, the pile, the open
        /// sets, the wall and the tile that was just drawn (for an opponent only that it was, never which: its slot is the last of its backs).
        void FillSnapshot(TableSnapshot& snapshot, const HandModel& hand, const Faces& faces, Seat bottom) {
            BE_PROFILE_SCOPE("MahjongTable::FillSnapshot");
            for (const DiscardEntry& entry : hand.Discards()) {
                snapshot.pile.push_back(entry.tile);
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                snapshot.melds[seat] = hand.GetHand(seat).melds;
                if (faces.Shows(seat)) {
                    snapshot.openHand[seat] = hand.GetHand(seat).concealed;
                }
                if (snapshot.drawSeat == kNoSeat && hand.DrawnTile(seat).IsValid()) {
                    snapshot.drawSeat = seat;
                    snapshot.drawIndex = faces.Shows(seat) ? DrawnIndexOf(hand, seat) : static_cast<int>(hand.GetHand(seat).concealed.size()) - 1;
                }
            }
            snapshot.wallRemaining = static_cast<int>(hand.GetWall().Remaining());
            // A mahjong on a discard: the winning tile joined the winner's concealed tiles, the last copy of it is the one that flew in
            if (hand.GetPhase() == Phase::Finished && hand.Result() && hand.Result()->source == WinSource::Discard && IsSeat(hand.Result()->winner)) {
                snapshot.winSeat = hand.Result()->winner;
                snapshot.winIndex = LastCopyIndex(hand.GetHand(snapshot.winSeat).concealed, hand.Result()->winningTile);
            }
        }

        /// The input of the layout of `hand` for `viewer`, written over `input` whose lists keep their capacity (the table is laid out every
        /// frame; a fresh input would allocate the melds and the pile each time)
        void FillInput(TableInput& input, const HandModel& hand, Seat viewer) {
            BE_PROFILE_SCOPE("MahjongTable::FillInput");
            input.viewer = viewer;
            input.profile = TableProfile{};
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PlayerHand& held = hand.GetHand(seat);
                input.concealedCount[seat] = static_cast<int>(held.concealed.size());
                input.melds[seat] = held.melds;
                input.bonusCount[seat] = static_cast<int>(held.bonus.size());
            }
            input.discardSeats.clear();
            for (const DiscardEntry& entry : hand.Discards()) {
                input.discardSeats.push_back(entry.seat);
            }
            input.wallRemaining = static_cast<int>(hand.GetWall().Remaining());
        }

        /// `snapshot` emptied for the next draw, the capacity of its lists kept, so refilling it every frame does not allocate
        void ClearSnapshot(TableSnapshot& snapshot) {
            TableSnapshot cleared;
            cleared.layout.slots = eastl::move(snapshot.layout.slots);
            cleared.layout.slots.clear();
            cleared.layout.zones.freeRegions = eastl::move(snapshot.layout.zones.freeRegions);
            cleared.layout.zones.freeRegions.clear();
            cleared.pile = eastl::move(snapshot.pile);
            cleared.pile.clear();
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                cleared.melds[seat] = eastl::move(snapshot.melds[seat]);
                cleared.melds[seat].clear();
                cleared.openHand[seat] = eastl::move(snapshot.openHand[seat]);
                cleared.openHand[seat].clear();
            }
            snapshot = eastl::move(cleared);
        }

        /// The size of the view the scene draws in now (the active view: the design of the shape the output's aspect gives); the design size of
        /// the landscape table before the scene has a view.
        struct ViewSize {
            float width = kViewWidth;
            float height = kViewHeight;
        };
        ViewSize ActiveViewSize(const Scene& scene) {
            const bool known = scene.GetViewWidth() > 0.0f && scene.GetViewHeight() > 0.0f;
            return known ? ViewSize{scene.GetViewWidth(), scene.GetViewHeight()} : ViewSize{};
        }

        /// The profile the window the table is drawn in gets (`TableProfileFor`): the output size of the renderer, the density of the window
        /// and the touch minimum of the engine, as `PublishScreen` reads them for the action buttons, and the active view (a portrait view
        /// gets the desktop profile).
        TableProfile CurrentTableProfile(const ViewSize& view) {
            const IRenderer& renderer = *CoreManager::GetRenderer();
            const UIEnvironment& environment = CoreManager::GetUIEnvironment();
            const UILayoutContext window = environment.MakeContext(CoreManager::GetMainWindow().Get(), renderer.GetOutputWidth(), renderer.GetOutputHeight());
            return TableProfileFor(window.outputWidth, window.outputHeight, window.pixelDensity, environment.GetSettings().minTouchSize, view.width, view.height);
        }

        Color PlainMarkColor(Tile tile) {
            if (tile.IsBonus()) {
                return kPlainBonus;
            }
            return tile.IsHonour() ? kPlainHonour : kPlainSuit;
        }

        Rect Padded(const Rect& rect, float margin) {
            return Rect(rect.x - margin, rect.y - margin, rect.w + 2.0f * margin, rect.h + 2.0f * margin);
        }

        /// Four bars of `width` on the border of `rect`, inside it; the two side bars lie between the top and the bottom one, so no
        /// corner is drawn twice (the colour is translucent).
        void DrawFrame(IRenderer& renderer, const Rect& rect, float width, const Color& color) {
            renderer.DrawFilledRect(rect.x, rect.y, rect.w, width, color);
            renderer.DrawFilledRect(rect.x, rect.y + rect.h - width, rect.w, width, color);
            renderer.DrawFilledRect(rect.x, rect.y + width, width, rect.h - 2.0f * width, color);
            renderer.DrawFilledRect(rect.x + rect.w - width, rect.y + width, width, rect.h - 2.0f * width, color);
        }

        /// The emblem of a seat in its ring box, then the turn ring and the calling ring over it (white cells of the atlas, tinted by the palette
        /// roles); both rings can show together. Without the atlas the calling ring falls back to a frame on the box (the turn is the plate's own
        /// frame then), and a person or a computer has no picture.
        void DrawEmblem(IRenderer& renderer, ITexture* atlas, const Rect& box, SeatKind kind, ComputerLevel level, bool turn, bool calling, float unit) {
            const auto drawCell = [&](int cell, const Color& tint) {
                const Rect source = EmblemRect(cell);
                renderer.DrawTexture(*atlas, &source, box.x, box.y, box.w, box.h, tint);
            };
            if (atlas == nullptr) {
                if (calling) {
                    DrawFrame(renderer, box, kCallingFrameWidth * unit, kCallingFrame);
                }
                return;
            }
            drawCell(kind == SeatKind::Computer ? ComputerCell(level) : PersonCell(), kUntinted);
            if (turn) {
                drawCell(kTurnRingCell, kSeatTurnFrame);
            }
            if (calling) {
                drawCell(kCallingRingCell, kCallingFrame);
            }
        }

    }  // namespace

    MahjongTableComponent* MahjongTableComponent::s_active = nullptr;

    MahjongTableComponent::~MahjongTableComponent() {
        ReleaseFelt();
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void MahjongTableComponent::ReleaseFelt() {
        if (_felt) {
            // A frame in flight may still read the texture: the GPU has to be idle before it is freed (and a texture of another renderer is not ours to wait for)
            const auto& renderer = CoreManager::GetRenderer();
            if (renderer && renderer->GetId() == _feltRenderer) {
                renderer->WaitIdle();
            }
            _felt = {};
        }
        _feltRenderer = 0;
    }

    void MahjongTableComponent::OnApplicationCleanUp() {
        // The texture is a GPU object and components outlive the renderer: it goes when the application cleans up, before the renderer is destroyed
        ReleaseFelt();
    }

    ITexture* MahjongTableComponent::FeltTexture(IRenderer& renderer) {
        BE_PROFILE_SCOPE("MahjongTable::FeltTexture");
        if (renderer.GetId() != _feltRenderer) {
            _felt = {};  // made by another renderer, which has its own way of freeing it
            _feltRenderer = renderer.GetId();
            // Made once per renderer, even when it fails (the flat felt is drawn then)
            const FeltPixels pixels = MakeFeltPixels(kFeltTextureWidth, kFeltTextureHeight);
            _felt = renderer.CreateTexture(pixels.rgba.data(), pixels.width, pixels.height);
        }
        return _felt.Get();
    }

    void MahjongTableComponent::OnAttached() {
        s_active = this;
        _atlas = CoreManager::GetResourceManager().Load<ITexture>(kAtlasPath);
        if (!_atlas) {
            LOG_WARNING(Format("MahjongTableComponent: the tile atlas '{}' did not load, the tiles are drawn as plain rectangles", kAtlasPath));
        }
        _emblems = CoreManager::GetResourceManager().Load<ITexture>(kEmblemAtlasPath);
        if (!_emblems) {
            LOG_WARNING(Format("MahjongTableComponent: the emblem atlas '{}' did not load, the seat plates have no emblems", kEmblemAtlasPath));
        }
        LoadTableLayoutSpec();
        Subscribe<ApplicationEvents::ApplicationCleanUpEvent, &MahjongTableComponent::OnApplicationCleanUp>(this);
        SubscribeInScene<SDLEvents::MouseButtonDownEvent, &MahjongTableComponent::OnMouseDown>(this);
        SubscribeInScene<SDLEvents::MouseMotionEvent, &MahjongTableComponent::OnMouseMotion>(this);
        SubscribeInScene<InputEvents::ActionEvent, &MahjongTableComponent::OnAction>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &MahjongTableComponent::OnUpdate>(this);
    }

    bool MahjongTableComponent::OfferTableLayoutConfig() {
        ConfigManager& configs = CoreManager::GetConfigManager();
        const PoolString name = "TableLayout"_intern;
        if (!configs.HasConfig(name)) {
            return false;
        }
        const TableLayoutOfferResult offer = OfferTableLayoutSpec(configs.GetConfig(name).GetPugiNode());
        if (offer.outcome != TableLayoutOffer::Rejected) {
            _layoutRefused.clear();
            if (offer.outcome == TableLayoutOffer::Applied) {
                LOG_INFO("MahjongTableComponent: the table layout of 'TableLayout' is in use");
            }
            return true;
        }
        eastl::string text;
        for (const eastl::string& error : offer.errors) {
            text += error;
            text += '\n';
        }
        if (text != _layoutRefused) {
            _layoutRefused = text;
            for (const eastl::string& error : offer.errors) {
                LOG_ERROR(Format("MahjongTableComponent: the table layout is refused, the last good one stays: {}", error));
            }
        }
        return true;
    }

    void MahjongTableComponent::LoadTableLayoutSpec() {
        BE_PROFILE_SCOPE("MahjongTable::LoadTableLayoutSpec");
        _layoutPolled = std::chrono::steady_clock::now();
        if (!OfferTableLayoutConfig()) {
            return;
        }
        std::error_code error;
        const std::filesystem::path file = CoreManager::GetConfigManager().GetConfigFilePath("TableLayout"_intern);
        const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(file, error);
        _layoutStamp = error ? std::nullopt : std::optional(stamp);
    }

    void MahjongTableComponent::PollTableLayoutSpec() {
        BE_PROFILE_SCOPE("MahjongTable::PollTableLayoutSpec");
        const std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
        if (std::chrono::duration<float>(now - _layoutPolled).count() < kTableLayoutPollSeconds) {
            return;
        }
        _layoutPolled = now;
        ConfigManager& configs = CoreManager::GetConfigManager();
        const PoolString name = "TableLayout"_intern;
        if (!configs.HasConfig(name)) {
            return;
        }
        // A file that cannot be asked (a virtual file, as in the browser; deleted since) is silent: the cached document is all there is
        std::error_code error;
        const std::filesystem::file_time_type stamp = std::filesystem::last_write_time(configs.GetConfigFilePath(name), error);
        if (!error && (!_layoutStamp || *_layoutStamp != stamp)) {
            // The stamp is kept only when the reload worked, so a failed one is tried again at the next look
            if (configs.ReloadConfig(name)) {
                _layoutStamp = stamp;
            }
        }
        OfferTableLayoutConfig();
    }

    MahjongGame* MahjongTableComponent::PersonToAct() const {
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return nullptr;
        }
        return PersonActs(*game) ? game : nullptr;
    }

    MahjongTableComponent::DeclareTurn MahjongTableComponent::TurnOf(const MahjongGame& game) {
        return DeclareTurn{game.GetSession().Actor(), game.GetMatch().Hand().Discards().size()};
    }

    const eastl::vector<HandAction>& MahjongTableComponent::LegalFor(const MahjongGame& game, Seat actor) {
        if (!game.IsOnlineTable()) {
            _legal = game.GetMatch().Hand().LegalActions(actor);
            return _legal;
        }
        _legal.clear();
        if (const OnlineClient* client = game.FindOnline(); client != nullptr && ServerListsActions(game)) {
            for (const ControlAction& listed : client->Table().actions) {
                if (!listed.nextHand) {
                    _legal.push_back(listed.hand);
                }
            }
        }
        return _legal;
    }

    HandStatus MahjongTableComponent::Perform(MahjongGame& game, const HandAction& action) {
        if (!game.IsOnlineTable()) {
            return game.GetSession().PerformPerson(action);
        }
        OnlineClient& client = game.GetOnline();
        const eastl::vector<ControlAction>& listed = client.Table().actions;
        // The coach of the room judges the move the player sends, on the view he chose it on (before the table moves on)
        CoachLedger& coach = game.GetCoach();
        std::optional<TableView> before;
        if (coach.Enabled()) {
            before = MahjongHintsComponent::ViewOf(game, game.GetSession().Viewer());
        }
        for (size_t i = 0; i < listed.size(); ++i) {
            if (!listed[i].nextHand && listed[i].hand == action && client.Act(i)) {
                if (before.has_value()) {
                    coach.OnPersonMove(*before, action, game.CoachContext());
                }
                return {};
            }
        }
        return std::unexpected(HandError::NotYourTurn);
    }

    ActionButtons MahjongTableComponent::CurrentButtons(const MahjongGame& game) {
        ActionButtons buttons;
        const TableSession& session = game.GetSession();
        if (PersonActs(game)) {
            const ButtonsKey key{&game, game.IsOnlineTable(), session.Version(), session.StartCount(), session.Actor()};
            if (_offeredKey != key) {
                const HandModel& hand = game.GetMatch().Hand();
                // The tile of the window: the discard that waits for a claim, the tile of a kong that waits to be robbed (invalid in a turn)
                _offered = MakeActionButtons(LegalFor(game, key.actor), hand.PendingTile());
                _offeredKey = key;
            }
            buttons = _offered;
        }
        // The flag belongs to the turn it was set on: gone when the button goes, and when the turn moved on without the table's discard
        if (!buttons[ButtonKind::Declare].shown || TurnOf(game) != _declareTurn) {
            _declareNext = false;
        }
        return buttons;
    }

    void MahjongTableComponent::PublishScreen(const MahjongGame* game) {
        BE_PROFILE_SCOPE("MahjongTable::PublishScreen");
        const ActionButtons buttons = game != nullptr ? CurrentButtons(*game) : ActionButtons{};
        UIVars& vars = CoreManager::GetUIVars();
        Scene& scene = GetScene();

        // The canvas of the buttons: `Actions` while its buttons are touch targets, else `ActionsCompact` (the same buttons by name); the
        // button of the hints goes with them: `HintsButton` where `Actions` is, its copy in `ActionsCompact` where that is
        const WindowNumbers numbers = ReadWindow();
        const UILayoutContext& window = numbers.context;
        const ViewSize view = ActiveViewSize(scene);
        // `ActionsCompact` keeps to the safe area, so the rule takes the inset it is lifted by (an empty safe area is the whole output)
        const float bottomInset = window.safeArea.IsEmpty() ? 0.0f : window.outputHeight - (window.safeArea.y + window.safeArea.h);
        const bool compact = NeedsCompactActions(window.outputWidth, window.outputHeight, window.pixelDensity, numbers.minTouchSize, window.userScale, view.width, view.height, bottomInset);
        if (compact != _compactActions) {
            _compactActions = compact;
            UICanvasComponent::SetVisibleByName(scene, ActionsNode(), !compact);
            UICanvasComponent::SetVisibleByName(scene, HintsButtonNode(), !compact);
            UICanvasComponent::SetVisibleByName(scene, ActionsCompactNode(), compact);
        }
        const IntrusivePtr<UICanvasComponent> actionsCanvas = FindCanvasOf(scene, ActionsNode());
        const IntrusivePtr<UICanvasComponent> canvases[] = {actionsCanvas, FindCanvasOf(scene, ActionsCompactNode())};
        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonKind kind = static_cast<ButtonKind>(i);
            const ButtonState& now = buttons[kind];
            const ButtonState& before = _published[kind];
            const ButtonSpec& spec = SpecOf(kind);
            if (now.shown != before.shown) {
                UICanvasComponent::SetElementVisibleByName(scene, PoolString::Intern(spec.element), now.shown);
            }
            // The tiles pictured on the button: one element with a `UIImageComponent` per tile, hidden in the file, shown exactly while the button shows that tile
            for (int t = 0; t < spec.maxTiles; ++t) {
                const bool showNow = now.shown && t < now.tileCount;
                const bool showBefore = before.shown && t < before.tileCount;
                const bool sameTile = showNow && showBefore && now.tiles[static_cast<size_t>(t)] == before.tiles[static_cast<size_t>(t)];
                if (showNow == showBefore && (!showNow || sameTile)) {
                    continue;
                }
                const PoolString imageName = PoolString::Intern(TileImageName(kind, t));
                if (showNow) {
                    for (const IntrusivePtr<UICanvasComponent>& canvas : canvases) {
                        UIElement* element = FindElementIn(canvas, imageName);
                        if (UIImageComponent* image = element != nullptr ? element->GetComponent<UIImageComponent>() : nullptr) {
                            image->sprite = PoolString::Intern(SpriteName(now.tiles[static_cast<size_t>(t)]));
                        }
                    }
                }
                if (showNow != showBefore) {
                    UICanvasComponent::SetElementVisibleByName(scene, imageName, showNow);
                }
            }
        }
        _published = buttons;
        vars.Set("declareNext"_intern, _declareNext ? kDeclareOn : kDeclareOff);

        // The hint of the button under the pointer, in the label `Hint` that shows exactly while there is one; the sentence is
        // looked up again when the language changed under it
        const UIStrings& strings = CoreManager::GetUIStrings();
        _hoveredButton = compact ? std::nullopt : HoveredButton(actionsCanvas, buttons);
        const eastl::string_view hintKey = _hoveredButton ? buttons[*_hoveredButton].hintKey : eastl::string_view{};
        const PoolString actionHint = "actionHint"_intern;
        if (hintKey != _hintKey || strings.GetVersion() != _hintStringsVersion || !vars.Has(actionHint)) {
            _hintKey = hintKey;
            _hintStringsVersion = strings.GetVersion();
            const eastl::string hint = hintKey.empty() ? eastl::string() : Tr(strings, hintKey);
            vars.Set(actionHint, eastl::string_view(hint));
            UICanvasComponent::SetElementVisibleByName(scene, "Hint"_intern, !hint.empty());
        }

        // The cover of a hot seat: a modal canvas, shown exactly while the screen waits for the next person
        const bool handoff = game != nullptr && game->GetSession().HandoffPending();
        if (handoff) {
            const Seat actor = game->GetSession().Actor();
            vars.Set("handoffSeat"_intern, eastl::string_view(SeatName(strings, actor, game->GetMatch().Hand().SeatWind(actor))));
        }
        if (handoff != _handoffShown) {
            _handoffShown = handoff;
            UICanvasComponent::SetVisibleByName(scene, "Handoff"_intern, handoff);
        }
    }

    bool MahjongTableComponent::TapToSelect() const {
        if (_touchSeen) {
            return true;
        }
        // The window of the last draw, not the renderer's output now: in the editor the table is drawn into the viewport's target, and an
        // input event or the scene update sees the whole editor window, so the regime would be on at a click and off a moment later
        if (_drawnSmallHandTile.has_value()) {
            return *_drawnSmallHandTile;
        }
        return SmallHandTileNow();
    }

    bool MahjongTableComponent::SmallHandTileNow() const {
        if (!CoreManager::GetRenderer()) {
            return false;
        }
        const WindowNumbers numbers = ReadWindow();
        const UILayoutContext& window = numbers.context;
        const ViewSize view = ActiveViewSize(GetScene());
        const TableProfile profile = TableProfileFor(window.outputWidth, window.outputHeight, window.pixelDensity, numbers.minTouchSize, view.width, view.height);
        return UsesTapToSelect(false, window.outputWidth, window.outputHeight, window.pixelDensity, numbers.minTouchSize, profile.handTileW, view.width, view.height);
    }

    bool MahjongTableComponent::SelectionHolds(const MahjongGame& game) const {
        const TableSession& session = game.GetSession();
        const HandModel& hand = game.GetMatch().Hand();
        // A selection belongs to one discard turn of one person: a claim window, the cover, the pause and the end of the hand drop it
        if (!_selected || !PersonActs(game) || hand.GetPhase() != Phase::Discard || session.Version() != _selectedVersion || SeenHandOf(game) != _selectedHand || TurnOf(game) != _selectedTurn) {
            return false;
        }
        const eastl::vector<Tile>& concealed = hand.GetHand(session.Actor()).concealed;
        return *_selected >= 0 && *_selected < static_cast<int>(concealed.size()) && concealed[static_cast<size_t>(*_selected)] == _selectedTile && TapToSelect();
    }

    void MahjongTableComponent::RefreshSelection(const MahjongGame* game) {
        BE_PROFILE_SCOPE("MahjongTable::RefreshSelection");
        if (_selected && (game == nullptr || !SelectionHolds(*game))) {
            _selected.reset();
        }
    }

    void MahjongTableComponent::TapHandTile(MahjongGame& game, const std::optional<int>& hit) {
        TableSession& session = game.GetSession();
        const HandModel& hand = game.GetMatch().Hand();
        const TapOutcome outcome = StepTap(_selected, hit, TapToSelect());
        switch (outcome.action) {
            case TapAction::Nothing:
                break;
            case TapAction::Clear:
                _selected.reset();
                break;
            case TapAction::Select:
                // Only a discard turn has a tile to pick; a claim window has none to raise
                if (hand.GetPhase() == Phase::Discard) {
                    _selected = outcome.index;
                    _selectedTile = hand.GetHand(session.Actor()).concealed[static_cast<size_t>(outcome.index)];
                    _selectedTurn = TurnOf(game);
                    _selectedVersion = session.Version();
                    _selectedHand = SeenHandOf(game);
                }
                break;
            case TapAction::Discard:
                if (Perform(game, HandAction::MakeDiscard(hand.GetHand(session.Actor()).concealed[static_cast<size_t>(outcome.index)], _declareNext)).has_value()) {
                    _declareNext = false;
                    _selected.reset();
                }  // else the click changes nothing: a tile that cannot declare while the flag is on, or the model moved on since the draw
                break;
        }
    }

    void MahjongTableComponent::OnMouseDown(const SDLEvents::MouseButtonDownEvent& event) {
        BE_PROFILE_SCOPE("MahjongTable::OnMouseDown");
        if (IsTouchClick(event.button)) {
            _touchSeen = true;
        }
        if (event.button.button != SDL_BUTTON_LEFT) {
            return;
        }
        MahjongGame* game = PersonToAct();
        if (game == nullptr) {
            return;
        }
        RefreshSelection(game);
        const std::optional<int> hit = HitHandTile(_shown.layout, GetScene().ScreenToSceneX(event.button.x), GetScene().ScreenToSceneY(event.button.y));
        const Seat actor = game->GetSession().Actor();
        const PlayerHand& held = game->GetMatch().Hand().GetHand(actor);
        // The layout is the last frame's: it has to be of this hand
        if (hit && (_shown.layout.viewer != actor || *hit < 0 || *hit >= static_cast<int>(held.concealed.size()))) {
            return;
        }
        TapHandTile(*game, hit);
    }

    void MahjongTableComponent::OnMouseMotion(const SDLEvents::MouseMotionEvent& event) {
        const SDL_MouseMotionEvent& motion = event.motion;
        // A touch or a pen has no pointer to rest on a tile
        _pointerSeen = !IsTouchDevice(motion.which);
        _pointerX = motion.x;
        _pointerY = motion.y;
    }

    void MahjongTableComponent::OnAction(const InputEvents::ActionEvent& event) {
        BE_PROFILE_SCOPE("MahjongTable::OnAction");
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        TableSession& session = game->GetSession();
        if (event.action.ToStringView() == kRevealAction) {
            if (session.HandoffPending()) {
                session.Reveal();
            }
            return;
        }
        const std::optional<ButtonKind> kind = ButtonOf(event.action.ToStringView(), event.value.ToStringView());
        if (!kind) {
            return;
        }
        // What is offered now, not what the last frame showed
        const ActionButtons buttons = CurrentButtons(*game);
        if (!buttons[*kind].shown) {
            return;
        }
        if (*kind == ButtonKind::Declare) {
            // The toggle emits the state it asks for
            _declareNext = event.value.ToStringView() == kDeclareOn;
            _declareTurn = TurnOf(*game);
            return;
        }
        // A refused action is a button that was out of date; it changes nothing
        const int serialBefore = WindowSerial(game->GetMatch().Hand());
        if (const HandStatus performed = Perform(*game, buttons[*kind].action); !performed.has_value()) {
            if (game->IsOnlineTable()) {
                return;  // the server lists no such action any more: the next view corrects the table, there is nothing to say
            }
            LOG_WARNING(Format("MahjongTableComponent: the button '{}' was refused: {}", SpecOf(*kind).element, EnumUtils<HandError>::ToString(performed.error())));
            return;
        }
        HoldScoreTableIfBeaten(*game, serialBefore);
    }

    bool MahjongTableComponent::HandOverHeld() const {
        const MahjongGame* game = MahjongGame::GetActive();
        return _handOverHoldLeft > 0.0f && game != nullptr && SeenHandOf(*game) == _handOverHoldOf;
    }

    MahjongTableComponent::SeenHand MahjongTableComponent::SeenHandOf(const MahjongGame& game) {
        const Match& match = game.GetMatch();
        return SeenHand{&game, match.HandsPlayed(), match.History().size(), game.GetSession().StartCount()};
    }

    void MahjongTableComponent::HoldScoreTableIfBeaten(const MahjongGame& game, int serialBefore) {
        const HandModel& hand = game.GetMatch().Hand();
        const std::optional<ClaimWindowResult>& window = hand.LastClaimWindow();
        if (!window || window->serial == serialBefore || hand.GetPhase() != Phase::Finished || !hand.Result()) {
            return;
        }
        // The window ended the hand: a mahjong on its discard won it, and a claim of a person lost to it
        if (hand.Result()->source != WinSource::Discard || hand.Result()->winner != window->winner) {
            return;
        }
        const bool personBeaten = eastl::any_of(window->beaten.begin(), window->beaten.end(), [&](const LostClaim& lost) { return game.GetSession().KindOf(lost.seat) == SeatKind::Person; });
        if (personBeaten) {
            _handOverHoldLeft = ScaledSeconds(kClaimLostSeconds, game.GetAnimationSpeed());
            _handOverHoldOf = SeenHandOf(game);
        }
    }

    MahjongTableComponent::NewMoves MahjongTableComponent::FollowMoves(const MahjongGame& game) {
        BE_PROFILE_SCOPE("MahjongTable::FollowMoves");
        BE_PROFILE_SCOPE("MahjongTable::FollowMoves");
        const Match& match = game.GetMatch();
        const eastl::vector<HandMove>& moves = match.Hand().Moves();
        const SeenHand now = SeenHandOf(game);
        if (now != _seenHand || moves.size() < _movesSeen) {
            // Another hand, match or game: what is on the table now is not news
            _seenHand = now;
            _movesSeen = moves.size();
            _highlightLeft = 0.0f;
            _highlightWaits = false;
            _lostLeft = 0.0f;
            _windowSeen = WindowSerial(match.Hand());
            _handOverHoldLeft = 0.0f;
            return NewMoves{true, moves.size(), false};
        }
        const size_t first = _movesSeen;
        bool setLaid = false;
        for (size_t i = moves.size(); i > _movesSeen; --i) {
            if (moves[i - 1].meld >= 0) {
                _highlightSeat = moves[i - 1].seat;
                _highlightMeld = moves[i - 1].meld;
                _highlightLeft = Seconds(kClaimHighlightSeconds);
                _highlightWaits = false;  // `PlanBatch` makes it wait when the tiles of the set fly
                setLaid = true;
                break;
            }
        }
        _movesSeen = moves.size();
        // A claim window settled since the last frame: when a claim lost in it, the winner and the losers are marked for a while (a pass is no
        // claim, so a window that nobody claimed in or that all passed marks nothing)
        if (const std::optional<ClaimWindowResult>& window = match.Hand().LastClaimWindow(); window && window->serial != _windowSeen) {
            _windowSeen = window->serial;
            _lostLeft = 0.0f;
            if (!window->beaten.empty()) {
                _lostWinner = window->winner;
                _lostSeats.fill(false);
                for (const LostClaim& lost : window->beaten) {
                    _lostSeats[lost.seat] = true;
                }
                _lostLeft = Seconds(kClaimLostSeconds);
            }
        }
        return NewMoves{false, first, setLaid};
    }

    void MahjongTableComponent::PlanBatch(const TableSnapshot& before, const NewMoves& news, const HandModel& hand) {
        BE_PROFILE_SCOPE("MahjongTable::PlanBatch");
        BE_PROFILE_SCOPE("MahjongTable::PlanBatch");
        const TableLayout& was = before.layout;
        const TableLayout& now = _shown.layout;
        const bool sameView = was.viewer == now.viewer && was.shape == now.shape && was.viewWidth == now.viewWidth && was.viewHeight == now.viewHeight && was.profile == now.profile;
        if (news.restarted || !sameView || _flightSeconds <= 0.0f) {
            EndFlights();
            return;
        }
        // Nothing new is the common frame: no move since the last draw, no tile drawn from the wall and no win (a win is no move)
        const eastl::vector<HandMove>& moves = hand.Moves();
        const eastl::span<const HandMove> fresh(moves.data() + news.first, moves.size() - news.first);
        const bool newWin = _shown.winSeat != kNoSeat && before.winSeat == kNoSeat;
        if (fresh.empty() && _shown.wallRemaining >= before.wallRemaining && !newWin) {
            return;
        }
        eastl::vector<TileFlight> planned = PlanFlights(before, _shown, fresh);
        if (!planned.empty()) {
            EndFlights();  // the running flights end at their targets: their slots are drawn in place from now on
            _flights = eastl::move(planned);
        }
        // The set that was just laid blinks once its tiles have landed: not while one of them is in the air
        if (news.setLaid) {
            _highlightWaits = eastl::any_of(_flights.begin(), _flights.end(),
                                            [&](const TileFlight& flight) { return flight.key.role == SlotRole::MeldTile && flight.key.seat == _highlightSeat && flight.key.index == _highlightMeld; });
        }
    }

    void MahjongTableComponent::EndFlights() {
        _flights.clear();
        _flightElapsed = 0.0f;
        _highlightWaits = false;
    }

    void MahjongTableComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        BE_PROFILE_SCOPE("MahjongTable::OnUpdate");
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            DropComputerStep();
            return;
        }
        _speed = game->GetAnimationSpeed();
        RefreshSelection(game);
        if (_highlightLeft > 0.0f && !_highlightWaits && TableIsLive(*game)) {
            _highlightLeft = eastl::max(0.0f, _highlightLeft - event.deltaTime);
        }
        if (_lostLeft > 0.0f && TableIsLive(*game)) {
            _lostLeft = eastl::max(0.0f, _lostLeft - event.deltaTime);
        }
        if (_handOverHoldLeft > 0.0f && TableIsLive(*game)) {
            _handOverHoldLeft = eastl::max(0.0f, _handOverHoldLeft - event.deltaTime);
        }
        if (!_flights.empty() && TableIsLive(*game)) {
            _flightElapsed += event.deltaTime;
            if (_flightElapsed >= Seconds(_flightSeconds)) {
                EndFlights();  // the set that waited for its tiles blinks from the next draw on, at the start of its time
            }
        }
        PlayComputers(*game, event.deltaTime);
    }

    void MahjongTableComponent::DropComputerStep() {
        _thinking.Drop();
        _step.reset();
        _burst = Burst::None;
        _firstInWindow = false;
    }

    bool MahjongTableComponent::BeginComputerStep(TableSession& session, const HandModel& hand) {
        BE_PROFILE_SCOPE("MahjongTable::BeginComputerStep");
        std::optional<ComputerStepJob> job = session.BeginComputerStep();
        if (!job) {
            return false;
        }
        if (_burst == Burst::None) {
            _firstInWindow = IsClaimWindow(hand.GetPhase());
            _burstSerial = WindowSerial(hand);
        }
        _step = StepInProgress{job->Version(), job->StartCount()};
        if (CoreManager::GetControlService().IsManualClock()) {
            job->Run();  // frames are `step`s of a script there: the answer is computed at once, whatever the clock of the machine says
        }
        _thinking.Start(std::move(*job));
        return true;
    }

    void MahjongTableComponent::PlayComputers(MahjongGame& game, float deltaTime) {
        BE_PROFILE_SCOPE("MahjongTable::PlayComputers");
        TableSession& session = game.GetSession();
        if (!ComputerActs(game)) {
            DropComputerStep();
            _computerClock = 0.0f;
            return;
        }
        if (_step && (_step->version != session.Version() || _step->starts != session.StartCount())) {
            // A control action, a new match or a person got in between: the answer is for a table that is gone. The pause already spent stays
            _thinking.Drop();
            _step.reset();
            _burst = Burst::None;
        }
        const HandModel& hand = game.GetMatch().Hand();
        const float pause = Seconds(kComputerStepSeconds);
        if (_burst == Burst::None && _computerClock < pause) {
            if (!_step && !BeginComputerStep(session, hand)) {
                return;
            }
            _computerClock += deltaTime;
        }
        // Past the pause the answer is applied when it is there; a search that is still running is not waited for (no blocking)
        while (_burst != Burst::None || _computerClock >= pause) {
            if (!_step && !BeginComputerStep(session, hand)) {
                return;
            }
            std::optional<ComputerStepJob> job = _thinking.TakeFinished();
            if (!job) {
                return;
            }
            _step.reset();
            const std::optional<StepResult> stepped = session.FinishComputerStep(*job);
            if (!stepped) {
                _burst = Burst::None;
                continue;
            }
            if (!stepped->has_value()) {
                LOG_ERROR("MahjongTableComponent: a computer seat's action was refused");
                DropComputerStep();
                return;
            }
            if (_burst == Burst::None) {
                _computerClock = 0.0f;  // one action a period, whatever the frame took
                if (!_firstInWindow) {
                    return;
                }
                _burst = Burst::Window;
            } else if (_burst == Burst::Discard) {
                _burst = Burst::None;
                HoldScoreTableIfBeaten(game, _burstSerial);
                return;
            }
            // The pause after a discard is one whether or not a window opened (a longer one would tell a person that a computer holds a
            // claim): the computers settle a window within the pause, and a computer that is to discard after it does so in the same pause.
            if (_burst == Burst::Window && !(ComputerActs(game) && IsClaimWindow(hand.GetPhase()))) {
                _burst = Burst::Discard;
            }
            if (_burst == Burst::Discard && !ComputerActs(game)) {
                _burst = Burst::None;
                HoldScoreTableIfBeaten(game, _burstSerial);
                return;
            }
        }
    }

    void MahjongTableComponent::PublishSelectedText(Tile tile) {
        BE_PROFILE_SCOPE("MahjongTable::PublishSelectedText");
        UIVars& vars = CoreManager::GetUIVars();
        const UIStrings& strings = CoreManager::GetUIStrings();
        const PoolString name = "selectedTileText"_intern;
        // The label `SelectedTile` of the node `Hud` is shown exactly while a tile is selected (this is its only owner)
        if (tile.IsValid() != _selectedLabelShown) {
            _selectedLabelShown = tile.IsValid();
            UICanvasComponent::SetElementVisibleByName(GetScene(), "SelectedTile"_intern, _selectedLabelShown);
        }
        if (tile == _textTile && (!tile.IsValid() || strings.GetVersion() == _textStringsVersion) && vars.Has(name)) {
            return;
        }
        _textTile = tile;
        _textStringsVersion = strings.GetVersion();
        const eastl::string text = tile.IsValid() ? SelectedTileText(strings, tile) : eastl::string();
        vars.Set(name, eastl::string_view(text));
    }

    void MahjongTableComponent::PublishHover(const MahjongGame* game, float viewWidth, float viewHeight) {
        BE_PROFILE_SCOPE("MahjongTable::PublishHover");
        Scene& scene = GetScene();
        const bool live = game != nullptr && game->GetMatch().GetPhase() != MatchPhase::NotStarted;
        // A pointer that the table cannot follow any more is forgotten, not kept: something else took it (the pause, the cover, a canvas
        // that consumes its motion), so it can have moved on without the table hearing of it
        if (_pointerSeen && (!live || game->IsTablePaused() || game->GetSession().HandoffPending() || CanvasHasPointer(scene))) {
            _pointerSeen = false;
        }
        const TileSlot* slot = nullptr;
        Tile tile;
        if (_pointerSeen) {
            slot = SlotAt(_shown.layout, scene.ScreenToSceneX(_pointerX), scene.ScreenToSceneY(_pointerY));
            // A tile in the air is not where its slot is
            if (slot != nullptr && eastl::any_of(_flights.begin(), _flights.end(), [&](const TileFlight& flight) { return flight.key.Matches(*slot); })) {
                slot = nullptr;
            }
            if (slot != nullptr) {
                const std::optional<Tile> shown = TileOfSlot(game->GetMatch().Hand(), *slot, FacesOf(game->GetSession()).Shows(slot->seat));
                tile = shown.value_or(Tile{});
            }
            if (!tile.IsValid()) {
                slot = nullptr;
            }
        }

        UIVars& vars = CoreManager::GetUIVars();
        const UIStrings& strings = CoreManager::GetUIStrings();
        const PoolString name = "tileHoverText"_intern;
        if (tile.IsValid() != _hoverShown) {
            _hoverShown = tile.IsValid();
            UICanvasComponent::SetElementVisibleByName(scene, "TileHoverPlate"_intern, _hoverShown);
        }
        // A tile of the viewer's own hand says what the bar of the hints on it means (the hints draw it a frame ahead of this word at most)
        DangerMark danger = DangerMark::None;
        if (slot != nullptr && slot->role == SlotRole::HandTile && slot->seat == _shown.layout.viewer) {
            if (const MahjongHintsComponent* hints = MahjongHintsComponent::GetActive()) {
                danger = hints->DangerOf(tile);
            }
        }
        const bool wordsChanged = tile.IsValid() && strings.GetVersion() != _hoverStringsVersion;
        if (tile != _hoverTile || wordsChanged || !vars.Has(name)) {
            _hoverTile = tile;
            const eastl::string text = tile.IsValid() ? TileName(strings, tile) : eastl::string();
            vars.Set(name, eastl::string_view(text));
        }
        const PoolString dangerName = "tileHoverDanger"_intern;
        const bool dangerMoved = danger != _hoverDanger;
        if (dangerMoved || wordsChanged || !vars.Has(dangerName)) {
            const eastl::string text = danger != DangerMark::None ? Tr(strings, DangerHoverKey(danger)) : eastl::string();
            vars.Set(dangerName, eastl::string_view(text));
        }
        if (tile.IsValid()) {
            _hoverStringsVersion = strings.GetVersion();
        }
        if (slot == nullptr && !dangerMoved) {
            return;
        }
        // The plate is laid out again each time it moves or grows; it is in place when the canvas of the node draws, later in this frame
        const IntrusivePtr<UICanvasComponent> canvas = FindCanvasOf(scene, "TileHover"_intern);
        UIElement* plate = FindElementIn(canvas, "TileHoverPlate"_intern);
        if (plate == nullptr) {
            return;
        }
        UIRectSpec& spec = plate->style.rect;
        if (dangerMoved) {
            const bool worded = danger != DangerMark::None;
            if (worded != (_hoverDanger != DangerMark::None)) {
                spec.height = UILength::Px(worded ? kHoverPlateTallHeight : kHoverPlateHeight);
                UICanvasComponent::SetElementVisibleByName(scene, "TileHoverDanger"_intern, worded);
            }
            if (UIElement* line = FindElementIn(canvas, "TileHoverDanger"_intern); UITextComponent* label = line != nullptr ? line->GetComponent<UITextComponent>() : nullptr) {
                label->textColor = DangerMarkColor(danger);
            }
            _hoverDanger = danger;
            canvas->Invalidate();
        }
        if (slot == nullptr) {
            return;
        }
        const Rect place = HoverPlateRect(slot->rect, spec.width.value, spec.height.value, viewWidth, viewHeight);
        if (spec.x.value != place.x || spec.y.value != place.y) {
            spec.x = UILength::Px(place.x);
            spec.y = UILength::Px(place.y);
            canvas->Invalidate();
        }
    }

    void MahjongTableComponent::PublishRegions(const TableLayout& layout) {
        BE_PROFILE_SCOPE("MahjongTable::PublishRegions");
        RebuildTableRegions(layout, _regions);
        bool publish = !SameRegions(_regions, _publishedRegions);
        // A node the scene lacks is looked for again (the scene of a test may get it later); in the game all are found at the first draw
        Scene& scene = GetScene();
        for (size_t i = 0; i < kRegionNodeCount; ++i) {
            if (!_regionCanvases[i]) {
                _regionCanvases[i] = FindCanvasOf(scene, PoolString::Intern(kRegionNodes[i]));
                publish = publish || _regionCanvases[i];
            }
        }
        if (!publish) {
            return;
        }
        // The names are interned for the new set only; a name that left the spec is withdrawn from every canvas
        eastl::vector<PoolString> names;
        names.reserve(_regions.size());
        for (const TableRegion& region : _regions) {
            names.push_back(PoolString::Intern(eastl::string_view(region.name.data(), region.name.size())));
        }
        for (const PoolString& old : _regionNames) {
            if (eastl::find(names.begin(), names.end(), old) == names.end()) {
                for (const IntrusivePtr<UICanvasComponent>& canvas : _regionCanvases) {
                    if (canvas) {
                        canvas->RemoveRegion(old);
                    }
                }
            }
        }
        for (const IntrusivePtr<UICanvasComponent>& canvas : _regionCanvases) {
            if (!canvas) {
                continue;
            }
            for (size_t i = 0; i < _regions.size(); ++i) {
                canvas->SetRegion(names[i], _regions[i].rect);
            }
        }
        _regionNames = eastl::move(names);
        _publishedRegions = _regions;
    }

    void MahjongTableComponent::SetAtlasTexture(IntrusivePtr<ITexture> texture) {
        _atlas = ResourceHandle<ITexture>(std::move(texture));
    }

    void MahjongTableComponent::SetEmblemTexture(IntrusivePtr<ITexture> texture) {
        _emblems = ResourceHandle<ITexture>(std::move(texture));
    }

    void MahjongTableComponent::OnDraw() {
        BE_PROFILE_SCOPE("MahjongTable::OnDraw");
        PollTableLayoutSpec();
        _drawnSmallHandTile = SmallHandTileNow();
        IRenderer& renderer = *CoreManager::GetRenderer();
        // The active view: the design of one of the four shapes (`Scene::Draw` picked it before this draw)
        const ViewSize view = ActiveViewSize(GetScene());
        const float viewWidth = view.width;
        const float viewHeight = view.height;
        // The felt is one soft picture over the view; without a texture (the backend refused it, a test renderer) it is the flat colour. It is
        // a vignette and a grain of 3 levels, so over the tall portrait view the vignette is an ellipse and the grain about 3:1 taller than
        // wide: not visible (a portrait texture of its own is for a look that says it is)
        if (ITexture* felt = FeltTexture(renderer)) {
            renderer.DrawTexture(*felt, nullptr, 0.0f, 0.0f, viewWidth, viewHeight);
        } else {
            renderer.DrawFilledRect(0.0f, 0.0f, viewWidth, viewHeight, kFelt);
        }
        // The layout is built in design units (those of the shape of the view) and scaled to the view, so what is measured in units here is
        // scaled the same way
        const float unit = LayoutScale(viewWidth, viewHeight);
        const float rim = kRimWidth * unit;
        renderer.DrawFilledRect(0.0f, 0.0f, viewWidth, rim, kFeltRim);
        renderer.DrawFilledRect(0.0f, viewHeight - rim, viewWidth, rim, kFeltRim);
        renderer.DrawFilledRect(0.0f, rim, rim, viewHeight - 2.0f * rim, kFeltRim);
        renderer.DrawFilledRect(viewWidth - rim, rim, rim, viewHeight - 2.0f * rim, kFeltRim);
        // The lip on the inner side of the rim: four bars, the side ones between the top and the bottom one
        const float lip = kFeltLipWidth * unit;
        renderer.DrawFilledRect(rim, rim, viewWidth - 2.0f * rim, lip, kFeltLip);
        renderer.DrawFilledRect(rim, viewHeight - rim - lip, viewWidth - 2.0f * rim, lip, kFeltLip);
        renderer.DrawFilledRect(rim, rim + lip, lip, viewHeight - 2.0f * (rim + lip), kFeltLip);
        renderer.DrawFilledRect(viewWidth - rim - lip, rim + lip, lip, viewHeight - 2.0f * (rim + lip), kFeltLip);

        // The two snapshots trade places: `_previous` is what the last draw drew, `_shown` the one before it, emptied and refilled below with
        // the capacity it has (no snapshot is made new, so a table at rest draws without the heap)
        eastl::swap(_previous, _shown);
        ClearSnapshot(_shown);
        const TableSnapshot& before = _previous;
        _chowTakes = ChowTakes{};
        _drawnSelected.reset();
        const MahjongGame* game = MahjongGame::GetActive();
        if (game != nullptr) {
            _speed = game->GetAnimationSpeed();
        }
        PublishScreen(game);
        RefreshSelection(game);
        if (game == nullptr || game->GetMatch().GetPhase() == MatchPhase::NotStarted) {
            PublishSelectedText(Tile{});
            PublishHover(nullptr, viewWidth, viewHeight);
            EndFlights();
            return;
        }
        const TableSession& session = game->GetSession();
        const HandModel& hand = game->GetMatch().Hand();
        const NewMoves news = FollowMoves(*game);
        const Faces faces = FacesOf(session);
        const Seat bottom = BottomSeat(session);
        FillInput(_input, hand, bottom);
        TableInput& input = _input;
        input.profile = CurrentTableProfile(view);
        RebuildTableLayout(_shown.layout, input, viewWidth, viewHeight);
        PublishRegions(_shown.layout);
        FillSnapshot(_shown, hand, faces, bottom);
        PlanBatch(before, news, hand);
        const TableLayout& layout = _shown.layout;
        // The two tiles of the viewer's hand that the offered chow takes (the hovered one; the only one when a single chow is on offer) are
        // framed in place: only in a hand that is face up and while the viewer is the person to act (so not under the cover)
        eastl::array<int, 2> chowIndex{-1, -1};
        if (IsSeat(bottom) && PersonActs(*game) && session.Actor() == bottom && faces.seat == bottom) {
            _chowTakes = ChowTilesTaken(_published, hand.PendingTile(), _hoveredButton);
            const PlayerHand& viewer = hand.GetHand(bottom);
            for (int k = 0; k < _chowTakes.count; ++k) {
                for (size_t i = 0; i < viewer.concealed.size(); ++i) {
                    if (viewer.concealed[i] == _chowTakes.tiles[static_cast<size_t>(k)]) {
                        chowIndex[static_cast<size_t>(k)] = static_cast<int>(i);
                        break;  // one copy of each tile of the sequence
                    }
                }
            }
        }
        // The selected tile (a tap in the touch regime) is the viewer's own and drawn raised, in a hand that is face up
        if (_selected && IsSeat(bottom) && PersonActs(*game) && session.Actor() == bottom && faces.seat == bottom) {
            _drawnSelected = _selected;
        }
        PublishSelectedText(_drawnSelected ? _selectedTile : Tile{});
        // The tile the viewer has just drawn is tinted in its place, only in a hand that is face up: under the cover and for a spectator
        // nothing marks it (and the layout is the same plain row either way)
        const int drawnIndex = IsSeat(bottom) && session.GetPhase() == SessionPhase::Playing && faces.seat == bottom ? DrawnIndexOf(hand, bottom) : -1;

        // The discards that keep a declared hand waiting: only on the viewer's own turn in a hand that is face up (the guard is cheap, the
        // view is made only when it can say something); not tied to the hints switch
        eastl::array<bool, kTileIdCount> keepsWaiting{};
        if (IsSeat(bottom) && session.GetPhase() == SessionPhase::Playing && faces.seat == bottom && hand.GetPhase() == Phase::Discard && hand.CurrentSeat() == bottom &&
            hand.GetHand(bottom).callingDeclared) {
            // The view allocates: the bars are worked out again only when the session moved (the same key as the buttons)
            const ButtonsKey key{game, game->IsOnlineTable(), session.Version(), session.StartCount(), bottom};
            if (_keepsKey != key) {
                TableView view = MakeTableView(hand, bottom);
                if (game->IsOnlineTable()) {
                    view.legal = LegalFor(*game, bottom);  // a display hand lists nothing: the server's list says what may be discarded
                }
                _keeps = KeepsWaitingDiscards(view);
                _keepsKey = key;
            }
            keepsWaiting = _keeps;
        } else {
            _keepsKey.reset();
        }

        // The winds that score for the viewer, marked on its own face-up tiles while the hand is played: under the cover and for a spectator
        // nothing marks them (the winds are read from the model only then)
        const bool windMarks = IsSeat(bottom) && session.GetPhase() == SessionPhase::Playing && faces.seat == bottom;
        const Wind seatWind = windMarks ? hand.SeatWind(bottom) : Wind::East;
        const Wind roundWind = windMarks ? hand.RoundWind() : Wind::East;

        // A face (the tile) or the back (nullopt) placed as `place` says: from the atlas, else a plain rectangle. `stack` draws the wall
        // stack seen from above (its own cell, a plain back without the atlas) instead of a tile. A turned tile (a side seat, or a tile in
        // the air between two turns) hands the draw the unturned rectangle and the turn about the pivot; a tile that is not turned is the
        // plain draw of its rectangle (`place.upright`, which is the screen rectangle then).
        const auto drawFace = [&](const TilePlacement& place, std::optional<Tile> face, bool stack = false) {
            const Rect& upright = place.upright;
            const DrawTransform turn{place.rotation, place.pivotX, place.pivotY};
            const bool turned = !turn.IsIdentity();
            if (_atlas) {
                const Rect source = stack ? StackRect() : face ? SourceRect(*face) : BackRect();
                if (turned) {
                    renderer.DrawTransformedTexture(*_atlas, &source, upright.x, upright.y, upright.w, upright.h, kUntinted, turn);
                } else {
                    renderer.DrawTexture(*_atlas, &source, upright.x, upright.y, upright.w, upright.h);
                }
                return;
            }
            const auto fill = [&](const Rect& part, const Color& color) {
                if (turned) {
                    renderer.DrawTransformedRect(part.x, part.y, part.w, part.h, color, turn);
                } else {
                    renderer.DrawFilledRect(part.x, part.y, part.w, part.h, color);
                }
            };
            fill(upright, face ? kPlainFace : kPlainBack);
            if (face) {
                const float insetX = upright.w * kPlainMarkInset;
                const float insetY = upright.h * kPlainMarkInset;
                fill(Rect{upright.x + insetX, upright.y + insetY, upright.w - 2.0f * insetX, upright.h - 2.0f * insetY}, PlainMarkColor(*face));
            }
        };
        // The shadow under a tile (not under a wall stack), two layers: the wide faint one, then the tight one at the offset; the frame of
        // a pending discard lies over both, so the gold is even
        const auto drawShadow = [&](const Rect& rect) {
            const float margin = kShadowSoftMargin * unit;
            renderer.DrawFilledRect(rect.x + kShadowOffset * unit - margin, rect.y + kShadowOffset * unit - margin, rect.w + 2.0f * margin, rect.h + 2.0f * margin, kTileShadowSoft);
            renderer.DrawFilledRect(rect.x + kShadowOffset * unit, rect.y + kShadowOffset * unit, rect.w, rect.h, kTileShadow);
        };
        const auto drawTile = [&](const Rect& rect, const TilePlacement& place, std::optional<Tile> face) {
            drawShadow(rect);
            drawFace(place, face);
        };

        // The centre panel (public data only, like the plates): its ground, the light on the edge that faces the seat whose turn it is (none
        // unless the session plays) and the mark of the dealer in the dealer's cell (East of the hand shown, as for the plates)
        const Seat turnSeat = TurnSeat(session, hand);
        const Rect& panel = layout.zones.centerPanel;
        renderer.DrawFilledRect(panel.x, panel.y, panel.w, panel.h, kSeatPlate);
        if (IsSeat(turnSeat)) {
            const Rect light = PanelTurnLight(panel, SeatPosition(bottom, turnSeat), kPanelTurnLightWidth * unit);
            renderer.DrawFilledRect(light.x, light.y, light.w, light.h, kPanelTurnLight);
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (hand.SeatWind(seat) == Wind::East) {
                const Rect mark = PanelDealerMark(panel, SeatPosition(bottom, seat), kDealerMarkHeight * unit);
                renderer.DrawFilledRect(mark.x, mark.y, mark.w, mark.h, kPanelDealerMark);
            }
        }

        // The seat plates (public data only, so also under the cover, for a spectator and at the end of a hand): the base of each, on the
        // plate of the seat whose turn it is the gold tint and frame, on the dealer's (East of the hand shown: the same at the end of the
        // match as in the hand) the bar of the dealer, then the emblem of the seat with its rings (the turn ring while it is the seat's turn,
        // the calling ring while its hand is declared); the last claim window's blink and frame go over all of it
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const Rect& box = layout.zones.seatPlates[seat];
            renderer.DrawFilledRect(box.x, box.y, box.w, box.h, kSeatPlate);
            if (seat == turnSeat) {
                renderer.DrawFilledRect(box.x, box.y, box.w, box.h, kTurnMark);
                DrawFrame(renderer, box, kSeatTurnFrameWidth * unit, kSeatTurnFrame);
            }
            if (hand.SeatWind(seat) == Wind::East) {
                renderer.DrawFilledRect(box.x, box.y, box.w, kDealerMarkHeight * unit, kDealerMark);
            }
            // The same public reads as `MakeSeatPlates` (kind, level, turn, declared hand), without its words: no string is made per frame
            DrawEmblem(renderer, _emblems.Get(), EmblemBox(box, unit), session.KindOf(seat), session.LevelOf(seat), seat == turnSeat, hand.GetHand(seat).callingDeclared, unit);
            // The last claim window (public): the plate of the seat that won it blinks as a set just laid does, the plate of each seat whose
            // claim lost has a frame
            if (_lostLeft > 0.0f) {
                if (seat == _lostWinner && BlinkIsOn(Seconds(kClaimLostSeconds) - _lostLeft, Seconds(kClaimBlinkSeconds))) {
                    renderer.DrawFilledRect(box.x, box.y, box.w, box.h, kClaimHighlight);
                }
                if (_lostSeats[seat]) {
                    DrawFrame(renderer, box, kSeatTurnFrameWidth * unit, kClaimLostFrame);
                }
            }
        }

        // The rack under the viewer's hand (public: the seat at the bottom, face up or not): the dark plate, the lighter body on it with the
        // dark edge left on three sides. It ends at the rim, which the hand's own padding would reach.
        if (IsSeat(bottom)) {
            const Rect bounds = BoundsOf(layout, SlotRole::HandTile, bottom);
            if (bounds.w > 0.0f) {
                Rect plate = Padded(bounds, kRackPadding * unit);
                plate.h = eastl::min(plate.h, viewHeight - rim - plate.y);
                const float edge = kHandRackLipWidth * unit;
                renderer.DrawFilledRect(plate.x, plate.y, plate.w, plate.h, kHandRackLip);
                renderer.DrawFilledRect(plate.x + edge, plate.y, plate.w - 2.0f * edge, plate.h - edge, kHandRack);
            }
        }

        if (IsSeat(turnSeat)) {
            const Rect bounds = BoundsOf(layout, SlotRole::HandTile, turnSeat);
            if (bounds.w > 0.0f) {
                const Rect marked = Padded(bounds, kActingMargin * unit);
                renderer.DrawFilledRect(marked.x, marked.y, marked.w, marked.h, kTurnMark);
            }
        }
        // The set that was just laid blinks behind its tiles: on at the moment it is laid (its tiles have landed), then half a period on and
        // half off; nothing while a tile of it is in the air
        if (_highlightLeft > 0.0f && !_highlightWaits) {
            const Rect bounds = BoundsOfMeld(layout, _highlightSeat, _highlightMeld);
            if (bounds.w > 0.0f && BlinkIsOn(Seconds(kClaimHighlightSeconds) - _highlightLeft, Seconds(kClaimBlinkSeconds))) {
                const Rect marked = Padded(bounds, kActingMargin * unit);
                renderer.DrawFilledRect(marked.x, marked.y, marked.w, marked.h, kClaimHighlight);
            }
        }
        // A declared calling hand is public: a frame round the hand of each seat that has declared it, under the tiles (it reads the flag
        // only, so it shows under the cover and at the end of the hand too)
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (!hand.GetHand(seat).callingDeclared) {
                continue;
            }
            const Rect bounds = BoundsOf(layout, SlotRole::HandTile, seat);
            if (bounds.w > 0.0f) {
                DrawFrame(renderer, Padded(bounds, kActingMargin * unit), kCallingFrameWidth * unit, kCallingFrame);
            }
        }
        // The tiles that cannot declare the calling hand when they are discarded, while the next discard is to declare it
        eastl::array<bool, kTileIdCount> cannotDeclare{};
        if (_declareNext) {
            cannotDeclare.fill(true);
            for (const HandAction& action : LegalFor(*game, session.Actor())) {
                if (action.kind == ActionKind::Discard && action.declareCalling) {
                    cannotDeclare[action.tile.Id()] = false;
                }
            }
        }
        // The frame on the discard that waits tells a person that a claim is open for them: only the viewer who is asked sees it (a window
        // that only computers may claim in shows nothing, and nothing shows while the screen is covered)
        bool claimPending = false;
        if (session.GetPhase() == SessionPhase::Playing && hand.GetPhase() == Phase::Claim && !hand.Discards().empty() && IsSeat(faces.seat)) {
            claimPending = hand.AwaitsClaim(faces.seat);
        }
        const int pendingEntry = static_cast<int>(hand.Discards().size()) - 1;
        const int lastDiscard = LastDiscardEntry(hand);

        // The bar along the top edge of a hand tile that is the own wind (left half the seat's colour, right half the round's when it is both)
        const auto drawWindMark = [&](const Rect& rect, WindMark mark) {
            if (mark == WindMark::None) {
                return;
            }
            const float bar = kWindMarkBarHeight * unit;
            if (mark == WindMark::Both) {
                const float half = rect.w * 0.5f;
                renderer.DrawFilledRect(rect.x, rect.y, half, bar, kSeatWindMark);
                renderer.DrawFilledRect(rect.x + half, rect.y, rect.w - half, bar, kRoundWindMark);
                return;
            }
            renderer.DrawFilledRect(rect.x, rect.y, rect.w, bar, mark == WindMark::Seat ? kSeatWindMark : kRoundWindMark);
        };

        // One slot at the screen rectangle `rect` placed as `place`: its own, or where it is in the air; the face turns as `place` says and
        // every mark of the slot follows the screen rectangle
        const auto drawSlot = [&](const TileSlot& slot, const Rect& rect, const TilePlacement& place) {
            const PlayerHand& held = hand.GetHand(slot.seat);
            switch (slot.role) {
                case SlotRole::HandTile:
                    drawTile(rect, place, faces.Shows(slot.seat) ? std::optional<Tile>(held.concealed[slot.index]) : std::nullopt);
                    if (slot.seat == bottom && slot.index == drawnIndex) {
                        renderer.DrawFilledRect(rect.x, rect.y, rect.w, rect.h, kDrawnTileMark);
                    }
                    if (slot.seat == bottom && (slot.index == chowIndex[0] || slot.index == chowIndex[1])) {
                        DrawFrame(renderer, rect, kPendingMargin * unit, kPendingFrame);
                    }
                    if (slot.seat == bottom && keepsWaiting[held.concealed[slot.index].Id()]) {
                        const float bar = kKeepsWaitingBarHeight * unit;
                        renderer.DrawFilledRect(rect.x, rect.y + rect.h - bar, rect.w, bar, kKeepsWaitingMark);
                    }
                    if (windMarks && slot.seat == bottom) {
                        drawWindMark(rect, WindMarkOf(held.concealed[slot.index], seatWind, roundWind));
                    }
                    if (_declareNext && slot.seat == session.Actor() && cannotDeclare[held.concealed[slot.index].Id()]) {
                        renderer.DrawFilledRect(rect.x, rect.y, rect.w, rect.h, kVeil);
                    }
                    break;
                case SlotRole::MeldTile: {
                    const Meld& meld = held.melds[slot.index];
                    // An open set is public; a concealed kong is as hidden as the hand it was made from.
                    const bool shown = !meld.concealed || faces.Shows(slot.seat);
                    drawTile(rect, place, shown ? std::optional<Tile>(MeldTileAt(meld, slot.part)) : std::nullopt);
                    if (slot.claimEdge != Edge::None) {
                        const Rect bar = EdgeBar(rect, slot.claimEdge, kClaimedEdgeWidth * unit);
                        renderer.DrawFilledRect(bar.x, bar.y, bar.w, bar.h, kClaimedEdge);
                    }
                    break;
                }
                case SlotRole::BonusTile:
                    drawTile(rect, place, held.bonus[slot.index]);
                    break;
                case SlotRole::DiscardTile:
                    if (claimPending && slot.index == pendingEntry) {
                        drawShadow(rect);
                        const Rect framed = Padded(rect, kPendingMargin * unit);
                        renderer.DrawFilledRect(framed.x, framed.y, framed.w, framed.h, kPendingFrame);
                        drawFace(place, hand.Discards()[slot.index].tile);
                    } else {
                        drawTile(rect, place, hand.Discards()[slot.index].tile);
                    }
                    if (slot.index == lastDiscard) {
                        renderer.DrawFilledRect(rect.x, rect.y, rect.w, rect.h, kLastDiscardMark);
                    }
                    break;
                case SlotRole::WallStack:
                    drawFace(place, std::nullopt, /*stack=*/true);
                    break;
            }
        };

        // A slot whose tile is in the air is drawn once, in the second pass, over everything else; a flight whose slot the layout no longer
        // has is ignored
        const auto flightOf = [&](const TileSlot& slot) -> const TileFlight* {
            for (const TileFlight& flight : _flights) {
                if (flight.key.Matches(slot)) {
                    return &flight;
                }
            }
            return nullptr;
        };
        // The raised tile is drawn after the other slots: it rises over the bonus row above the hand, which comes later in the layout
        const TileSlot* raised = nullptr;
        for (const TileSlot& slot : layout.slots) {
            if (flightOf(slot) != nullptr) {
                continue;
            }
            if (_drawnSelected && slot.role == SlotRole::HandTile && slot.seat == bottom && slot.index == *_drawnSelected) {
                raised = &slot;
                continue;
            }
            drawSlot(slot, slot.rect, PlacementOf(slot.rect, slot.rotation));
        }
        if (raised != nullptr) {
            const Rect lifted = RaisedRect(raised->rect);
            drawSlot(*raised, lifted, PlacementOf(lifted, raised->rotation));
        }
        if (!_flights.empty()) {
            const float progress = Ease(kTileFlightEase, _flightSeconds > 0.0f ? _flightElapsed / Seconds(_flightSeconds) : 1.0f);
            for (const TileSlot& slot : layout.slots) {
                if (const TileFlight* flight = flightOf(slot)) {
                    drawSlot(slot, FlightRect(*flight, slot.rect, progress), FlightPlacement(*flight, slot, progress));
                }
            }
        }
        PublishHover(game, viewWidth, viewHeight);
    }

}  // namespace BECore::Mahjong
