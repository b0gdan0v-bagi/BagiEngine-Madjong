#include "MahjongHintsComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImageComponent.h>
#include <BECore/UI/UITextComponent.h>
#include <Events/InputEvents.h>
#include <Generated/MahjongHintsComponent.gen.hpp>
#include <Mahjong/Components/ColorTokens.h>
#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/Components/WindowPointerShape.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/TileAtlas.h>
#include <Mahjong/Table/UiPalette.h>

namespace BECore::Mahjong {

    namespace {

        constexpr StringView kSetHintsAction = "MahjongSetHints";
        constexpr StringView kAdviceAction = "MahjongHintAdvice";
        // Names of the canvas of the node `Hints` (`WaitCell<i>` holds `WaitTile<i>` and `WaitCount<i>`) and of the variables the labels read
        constexpr StringView kWaitCellPrefix = "WaitCell";
        constexpr StringView kWaitTilePrefix = "WaitTile";
        constexpr StringView kHintCountPrefix = "hintCount";
        constexpr StringView kWaitPanel = "WaitPanel";
        constexpr StringView kHintButton = "HintButton";
        constexpr StringView kAdviceLabel = "AdviceText";
        constexpr StringView kAdviceVar = "hintAdvice";
        constexpr StringView kRoomHintsLabel = "RoomHintsText";
        constexpr StringView kRoomHintsVar = "roomHintsText";
        // The card of the coach (node `HintsWhy`, `Table/HintCard.h`): row `i` is `CardRow<i>` with the label `CardSteps<i>` and the pictures `Card<i>Tile<k>`
        constexpr StringView kCoachCard = "CoachCard";
        constexpr StringView kCardHead = "CardHead";
        constexpr StringView kCardRowPrefix = "CardRow";
        constexpr StringView kCardStepsPrefix = "CardSteps";
        constexpr StringView kCardTitleVar = "cardTitle";
        constexpr StringView kCardNowVar = "cardNow";
        constexpr StringView kCardWhoVar = "cardWho";
        constexpr StringView kCardMoveVar = "cardMove";
        constexpr StringView kCardStepsVar = "cardSteps";
        constexpr StringView kCardUsefulVar = "cardUseful";
        constexpr StringView kCardFootVar = "cardFoot";

        constexpr float kMarkGap = 1.0f;         ///< between the tile and its bar
        constexpr float kMarkThickness = 4.0f;   ///< the bar
        constexpr float kFrameThickness = 3.0f;  ///< drawn inside the edge of the tile

        PoolString IndexedName(StringView prefix, int index) {
            return PoolString::Intern(Format("{}{}", prefix, index));
        }

        PoolString NameOf(StringView name) {
            return PoolString::Intern(name);
        }

        PoolString CardTileName(int row, int tile) {
            return PoolString::Intern(Format("Card{}Tile{}", row, tile));
        }

        /// The canvas under `node` (or a child of it) that holds the element `name`.
        UICanvasComponent* CanvasWith(SceneNode& node, PoolString name) {
            if (auto canvas = node.GetComponent<UICanvasComponent>()) {
                if (canvas->GetRoot().FindByName(name) != nullptr) {
                    return canvas.Get();
                }
            }
            for (const auto& child : node.GetChildren()) {
                if (UICanvasComponent* found = CanvasWith(*child, name)) {
                    return found;
                }
            }
            return nullptr;
        }

        /// The fill of a row of the card: the computer's choice raised, the person's own move in the colour of a pass, the others none.
        void SetRowFill(UIElement& row, CardRowKind kind) {
            switch (kind) {
                case CardRowKind::Best:
                    SetColorToken(row, row.color, "surfaceRaised"_intern);
                    return;
                case CardRowKind::Yours:
                    SetColorToken(row, row.color, "danger"_intern);
                    return;
                case CardRowKind::Other:
                    break;
            }
            SetPlainColor(row.color, Color{0, 0, 0, 0});
        }

