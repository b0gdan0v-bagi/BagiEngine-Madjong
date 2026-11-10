#pragma once

#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/IComponent.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/TileFlights.h>

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

    /// The pause before a computer seat plays at `Normal` speed (the component steps one computer action per period while a computer has to act);
    /// `MahjongGame::GetAnimationSpeed()` scales it, and the flights and the claim marks with it (`Table/AnimationSpeed.h`).
    inline constexpr float kComputerStepSeconds = 1.0f;

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
     * **The seat plates.** Each seat has a plate (`TableLayout::zones.seatPlates`, indexed by seat: each lies at `SeatPosition(viewer, seat)`) painted after the plates of the
     * discards and the wall: the base `kSeatPlate`, on the plate of the seat whose turn it is (`TurnSeat`, never the seat asked to
     * claim) the tint `kTurnMark` and a frame `kSeatTurnFrame`, and along the top edge of the dealer's plate (East of the hand shown) a
     * bar `kDealerMark`, which is red, not gold. All public data, so they show under the cover, for a spectator and at the end of a hand;
     * the words on the plates are the labels of the node `SeatPlates` (`MahjongHudComponent`).
     *
     * **The last move.** Three marks of the gold family (`Table/UiPalette.h`) show what has just happened, from the public data of `HandModel`
     * (`Moves()`, `Discards()`, the open sets): a tint over the last discard (`kLastDiscardMark`; the frame of a pending discard shows
     * besides it), a bar along the edge of the claimed tile of an open set that faces the seat it came from (`kClaimedEdge`, from
     * `TileSlot::claimEdge`), and the tile the viewer has just drawn, which stays in its place in the sorted row and has a tint over its
     * face (`kDrawnTileMark`; only in a hand that is face up: under the cover and for a spectator nothing marks it); a click on it
     * discards it like any other tile. The set a seat
     * has just laid (a claimed chow, pung or kong, a concealed kong, an upgraded kong: the last entry of `Moves()` that made one among those
     * not seen yet, so a claim and the discard after it in one step still show the set) has a fill behind it that blinks for
     * `kClaimHighlightSeconds` (`kClaimHighlight`, on for the first half of each `kClaimBlinkSeconds`); the time runs on `SceneUpdateEvent`
     * (also under the cover, not while the seat choice is open) and the highlight changes no game state.
     *
     * **A claim that lost.** A claim window leaves a record (`HandModel::LastClaimWindow()`, public: every claim is said aloud at a table);
     * when some claim lost to a better one, for `kClaimLostSeconds` the seat plate of the window's winner blinks (`kClaimHighlight`, as a set
     * just laid) and the plate of every seat whose claim lost has a frame (`kClaimLostFrame`), drawn after the base and the turn mark of the plate,
     * before the tiles; they show under the cover, for a spectator and at the end of the hand, the time runs on `SceneUpdateEvent` (it stops with
     * the table pause) and a new hand, match or game starts without them. A mahjong on a discard also flies from its pile slot into the
     * winner's hand (a win is no move, so it is seen as a new winner in the snapshot). When the component itself carried out the action that
     * settled the window (a click on a button, a step of the pace; never a control API action) and a person's claim lost to a mahjong, the
     * hand is over at once and the score table would cover the flight and the plates: `HandOverHeld()` is true for `kClaimLostSeconds` of
     * table time (it stops with the table pause, and is void in any other hand, match or game) and `MahjongHudComponent` shows `HandOver`
     * only when it is false.
     *
     * **Flights** (`Table/TileFlights.h`). The tiles that arrive somewhere new fly there in `kTileFlightSeconds`: a discard from its hand to the
     * pile, a claimed tile from the pile (or, when the discard and the claim fall into one frame, from the discarder's hand) and the tiles of
     * the set from the claimer's hand, the tiles of a kong, a draw from the wall. Every frame the component keeps a `TableSnapshot` of what it
     * drew; the new snapshot against the old one (`PlanFlights`) says which slots are new and where they come from, and all the moves between
     * two draws are one batch, whoever made them (a computer's step, a click, the control API). A flight carries no face: the slot is drawn by
     * the same code and with the same face rule at an interpolated rectangle (`FlightRect`), second pass and on top, so an opponent's tile or a
     * hidden kong flies as a back and nothing is face up that the slot would not show; the marks of the slot (the tints, the claimed edge, the
     * frame of the pending discard, the veil, the bars) follow the rectangle, and the blink of a set just laid starts when its tiles have
     * landed (the flight time is added before its `kClaimHighlightSeconds`; nothing waits when nothing of the set flies). A new batch ends
     * the running flights at their targets, a new hand, match or game, another viewer or view size starts without any. The time is the
     * component's own clock on `SceneUpdateEvent` (it stops with the table pause); `SetTileFlightSeconds(0)` switches the flights off. The model,
     * the session and the control API are not touched.
     *
     * **A declared calling hand** (`PlayerHand::callingDeclared`, public: the control state lists it as `calling`) has a frame of
     * `kCallingFrame` round its hand (four bars of `kCallingFrameWidth` units on the border of the rectangle of the turn backdrop),
     * drawn before the tiles, for every seat that has declared, under the cover and at the end of the hand too (it reads the flag, never
     * a tile); it follows the flag, so it is gone in the next hand.
     * While the viewer's own hand is declared, face up and it is its turn, every tile whose discard leaves the hand waiting
     * (`KeepsWaitingDiscards`, `Table/CallingMarks.h`, from the viewer's `TableView`) has a bar of `kKeepsWaitingMark` inside its bottom
     * edge, whatever the `Hints` switch says.
     *
     * **Playing.** While a person has to act (`TableSession::ActorIsPerson()`, no hot-seat cover) a left click on a tile of the viewer's hand
     * discards it (`PerformPerson(MakeDiscard(tile, declareNext))`); a refused action changes nothing. The other actions are the buttons of the
     * canvas `Actions` (`Table/ActionButtons.h`; `ActionsCompact` holds the same buttons by name for the windows where those would be under the
     * touch minimum and the bar under the table holds the row at its height there, `NeedsCompactActions`, and exactly one of the two is shown; the node `HintsButton`, the button of the hints on the
     * table, is shown with `Actions`, and `ActionsCompact` holds a `HintButton` of its own), shown from `LegalActions` every frame
     * (`SetElementVisibleByName`; each button says a word of the string table in the file (`{#button.<Kind>}`) and the tiles of its action are `UIImage` children whose sprite the component sets), and
     * answered here as `InputEvents::ActionEvent`s. While the pointer is over a shown button (`UIElement::hovered`, set by the canvas) its sentence (a key of the string table, looked up in the
     * current language and again when it changes) is the variable `actionHint` and the label `Hint` is shown; a touch screen has no hover and so no hint. `Declare` is a `UIToggle` bound to the flag
     * "the next discard declares the calling hand" (variable `declareNext`, `on` / `off`; it emits the state it asks for; the tiles whose declaring discard is not legal are dimmed while it is on);
     * the flag is cleared by the discard, when the button is no longer offered and when the turn it was set on is over (the seat or the number of discards changed, e.g. a control client played for
     * the person). With two or more people the canvas `Handoff` (a modal, hidden at the start) is shown exactly while `HandoffPending()`; its button `MahjongReveal` calls `Reveal()`. **Pace:** while
     * a computer has to act the component plays one step every `kComputerStepSeconds` of `SceneUpdateEvent` time (a claim window and the discard after it count as one step), and nothing while a
     * person acts, the cover is up, the seat choice `Setup` is open (`MahjongGame::IsSetupOpen()`: no click is taken either) or the hand is over. A claim window is settled within one period (the
     * computers decide one after the other without a pause between them) and a computer that is to discard after it does so in the same period, so between computers the time from a discard to the
     * next one is the same whether or not a window opened (a person who acts next gets the turn at once when no window opened and after the pause when one did).
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

        /// The tiles of the viewer's hand framed by the last `OnDraw` for the offered chow (see `ChowTilesTaken`); none under the cover, with no
        /// chow on offer or when several are and the pointer is on none of them.
        [[nodiscard]] const ChowTakes& GetChowTakes() const noexcept {
            return _chowTakes;
        }

        /// Draws with `texture` (made from pixels, `IRenderer::CreateTexture()`) instead of the atlas from the file; a test hands one.
        void SetAtlasTexture(IntrusivePtr<ITexture> texture);

        /// The layout of the last `OnDraw` in the view units of the scene (empty before the first draw and while nothing is drawn).
        [[nodiscard]] const TableLayout& GetLayout() const noexcept {
            return _shown.layout;
        }

        /// How long a flight lasts at `Normal` speed (default `kTileFlightSeconds`; the game's speed scales it); 0 or less plans no flight (the tests of the table switch them off).
        void SetTileFlightSeconds(float seconds) noexcept {
            _flightSeconds = seconds;
        }
        [[nodiscard]] float GetTileFlightSeconds() const noexcept {
            return _flightSeconds;
        }
        /// The tiles in the air now.
        [[nodiscard]] size_t FlightCount() const noexcept {
            return _flights.size();
        }

        /// Whether the score table of the finished hand is held back now: the hand was won on a discard by a claim of a mahjong that beat a
        /// claim of a person, the component carried out the action that settled the window, and `kClaimLostSeconds` of table time have not
        /// passed (the time stops with the table pause). The HUD shows `HandOver` only when this is false. False for a hand a control
        /// client settled, once the hand is left (`next_hand`, a new match, `Reset`) and in every other case.
        [[nodiscard]] bool HandOverHeld() const;

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
        /// The hand `_movesSeen` entries of whose `Moves()` the component has looked at (the game, the hands played and the settled hands tell one hand from the next)
        struct SeenHand {
            const MahjongGame* game = nullptr;
            int handsPlayed = -1;
            size_t settled = 0;
            uint64_t starts = 0;  // `TableSession::StartCount()`: a new match is not the old one however many moves it has made

            bool operator==(const SeenHand&) const = default;
        };
        /// The hand, match and game of the frame.
        [[nodiscard]] static SeenHand SeenHandOf(const MahjongGame& game);
        /// Starts the hold of the score table when the claim window settled since `serialBefore` ended the hand with a mahjong that beat the
        /// claim of a person; called after an action the component carried out itself (a click on a button, a step of a computer).
        void HoldScoreTableIfBeaten(const MahjongGame& game, int serialBefore);
        /// One action of the computer that has to act; false (and a log line) when the model refused it.
        static bool StepComputerChecked(TableSession& session);
        /// Pushes the buttons, the tile pictures on them, the hint of the hovered button, the choice between `Actions` (with `HintsButton`) and
        /// `ActionsCompact` and the cover to the canvases of the scene, only what changed.
        void PublishScreen(const MahjongGame* game);

        /// The entries of `Moves()` not seen before the frame: `first` is the index of the first of them; `restarted` when the hand, match or
        /// game is not the one seen before (what is on the table then is not news).
        struct NewMoves {
            bool restarted = false;
            size_t first = 0;
            bool setLaid = false;  // a set was laid among them: the highlight of `_highlightSeat` / `_highlightMeld` has just started
        };

        /// Looks at the entries of `Moves()` not seen yet and starts the highlight of the set the last of them laid, and at the claim window
        /// settled since the last frame (`LastClaimWindow()`, by its serial) and starts the marks of the claims that lost; a hand, a match
        /// or a game that is not the one seen before starts over without either.
        NewMoves FollowMoves(const MahjongGame& game);

        /// Plans the flights of the frame just laid out (`_shown`) against `before`, the frame drawn last: the new batch replaces the running
        /// flights when it has any, a restart, another viewer or another view size drops them.
        void PlanBatch(const TableSnapshot& before, const NewMoves& news, const HandModel& hand);

        /// `baseSeconds` at the speed of the game.
        [[nodiscard]] float Seconds(float baseSeconds) const noexcept {
            return ScaledSeconds(baseSeconds, _speed);
        }

        /// Ends the running flights where they are (their slots are drawn in place from now on); a highlight that waited for them starts.
        void EndFlights();

        ResourceHandle<ITexture> _atlas;
        TableSnapshot _shown;                            // what the last `OnDraw` drew: its layout is `GetLayout()`, empty while nothing is drawn
        eastl::vector<TileFlight> _flights;              // the tiles in the air, flying to slots of `_shown.layout`
        float _flightElapsed = 0.0f;                     // seconds since the batch of `_flights` was planned
        float _flightSeconds = kTileFlightSeconds;       // at Normal speed: `Seconds(_flightSeconds)` is the length of a flight
        AnimationSpeed _speed = kDefaultAnimationSpeed;  // the game's speed as of the last update or draw (the table does not move while `Options` is open)
        ActionButtons _published;                        // what the canvases show; the scene file starts with every button hidden
        std::optional<ButtonKind> _hoveredButton;        // the shown button of `Actions` under the pointer at the last publication
        ChowTakes _chowTakes;                            // what the last `OnDraw` framed in the hand for the offered chow
        eastl::string_view _hintKey;                     // the key of the sentence in `actionHint` (a literal of `SpecOf`, which lives as long as the program)
        uint64_t _hintStringsVersion = 0;                // the version of the string table that sentence was looked up at
        bool _handoffShown = false;
        bool _compactActions = false;  // `ActionsCompact` is the shown canvas of the buttons; the scene file starts with `Actions`
        bool _declareNext = false;
        DeclareTurn _declareTurn;     // the turn `_declareNext` was set on
        float _computerClock = 0.0f;  // seconds a computer has been waiting to play
        SeenHand _seenHand;
        size_t _movesSeen = 0;
        Seat _highlightSeat = kNoSeat;  // the set that blinks: the meld `_highlightMeld` of this seat
        int _highlightMeld = -1;
        float _highlightLeft = 0.0f;                  // seconds of the highlight left, 0 for none
        bool _highlightWaits = false;                 // the tiles of the set are still in the air: the highlight does not run and is not drawn until they land
        int _windowSeen = 0;                          // the serial of the last claim window looked at (`ClaimWindowResult::serial`), 0 for none
        Seat _lostWinner = kNoSeat;                   // the seat that won the window whose losers are marked
        eastl::array<bool, kSeatCount> _lostSeats{};  // the seats whose claim lost in it
        float _lostLeft = 0.0f;                       // seconds of the marks of that window left, 0 for none
        float _handOverHoldLeft = 0.0f;               // seconds the score table is still held back, 0 for none
        SeenHand _handOverHoldOf;                     // the hand it is held for: a hold is void in any other
        static MahjongTableComponent* s_active;
    };

}  // namespace BECore::Mahjong
