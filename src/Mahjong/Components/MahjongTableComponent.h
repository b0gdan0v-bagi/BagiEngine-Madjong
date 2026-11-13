#pragma once

#include <BECore/Renderer/IRenderer.h>
#include <BECore/Renderer/ITexture.h>
#include <BECore/Resource/ResourceHandle.h>
#include <BECore/Scene/IComponent.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Mahjong/ComputerThinking.h>
#include <Mahjong/Table/ActionButtons.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/DangerMarks.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/TableRegions.h>
#include <Mahjong/Table/TileFlights.h>
#include <chrono>

namespace BECore {

    class IRenderer;

    namespace SDLEvents {
        struct MouseButtonDownEvent;
        struct MouseMotionEvent;
    }  // namespace SDLEvents

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

    /// How often the table looks at `config/TableLayout.xml` (wall-clock seconds), see the class comment.
    inline constexpr float kTableLayoutPollSeconds = 0.5f;

    /// The nodes of the scene whose `SceneView` canvas get the regions of the table (`PublishRegions`), by the names of the scene file.
    inline constexpr const char* kRegionNodes[] = {"Hud", "SeatPlates", "CenterPanel", "Hints", "HintsButton", "HintsWhy", "Actions", "HandOver"};
    inline constexpr size_t kRegionNodeCount = std::size(kRegionNodes);

