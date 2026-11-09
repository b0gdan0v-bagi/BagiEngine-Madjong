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
     * asked to claim first: that would tell a person that this computer holds a claim) and the discard that waits for a claim has a frame,
     * drawn only while the viewer is among the seats that have yet to decide (and no cover is up).
     *
     * **The last move.** Three marks of the gold family (`Table/UiPalette.h`) show what has just happened, from the public data of `HandModel`
     * (`Moves()`, `Discards()`, the open sets): a tint over the last discard (`kLastDiscardMark`; the frame of a pending discard shows
     * besides it), a bar along the edge of the claimed tile of an open set that faces the seat it came from (`kClaimedEdge`, from
     * `TileSlot::claimEdge`), and the tile the viewer has just drawn, which lies apart at the end of its row (only in a hand that is
     * face up: under the cover and for a spectator the hand is the plain row); a click on it discards it like any other tile. The set a seat
     * has just laid (a claimed chow, pung or kong, a concealed kong, an upgraded kong: the last entry of `Moves()` that made one among those
     * not seen yet, so a claim and the discard after it in one step still show the set) has a fill behind it that blinks for
     * `kClaimHighlightSeconds` (`kClaimHighlight`, on for the first half of each `kClaimBlinkSeconds`); the time runs on `SceneUpdateEvent`
     * (also under the cover, not while the seat choice is open) and the highlight changes no game state.
     *
     * **Playing.** While a person has to act (`TableSession::ActorIsPerson()`, no hot-seat cover) a left click on a tile of the viewer's hand
     * discards it (`PerformPerson(MakeDiscard(tile, declareNext))`); a refused action changes nothing. The other actions are the buttons of the
     * canvas `Actions` (`Table/ActionButtons.h`; `ActionsCompact` holds the same buttons by name for the windows where those would be under the
     * touch minimum and the bar under the table holds a line of it, `NeedsCompactActions`, and exactly one of the two is shown), shown from `LegalActions` every frame (`SetElementVisibleByName`; each
     * button says a word of the string table in the file (`{#button.<Kind>}`) and the tiles of its action are `UIImage` children whose sprite the component sets), and answered here as
     * `InputEvents::ActionEvent`s. While the pointer is over a shown button (`UIElement::hovered`, set by the canvas) its sentence (a key of the
     * string table, looked up in the current language and again when it changes) is the variable `actionHint` and the label `Hint` is shown; a touch screen has no hover and so no hint. `Declare` is a
     * `UIToggle` bound to the flag "the next discard declares the calling hand" (variable `declareNext`, `on` / `off`; it emits the state it asks for; the tiles whose declaring discard is not legal
     * are dimmed while it is on); the flag is cleared by the discard, when the button is no longer offered and when the turn it was set on is over (the seat or the number of discards changed, e.g. a
     * control client played for the person). With two or more people the canvas `Handoff` (a modal, hidden at the start) is shown exactly while `HandoffPending()`; its button `MahjongReveal` calls
     * `Reveal()`. **Pace:** while a computer has to act the component plays one step every `kComputerStepSeconds` of `SceneUpdateEvent` time (a claim window and the discard after it count as one
     * step), and nothing while a person acts, the cover is up, the seat choice `Setup` is open (`MahjongGame::IsSetupOpen()`: no click is taken either) or the hand is over. A claim window is settled
     * within one period (the computers decide one after the other without a pause between them) and a computer that is to discard after it does so in the same period, so between computers the time
     * from a discard to the next one is the same whether or not a window opened (a person who acts next gets the turn at once when no window opened and after the pause when one did).
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
        /// Pushes the buttons, the tile pictures on them, the hint of the hovered button, the choice between `Actions` and `ActionsCompact`
        /// and the cover to the canvases of the scene, only what changed.
        void PublishScreen(const MahjongGame* game);

        /// Looks at the entries of `Moves()` not seen yet and starts the highlight of the set the last of them laid; a hand, a match or a
        /// game that is not the one seen before starts over without a highlight.
        void FollowMoves(const MahjongGame& game);

        ResourceHandle<ITexture> _atlas;
        TableLayout _layout;
        ActionButtons _published;          // what the canvases show; the scene file starts with every button hidden
        eastl::string_view _hintKey;       // the key of the sentence in `actionHint` (a literal of `SpecOf`, which lives as long as the program)
        uint64_t _hintStringsVersion = 0;  // the version of the string table that sentence was looked up at
        bool _handoffShown = false;
        bool _compactActions = false;  // `ActionsCompact` is the shown canvas of the buttons; the scene file starts with `Actions`
        bool _declareNext = false;
        DeclareTurn _declareTurn;     // the turn `_declareNext` was set on
        float _computerClock = 0.0f;  // seconds a computer has been waiting to play
        /// The hand `_movesSeen` entries of whose `Moves()` the component has looked at (the game, the hands played and the settled hands tell one hand from the next)
        struct SeenHand {
            const MahjongGame* game = nullptr;
            int handsPlayed = -1;
            size_t settled = 0;

            bool operator==(const SeenHand&) const = default;
        };
        SeenHand _seenHand;
        size_t _movesSeen = 0;
        Seat _highlightSeat = kNoSeat;  // the set that blinks: the meld `_highlightMeld` of this seat
        int _highlightMeld = -1;
        float _highlightLeft = 0.0f;  // seconds of the highlight left, 0 for none
        static MahjongTableComponent* s_active;
    };

}  // namespace BECore::Mahjong