        /// The colour of the steps of a row: the marks of the table when the move gets closer or farther, the text of the theme otherwise.
        void SetTrendColor(UIElement& steps, UITextComponent& label, CardTrend trend) {
            switch (trend) {
                case CardTrend::Closer:
                    SetPlainColor(label.textColor, kSafeMark);
                    return;
                case CardTrend::Farther:
                    SetPlainColor(label.textColor, kRiskyMark);
                    return;
                case CardTrend::Same:
                case CardTrend::None:
                    break;
            }
            SetColorToken(steps, label.textColor, "text"_intern);
        }

        /// The button `Hint` is offered: a person acts and it is the viewer, with something to choose from (a non-empty `legal`). In a room the
        /// display session names the viewer as the actor exactly while the server lists an action, and `legal` is that list.
        bool IsOffered(const TableSession& session, Seat viewer, const TableView& view) {
            return session.ActorIsPerson() && session.Actor() == viewer && !view.legal.empty();
        }

        void SetSelectedIn(SceneNode& node, PoolString name, bool selected) {
            if (auto canvas = node.GetComponent<UICanvasComponent>()) {
                if (UIElement* element = canvas->GetRoot().FindByName(name)) {
                    element->selected = selected;
                }
            }
            for (const auto& child : node.GetChildren()) {
                SetSelectedIn(*child, name, selected);
            }
        }

    }  // namespace

    MahjongHintsComponent* MahjongHintsComponent::s_active = nullptr;

    TableView MahjongHintsComponent::ViewOf(const MahjongGame& game, Seat viewer) {
        TableView view = MakeTableView(game.GetMatch().Hand(), viewer);
        if (game.IsOnlineTable()) {
            const OnlineClient* client = game.FindOnline();
            if (client != nullptr && client->Stage() == OnlineStage::Game) {
                ListHandActions(client->Table(), view.legal);
            }
        }
        return view;
    }

    MahjongHintsComponent::~MahjongHintsComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void MahjongHintsComponent::OnTargetAttached() {
        s_active = this;
        SubscribeInScene<InputEvents::ActionEvent, &MahjongHintsComponent::OnAction>(this);
    }

    MahjongHintsComponent::Key MahjongHintsComponent::KeyOf(const MahjongGame* game) {
        Key key;
        // In a room the host's decision comes first (`HintsActive`), so a view that changes it moves `applies`
        key.applies = game != nullptr && HintsApply(game->HintsActive(), game->IsTablePaused(), game->GetSession());
        if (key.applies) {
            key.viewer = game->GetSession().Viewer();
            key.version = game->GetSession().Version();
        }
        return key;
    }

    Optional<TableView> MahjongHintsComponent::ViewToAdvise(const MahjongGame* game) {
        if (!KeyOf(game).applies) {
            return std::nullopt;
        }
        const TableSession& session = game->GetSession();
        TableView view = ViewOf(*game, session.Viewer());
        if (!IsOffered(session, session.Viewer(), view)) {
            return std::nullopt;
        }
        return view;
    }

    void MahjongHintsComponent::OnTargetDraw() {
        BE_PROFILE_SCOPE("MahjongHints::OnDraw");
        SetShape(WholeWindowShape(GetScene(), *CoreManager::GetRenderer()));
        MahjongGame* game = MahjongGame::GetActive();
        if (game != nullptr && game->IsOptionsOpen()) {
            PublishChoice(*game);
        }
        const Key key = KeyOf(game);
        if (!_known || key != _key) {
            Refresh(game, key);
        }
        PushWaitingRow();
        if (_advised && (key != _adviceKey || !_offered || !_adviceOn)) {
            DropAdvice();
        }
        if (_adviceOn && _offered && !_advised) {
            Advise(*game, key);
        }
        const bool coach = game != nullptr && game->CoachActive();
        PushCoach(game, key);  // first: on the card the advice gives way to a note that is up
        PushAdvice(coach);
        PushCard(game, coach);
        PushRoomHints(game);  // last: it gives way to the sentence and to the card
        if (key.applies) {
            DrawMarks(*game);
        }
    }