    /**
     * @brief The table of the active `MahjongGame` on screen: draws the felt, the four hands, the open sets, the bonus tiles, the discards
     *        and the wall, and plays the person seats by mouse and buttons and the computer seats by pace.
     *
     * **Drawing** only reads the game. Every frame the component builds a `TableInput` from the model and the session, lays it out
     * (`MakeTableLayout`, for the active view of its scene, one of four shapes picked by the aspect of the window: landscape, portrait, narrow
     * or wide, `TableShape`) and draws the slots with the tile atlas (`Table/TileAtlas.h`, loaded once in `OnAttached`). The felt is one soft
     * picture (a vignette and a grain of 3 levels) stretched over whichever view is active: 22 percent wider than its design in the wide view, 21 percent
     * narrower in the narrow one, which a vignette hides; no screenshot of those two was looked at. Nothing is drawn but the felt when the active game is not Mahjong or its match was not started.
     *
     * **The zones come from data.** `MakeTableLayout` reads the active spec (`ActiveTableLayoutSpec`), which this component fills from the config
     * `TableLayout` when the game has one (`OnAttached`; the tests have none and set it themselves). While the table is drawn it looks every
     * `kTableLayoutPollSeconds` of wall-clock time (not the scene's time, so a paused table or an open modal keep polling) at the file stamp, reloads the config when
     * it changed and offers the document to `OfferTableLayoutSpec` (also when the stamp did not change, so an edit of the Config Editor shows without a save): the spec is
     * replaced only by one that reads and passes `CheckTableLayoutSpec`, and a refused file keeps the last good spec and is logged once per distinct text.
     *
     * **The regions.** Once the layout of a draw is built, the component publishes `MakeTableRegions` of it (`Table/TableRegions.h`: the plates, the
     * centre panel, the wall and the free ground of the spec) to the `SceneView` canvas of every node in `kRegionNodes` with `SetRegion`, so an
     * element anchors to the table's own places (`region="table.plate.0"`). The `Table` node is first in the scene, so they are set before those
     * canvases lay out in the same frame. They are published again only when a rectangle or a name moved (another viewer, shape, window, profile or
     * spec), and a withdrawn name is removed. A node the scene lacks is skipped and looked for again at the next draw.
     *
     * **No peeking.** While the hand is played only the concealed tiles of the viewer (`TableSession::Viewer()`) are drawn face up, and
     * none at all while `HandoffPending()` (a hot-seat cover). The other seats show backs, their concealed kongs too. When the hand is
     * over (`SessionPhase::HandOver` and `MatchOver`) all four hands are face up, and so they are all the time while no seat is a person
     * (`PersonCount() == 0`, a watched match): a choice of the screen only, the computers still decide from `MakeTableView`. The marks
     * that follow the viewer stay the viewer's, so a spectator has none. Open sets, bonus tiles and discards are public.
     *
     * Without an atlas texture (the file did not load, or a test did not give one) the tiles are coloured rectangles, so the layout
     * stays visible. The seat whose turn it is has a highlight behind its hand (in a claim window the discarder, never the seat that is
     * asked to claim first: that would tell a person that this computer holds a claim) and the discard that waits for a claim has a frame,
     * drawn only while the viewer is among the seats that have yet to decide (and no cover is up).
     *
     * **The felt** is the picture of `Table/TableLook.h` (made once per renderer by `IRenderer::CreateTexture`, freed with the GPU idle on
     * the application's clean-up, on a renderer change and with the component; the flat `kFelt` when the renderer gives none), the rim of
     * `kFeltRim` and its lip `kFeltLip` follow it; no box is painted under the discards or the wall.
     *
     * **The seat plates.** Each seat has a plate (`TableLayout::zones.seatPlates`, indexed by seat: each lies at `SeatPosition(viewer, seat)`) painted after the felt: the base (a rounded framed
     * panel, `kSeatPlate` over `kSeatPlateDeep` with its border and soft shadow: one `DrawGeometry` through `UIBoxMesh`, as the centre panel's), on the plate of the seat whose turn it is (`TurnSeat`,
     * never the seat asked to claim) the tint `kTurnMark` and a frame `kSeatTurnFrame` in the same call, and along the top edge of the dealer's plate (East of the hand shown) a bar `kDealerMark`,
     * which is red, not gold. Then the seat's emblem in `EmblemBox` (a person, or the computer of the seat's level; `Table/EmblemAtlas.h`) with the turn ring tinted `kSeatTurnFrame` while it is the
     * seat's turn and the calling ring tinted `kCallingFrame` while its hand is declared; the blink and the frame of a lost claim go over that (a call of their own each, rounded like the plate). A
     * bar of a mark along the edge of a panel is inset by the corner radius at both ends. The meshes are made into member lists (`PanelScratchCapacity()`), so a steady frame allocates nothing. All
     * public data, so they show under the cover, for a spectator and at the end of a hand; the words on the plates are the labels of the node `SeatPlates` (`MahjongHudComponent`).
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
     * the running flights at their targets, a new hand, match or game, another viewer, view size, shape or layout profile starts without any. The time is the
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
     * discards it (`PerformPerson(MakeDiscard(tile, declareNext))`); a refused action changes nothing. In the touch regime
     * (`UsesTapToSelect`: a click that SDL made from a touch or a pen was seen, or a hand tile is under the touch minimum) the first click only
     * selects the tile (it is drawn raised by `kSelectedTileRise`, on top of the bonus row, with its marks and the hints' marks) and a click on
     * the same tile discards it (`StepTap`); another tile moves the selection, a click on no tile clears it. The selection holds only in the
     * discard turn it was made in: it is gone when the turn, the hand or the tile at that index changes, in a claim window, under the cover, while
     * the table is paused and when the regime is off.
     *
     * `selectedTileText` is the name of the selected tile, empty with none; the label `SelectedTile` of the node `Hud` shows it and is shown
     * by this component exactly while a tile is selected.
     *
     * **The name on hover.** While the mouse pointer is over a tile that is drawn face up (`TileOfSlot`: the draw's own rule, so never a back, a wall
     * stack or a tile in the air) the variable `tileHoverText` is its `TileName` and the plate `TileHoverPlate` of the node `TileHover` is shown below
     * it (`HoverPlateRect`). The hit test runs on the layout of the frame; the pointer is forgotten while the table is paused, under the cover,
     * on a touch or a pen and whenever a canvas has it (a modal one, or a shown element reports it hovered).
     *
     * The other actions are the buttons of the canvas `Actions` (`Table/ActionButtons.h`; `ActionsCompact` holds the same buttons by name for the windows where those would be under the
     * touch minimum and the bar under the table holds the row at its height there, `NeedsCompactActions`, and exactly one of the two is shown; the node `HintsButton`, the button of the hints on the
     * table, is shown with `Actions`, and `ActionsCompact` holds a `HintButton` of its own), shown from `LegalActions` every frame
     * (`SetElementVisibleByName`; each button says a word of the string table in the file (`{#button.<Kind>}`) and the tiles of its action are children with a `UIImageComponent` whose sprite the
     * component sets), and answered here as `InputEvents::ActionEvent`s. While the pointer is over a shown button (`UIElement::hovered`, set by the canvas) its sentence (a key of the string table,
     * looked up in the current language and again when it changes) is the variable `actionHint` and the label `Hint` is shown; a touch screen has no hover and so no hint. `Declare` is a toggle row
     * (`UIToggleComponent`) bound to the flag "the next discard declares the calling hand" (variable `declareNext`, `on` / `off`; it emits the state it asks for; the tiles whose declaring discard is
     * not legal are dimmed while it is on); the flag is cleared by the discard, when the button is no longer offered and when the turn it was set on is over (the seat or the number of discards
     * changed, e.g. a control client played for the person). With two or more people the canvas `Handoff` (a modal, hidden at the start) is shown exactly while `HandoffPending()`; its button
     * `MahjongReveal` calls `Reveal()`. **Pace:** while a computer has to act the component plays one step every `kComputerStepSeconds` of `SceneUpdateEvent` time (a claim window and the discard
     * after it count as one step), and nothing while a person acts, the cover is up, the seat choice `Setup` is open (`MahjongGame::IsSetupOpen()`: no click is taken either) or the hand is over. A
     * claim window is settled within one period (the computers decide one after the other without a pause between them) and a computer that is to discard after it does so in the same period, so
     * between computers the time from a discard to the next one is the same whether or not a window opened (a person who acts next gets the turn at once when no window opened and after the pause when
     * one did).
     */
    class MahjongTableComponent : public IComponent {
        BE_CLASS(MahjongTableComponent)
    public:
        MahjongTableComponent() = default;
        ~MahjongTableComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// A computer seat's decision is being computed in the background (a search seat; no decision that needs no search is ever "thinking").
        [[nodiscard]] bool ComputerThinks() const {
            return _thinking.Running();
        }

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

