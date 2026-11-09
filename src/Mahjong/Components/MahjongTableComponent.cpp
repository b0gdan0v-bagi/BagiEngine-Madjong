#include "MahjongTableComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIEnvironment.h>
#include <BECore/UI/UIImage.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/MahjongTableComponent.gen.hpp>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/SeatName.h>
#include <Mahjong/Table/TileAtlas.h>
#include <Mahjong/Table/TouchRegime.h>
#include <Mahjong/Table/UiPalette.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kRevealAction = "MahjongReveal";
        constexpr Color kPlainFace{238, 230, 205, 255};
        constexpr Color kPlainBack{36, 120, 118, 255};
        constexpr Color kPlainSuit{70, 110, 200, 255};
        constexpr Color kPlainHonour{200, 60, 50, 255};
        constexpr Color kPlainBonus{60, 160, 90, 255};
        /// The highlight of the acting seat reaches this far past the hand; the pending discard's frame this far past the tile.
        constexpr float kActingMargin = 4.0f;
        constexpr float kPendingMargin = 3.0f;
        /// The rim along the edge of the view, and a tile's shadow falls this far down and right (a plate's padding is `kPlatePadding`).
        constexpr float kRimWidth = 8.0f;
        constexpr float kShadowOffset = 2.0f;
        /// The inner mark of a tile drawn without the atlas, as a fraction of the tile.
        constexpr float kPlainMarkInset = 0.25f;

        /// Whose concealed tiles are drawn face up: all of them once the hand is over, otherwise those of one seat (or of none).
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

        /// The element `name` of `canvas`; null when there is no canvas or no such element.
        UIElement* FindElementIn(const IntrusivePtr<UICanvasComponent>& canvas, PoolString name) {
            return canvas ? canvas->GetRoot().FindByName(name) : nullptr;
        }

        /// The key of the sentence of the shown button under the pointer (the canvas sets `UIElement::hovered` from the mouse); empty when none is.
        /// Only the buttons of `Actions` have a hint: a touch screen has no hover, and `ActionsCompact` has no label for it.
        eastl::string_view HoveredHintKey(const IntrusivePtr<UICanvasComponent>& actions, const ActionButtons& buttons) {
            for (int i = 0; i < kButtonCount; ++i) {
                const ButtonKind kind = static_cast<ButtonKind>(i);
                if (!buttons[kind].shown) {
                    continue;  // a hidden button may still carry the flag of the last motion
                }
                const UIElement* element = FindElementIn(actions, PoolString::Intern(SpecOf(kind).element));
                if (element != nullptr && element->hovered) {
                    return buttons[kind].hintKey;
                }
            }
            return {};
        }

        Faces FacesOf(const TableSession& session) {
            Faces faces;
            faces.all = session.GetPhase() != SessionPhase::Playing;
            faces.seat = session.HandoffPending() ? kNoSeat : session.Viewer();
            return faces;
        }

        /// The seat at the bottom: the viewer, else the person that waits for the cover to go, else the first person, else seat 0.
        Seat BottomSeat(const TableSession& session) {
            if (IsSeat(session.Viewer())) {
                return session.Viewer();
            }
            if (session.GetPhase() == SessionPhase::Playing && session.ActorIsPerson()) {
                return session.Actor();
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (session.KindOf(seat) == SeatKind::Person) {
                    return seat;
                }
            }
            return 0;
        }

        /// Whether the table is played now: it is not played while the seat choice (`Setup`) or the rules are open over it.
        bool TableIsLive(const MahjongGame& game) {
            return !game.IsTablePaused();
        }

        /// Whether a person is to act and may: the table is being played, the actor is a person and no cover is up.
        bool PersonActs(const MahjongGame& game) {
            const TableSession& session = game.GetSession();
            return TableIsLive(game) && session.GetPhase() == SessionPhase::Playing && session.ActorIsPerson() && !session.HandoffPending();
        }

        /// Whether a computer is to act and may: the table is being played and the actor is a computer.
        bool ComputerActs(const MahjongGame& game) {
            const TableSession& session = game.GetSession();
            return TableIsLive(game) && session.GetPhase() == SessionPhase::Playing && !session.ActorIsPerson();
        }

        /// A claim window: a discard or an upgraded kong waits for the other seats to decide.
        bool IsClaimWindow(Phase phase) noexcept {
            return phase == Phase::Claim || phase == Phase::RobKong;
        }

        /// The seat whose turn it is on the table: the one that discards, the discarder (or the kong's owner) while the others decide.
        /// Not the seat that has to act: that one is the first seat with a claim in a window, which would tell a person that this
        /// computer could claim.
        Seat TurnSeat(const TableSession& session, const HandModel& hand) {
            return session.GetPhase() == SessionPhase::Playing ? hand.CurrentSeat() : kNoSeat;
        }

        /// The tile at `part` of a meld as laid: a chow runs up from its first tile, a pung and a kong repeat it.
        Tile MeldTileAt(const Meld& meld, int part) {
            if (meld.kind == MeldKind::Chow && meld.first.IsSuit()) {
                return Tile::Suited(meld.first.Suit(), meld.first.Rank() + part);
            }
            return meld.first;
        }

        /// The index, in the concealed tiles of `seat`, of the tile it has just drawn (the last copy: `AddConcealed` sorts equal tiles
        /// together and the drawn one is the one that was added last); -1 when it drew none.
        int DrawnIndexOf(const HandModel& hand, Seat seat) {
            const Tile drawn = hand.DrawnTile(seat);
            if (!drawn.IsValid()) {
                return -1;
            }
            const eastl::vector<Tile>& concealed = hand.GetHand(seat).concealed;
            for (int i = static_cast<int>(concealed.size()) - 1; i >= 0; --i) {
                if (concealed[static_cast<size_t>(i)] == drawn) {
                    return i;
                }
            }
            ASSERT(false, "DrawnIndexOf: the drawn tile is not in the concealed tiles");
            return -1;
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

        TableInput MakeInput(const HandModel& hand, Seat viewer, bool drawnApart) {
            TableInput input;
            input.viewer = viewer;
            if (drawnApart && IsSeat(viewer)) {
                input.drawnIndex = DrawnIndexOf(hand, viewer);
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PlayerHand& held = hand.GetHand(seat);
                input.concealedCount[seat] = static_cast<int>(held.concealed.size());
                input.melds[seat] = held.melds;
                input.bonusCount[seat] = static_cast<int>(held.bonus.size());
            }
            for (const DiscardEntry& entry : hand.Discards()) {
                input.discardSeats.push_back(entry.seat);
            }
            input.wallRemaining = static_cast<int>(hand.GetWall().Remaining());
            return input;
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

    }  // namespace

    MahjongTableComponent* MahjongTableComponent::s_active = nullptr;

    MahjongTableComponent::~MahjongTableComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void MahjongTableComponent::OnAttached() {
        s_active = this;
        _atlas = CoreManager::GetResourceManager().Load<ITexture>(kAtlasPath);
        if (!_atlas) {
            LOG_WARNING(Format("MahjongTableComponent: the tile atlas '{}' did not load, the tiles are drawn as plain rectangles", kAtlasPath));
        }
        SubscribeInScene<SDLEvents::MouseButtonDownEvent, &MahjongTableComponent::OnMouseDown>(this);
        SubscribeInScene<InputEvents::ActionEvent, &MahjongTableComponent::OnAction>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &MahjongTableComponent::OnUpdate>(this);
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

    ActionButtons MahjongTableComponent::CurrentButtons(const MahjongGame& game) {
        ActionButtons buttons;
        const TableSession& session = game.GetSession();
        if (PersonActs(game)) {
            const HandModel& hand = game.GetMatch().Hand();
            // The tile of the window: the discard that waits for a claim, the tile of a kong that waits to be robbed (invalid in a turn)
            buttons = MakeActionButtons(hand.LegalActions(session.Actor()), hand.PendingTile());
        }
        // The flag belongs to the turn it was set on: gone when the button goes, and when the turn moved on without the table's discard
        if (!buttons[ButtonKind::Declare].shown || TurnOf(game) != _declareTurn) {
            _declareNext = false;
        }
        return buttons;
    }

    void MahjongTableComponent::PublishScreen(const MahjongGame* game) {
        const ActionButtons buttons = game != nullptr ? CurrentButtons(*game) : ActionButtons{};
        UIVars& vars = CoreManager::GetUIVars();
        Scene& scene = GetScene();

        // The canvas of the buttons: `Actions` while its buttons are touch targets, else `ActionsCompact` (the same buttons by name); the
        // button of the hints goes with them: `HintsButton` where `Actions` is, its copy in `ActionsCompact` where that is
        const IRenderer& renderer = *CoreManager::GetRenderer();
        const UIEnvironment& environment = CoreManager::GetUIEnvironment();
        const UILayoutContext window = environment.MakeContext(CoreManager::GetMainWindow().Get(), renderer.GetOutputWidth(), renderer.GetOutputHeight());
        const bool compact = NeedsCompactActions(window.outputWidth, window.outputHeight, window.pixelDensity, environment.GetSettings().minTouchSize, window.userScale);
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
            // The tiles pictured on the button: one `UIImage` per tile, hidden in the file, shown exactly while the button shows that tile
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
                        if (UIImage* image = element != nullptr ? element->Cast<UIImage>() : nullptr) {
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
        const eastl::string_view hintKey = compact ? eastl::string_view{} : HoveredHintKey(actionsCanvas, buttons);
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

    void MahjongTableComponent::OnMouseDown(const SDLEvents::MouseButtonDownEvent& event) {
        if (event.button.button != SDL_BUTTON_LEFT) {
            return;
        }
        MahjongGame* game = PersonToAct();
        if (game == nullptr) {
            return;
        }
        const std::optional<int> hit = HitHandTile(_layout, GetScene().ScreenToSceneX(event.button.x), GetScene().ScreenToSceneY(event.button.y));
        if (!hit) {
            return;
        }
        TableSession& session = game->GetSession();
        const Seat actor = session.Actor();
        const PlayerHand& held = game->GetMatch().Hand().GetHand(actor);
        // The layout is the last frame's: it has to be of this hand
        if (_layout.viewer != actor || *hit < 0 || *hit >= static_cast<int>(held.concealed.size())) {
            return;
        }
        if (session.PerformPerson(HandAction::MakeDiscard(held.concealed[*hit], _declareNext)).has_value()) {
            _declareNext = false;
        }  // else the click changes nothing: a tile that cannot declare while the flag is on, or the model moved on since the draw
    }

    void MahjongTableComponent::OnAction(const InputEvents::ActionEvent& event) {
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
        if (const HandStatus performed = session.PerformPerson(buttons[*kind].action); !performed.has_value()) {
            LOG_WARNING(Format("MahjongTableComponent: the button '{}' was refused: {}", SpecOf(*kind).element, EnumUtils<HandError>::ToString(performed.error())));
        }
    }

    bool MahjongTableComponent::StepComputerChecked(TableSession& session) {
        if (const StepResult stepped = session.StepComputer(); !stepped.has_value()) {
            LOG_ERROR("MahjongTableComponent: a computer seat's action was refused");
            return false;
        }
        return true;
    }

    void MahjongTableComponent::FollowMoves(const MahjongGame& game) {
        const Match& match = game.GetMatch();
        const eastl::vector<HandMove>& moves = match.Hand().Moves();
        const SeenHand now{&game, match.HandsPlayed(), match.History().size()};
        if (now != _seenHand || moves.size() < _movesSeen) {
            // Another hand, match or game: what is on the table now is not news
            _seenHand = now;
            _movesSeen = moves.size();
            _highlightLeft = 0.0f;
            return;
        }
        for (size_t i = moves.size(); i > _movesSeen; --i) {
            if (moves[i - 1].meld >= 0) {
                _highlightSeat = moves[i - 1].seat;
                _highlightMeld = moves[i - 1].meld;
                _highlightLeft = kClaimHighlightSeconds;
                break;
            }
        }
        _movesSeen = moves.size();
    }

    void MahjongTableComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        if (_highlightLeft > 0.0f && TableIsLive(*game)) {
            _highlightLeft = eastl::max(0.0f, _highlightLeft - event.deltaTime);
        }
        TableSession& session = game->GetSession();
        if (!ComputerActs(*game)) {
            _computerClock = 0.0f;
            return;
        }
        _computerClock += event.deltaTime;
        if (_computerClock < kComputerStepSeconds) {
            return;
        }
        _computerClock = 0.0f;  // one action a period, whatever the frame took
        const HandModel& hand = game->GetMatch().Hand();
        const bool window = IsClaimWindow(hand.GetPhase());
        if (!StepComputerChecked(session)) {
            return;
        }
        if (!window) {
            return;
        }
        // The pause after a discard is one whether or not a window opened (a longer one would tell a person that a computer holds a
        // claim): the computers settle a window within the pause, and a computer that is to discard after it does so in the same pause.
        while (ComputerActs(*game) && IsClaimWindow(hand.GetPhase())) {
            if (!StepComputerChecked(session)) {
                return;
            }
        }
        if (ComputerActs(*game) && !StepComputerChecked(session)) {
            return;
        }
    }

    void MahjongTableComponent::SetAtlasTexture(IntrusivePtr<ITexture> texture) {
        _atlas = ResourceHandle<ITexture>(std::move(texture));
    }

    void MahjongTableComponent::OnDraw() {
        IRenderer& renderer = *CoreManager::GetRenderer();
        const float viewWidth = GetScene().GetViewWidth() > 0.0f ? GetScene().GetViewWidth() : kViewWidth;
        const float viewHeight = GetScene().GetViewHeight() > 0.0f ? GetScene().GetViewHeight() : kViewHeight;
        renderer.DrawFilledRect(0.0f, 0.0f, viewWidth, viewHeight, kFelt);
        // The layout is built in design units and scaled to the view, so what is measured in units here is scaled the same way
        const float unit = eastl::max(0.0f, eastl::min(viewWidth / kViewWidth, viewHeight / kViewHeight));
        const float rim = kRimWidth * unit;
        renderer.DrawFilledRect(0.0f, 0.0f, viewWidth, rim, kFeltRim);
        renderer.DrawFilledRect(0.0f, viewHeight - rim, viewWidth, rim, kFeltRim);
        renderer.DrawFilledRect(0.0f, rim, rim, viewHeight - 2.0f * rim, kFeltRim);
        renderer.DrawFilledRect(viewWidth - rim, rim, rim, viewHeight - 2.0f * rim, kFeltRim);

        _layout = TableLayout{};
        const MahjongGame* game = MahjongGame::GetActive();
        PublishScreen(game);
        if (game == nullptr || game->GetMatch().GetPhase() == MatchPhase::NotStarted) {
            return;
        }
        const TableSession& session = game->GetSession();
        const HandModel& hand = game->GetMatch().Hand();
        FollowMoves(*game);
        const Faces faces = FacesOf(session);
        // The drawn tile lies apart only in a hand that is face up: under the cover and for a spectator the hand is a plain row
        const Seat bottom = BottomSeat(session);
        const bool drawnApart = session.GetPhase() == SessionPhase::Playing && faces.Shows(bottom);
        _layout = MakeTableLayout(MakeInput(hand, bottom, drawnApart), viewWidth, viewHeight);

        // A face (the tile) or the back (nullopt) at `rect`: from the atlas, else a plain rectangle.
        const auto drawFace = [&](const Rect& rect, std::optional<Tile> face) {
            if (_atlas) {
                const Rect source = face ? SourceRect(*face) : BackRect();
                renderer.DrawTexture(*_atlas, &source, rect.x, rect.y, rect.w, rect.h);
                return;
            }
            renderer.DrawFilledRect(rect.x, rect.y, rect.w, rect.h, face ? kPlainFace : kPlainBack);
            if (face) {
                const float insetX = rect.w * kPlainMarkInset;
                const float insetY = rect.h * kPlainMarkInset;
                renderer.DrawFilledRect(rect.x + insetX, rect.y + insetY, rect.w - 2.0f * insetX, rect.h - 2.0f * insetY, PlainMarkColor(*face));
            }
        };
        // The shadow under a tile (not under a wall stack); the frame of a pending discard lies over it, so the gold is even
        const auto drawShadow = [&](const Rect& rect) { renderer.DrawFilledRect(rect.x + kShadowOffset * unit, rect.y + kShadowOffset * unit, rect.w, rect.h, kTileShadow); };
        const auto drawTile = [&](const Rect& rect, std::optional<Tile> face) {
            drawShadow(rect);
            drawFace(rect, face);
        };

        // The plates under the discard blocks and the wall ring
        const float plate = kPlatePadding * unit;
        for (const Rect& zone : _layout.zones.discards) {
            const Rect plated = Padded(zone, plate);
            renderer.DrawFilledRect(plated.x, plated.y, plated.w, plated.h, kZonePlate);
        }
        const Rect ring = Padded(_layout.zones.wall, plate);
        renderer.DrawFilledRect(ring.x, ring.y, ring.w, ring.h, kZonePlate);

        if (const Seat turn = TurnSeat(session, hand); IsSeat(turn)) {
            const Rect bounds = BoundsOf(_layout, SlotRole::HandTile, turn);
            if (bounds.w > 0.0f) {
                const Rect marked = Padded(bounds, kActingMargin * unit);
                renderer.DrawFilledRect(marked.x, marked.y, marked.w, marked.h, kTurnMark);
            }
        }
        // The set that was just laid blinks behind its tiles: on at the moment of the move, then half a period on and half off
        if (_highlightLeft > 0.0f) {
            const float elapsed = kClaimHighlightSeconds - _highlightLeft;
            const Rect bounds = BoundsOfMeld(_layout, _highlightSeat, _highlightMeld);
            if (bounds.w > 0.0f && std::fmod(elapsed, kClaimBlinkSeconds) < kClaimBlinkSeconds * 0.5f) {
                const Rect marked = Padded(bounds, kActingMargin * unit);
                renderer.DrawFilledRect(marked.x, marked.y, marked.w, marked.h, kClaimHighlight);
            }
        }
        // The tiles that cannot declare the calling hand when they are discarded, while the next discard is to declare it
        eastl::array<bool, kTileIdCount> cannotDeclare{};
        if (_declareNext) {
            cannotDeclare.fill(true);
            for (const HandAction& action : hand.LegalActions(session.Actor())) {
                if (action.kind == ActionKind::Discard && action.declareCalling) {
                    cannotDeclare[action.tile.Id()] = false;
                }
            }
        }
        // The frame on the discard that waits tells a person that a claim is open for them: only the viewer who is asked sees it (a window
        // that only computers may claim in shows nothing, and nothing shows while the screen is covered)
        bool claimPending = false;
        if (session.GetPhase() == SessionPhase::Playing && hand.GetPhase() == Phase::Claim && !hand.Discards().empty() && IsSeat(faces.seat)) {
            const eastl::vector<Seat> asked = hand.PendingClaimSeats();
            claimPending = eastl::find(asked.begin(), asked.end(), faces.seat) != asked.end();
        }
        const int pendingEntry = static_cast<int>(hand.Discards().size()) - 1;
        const int lastDiscard = LastDiscardEntry(hand);

        for (const TileSlot& slot : _layout.slots) {
            const PlayerHand& held = hand.GetHand(slot.seat);
            switch (slot.role) {
                case SlotRole::HandTile:
                    drawTile(slot.rect, faces.Shows(slot.seat) ? std::optional<Tile>(held.concealed[slot.index]) : std::nullopt);
                    if (_declareNext && slot.seat == session.Actor() && cannotDeclare[held.concealed[slot.index].Id()]) {
                        renderer.DrawFilledRect(slot.rect.x, slot.rect.y, slot.rect.w, slot.rect.h, kVeil);
                    }
                    break;
                case SlotRole::MeldTile: {
                    const Meld& meld = held.melds[slot.index];
                    // An open set is public; a concealed kong is as hidden as the hand it was made from.
                    const bool shown = !meld.concealed || faces.Shows(slot.seat);
                    drawTile(slot.rect, shown ? std::optional<Tile>(MeldTileAt(meld, slot.part)) : std::nullopt);
                    if (slot.claimEdge != Edge::None) {
                        const Rect bar = EdgeBar(slot.rect, slot.claimEdge, kClaimedEdgeWidth * unit);
                        renderer.DrawFilledRect(bar.x, bar.y, bar.w, bar.h, kClaimedEdge);
                    }
                    break;
                }
                case SlotRole::BonusTile:
                    drawTile(slot.rect, held.bonus[slot.index]);
                    break;
                case SlotRole::DiscardTile:
                    if (claimPending && slot.index == pendingEntry) {
                        drawShadow(slot.rect);
                        const Rect framed = Padded(slot.rect, kPendingMargin * unit);
                        renderer.DrawFilledRect(framed.x, framed.y, framed.w, framed.h, kPendingFrame);
                        drawFace(slot.rect, hand.Discards()[slot.index].tile);
                    } else {
                        drawTile(slot.rect, hand.Discards()[slot.index].tile);
                    }
                    if (slot.index == lastDiscard) {
                        renderer.DrawFilledRect(slot.rect.x, slot.rect.y, slot.rect.w, slot.rect.h, kLastDiscardMark);
                    }
                    break;
                case SlotRole::WallStack:
                    drawFace(slot.rect, std::nullopt);
                    break;
            }
        }
    }

}  // namespace BECore::Mahjong
