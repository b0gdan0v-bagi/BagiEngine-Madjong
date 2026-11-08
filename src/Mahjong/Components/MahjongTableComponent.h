#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/IComponent.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/TableLayout.h>

namespace BECore {

    namespace SDLEvents {
        struct MouseButtonDownEvent;
    }

    namespace InputEvents {
        struct ActionEvent;
    }

    namespace SceneEvents {
        struct SceneUpdateEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;
    class TableSession;

    /// The pause before a computer seat plays (the component steps one computer action per period while a computer has to act).
    inline constexpr float kComputerStepSeconds = 0.6f;

    /**
     * @brief The table of the active `MahjongGame` on screen: draws the felt, the four hands, the open sets, the bonus tiles, the discards
     *        and the wall, and plays the person seats by mouse and buttons and the computer seats by pace.
     *
     * **Drawing** only reads the game. Every frame the component builds a `TableInput` from the model and the session, lays it out
     * (`MakeTableLayout`, for the view size of its scene) and draws the slots with the tile atlas (`Table/TileAtlas.h`, loaded once in
     * `OnAttached`). Nothing is drawn but the felt when the active game is not Mahjong or its match was not started.
     *
     * **No peeking.** While the hand is played only the concealed tiles of the viewer (`TableSession::Viewer()`) are drawn face up, and
     * none at all while `HandoffPending()` (a hot-seat cover). The other seats show backs, their concealed kongs too. When the hand is
     * over (`SessionPhase::HandOver` and `MatchOver`) all four hands are face up. Open sets, bonus tiles and discards are public.
     *
     * Without an atlas texture (the file did not load, or a test did not give one) the tiles are coloured rectangles, so the layout
     * stays visible. The seat whose turn it is has a highlight behind its hand (in a claim window the discarder, never the seat that is
     * asked to claim first: that would tell a person that this computer holds a claim) and the discard that waits for a claim has a frame.
     *
     * **Playing.** While a person has to act (`TableSession::ActorIsPerson()`, no hot-seat cover) a left click on a tile of the viewer's
     * hand discards it (`PerformPerson(MakeDiscard(tile, declareNext))`); a refused action changes nothing. The other actions are the
     * buttons of the canvas `Actions` (`Table/ActionButtons.h`), shown from `LegalActions` every frame (`SetElementVisibleByName`, the
     * labels of the chow and kong buttons are `UIVars`), and answered here as `InputEvents::ActionEvent`s. `Declare` toggles the flag
     * "the next discard declares the calling hand" (variable `declareNext`, `on` / `off`; the tiles whose declaring discard is not legal
     * are dimmed while it is on); it is cleared by the discard, when the button is no longer offered and when the turn it was set on is
     * over (the seat or the number of discards changed, e.g. a control client played for the person). With two or more people the
     * canvas `Handoff` (a modal, hidden at the start) is shown exactly while `HandoffPending()`; its button `MahjongReveal` calls
     * `Reveal()`. **Pace:** while a computer has to act the component plays one of its actions every `kComputerStepSeconds` of
     * `SceneUpdateEvent` time, and nothing while a person acts, the cover is up or the hand is over. A claim window is settled within one
     * period (the computers decide one after the other without a pause between them) and a computer that is to discard after it does so in
     * the same period, so the time from a discard to the next one is the same whether or not a window opened.
     */
    class MahjongTableComponent : public IComponent {
        BE_CLASS(MahjongTableComponent)
    public:
        MahjongTableComponent() = default;
        ~MahjongTableComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// The next discard declares the calling hand (the button `Declare`).
        [[nodiscard]] bool DeclareNext() const noexcept {
            return _declareNext;
        }
        /// The buttons published by the last `OnDraw`.
        [[nodiscard]] const ActionButtons& GetButtons() const noexcept {
            return _published;
        }

        /// Draws with `texture` (made from pixels, `IRenderer::CreateTexture()`) instead of the atlas from the file; a test hands one.
        void SetAtlasTexture(IntrusivePtr<ITexture> texture);

        /// The layout of the last `OnDraw` in the view units of the scene (empty before the first draw and while nothing is drawn).
        [[nodiscard]] const TableLayout& GetLayout() const noexcept {
            return _layout;
        }

        /// The attached table, if any; the game assumes a single one.
        static MahjongTableComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        void OnMouseDown(const SDLEvents::MouseButtonDownEvent& event);
        void OnAction(const InputEvents::ActionEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);

        /// The game when a person has to act and can (Playing, a person to act, no cover), else null.
        [[nodiscard]] MahjongGame* PersonToAct() const;
        /// The buttons for what the person to act can do now (all hidden when none can); clears `_declareNext` when `Declare` is not
        /// offered.
        [[nodiscard]] ActionButtons CurrentButtons(const MahjongGame& game);
        /// The turn the `Declare` flag is set on: the acting seat and the number of discards so far (every turn follows a discard).
        struct DeclareTurn {
            Seat seat = kNoSeat;
            size_t discards = 0;

            bool operator==(const DeclareTurn&) const = default;
        };
        [[nodiscard]] static DeclareTurn TurnOf(const MahjongGame& game);
        /// One action of the computer that has to act; false (and a log line) when the model refused it.
        static bool StepComputerChecked(TableSession& session);
        /// Pushes the buttons, their labels and the cover to the canvases of the scene, only what changed.
        void PublishScreen(const MahjongGame* game);

        ResourceHandle<ITexture> _atlas;
        TableLayout _layout;
        ActionButtons _published;  // what the canvases show; the scene file starts with every button hidden
        bool _handoffShown = false;
        bool _declareNext = false;
        DeclareTurn _declareTurn;     // the turn `_declareNext` was set on
        float _computerClock = 0.0f;  // seconds a computer has been waiting to play
        static MahjongTableComponent* s_active;
    };

}  // namespace BECore::Mahjong