        /// The same for the emblem atlas of the seat plates (`Table/EmblemAtlas.h`); an empty texture draws the plates without emblems.
        void SetEmblemTexture(IntrusivePtr<ITexture> texture);

        /// The index (in the concealed tiles of the viewer) of the tile drawn raised by the last `OnDraw`; none when no tile is selected. The
        /// hints draw their marks on `RaisedRect` of that slot.
        [[nodiscard]] std::optional<int> SelectedHandIndex() const noexcept {
            return _drawnSelected;
        }

        /// The capacities of the scratch lists the panels are meshed into (see `DrawPanel`): a table drawn again unchanged leaves both as they were, since a steady frame allocates nothing.
        struct PanelScratch {
            size_t vertices = 0;
            size_t indices = 0;

            [[nodiscard]] bool operator==(const PanelScratch&) const = default;
        };
        [[nodiscard]] PanelScratch PanelScratchCapacity() const noexcept {
            return PanelScratch{_panelVertices.capacity(), _panelIndices.capacity()};
        }

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
        /// Keeps the position of the pointer (output pixels; a touch or a pen forgets it); only a motion no canvas took gets here.
        void OnMouseMotion(const SDLEvents::MouseMotionEvent& event);
        void OnAction(const InputEvents::ActionEvent& event);
        void OnUpdate(const SceneEvents::SceneUpdateEvent& event);
        void OnApplicationCleanUp();

