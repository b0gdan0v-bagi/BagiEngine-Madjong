#include "MahjongTableComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImage.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/MahjongTableComponent.gen.hpp>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/SeatName.h>
#include <Mahjong/Table/TileAtlas.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kRevealAction = "MahjongReveal";
        constexpr Color kFelt{22, 92, 62, 255};
        constexpr Color kActingSeat{255, 214, 80, 90};
        constexpr Color kPendingDiscard{255, 214, 80, 200};
        /// Over a tile whose declaring discard is not legal while the next discard declares the calling hand.
        constexpr Color kDimmedTile{0, 0, 0, 140};
        constexpr Color kPlainFace{238, 230, 205, 255};
        constexpr Color kPlainBack{36, 120, 118, 255};
        constexpr Color kPlainSuit{70, 110, 200, 255};
        constexpr Color kPlainHonour{200, 60, 50, 255};
        constexpr Color kPlainBonus{60, 160, 90, 255};
        /// The highlight of the acting seat reaches this far past the hand; the pending discard's frame this far past the tile.
        constexpr float kActingMargin = 4.0f;
        constexpr float kPendingMargin = 3.0f;
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

        /// The element `name` of the canvas on the node `Actions`; null when the scene has none.
        UIElement* FindActionElement(Scene& scene, PoolString name) {
            for (const auto& node : scene.GetRootNode().GetChildren()) {
                if (node->GetName() == "Actions"_intern) {
                    const auto canvas = node->GetComponent<UICanvasComponent>();
                    return canvas ? canvas->GetRoot().FindByName(name) : nullptr;
                }
            }
            return nullptr;
        }

        /// The sentence of the shown button under the pointer (the canvas sets `UIElement::hovered` from the mouse); empty when none is.
        eastl::string_view HoveredHint(Scene& scene, const ActionButtons& buttons) {
            for (int i = 0; i < kButtonCount; ++i) {
                const ButtonKind kind = static_cast<ButtonKind>(i);
                if (!buttons[kind].shown) {
                    continue;  // a hidden button may still carry the flag of the last motion
                }
                const UIElement* element = FindActionElement(scene, PoolString::Intern(SpecOf(kind).element));
                if (element != nullptr && element->hovered) {
                    return buttons[kind].hint;
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

        /// Whether the table is played now: it is not played while the seat choice (`Setup`) is open over it.
        bool TableIsLive(const MahjongGame& game) {
            return !game.IsSetupOpen();
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

        TableInput MakeInput(const HandModel& hand, Seat viewer) {
            TableInput input;
            input.viewer = viewer;
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
                    UIElement* element = FindActionElement(scene, imageName);
                    if (UIImage* image = element != nullptr ? element->Cast<UIImage>() : nullptr) {
                        image->sprite = PoolString::Intern(SpriteName(now.tiles[static_cast<size_t>(t)]));
                    }
                }
                if (showNow != showBefore) {
                    UICanvasComponent::SetElementVisibleByName(scene, imageName, showNow);
                }
            }
        }
        _published = buttons;
        vars.Set("declareNext"_intern, _declareNext ? kDeclareOn : kDeclareOff);

        // The hint of the button under the pointer, in the label `Hint` that shows exactly while there is one
        const eastl::string_view hint = HoveredHint(scene, buttons);
        const PoolString actionHint = "actionHint"_intern;
        if (hint != _hint || !vars.Has(actionHint)) {
            _hint = hint;
            vars.Set(actionHint, hint);
            UICanvasComponent::SetElementVisibleByName(scene, "Hint"_intern, !hint.empty());
        }

        // The cover of a hot seat: a modal canvas, shown exactly while the screen waits for the next person
        const bool handoff = game != nullptr && game->GetSession().HandoffPending();
        if (handoff) {
            const Seat actor = game->GetSession().Actor();
            vars.Set("handoffSeat"_intern, eastl::string_view(SeatName(CoreManager::GetUIStrings(), actor, game->GetMatch().Hand().SeatWind(actor))));
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

    void MahjongTableComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
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

        _layout = TableLayout{};
        const MahjongGame* game = MahjongGame::GetActive();
        PublishScreen(game);
        if (game == nullptr || game->GetMatch().GetPhase() == MatchPhase::NotStarted) {
            return;
        }
        const TableSession& session = game->GetSession();
        const HandModel& hand = game->GetMatch().Hand();
        const Faces faces = FacesOf(session);
        _layout = MakeTableLayout(MakeInput(hand, BottomSeat(session)), viewWidth, viewHeight);

        // A face (the tile) or the back (nullopt) at `rect`: from the atlas, else a plain rectangle.
        const auto drawTile = [&](const Rect& rect, std::optional<Tile> face) {
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

        if (const Seat turn = TurnSeat(session, hand); IsSeat(turn)) {
            const Rect bounds = BoundsOf(_layout, SlotRole::HandTile, turn);
            if (bounds.w > 0.0f) {
                const Rect marked = Padded(bounds, kActingMargin);
                renderer.DrawFilledRect(marked.x, marked.y, marked.w, marked.h, kActingSeat);
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

        for (const TileSlot& slot : _layout.slots) {
            const PlayerHand& held = hand.GetHand(slot.seat);
            switch (slot.role) {
                case SlotRole::HandTile:
                    drawTile(slot.rect, faces.Shows(slot.seat) ? std::optional<Tile>(held.concealed[slot.index]) : std::nullopt);
                    if (_declareNext && slot.seat == session.Actor() && cannotDeclare[held.concealed[slot.index].Id()]) {
                        renderer.DrawFilledRect(slot.rect.x, slot.rect.y, slot.rect.w, slot.rect.h, kDimmedTile);
                    }
                    break;
                case SlotRole::MeldTile: {
                    const Meld& meld = held.melds[slot.index];
                    // An open set is public; a concealed kong is as hidden as the hand it was made from.
                    const bool shown = !meld.concealed || faces.Shows(slot.seat);
                    drawTile(slot.rect, shown ? std::optional<Tile>(MeldTileAt(meld, slot.part)) : std::nullopt);
                    break;
                }
                case SlotRole::BonusTile:
                    drawTile(slot.rect, held.bonus[slot.index]);
                    break;
                case SlotRole::DiscardTile:
                    if (claimPending && slot.index == pendingEntry) {
                        const Rect framed = Padded(slot.rect, kPendingMargin);
                        renderer.DrawFilledRect(framed.x, framed.y, framed.w, framed.h, kPendingDiscard);
                    }
                    drawTile(slot.rect, hand.Discards()[slot.index].tile);
                    break;
                case SlotRole::WallStack:
                    drawTile(slot.rect, std::nullopt);
                    break;
            }
        }
    }

}  // namespace BECore::Mahjong
