#include "MahjongHintsComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UIImage.h>
#include <Events/InputEvents.h>
#include <Generated/MahjongHintsComponent.gen.hpp>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/TileAtlas.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kSetHintsAction = "MahjongSetHints";
        // The ids of the two segments of `Hints` in `Setup` (the values of the buttons `HintsOn` / `HintsOff` in the scene file)
        constexpr eastl::string_view kOnId = "on";
        constexpr eastl::string_view kOffId = "off";
        // The elements of the canvas on the node `Hints` and the variable of the counts (`WaitTile<i>`, `WaitCell<i>`, `hintCount<i>`)
        constexpr const char* kWaitPanel = "WaitPanel";

        PoolString IndexedName(const char* prefix, int index) {
            return PoolString::Intern(Format("{}{}", prefix, index));
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

    void MahjongHintsComponent::OnDraw() {
        const MahjongGame* game = MahjongGame::GetActive();
        if (game != nullptr && game->IsSetupOpen()) {
            PublishChoice(*game);  // an equal value changes nothing (the version of the variables stays)
        }
        PublishWaitingRow(game);
    }

    void MahjongHintsComponent::PublishWaitingRow(const MahjongGame* game) {
        const bool applies = game != nullptr && HintsApply(game->HintsOn(), game->IsSetupOpen(), game->GetSession());
        const TableSession* session = applies ? &game->GetSession() : nullptr;
        const Seat viewer = applies ? session->Viewer() : kNoSeat;
        const uint64_t version = applies ? session->Version() : 0;
        if (!_rowKnown || applies != _rowApplies || viewer != _rowViewer || version != _rowVersion) {
            _rowKnown = true;
            _rowApplies = applies;
            _rowViewer = viewer;
            _rowVersion = version;
            _row = applies ? WaitingTiles(MakeTableView(game->GetMatch().Hand(), viewer)) : eastl::vector<WaitEntry>{};
        }
        PushWaitingRow();
    }

    void MahjongHintsComponent::PushWaitingRow() {
        const int now = static_cast<int>(_row.size());
        const int before = static_cast<int>(_published.size());
        const auto canvas = GetNode().GetComponent<UICanvasComponent>();
        UIVars& vars = CoreManager::GetUIVars();
        Scene& scene = GetScene();
        for (int i = 0; i < kMaxWaitingTiles; ++i) {
            const bool showNow = i < now;
            const bool showBefore = i < before;
            if (showNow && (!showBefore || _row[i].tile != _published[i].tile)) {
                UIElement* element = canvas ? canvas->GetRoot().FindByName(IndexedName("WaitTile", i)) : nullptr;
                if (UIImage* image = element != nullptr ? element->Cast<UIImage>() : nullptr) {
                    image->sprite = PoolString::Intern(SpriteName(_row[i].tile));
                }
            }
            if (showNow && (!showBefore || _row[i].unseen != _published[i].unseen)) {
                vars.Set(IndexedName("hintCount", i), _row[i].unseen);
            }
            if (showNow != showBefore) {
                UICanvasComponent::SetElementVisibleByName(scene, IndexedName("WaitCell", i), showNow);
            }
        }
        if ((now > 0) != (before > 0)) {
            UICanvasComponent::SetElementVisibleByName(scene, PoolString::Intern(kWaitPanel), now > 0);
        }
        _published = _row;
    }

    void MahjongHintsComponent::PublishChoice(const MahjongGame& game) {
        CoreManager::GetUIVars().Set("hintsChoice"_intern, game.HintsOn() ? kOnId : kOffId);
    }

    void MahjongHintsComponent::OnAction(const InputEvents::ActionEvent& event) {
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr || event.action.ToStringView() != kSetHintsAction || !game->IsSetupOpen()) {
            return;  // not ours, or a click that arrives while the panel is not open
        }
        // Only the two values the panel offers; client text is never read as anything else
        const eastl::string_view value = event.value.ToStringView();
        if (value == kOnId || value == kOffId) {
            game->SetHintsOn(value == kOnId);
        }
        PublishChoice(*game);
    }

}  // namespace BECore::Mahjong