        /// The felt texture for `renderer`, made on first use and again for another renderer (`GetId()`); null when the renderer gave none.
        [[nodiscard]] ITexture* FeltTexture(IRenderer& renderer);
        /// Frees the felt texture, with the GPU idle when the renderer is the one that made it.
        void ReleaseFelt();

        /// Offers the config `TableLayout` to `OfferTableLayoutSpec` (never keeps the node: a reload invalidates it) and reports what came of it, a refusal once per
        /// distinct text. False when the game has no such config.
        bool OfferTableLayoutConfig();
        /// The start of the table: takes the config `TableLayout` as the active spec and remembers the stamp of its file.
        void LoadTableLayoutSpec();
        /// Once per `kTableLayoutPollSeconds`: reloads the config when its file changed and offers it.
        void PollTableLayoutSpec();

        /// The game when a person has to act and can (Playing, a person to act, no cover), else null.
        [[nodiscard]] MahjongGame* PersonToAct() const;
        /// Whether a click on a hand tile only selects it: `_touchSeen`, or a hand tile under the touch minimum in the window of the last draw
        /// (`UsesTapToSelect`, the active view and the hand tile of its profile, `TableProfileFor`); before any draw the window as it is now.
        [[nodiscard]] bool TapToSelect() const;
        /// The size part of the regime for the renderer's output as it is now (a hand tile under the touch minimum); `OnDraw` keeps it.
        [[nodiscard]] bool SmallHandTileNow() const;
        /// What a selection must still be to count: the regime on, a person to act in the discard turn of the hand and match it was made in,
        /// at the version of the session it was made at, and the tile at its index still the one selected.
        [[nodiscard]] bool SelectionHolds(const MahjongGame& game) const;
        /// Drops the selection that no longer holds (and any, with no game).
        void RefreshSelection(const MahjongGame* game);
        /// A click on a hand tile: `StepTap` says what it does. Needs `PersonToAct()`.
        void TapHandTile(MahjongGame& game, const std::optional<int>& hit);
        /// What the person to act at @p actor may do now: `LegalActions` of the hand, and in a room the server's own list (`OnlineTable::actions`,
        /// without `next_hand`: a display hand lists nothing). Valid until the next call.
        [[nodiscard]] const eastl::vector<HandAction>& LegalFor(const MahjongGame& game, Seat actor);
        /// Plays @p action for the person to act: through the session, or in a room as the first entry of the server's list that equals it
        /// (`OnlineClient::Act`). An action the server does not list is refused (`NotYourTurn`), which is a click that is out of date.
        HandStatus Perform(MahjongGame& game, const HandAction& action);
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
        /// The pace of the computers, once a frame: begins the step of the computer that has to act at the start of its pause, applies it at
        /// the pause end (when a search is still running, when it arrives), then the rest of a claim window and the discard after it, each begun
        /// as soon as the one before was applied. Drops the step in progress when it can no longer be applied.
        void PlayComputers(MahjongGame& game, float deltaTime);
        /// Forgets the step in progress: the task is cancelled, nothing of it is applied, the burst of a claim window is over. The pause
        /// already spent stays when `Version()` or `StartCount()` moved: the next frame begins the step again without waiting a second time.
        void DropComputerStep();
        /// Begins the next step of the actor (`BeginComputerStep`) and hands it to `_thinking`; false when there is none.
        bool BeginComputerStep(TableSession& session, const HandModel& hand);
        /// Sets the variable `selectedTileText` to the name of `tile` (empty for none), only when the tile or the language changed.
        void PublishSelectedText(Tile tile);
        /// The name on hover, from the layout just built: sets `tileHoverText`, shows the plate `TileHoverPlate` exactly while the pointer is over a
        /// face-up tile (`Table/TileHover.h`) and moves it only when its place changed. `game` null: nothing is hovered.
        void PublishHover(const MahjongGame* game, float viewWidth, float viewHeight);
        /// Publishes the regions of `layout` (just built) to the canvases of `kRegionNodes`, only what changed.
        void PublishRegions(const TableLayout& layout);
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