    void MahjongHintsComponent::Refresh(const MahjongGame* game, const Key& key) {
        BE_PROFILE_SCOPE("MahjongHints::Refresh");
        _known = true;
        _key = key;
        _row.clear();
        _declaring.fill(false);
        _dangers.fill(DangerMark::None);
        _offered = false;
        if (!key.applies) {
            return;
        }
        const TableView view = ViewOf(*game, key.viewer);
        _row = WaitingTiles(view);
        const TableSession& session = game->GetSession();
        _offered = IsOffered(session, key.viewer, view);
        if (_offered) {
            _declaring = DeclaringDiscards(view);
            _dangers = DangerMarks(view);
        }
    }

    void MahjongHintsComponent::PushWaitingRow() {
        const int now = static_cast<int>(_row.size());
        const int before = static_cast<int>(_published.size());
        UIVars& vars = CoreManager::GetUIVars();
        Scene& scene = GetScene();
        for (int i = 0; i < kMaxWaitingTiles; ++i) {
            const bool showNow = i < now;
            const bool showBefore = i < before;
            if (showNow && (!showBefore || _row[i].tile != _published[i].tile)) {
                const auto canvas = GetNode().GetComponent<UICanvasComponent>();
                UIElement* element = canvas ? canvas->GetRoot().FindByName(IndexedName(kWaitTilePrefix, i)) : nullptr;
                if (UIImageComponent* image = element != nullptr ? element->GetComponent<UIImageComponent>() : nullptr) {
                    image->sprite = PoolString::Intern(SpriteName(_row[i].tile));
                }
            }
            if (showNow && (!showBefore || _row[i].unseen != _published[i].unseen)) {
                vars.Set(IndexedName(kHintCountPrefix, i), _row[i].unseen);
            }
            if (showNow != showBefore) {
                UICanvasComponent::SetElementVisibleByName(scene, IndexedName(kWaitCellPrefix, i), showNow);
            }
        }
        if ((now > 0) != (before > 0)) {
            UICanvasComponent::SetElementVisibleByName(scene, NameOf(kWaitPanel), now > 0);
        }
        _published = _row;
    }

    void MahjongHintsComponent::Advise(const MahjongGame& game, const Key& key) {
        BE_PROFILE_SCOPE("MahjongHints::Advise");
        TableView view = ViewOf(game, key.viewer);
        _explained = ExplainAdviceFor(view);
        _advice = _explained ? Optional<HandAction>(_explained->Action()) : AdviceFor(view);
        _advised = true;
        _adviceKey = key;
        _adviceView = std::move(view);
        _adviceDirty = true;
        ++_adviceGeneration;
    }

    void MahjongHintsComponent::DropAdvice() {
        _advised = false;
        _advice.reset();
        _explained.reset();
        _adviceView = TableView{};
        _adviceDirty = false;
    }

    void MahjongHintsComponent::PushAdvice(bool coach) {
        Scene& scene = GetScene();
        if (_offered != _offeredShown) {
            _offeredShown = _offered;
            UICanvasComponent::SetElementVisibleByName(scene, NameOf(kHintButton), _offered);
        }
        if (_adviceOn != _adviceOnShown) {
            _adviceOnShown = _adviceOn;
            SetSelectedIn(scene.GetRootNode(), NameOf(kHintButton), _adviceOn);
        }
        // In the mode Coach the sentence is the title of the card, not a label of its own
        const bool show = _advice.has_value() && !coach;
        UIVars& vars = CoreManager::GetUIVars();
        const UIStrings& strings = CoreManager::GetUIStrings();
        if (show && (!_adviceShown || _adviceDirty || strings.GetVersion() != _adviceStringsVersion)) {
            _adviceDirty = false;
            _adviceStringsVersion = strings.GetVersion();
            vars.Set(NameOf(kAdviceVar), StringView(AdviceText(strings, _adviceView, *_advice)));
        }
        if (show != _adviceShown) {
            _adviceShown = show;
            if (!show) {
                vars.Set(NameOf(kAdviceVar), StringView());
            }
            UICanvasComponent::SetElementVisibleByName(scene, NameOf(kAdviceLabel), show);
        }
    }

