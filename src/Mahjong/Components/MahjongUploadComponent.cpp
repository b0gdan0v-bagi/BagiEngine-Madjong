#include "MahjongUploadComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <BECore/UI/UITextComponent.h>
#include <Events/InputEvents.h>
#include <Events/SceneEvents.h>
#include <Generated/MahjongUploadComponent.gen.hpp>
#include <Mahjong/BuildInfo.h>
#include <Mahjong/Components/MahjongOnlineComponent.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Platform.h>
#include <Mahjong/Table/UiPalette.h>
#include <Mahjong/Table/Words.h>
#include <Mahjong/UploadPrefs.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kAcceptAction = "MahjongUploadAccept";
        constexpr eastl::string_view kDeclineAction = "MahjongUploadDecline";
        constexpr eastl::string_view kSetUploadAction = "MahjongSetUpload";
        constexpr eastl::string_view kNicknameAction = "MahjongNickname";
        constexpr eastl::string_view kOnValue = "on";
        constexpr eastl::string_view kOffValue = "off";
        // The node of the question (`config/nodes/MahjongConsent.xml`)
        constexpr eastl::string_view kConsentNode = "Consent";
        // The page `Options` (`config/nodes/MahjongOptions.xml`): the rows this component shows, the plate they grow and the label of the nickname
        constexpr eastl::string_view kOptionsNode = "Options";
        constexpr eastl::string_view kOptionsPlate = "Plate";
        constexpr eastl::string_view kNicknameField = "NicknameField";
        constexpr eastl::string_view kOptionsRows[] = {"UploadRow", "NicknameRow", "NicknameNote"};
        // 376 high without the rows, plus what the file's header comment counts for them: two rows of 64 and a note of 24, three gaps of 8
        constexpr float kOptionsPlateMaxHeight = 376.0f + 2.0f * 64.0f + 24.0f + 3.0f * 8.0f;
        // The roles `text` and `textMuted` in `kUiPalette` / `kUiPaletteNames`
        constexpr size_t kTextIndex = 9;
        constexpr size_t kTextMutedIndex = 10;
        constexpr eastl::string_view kNicknameNoneKey = "options.nicknameNone";

        IntrusivePtr<UICanvasComponent> FindCanvasOf(Scene& scene, PoolString nodeName) {
            for (const auto& child : scene.GetRootNode().GetChildren()) {
                if (child->GetName() == nodeName) {
                    return child->GetComponent<UICanvasComponent>();
                }
            }
            return {};
        }

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
        _remembered = overrides.remembered.has_value() ? *overrides.remembered : game.SavesAreRemembered();
        game.GetUploadPrefs() = LoadUploadPrefs(_folder);
        // In this order, so that no match can begin recorded before the choice is applied
        _sink.SetPoster(_poster);
        game.GetUpload().SetSink(&_sink);
        Apply(game);
        ShowOptionsRows();
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

    void MahjongUploadComponent::SetNickname(eastl::string_view nickname) {
        MahjongGame* game = MahjongGame::GetActive();
        if (!_running || game == nullptr) {
            return;
        }
        game->GetUploadPrefs().nickname.assign(nickname.data(), nickname.size());
        StoreUploadPrefs(_folder, game->GetUploadPrefs());
        Apply(*game);
    }

    void MahjongUploadComponent::ShowOptionsRows() {
        Scene& scene = GetScene();
        for (const eastl::string_view row : kOptionsRows) {
            UICanvasComponent::SetElementVisibleByName(scene, PoolString::Intern(row), true);
        }
        const IntrusivePtr<UICanvasComponent> options = FindCanvasOf(scene, PoolString::Intern(kOptionsNode));
        if (UIElement* plate = options ? options->GetRoot().FindByName(PoolString::Intern(kOptionsPlate)) : nullptr) {
            plate->style.rect.maxHeight = UILength::Px(kOptionsPlateMaxHeight);
            options->Invalidate();
        }
    }

    void MahjongUploadComponent::PublishOptionsRows(const MahjongGame& game) {
        const UploadPrefs& prefs = game.GetUploadPrefs();
        const UIStrings& strings = CoreManager::GetUIStrings();
        if (_rowsPublished && _publishedConsent == prefs.consent && _publishedNickname == prefs.nickname && _publishedStrings == strings.GetVersion()) {
            return;
        }
        _rowsPublished = true;
        _publishedConsent = prefs.consent;
        _publishedNickname = prefs.nickname;
        _publishedStrings = strings.GetVersion();
        UIVars& vars = CoreManager::GetUIVars();
        switch (prefs.consent) {
            case UploadConsent::Granted:
                vars.Set("uploadChoice"_intern, kOnValue);
                break;
            case UploadConsent::Declined:
                vars.Set("uploadChoice"_intern, kOffValue);
                break;
            case UploadConsent::Unasked:
                vars.Set("uploadChoice"_intern, eastl::string_view());
                break;
        }
        const bool none = prefs.nickname.empty();
        vars.Set("uploadNickname"_intern, eastl::string_view(none ? Tr(strings, kNicknameNoneKey) : prefs.nickname));
        const IntrusivePtr<UICanvasComponent> options = FindCanvasOf(GetScene(), PoolString::Intern(kOptionsNode));
        UIElement* field = options ? options->GetRoot().FindByName(PoolString::Intern(kNicknameField)) : nullptr;
        if (UITextComponent* label = field != nullptr ? field->GetComponent<UITextComponent>() : nullptr) {
            label->textColor = kUiPalette[none ? kTextMutedIndex : kTextIndex];
        }
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
        PublishOptionsRows(*game);
        _sink.Pump(_now);
    }

    void MahjongUploadComponent::OnAction(const InputEvents::ActionEvent& event) {
        if (!_running) {
            return;
        }
        const eastl::string_view action = event.action.ToStringView();
        if (action == kSetUploadAction || action == kNicknameAction) {
            // The rows of `Options`; a click that arrives when the page is not up, or under the question, is stale
            const MahjongGame* game = MahjongGame::GetActive();
            if (game == nullptr || !game->IsOptionsOpen() || _consentShown) {
                return;
            }
            if (action == kNicknameAction) {
                if (MahjongOnlineComponent* pad = MahjongOnlineComponent::GetActive()) {
                    pad->OpenNicknameEntry(game->GetUploadPrefs().nickname);
                }
            } else if (const eastl::string_view value = event.value.ToStringView(); value == kOnValue || value == kOffValue) {
                Choose(value == kOnValue ? UploadConsent::Granted : UploadConsent::Declined);
            }
            return;
        }
        if (!_consentShown) {
            return;  // not ours, or a click that arrives while the question is not up
        }
        if (action == kAcceptAction) {
            Choose(UploadConsent::Granted);
        } else if (action == kDeclineAction) {
            Choose(UploadConsent::Declined);
        }
    }

}  // namespace BECore::Mahjong
