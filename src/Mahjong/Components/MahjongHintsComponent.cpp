#include "MahjongHintsComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <Events/InputEvents.h>
#include <Generated/MahjongHintsComponent.gen.hpp>
#include <Mahjong/MahjongGame.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kSetHintsAction = "MahjongSetHints";
        // The ids of the two segments of `Hints` in `Setup` (the values of the buttons `HintsOn` / `HintsOff` in the scene file)
        constexpr eastl::string_view kOnId = "on";
        constexpr eastl::string_view kOffId = "off";

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