    void MahjongHintsComponent::PushRoomHints(const MahjongGame* game) {
        RoomHintsWord word = RoomHintsWord::None;
        if (game != nullptr && game->IsOnlineTable() && !game->IsTablePaused() && game->GetSession().GetPhase() == SessionPhase::Playing && !_adviceShown && _cardKey.source == CardSource::None) {
            if (!game->FindOnline()->Table().hintsAllowed) {
                word = RoomHintsWord::NotAllowed;
            } else {
                word = game->GetHintsMode() == HintsMode::Off ? RoomHintsWord::AllowedOff : RoomHintsWord::Allowed;
            }
        }
        const UIStrings& strings = CoreManager::GetUIStrings();
        if (word == _roomWord && (word == RoomHintsWord::None || strings.GetVersion() == _roomStringsVersion)) {
            return;
        }
        UIVars& vars = CoreManager::GetUIVars();
        switch (word) {
            case RoomHintsWord::Allowed:
                vars.Set(NameOf(kRoomHintsVar), StringView(Tr(strings, "online.table.hintsAllowed")));
                break;
            case RoomHintsWord::AllowedOff:
                vars.Set(NameOf(kRoomHintsVar), StringView(Tr(strings, "online.table.hintsAllowedOff")));
                break;
            case RoomHintsWord::NotAllowed:
                vars.Set(NameOf(kRoomHintsVar), StringView(Tr(strings, "online.table.hintsOff")));
                break;
            case RoomHintsWord::None:
                vars.Set(NameOf(kRoomHintsVar), StringView());
                break;
        }
        _roomStringsVersion = strings.GetVersion();
        if ((word == RoomHintsWord::None) != (_roomWord == RoomHintsWord::None)) {
            UICanvasComponent::SetElementVisibleByName(GetScene(), NameOf(kRoomHintsLabel), word != RoomHintsWord::None);
        }
        _roomWord = word;
    }

    void MahjongHintsComponent::PushCoach(MahjongGame* game, const Key& key) {
        CoachLedger* ledger = game != nullptr ? &game->GetCoach() : nullptr;
        bool show = false;
        if (ledger != nullptr && ledger->Note().has_value()) {
            const CoachHandKey hand = HandKeyOf(game->CoachContext());
            if (_coachOf != game || ledger->Version() != _coachSeen) {
                _coachOf = game;
                _coachSeen = ledger->Version();  // a note not seen yet belongs to the hand it is first seen in
                _coachHand = hand;
            }
            show = game->CoachActive() && key.applies && hand == _coachHand && ledger->Note()->seat == key.viewer;
            if (!show) {
                ledger->ClearNote();
            }
        }
        _coachShown = show;
        if (show) {
            _coachPublished = ledger->Version();  // the note a press may take down: one this very press made was never shown
        }
    }

    void MahjongHintsComponent::PushCard(MahjongGame* game, bool coach) {
        CardKey next;
        if (_coachShown) {
            next.source = CardSource::Note;
            next.version = game->GetCoach().Version();
        } else if (coach && _advice.has_value() && _explained.has_value()) {
            next.source = CardSource::Advice;
            next.version = _adviceGeneration;
        }
        if (next.source != CardSource::None) {
            const MoveContext context = game->CoachContext();
            next.strings = CoreManager::GetUIStrings().GetVersion();
            next.hand = game->GetCoach().HandTallyOf(HandKeyOf(context));
            next.match = game->GetCoach().MatchTallyOf(context.startCount);
        }
        if (next == _cardKey) {
            return;
        }
        BE_PROFILE_SCOPE("MahjongHints::PushCard");
        _cardKey = next;
        HintCard card;
        if (next.source != CardSource::None) {
            const UIStrings& strings = CoreManager::GetUIStrings();
            const String tally = CardTallyText(strings, next.hand, next.match);
            card = next.source == CardSource::Note ? MakeNoteCard(strings, *game->GetCoach().Note(), tally) : MakeAdviceCard(strings, _adviceView, *_explained, tally);
        }
        PublishCard(card, next.source != CardSource::None);
    }