        /// The panels of the table (the seat plates and the centre panel) are one `DrawGeometry` each: `DrawPanel` meshes the soft shadow, the fill, the border and the highlight of `rect`
        /// (and, with `turn`, the tint and the frame of the seat whose turn it is) into the member lists, `DrawPanelTint` and `DrawPanelFrame` mesh one more layer over a plate (the blink and
        /// the frame of a claim). `unit` is the table's `LayoutScale`: the radius and the widths are in design units.
        void DrawPanel(IRenderer& renderer, const Rect& rect, float unit, bool turn);
        void DrawPanelTint(IRenderer& renderer, const Rect& rect, float unit, const Color& color);
        void DrawPanelFrame(IRenderer& renderer, const Rect& rect, float unit, float width, const Color& color);
        /// Empties the scratch lists and gives them their first capacity, so the first frame is the only one that makes them.
        void BeginPanelMesh();

        /// `baseSeconds` at the speed of the game.
        [[nodiscard]] float Seconds(float baseSeconds) const noexcept {
            return ScaledSeconds(baseSeconds, _speed);
        }

        /// Ends the running flights where they are (their slots are drawn in place from now on); a highlight that waited for them starts.
        void EndFlights();

        ResourceHandle<ITexture> _atlas;
        ResourceHandle<ITexture> _emblems;               // the emblem atlas of the seat plates
        IntrusivePtr<ITexture> _felt;                    // the felt made from pixels (`Table/TableLook.h`); empty when the renderer gave none
        uint64_t _feltRenderer = 0;                      // `IRenderer::GetId()` of the renderer `_felt` was asked of, 0 for none yet
        TableSnapshot _shown;                            // what the last `OnDraw` drew: its layout is `GetLayout()`, empty while nothing is drawn
        TableSnapshot _previous;                         // the snapshot drawn before `_shown`; `OnDraw` swaps the two and refills one, so its lists keep their capacity
        TableInput _input;                               // the input of the last layout, kept for the capacity of its lists
        eastl::vector<TileFlight> _flights;              // the tiles in the air, flying to slots of `_shown.layout`
        float _flightElapsed = 0.0f;                     // seconds since the batch of `_flights` was planned
        float _flightSeconds = kTileFlightSeconds;       // at Normal speed: `Seconds(_flightSeconds)` is the length of a flight
        AnimationSpeed _speed = kDefaultAnimationSpeed;  // the game's speed as of the last update or draw (the table does not move while `Options` is open)
        ActionButtons _published;                        // what the canvases show; the scene file starts with every button hidden
        /// What the buttons were made from: `LegalActions` allocates, and the model moves only through the session, which bumps its version
        struct ButtonsKey {
            const MahjongGame* game = nullptr;
            bool online = false;  // the display session of a room has versions of its own
            uint64_t version = 0;
            uint64_t starts = 0;
            Seat actor = kNoSeat;

