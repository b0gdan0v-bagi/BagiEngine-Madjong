#include "MahjongHintsComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImage.h>
#include <BECore/UI/UILabel.h>
#include <CoreSDL/SDLEvents.h>
#include <Events/InputEvents.h>
#include <Generated/MahjongHintsComponent.gen.hpp>
#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/TileAtlas.h>
#include <Mahjong/Table/UiPalette.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kSetHintsAction = "MahjongSetHints";
        constexpr eastl::string_view kAdviceAction = "MahjongHintAdvice";
        // The names of the canvas on the node `Hints`: the cells of the row (`WaitCell<i>` holds `WaitTile<i>` and `WaitCount<i>`), the
        // variable of each count (`hintCount<i>`), the panel, the button and the label of the advice (its variable is `hintAdvice`)
        constexpr eastl::string_view kWaitCellPrefix = "WaitCell";
        constexpr eastl::string_view kWaitTilePrefix = "WaitTile";
        constexpr eastl::string_view kHintCountPrefix = "hintCount";
        constexpr eastl::string_view kWaitPanel = "WaitPanel";
        constexpr eastl::string_view kHintButton = "HintButton";
        constexpr eastl::string_view kAdviceLabel = "AdviceText";
        constexpr eastl::string_view kAdviceVar = "hintAdvice";
        // The card of the coach (node `HintsWhy`, `Table/HintCard.h`): the plate, the head row, row `i` (`CardRow<i>` with the label
        // `CardSteps<i>` and the pictures `Card<i>Tile<k>`) and the variables of the words
        constexpr eastl::string_view kCoachCard = "CoachCard";
        constexpr eastl::string_view kCardHead = "CardHead";
        constexpr eastl::string_view kCardRowPrefix = "CardRow";
        constexpr eastl::string_view kCardStepsPrefix = "CardSteps";
        constexpr eastl::string_view kCardTitleVar = "cardTitle";
        constexpr eastl::string_view kCardNowVar = "cardNow";
        constexpr eastl::string_view kCardWhoVar = "cardWho";
        constexpr eastl::string_view kCardMoveVar = "cardMove";
        constexpr eastl::string_view kCardStepsVar = "cardSteps";
        constexpr eastl::string_view kCardUsefulVar = "cardUseful";
        constexpr eastl::string_view kCardFootVar = "cardFoot";

        constexpr float kMarkGap = 1.0f;         ///< between the tile and its bar
        constexpr float kMarkThickness = 4.0f;   ///< the bar, and below the hand there are 8 units to the edge of the view
        constexpr float kFrameThickness = 3.0f;  ///< drawn inside the edge of the tile

        PoolString IndexedName(eastl::string_view prefix, int index) {
            return PoolString::Intern(Format("{}{}", prefix, index));
        }

        PoolString NameOf(eastl::string_view name) {
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
        Color RowFill(CardRowKind kind) {
            switch (kind) {
                case CardRowKind::Best:
                    return kUiPalette[2];  // surfaceRaised
                case CardRowKind::Yours:
                    return kUiPalette[8];  // danger
                case CardRowKind::Other:
                    break;
            }
            return Color{0, 0, 0, 0};
        }

        /// The colour of the steps of a row: green closer to a ready hand, amber farther, white the same.
        Color TrendColor(CardTrend trend) {
            switch (trend) {
                case CardTrend::Closer:
                    return kSafeMark;
                case CardTrend::Farther:
                    return kRiskyMark;
                case CardTrend::Same:
                case CardTrend::None:
                    break;
            }
            return kUiPalette[9];  // text
        }

        /// The button `Hint` is offered: a person acts and it is the viewer, with something to choose from (a non-empty `legal`).
        bool IsOffered(const TableSession& session, Seat viewer, const TableView& view) {
            return session.ActorIsPerson() && session.Actor() == viewer && !view.legal.empty();
        }

        /// Draws every element `name` of the canvases under `node` as chosen (`selected`) or not.
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

        Color DangerColor(DangerMark mark) {
            switch (mark) {
                case DangerMark::Safe:
                    return kSafeMark;
                case DangerMark::Risky:
                    return kRiskyMark;
                case DangerMark::Dangerous:
                case DangerMark::None:
                    break;
            }
            return kDangerMark;
        }

    }  // namespace

    MahjongHintsComponent* MahjongHintsComponent::s_active = nullptr;

    MahjongHintsComponent::~MahjongHintsComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void MahjongHintsComponent::OnAttached() {
        s_active = this;
        SubscribeInScene<InputEvents::ActionEvent, &MahjongHintsComponent::OnAction>(this);
        SubscribeInScene<SDLEvents::MouseButtonDownEvent, &MahjongHintsComponent::OnMouseDown>(this);
    }

    MahjongHintsComponent::Key MahjongHintsComponent::KeyOf(const MahjongGame* game) {
        Key key;
        key.applies = game != nullptr && HintsApply(game->HintsOn(), game->IsTablePaused(), game->GetSession());
        if (key.applies) {
            key.viewer = game->GetSession().Viewer();
            key.version = game->GetSession().Version();
        }
        return key;
    }

    std::optional<TableView> MahjongHintsComponent::ViewToAdvise(const MahjongGame* game) {
        if (!KeyOf(game).applies) {
            return std::nullopt;
        }
        const TableSession& session = game->GetSession();
        TableView view = MakeTableView(game->GetMatch().Hand(), session.Viewer());
        if (!IsOffered(session, session.Viewer(), view)) {
            return std::nullopt;
        }
        return view;
    }

    void MahjongHintsComponent::OnDraw() {
        BE_PROFILE_SCOPE("MahjongHints::OnDraw");
        MahjongGame* game = MahjongGame::GetActive();
        if (game != nullptr && game->IsOptionsOpen()) {
            PublishChoice(*game);  // an equal value changes nothing (the version of the variables stays)
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
            Advise(*game, key);  // `_offered` holds only with a game whose hints apply
        }
        const bool coach = game != nullptr && game->CoachOn();
        PushCoach(game, key);  // first: the advice gives way on the card to a note that is up
        PushAdvice(coach);
        PushCard(game, coach);
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
        const TableView view = MakeTableView(game->GetMatch().Hand(), key.viewer);
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
                // The canvas is looked up only when a sprite must change
                const auto canvas = GetNode().GetComponent<UICanvasComponent>();
                UIElement* element = canvas ? canvas->GetRoot().FindByName(IndexedName(kWaitTilePrefix, i)) : nullptr;
                if (UIImage* image = element != nullptr ? element->Cast<UIImage>() : nullptr) {
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
        TableView view = MakeTableView(game.GetMatch().Hand(), key.viewer);
        _explained = ExplainAdviceFor(view);
        _advice = _explained ? std::optional<HandAction>(_explained->Action()) : AdviceFor(view);
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
        // In the mode Coach the sentence is the title of the card in the middle of the table, not a label of its own
        const bool show = _advice.has_value() && !coach;
        UIVars& vars = CoreManager::GetUIVars();
        const UIStrings& strings = CoreManager::GetUIStrings();
        if (show && (!_adviceShown || _adviceDirty || strings.GetVersion() != _adviceStringsVersion)) {
            _adviceDirty = false;
            _adviceStringsVersion = strings.GetVersion();
            vars.Set(NameOf(kAdviceVar), eastl::string_view(AdviceText(strings, _adviceView, *_advice)));
        }
        if (show != _adviceShown) {
            _adviceShown = show;
            if (!show) {
                vars.Set(NameOf(kAdviceVar), eastl::string_view());  // nothing of the last advice stays in a variable
            }
            UICanvasComponent::SetElementVisibleByName(scene, NameOf(kAdviceLabel), show);
        }
    }

    void MahjongHintsComponent::PushCoach(MahjongGame* game, const Key& key) {
        CoachLedger* ledger = game != nullptr ? &game->GetCoach() : nullptr;
        bool show = false;
        if (ledger != nullptr && ledger->Note().has_value()) {
            const CoachHandKey hand = HandKeyOf(game->GetSession());
            if (_coachOf != game || ledger->Version() != _coachSeen) {
                _coachOf = game;
                _coachSeen = ledger->Version();  // a note not seen yet belongs to the hand it is first seen in
                _coachHand = hand;
            }
            show = game->CoachOn() && key.applies && hand == _coachHand && ledger->Note()->seat == key.viewer;
            if (!show) {
                ledger->ClearNote();  // the hand is over, the match is another, the table is covered or paused, the mode changed
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
            const TableSession& session = game->GetSession();
            next.strings = CoreManager::GetUIStrings().GetVersion();
            next.hand = game->GetCoach().HandTallyOf(HandKeyOf(session));
            next.match = game->GetCoach().MatchTallyOf(session.StartCount());
        }
        if (next == _cardKey) {
            return;
        }
        BE_PROFILE_SCOPE("MahjongHints::PushCard");
        _cardKey = next;
        HintCard card;
        if (next.source != CardSource::None) {
            const UIStrings& strings = CoreManager::GetUIStrings();
            const eastl::string tally = CardTallyText(strings, next.hand, next.match);
            card = next.source == CardSource::Note ? MakeNoteCard(strings, *game->GetCoach().Note(), tally) : MakeAdviceCard(strings, _adviceView, *_explained, tally);
        }
        PublishCard(card, next.source != CardSource::None);
    }

    void MahjongHintsComponent::PublishCard(const HintCard& card, bool open) {
        UIVars& vars = CoreManager::GetUIVars();
        // Nothing of the last card stays in a variable once it is down: every variable is set, empty where the card has nothing
        vars.Set(NameOf(kCardTitleVar), eastl::string_view(card.title));
        vars.Set(NameOf(kCardNowVar), eastl::string_view(card.now));
        UICanvasComponent* canvas = CanvasWith(GetScene().GetRootNode(), NameOf(kCoachCard));
        if (canvas == nullptr) {
            return;
        }
        UIElement& root = canvas->GetRoot();
        for (size_t i = 0; i < kMaxCardRows; ++i) {
            const int at = static_cast<int>(i);
            const CardRow* row = i < card.rows.size() ? &card.rows[i] : nullptr;
            vars.Set(IndexedName(kCardWhoVar, at), row != nullptr ? eastl::string_view(row->who) : eastl::string_view());
            vars.Set(IndexedName(kCardMoveVar, at), row != nullptr ? eastl::string_view(row->move) : eastl::string_view());
            vars.Set(IndexedName(kCardStepsVar, at), row != nullptr ? eastl::string_view(row->steps) : eastl::string_view());
            vars.Set(IndexedName(kCardUsefulVar, at), row != nullptr ? eastl::string_view(row->useful) : eastl::string_view());
            const PoolString rowName = IndexedName(kCardRowPrefix, at);
            if (UIElement* element = root.FindByName(rowName)) {
                element->color = RowFill(row != nullptr ? row->kind : CardRowKind::Other);
            }
            if (UIElement* steps = root.FindByName(IndexedName(kCardStepsPrefix, at)); UILabel* label = steps != nullptr ? steps->Cast<UILabel>() : nullptr) {
                label->textColor = TrendColor(row != nullptr ? row->trend : CardTrend::None);
            }
            for (size_t k = 0; k < kMaxCardTiles; ++k) {
                const PoolString name = CardTileName(at, static_cast<int>(k));
                const bool shown = row != nullptr && row->tiles[k].IsValid();
                if (UIElement* element = root.FindByName(name); UIImage* image = element != nullptr ? element->Cast<UIImage>() : nullptr) {
                    image->sprite = shown ? PoolString::Intern(SpriteName(row->tiles[k])) : PoolString();
                }
                canvas->SetElementVisible(name, shown);
            }
            canvas->SetElementVisible(rowName, row != nullptr);
        }
        for (size_t i = 0; i < kMaxCardFooter; ++i) {
            vars.Set(IndexedName(kCardFootVar, static_cast<int>(i)), i < card.footer.size() ? eastl::string_view(card.footer[i]) : eastl::string_view());
        }
        canvas->SetElementVisible(NameOf(kCardHead), !card.rows.empty());
        canvas->SetElementVisible(NameOf(kCoachCard), open);
    }

    void MahjongHintsComponent::OnMouseDown(const SDLEvents::MouseButtonDownEvent& event) {
        MahjongGame* game = MahjongGame::GetActive();
        // A left or touch press (a touch makes a left press too) takes the note that is on screen; a note this very press made (the discard it
        // is) was never shown and stays
        if (event.button.button == SDL_BUTTON_LEFT && game != nullptr && _coachShown && game->GetCoach().Version() == _coachPublished) {
            game->GetCoach().ClearNote();
        }
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
        const std::optional<int> raised = table->SelectedHandIndex();
        // The bar of the danger sits between the bars the table draws along the top and the bottom edge of a hand tile (scaled with the view)
        const Scene& scene = GetScene();
        const float unit = LayoutScale(scene.GetViewWidth(), scene.GetViewHeight());
        const float dangerTop = eastl::max(kWindMarkBarHeight * unit, kFrameThickness);
        const float dangerBottom = eastl::max(kKeepsWaitingBarHeight * unit, kFrameThickness);
        for (const TileSlot& slot : table->GetLayout().slots) {
            // The layout is of the last draw of the table: it has to be of this hand
            if (slot.role != SlotRole::HandTile || slot.seat != viewer || slot.index < 0 || slot.index >= static_cast<int>(concealed.size())) {
                continue;
            }
            const Tile tile = concealed[slot.index];
            // The tile the table drew raised carries its marks with it
            const Rect rect = raised && *raised == slot.index ? RaisedRect(slot.rect) : slot.rect;
            if (_declaring[tile.Id()]) {
                renderer->DrawFilledRect(rect.x, rect.y + rect.h + kMarkGap, rect.w, kMarkThickness, kDeclareMark);
            }
            if (const DangerMark danger = _dangers[tile.Id()]; danger != DangerMark::None) {
                // Inside the left edge, clear of the advised frame that runs along the edge
                renderer->DrawFilledRect(rect.x + kFrameThickness, rect.y + dangerTop, kDangerMarkWidth, rect.h - dangerTop - dangerBottom, DangerColor(danger));
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
        const std::string_view id = HintsModeText(game.GetHintsMode());
        CoreManager::GetUIVars().Set("hintsChoice"_intern, eastl::string_view(id.data(), id.size()));
    }

    void MahjongHintsComponent::OnAction(const InputEvents::ActionEvent& event) {
        BE_PROFILE_SCOPE("MahjongHints::OnAction");
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        const eastl::string_view action = event.action.ToStringView();
        if (action == kAdviceAction) {
            // What is offered now, not what the last frame showed: a late press changes nothing
            if (!ViewToAdvise(game)) {
                return;
            }
            // A switch: the next draw works the advice out (or drops it)
            _adviceOn = !_adviceOn;
            game->GetCoach().ClearNote();  // the press is a click: it takes the note down, as a click on the table does
            return;
        }
        if (action != kSetHintsAction || !game->IsOptionsOpen()) {
            return;  // not ours, or a click that arrives while the page is not open
        }
        // Only the three values the panel offers; client text is never read as anything else
        const eastl::string_view value = event.value.ToStringView();
        if (const std::optional<HintsMode> mode = TryParseHintsMode(std::string_view(value.data(), value.size()))) {
            game->SetHintsMode(*mode);
        }
        PublishChoice(*game);
    }

}  // namespace BECore::Mahjong