    void MahjongHintsComponent::PublishCard(const HintCard& card, bool open) {
        UIVars& vars = CoreManager::GetUIVars();
        // Every variable is set, empty where the card has nothing, so nothing of the last card stays once it is down
        vars.Set(NameOf(kCardTitleVar), StringView(card.title));
        vars.Set(NameOf(kCardNowVar), StringView(card.now));
        UICanvasComponent* canvas = CanvasWith(GetScene().GetRootNode(), NameOf(kCoachCard));
        if (canvas == nullptr) {
            return;
        }
        UIElement& root = canvas->GetRoot();
        for (size_t i = 0; i < kMaxCardRows; ++i) {
            const int at = static_cast<int>(i);
            const CardRow* row = i < card.rows.size() ? &card.rows[i] : nullptr;
            vars.Set(IndexedName(kCardWhoVar, at), row != nullptr ? StringView(row->who) : StringView());
            vars.Set(IndexedName(kCardMoveVar, at), row != nullptr ? StringView(row->move) : StringView());
            vars.Set(IndexedName(kCardStepsVar, at), row != nullptr ? StringView(row->steps) : StringView());
            vars.Set(IndexedName(kCardUsefulVar, at), row != nullptr ? StringView(row->useful) : StringView());
            const PoolString rowName = IndexedName(kCardRowPrefix, at);
            if (UIElement* element = root.FindByName(rowName)) {
                SetRowFill(*element, row != nullptr ? row->kind : CardRowKind::Other);
            }
            if (UIElement* steps = root.FindByName(IndexedName(kCardStepsPrefix, at)); UITextComponent* label = steps != nullptr ? steps->GetComponent<UITextComponent>() : nullptr) {
                SetTrendColor(*steps, *label, row != nullptr ? row->trend : CardTrend::None);
            }
            for (size_t k = 0; k < kMaxCardTiles; ++k) {
                const PoolString name = CardTileName(at, static_cast<int>(k));
                const bool shown = row != nullptr && row->tiles[k].IsValid();
                if (UIElement* element = root.FindByName(name); UIImageComponent* image = element != nullptr ? element->GetComponent<UIImageComponent>() : nullptr) {
                    image->sprite = shown ? PoolString::Intern(SpriteName(row->tiles[k])) : PoolString();
                }
                canvas->SetElementVisible(name, shown);
            }
            canvas->SetElementVisible(rowName, row != nullptr);
        }
        for (size_t i = 0; i < kMaxCardFooter; ++i) {
            vars.Set(IndexedName(kCardFootVar, static_cast<int>(i)), i < card.footer.size() ? StringView(card.footer[i]) : StringView());
        }
        canvas->SetElementVisible(NameOf(kCardHead), !card.rows.empty());
        canvas->SetElementVisible(NameOf(kCoachCard), open);
    }

    bool MahjongHintsComponent::OnPointerPress(const PointerEvent& event) {
        MahjongGame* game = MahjongGame::GetActive();
        if (event.button != SDL_BUTTON_LEFT || game == nullptr) {
            return false;
        }
        // This component is asked before the table: a note shown now is cleared before the discard of this very press makes the next one
        if (_coachShown && game->GetCoach().Version() == _coachPublished) {
            game->GetCoach().ClearNote();
            return false;
        }
        // A press on the card of the advice takes it down, as the button `Hint` does; the card is no control (a `SceneView` canvas is under the
        // touch size), so the press is read here
        if (_cardKey.source == CardSource::Advice && _adviceOn) {
            Scene& scene = GetScene();
            UICanvasComponent* canvas = CanvasWith(scene.GetRootNode(), NameOf(kCoachCard));
            const UIElement* card = canvas != nullptr ? canvas->GetRoot().FindByName(NameOf(kCoachCard)) : nullptr;
            // A hidden card has an empty rect, which no point is inside
            if (card != nullptr) {
                const Rect& r = card->rect;
                if (event.sceneX >= r.x && event.sceneX < r.x + r.w && event.sceneY >= r.y && event.sceneY < r.y + r.h) {
                    _adviceOn = false;
                }
            }
        }
        return false;
    }