            bool operator==(const ButtonsKey&) const = default;
        };
        std::optional<ButtonsKey> _offeredKey;      // the key of `_offered`; none while no person acts
        ActionButtons _offered;                     // `MakeActionButtons` of the person to act, made again only when the key moves
        eastl::vector<HandAction> _legal;           // `LegalFor`, a member so that it keeps its capacity
        std::optional<ButtonsKey> _keepsKey;        // the key `_keeps` was made for; none while the keeps-waiting bars are not asked for
        eastl::array<bool, kTileIdCount> _keeps{};  // `KeepsWaitingDiscards` of the viewer's own turn
        std::optional<ButtonKind> _hoveredButton;   // the shown button of `Actions` under the pointer at the last publication
        ChowTakes _chowTakes;                       // what the last `OnDraw` framed in the hand for the offered chow
        eastl::string_view _hintKey;                // the key of the sentence in `actionHint` (a literal of `SpecOf`, which lives as long as the program)
        uint64_t _hintStringsVersion = 0;           // the version of the string table that sentence was looked up at
        bool _handoffShown = false;
        bool _compactActions = false;  // `ActionsCompact` is the shown canvas of the buttons; the scene file starts with `Actions`
        bool _declareNext = false;
        bool _touchSeen = false;                  // a click made by a touch or a pen was seen: the tap regime stays on from then
        std::optional<bool> _drawnSmallHandTile;  // `SmallHandTileNow()` of the last draw: what the regime reads between draws
        std::optional<int> _selected;             // the selected hand tile (an index of the concealed tiles of the person to act) and what it holds for
        Tile _selectedTile;                       // the tile it was made on
        DeclareTurn _selectedTurn;                // the turn,
        uint64_t _selectedVersion = 0;            // the version of the session
        SeenHand _selectedHand;                   // and the hand and match
        std::optional<int> _drawnSelected;        // the tile the last `OnDraw` raised
        bool _selectedLabelShown = false;         // the label `SelectedTile` is shown
        Tile _textTile;                           // the tile `selectedTileText` names now (invalid for none) and the string table version it was worded at
        uint64_t _textStringsVersion = 0;
        float _pointerX = 0.0f;  // the pointer of the mouse in output pixels, valid while `_pointerSeen`: the last motion the table got (converted to the view when
        float _pointerY = 0.0f;  // it is used, so a resized window keeps it where it is on the screen)
        bool _pointerSeen = false;
        bool _hoverShown = false;  // the plate `TileHoverPlate` is shown
        Tile _hoverTile;           // the tile `tileHoverText` names now (invalid for none) and the string table version it was worded at
        uint64_t _hoverStringsVersion = 0;
        DangerMark _hoverDanger = DangerMark::None;  // the danger mark `tileHoverDanger` words now (the hovered tile of the viewer's hand)
        DeclareTurn _declareTurn;                    // the turn `_declareNext` was set on
        float _computerClock = 0.0f;                 // seconds a computer has been waiting to play
        /// The step of a computer that was begun and is not applied yet: the session `Version()` and `StartCount()` it was begun at (the job
        /// itself is in `_thinking`).
        struct StepInProgress {
            uint64_t version = 0;
            uint64_t starts = 0;
        };
        std::optional<StepInProgress> _step;
        ComputerThinking _thinking;  // runs the search of `_step`; ends with the component
        /// After the first step of a pause that was in a claim window: the other computers of the window and the discard after it go
        /// without another pause. `_burstSerial` is the window serial before the first step (`HoldScoreTableIfBeaten`).
        enum class Burst : uint8_t { None, Window, Discard };
        Burst _burst = Burst::None;
        bool _firstInWindow = false;  // the step in progress is the first of its pause and began in a claim window
        int _burstSerial = 0;
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

        eastl::vector<GeometryVertex> _panelVertices;  // the scratch of `DrawPanel`: cleared before each panel, never made again
        eastl::vector<int> _panelIndices;

        eastl::vector<TableRegion> _regions;                                              // the regions of the last layout (refilled every draw, for its capacity)
        eastl::vector<TableRegion> _publishedRegions;                                     // what the canvases of `_regionCanvases` hold,
        eastl::vector<PoolString> _regionNames;                                           // and the interned names of them, in the same order
        eastl::array<IntrusivePtr<UICanvasComponent>, kRegionNodeCount> _regionCanvases;  // the canvases of `kRegionNodes`, null for a node the scene lacks

        std::optional<std::filesystem::file_time_type> _layoutStamp;  // the stamp of the file of the config `TableLayout` at the last look, none when it has none
        std::chrono::steady_clock::time_point _layoutPolled;          // the last look
        eastl::string _layoutRefused;                                 // the text of the refusal that was logged last, empty when the last offer was not refused
        static MahjongTableComponent* s_active;
    };

}  // namespace BECore::Mahjong
