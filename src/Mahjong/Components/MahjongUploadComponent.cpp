#include "MahjongUploadComponent.h"

#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/MahjongUploadComponent.gen.hpp>
#include <Mahjong/BuildInfo.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Platform.h>
#include <Mahjong/UploadPrefs.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kAcceptAction = "MahjongUploadAccept";
        constexpr eastl::string_view kDeclineAction = "MahjongUploadDecline";
        // The node of the question (`config/nodes/MahjongConsent.xml`)
        constexpr eastl::string_view kConsentNode = "Consent";

    }  // namespace

    MahjongUploadComponent* MahjongUploadComponent::s_active = nullptr;

    MahjongUploadComponent::Overrides& MahjongUploadComponent::TestOverrides() noexcept {
        static Overrides overrides;
        return overrides;
    }

    MahjongUploadComponent::~MahjongUploadComponent() {
        if (_running) {
            // The scene goes after `Stop()` gave the running match up: post what is queued now. The game may outlive the component, so it
            // must not keep a pointer to the sink
            _sink.Flush(_now);
            if (MahjongGame* game = MahjongGame::GetActive(); game != nullptr) {
                game->GetUpload().SetSink(nullptr);
            }
        }
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void MahjongUploadComponent::OnAttached() {
        s_active = this;
        SubscribeInScene<InputEvents::ActionEvent, &MahjongUploadComponent::OnAction>(this);
        SubscribeInScene<SceneEvents::SceneUpdateEvent, &MahjongUploadComponent::OnUpdate>(this);
    }

    void MahjongUploadComponent::OnUpdate(const SceneEvents::SceneUpdateEvent& event) {
        _now += static_cast<double>(event.deltaTime);
    }

    bool MahjongUploadComponent::TakeOver(MahjongGame& game) {
        const Overrides& overrides = TestOverrides();
        _poster = overrides.poster != nullptr ? overrides.poster : &_enginePoster;
        if (!_poster->Available()) {
            return false;
        }
        _folder = overrides.folder.has_value() ? *overrides.folder : UploadPrefsFolder();
        _remembered = overrides.remembered.has_value() ? *overrides.remembered : UploadChoiceIsRemembered();
        game.GetUploadPrefs() = LoadUploadPrefs(_folder);
        // In this order, so that no match can begin recorded before the choice is applied
        _sink.SetPoster(_poster);
        game.GetUpload().SetSink(&_sink);
        Apply(game);
        return true;
    }

    void MahjongUploadComponent::Apply(MahjongGame& game) {
        const UploadPrefs& prefs = game.GetUploadPrefs();
        game.GetUpload().SetConsent(prefs.consent, _remembered);
        // A nickname goes into a log only while the consent is `Granted`: a local file never carries a name that was not sent
        const eastl::string_view nickname = prefs.consent == UploadConsent::Granted ? eastl::string_view(prefs.nickname) : eastl::string_view();
        game.GetLocalSession().SetLogOrigin(MakeMatchOrigin(kBrowserBuild, nickname));
    }

    void MahjongUploadComponent::Choose(UploadConsent consent) {
        MahjongGame* game = MahjongGame::GetActive();
        if (!_running || game == nullptr) {
            return;
        }
        game->GetUploadPrefs().consent = consent;
        // A failed write (no folder) leaves the choice for this visit only; the screen comes back at the next one
        StoreUploadPrefs(_folder, game->GetUploadPrefs());
        Apply(*game);
        PublishConsentScreen(consent == UploadConsent::Unasked);
    }

    void MahjongUploadComponent::PublishConsentScreen(bool shown) {
        if (shown == _consentShown) {
            return;
        }
        _consentShown = shown;
        UICanvasComponent::SetVisibleByName(GetScene(), PoolString::Intern(kConsentNode), shown);
    }

    void MahjongUploadComponent::OnDraw() {
        if (_started && !_running) {
            return;  // the page cannot post: nothing of this component is ever done
        }
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        if (!_started) {
            _started = true;
            _running = TakeOver(*game);
            if (!_running) {
                return;
            }
        }
        PublishConsentScreen(game->GetUploadPrefs().consent == UploadConsent::Unasked);
        _sink.Pump(_now);
    }

    void MahjongUploadComponent::OnAction(const InputEvents::ActionEvent& event) {
        if (!_running || !_consentShown) {
            return;  // not ours, or a click that arrives while the question is not up
        }
        const eastl::string_view action = event.action.ToStringView();
        if (action == kAcceptAction) {
            Choose(UploadConsent::Granted);
        } else if (action == kDeclineAction) {
            Choose(UploadConsent::Declined);
        }
    }

}  // namespace BECore::Mahjong