    bool MahjongHintsComponent::OnPointerMove(const PointerEvent& /*event*/) {
        return false;
    }

    void MahjongHintsComponent::DrawMarks(const MahjongGame& game) const {
        const MahjongTableComponent* table = MahjongTableComponent::GetActive();
        IRenderer* renderer = CoreManager::GetRenderer().Get();
        const Seat viewer = _key.viewer;
        if (table == nullptr || renderer == nullptr || !IsSeat(viewer) || table->GetLayout().viewer != viewer) {
            return;
        }
        const auto& concealed = game.GetMatch().Hand().GetHand(viewer).concealed;
        const Tile advised = _advice && _advice->kind == ActionKind::Discard ? _advice->tile : Tile{};
        const Optional<int> raised = table->SelectedHandIndex();
        // The danger bar sits between the bars the table draws along the top and the bottom edge of a hand tile
        const Scene& scene = GetScene();
        const float unit = LayoutScale(scene.GetViewWidth(), scene.GetViewHeight());
        const float dangerTop = eastl::max(kWindMarkBarHeight * unit, kFrameThickness);
        const float dangerBottom = eastl::max(kKeepsWaitingBarHeight * unit, kFrameThickness);
        for (const TileSlot& slot : table->GetLayout().slots) {
            // The layout is of the table's last draw: it may be of another hand
            if (slot.role != SlotRole::HandTile || slot.seat != viewer || slot.index < 0 || slot.index >= static_cast<int>(concealed.size())) {
                continue;
            }
            const Tile tile = concealed[slot.index];
            const Rect rect = raised && *raised == slot.index ? RaisedRect(slot.rect) : slot.rect;
            if (_declaring[tile.Id()]) {
                renderer->DrawFilledRect(rect.x, rect.y + rect.h + kMarkGap, rect.w, kMarkThickness, kDeclareMark);
            }
            if (const DangerMark danger = _dangers[tile.Id()]; danger != DangerMark::None) {
                renderer->DrawFilledRect(rect.x + kFrameThickness, rect.y + dangerTop, kDangerMarkWidth, rect.h - dangerTop - dangerBottom, DangerMarkColor(danger));
            }
            if (advised.IsValid() && tile == advised) {
                renderer->DrawFilledRect(rect.x, rect.y, rect.w, kFrameThickness, kAdvisedFrame);
                renderer->DrawFilledRect(rect.x, rect.y + rect.h - kFrameThickness, rect.w, kFrameThickness, kAdvisedFrame);
                renderer->DrawFilledRect(rect.x, rect.y + kFrameThickness, kFrameThickness, rect.h - 2.0f * kFrameThickness, kAdvisedFrame);
                renderer->DrawFilledRect(rect.x + rect.w - kFrameThickness, rect.y + kFrameThickness, kFrameThickness, rect.h - 2.0f * kFrameThickness, kAdvisedFrame);
            }
        }
    }

    void MahjongHintsComponent::PublishChoice(const MahjongGame& game) {
        const StringView id = HintsModeText(game.GetHintsMode());
        CoreManager::GetUIVars().Set("hintsChoice"_intern, id);
    }

    void MahjongHintsComponent::OnAction(const InputEvents::ActionEvent& event) {
        BE_PROFILE_SCOPE("MahjongHints::OnAction");
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        const StringView action = event.action.ToStringView();
        if (action == kAdviceAction) {
            // What is offered now, not what the last frame showed: a late press changes nothing
            if (!ViewToAdvise(game)) {
                return;
            }
            _adviceOn = !_adviceOn;
            game->GetCoach().ClearNote();
            return;
        }
        if (action != kSetHintsAction || !game->IsOptionsOpen()) {
            return;
        }
        const StringView value = event.value.ToStringView();
        if (const Optional<HintsMode> mode = TryParseHintsMode(value)) {
            game->SetHintsMode(*mode);
        }
        PublishChoice(*game);
    }

}  // namespace BECore::Mahjong
