#pragma once

#include <BECore/Scene/IComponent.h>
#include <Mahjong/WebUploadSink.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;

    /**
     * @brief The glue of the tester upload (`docs/tester-match-logs.md`): the node `Consent` (`config/nodes/MahjongConsent.xml`) carries this component
     *        and the question it shows. It owns the `WebUploadSink` and the page's post call (`EngineWebPoster`).
     *
     * **Only where the page can post** (`IWebPoster::Available()`, a browser build with the script): everywhere else the component does nothing at
     * all, no screen, no file, and the game's `GetUpload()` is untouched, so the desktop is as it was.
     *
     * **The first frame** (the game is active, the scene draws) reads the tester's choice from `UploadPrefsFolder()` into
     * `MahjongGame::GetUploadPrefs()` and then, in this order, gives the uploader its sink, its consent (`UploadChoiceIsRemembered()` says whether
     * the choice is kept) and the session its origin with the nickname (only while the consent is `Granted`), so no match can begin recorded before
     * the choice is applied. `Choose` does the same for a later change and stores the file. **Privacy:** nothing leaves unless the consent is
     * `Granted`, and a nickname is in a log only then.
     *
     * **The question** is up exactly while the consent is `Unasked` (the actions `MahjongUploadAccept` / `MahjongUploadDecline`, honoured only
     * then). Where the choice is not remembered the next visit asks again by itself, as nothing is read. Each frame pumps the sink with the
     * clock of the scene (the sum of the frame times); a frame in which nothing moved allocates nothing. The sink is flushed when the component
     * goes (the scene is torn down after `Stop()` gave the running match up), so that the `Abandoned` piece is posted at once.
     */
    class MahjongUploadComponent : public IComponent {
        BE_CLASS(MahjongUploadComponent)
    public:
        /// What a test replaces (a field left empty is the real thing); set before the scene is made and cleared after.
        struct Overrides {
            IWebPoster* poster = nullptr;                 ///< not owned
            std::optional<std::filesystem::path> folder;  ///< the folder of `upload.txt` (empty path: nowhere)
            std::optional<bool> remembered;               ///< `UploadChoiceIsRemembered()`
        };
        [[nodiscard]] static Overrides& TestOverrides() noexcept;

        MahjongUploadComponent() = default;
        ~MahjongUploadComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// The attached component, if any; the game assumes a single one.
        static MahjongUploadComponent* GetActive() noexcept {
            return s_active;
        }

        /// Whether the page can post and the component has taken over the game's upload (known after the first frame)
        [[nodiscard]] bool IsRunning() const noexcept {
            return _running;
        }
        /// Whether the question is up
        [[nodiscard]] bool IsConsentShown() const noexcept {
            return _consentShown;
        }
        [[nodiscard]] const WebUploadSink& Sink() const noexcept {
            return _sink;
        }

        /// Sets the consent, keeps the choice in the file and applies it to the uploader and the origin at once. Nothing when not running.
        void Choose(UploadConsent consent);

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        /// Reads the choice and gives it to the game; false when the page cannot post (then nothing more is done, ever)
        bool TakeOver(MahjongGame& game);
        /// The consent and the nickname of the prefs into the uploader and the session's origin
        void Apply(MahjongGame& game);
        void PublishConsentScreen(bool shown);

        EngineWebPoster _enginePoster;
        WebUploadSink _sink;
        IWebPoster* _poster = nullptr;
        std::filesystem::path _folder;
        bool _remembered = false;
        bool _started = false;  ///< `TakeOver` was tried (once)
        bool _running = false;  ///< it succeeded
        bool _consentShown = false;
        double _now = 0.0;  ///< the clock of the sink: the sum of the frame times

        static MahjongUploadComponent* s_active;
    };

}  // namespace BECore::Mahjong
