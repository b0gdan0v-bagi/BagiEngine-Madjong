#include "MahjongHintsComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Renderer/IRenderer.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImage.h>
#include <Events/InputEvents.h>
#include <Generated/MahjongHintsComponent.gen.hpp>
#include <Mahjong/Components/MahjongTableComponent.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/TapSelect.h>
#include <Mahjong/Table/TileAtlas.h>
#include <Mahjong/Table/UiPalette.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kSetHintsAction = "MahjongSetHints";
        constexpr eastl::string_view kAdviceAction = "MahjongHintAdvice";
        // The ids of the two segments of `Hints` on the page `Options` (the values of the buttons `HintsOn` / `HintsOff` in the scene file)
        constexpr eastl::string_view kOnId = "on";
        constexpr eastl::string_view kOffId = "off";
        // The names of the canvas on the node `Hints`: the cells of the row (`WaitCell<i>` holds `WaitTile<i>` and `WaitCount<i>`), the
        // variable of each count (`hintCount<i>`), the panel, the button and the label of the advice (its variable is `hintAdvice`)
        constexpr eastl::string_view kWaitCellPrefix = "WaitCell";
        constexpr eastl::string_view kWaitTilePrefix = "WaitTile";
        constexpr eastl::string_view kHintCountPrefix = "hintCount";
        constexpr eastl::string_view kWaitPanel = "WaitPanel";
        constexpr eastl::string_view kHintButton = "HintButton";
        constexpr eastl::string_view kAdviceLabel = "AdviceText";
        constexpr eastl::string_view kAdviceVar = "hintAdvice";
        // The panel of the reasons (node `HintsWhy`): its plate and the variable of line `i` (`hintWhy<i>`, `kMaxReasonLines` of them)
        constexpr eastl::string_view kWhyPanel = "WhyPanel";
        constexpr eastl::string_view kWhyVarPrefix = "hintWhy";

        constexpr float kMarkGap = 1.0f;         ///< between the tile and its bar
        constexpr float kMarkThickness = 4.0f;   ///< the bar, and below the hand there are 8 units to the edge of the view
        constexpr float kFrameThickness = 3.0f;  ///< drawn inside the edge of the tile

        PoolString IndexedName(eastl::string_view prefix, int index) {
            return PoolString::Intern(Format("{}{}", prefix, index));
        }

        PoolString NameOf(eastl::string_view name) {
            return PoolString::Intern(name);
        }

        /// The button `Hint` is offered: a person acts and it is the viewer, with something to choose from (a non-empty `legal`).
        bool IsOffered(const TableSession& session, Seat viewer, const TableView& view) {
            return session.ActorIsPerson() && session.Actor() == viewer && !view.legal.empty();
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
        const MahjongGame* game = MahjongGame::GetActive();
        if (game != nullptr && game->IsOptionsOpen()) {
            PublishChoice(*game);  // an equal value changes nothing (the version of the variables stays)
        }
        const Key key = KeyOf(game);
        if (!_known || key != _key) {
            Refresh(game, key);
        }
        PushWaitingRow();
        if (_advice && (key != _adviceKey || !_offered)) {
            DropAdvice();
        }
        PushAdvice();
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

    void MahjongHintsComponent::DropAdvice() {
        _advice.reset();
        _explained.reset();
        _whyOpen = false;
        _whyDirty = false;
        _adviceView = TableView{};
        _adviceDirty = false;
    }

    void MahjongHintsComponent::PushAdvice() {
        Scene& scene = GetScene();
        if (_offered != _offeredShown) {
            _offeredShown = _offered;
            UICanvasComponent::SetElementVisibleByName(scene, NameOf(kHintButton), _offered);
        }
        const bool show = _advice.has_value();
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
        PushWhy(show && _whyOpen && _explained.has_value());
    }

    void MahjongHintsComponent::PushWhy(bool open) {
        UIVars& vars = CoreManager::GetUIVars();
        const UIStrings& strings = CoreManager::GetUIStrings();
        if (open && (!_whyShown || _whyDirty || strings.GetVersion() != _whyStringsVersion)) {
            _whyDirty = false;
            _whyStringsVersion = strings.GetVersion();
            const eastl::vector<eastl::string> lines = ReasonLines(strings, _adviceView, *_explained);
            for (size_t i = 0; i < kMaxReasonLines; ++i) {
                vars.Set(IndexedName(kWhyVarPrefix, static_cast<int>(i)), i < lines.size() ? eastl::string_view(lines[i]) : eastl::string_view());
            }
        }
        if (open != _whyShown) {
            _whyShown = open;
            if (!open) {
                // Nothing of the last advice stays in a variable
                for (size_t i = 0; i < kMaxReasonLines; ++i) {
                    vars.Set(IndexedName(kWhyVarPrefix, static_cast<int>(i)), eastl::string_view());
                }
            }
            UICanvasComponent::SetElementVisibleByName(GetScene(), NameOf(kWhyPanel), open);
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
            if (advised.IsValid() && tile == advised) {
                renderer->DrawFilledRect(rect.x, rect.y, rect.w, kFrameThickness, kAdvisedFrame);
                renderer->DrawFilledRect(rect.x, rect.y + rect.h - kFrameThickness, rect.w, kFrameThickness, kAdvisedFrame);
                renderer->DrawFilledRect(rect.x, rect.y + kFrameThickness, kFrameThickness, rect.h - 2.0f * kFrameThickness, kAdvisedFrame);
                renderer->DrawFilledRect(rect.x + rect.w - kFrameThickness, rect.y + kFrameThickness, kFrameThickness, rect.h - 2.0f * kFrameThickness, kAdvisedFrame);
            }
        }
    }

    void MahjongHintsComponent::PublishChoice(const MahjongGame& game) {
        CoreManager::GetUIVars().Set("hintsChoice"_intern, game.HintsOn() ? kOnId : kOffId);
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
            std::optional<TableView> view = ViewToAdvise(game);
            if (!view) {
                return;
            }
            const Key key = KeyOf(game);
            if (_advice && key == _adviceKey && _explained) {
                // The advice of this table is up: a press opens the reasons, the next one closes them
                _whyOpen = !_whyOpen;
                _whyDirty = true;
                return;
            }
            _explained = ExplainAdviceFor(*view);
            _advice = _explained ? std::optional<HandAction>(_explained->Action()) : AdviceFor(*view);
            _adviceKey = key;
            _adviceView = std::move(*view);
            _adviceDirty = true;
            _whyOpen = false;
            return;
        }
        if (action != kSetHintsAction || !game->IsOptionsOpen()) {
            return;  // not ours, or a click that arrives while the page is not open
        }
        // Only the two values the panel offers; client text is never read as anything else
        const eastl::string_view value = event.value.ToStringView();
        if (value == kOnId || value == kOffId) {
            game->SetHintsOn(value == kOnId);
        }
        PublishChoice(*game);
    }

}  // namespace BECore::Mahjong
