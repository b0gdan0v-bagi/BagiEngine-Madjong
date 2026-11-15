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
#include <Mahjong/Table/TablePalette.h>
#include <Mahjong/Table/TableRegions.h>
#include <Mahjong/Table/TileAtlas.h>
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

    /// The pause before a computer seat plays at `Normal` speed; `MahjongGame::GetAnimationSpeed()` scales it, and the flights and the
    /// claim marks with it (`Table/AnimationSpeed.h`).
    inline constexpr float kComputerStepSeconds = 1.0f;

    /// Wall-clock seconds between looks at `config/TableLayout.xml`.
    inline constexpr float kTableLayoutPollSeconds = 0.5f;

    /// The nodes whose `SceneView` canvas gets the regions of the table (`PublishRegions`), by the names of the scene file.
    inline constexpr const char* kRegionNodes[] = {"Hud", "SeatPlates", "CenterPanel", "Hints", "HintsButton", "HintsWhy", "Actions", "HandOver"};
    inline constexpr size_t kRegionNodeCount = std::size(kRegionNodes);

    /**
     * @brief The table of the active `MahjongGame` on screen: draws the felt, the hands, the open sets, the discards and the wall, plays the
     *        person seats by mouse and buttons and paces the computer seats. What it draws, marks and animates:
     *        `src/Mahjong/Components/CLAUDE.md`, "The table (`MahjongTableComponent`)".
     *
     * **Drawing** only reads the game; the game changes only through the session. Every frame builds a `TableInput` and lays it out
     * (`MakeTableLayout`, for the active view of the scene), then draws the slots with the tile atlas (loaded once in `OnAttached`). Only the
     * felt is drawn when the active game is not Mahjong or its match is not started. Without an atlas texture (a test gives none) the
     * tiles are coloured rectangles, so the layout stays visible. The felt is one soft picture stretched over whichever view is active; its
     * vignette hides that the wide and the narrow view are not its design aspect (no screenshot of those two views was looked at).
     *
     * **The zones come from data.** `OnAttached` takes the spec from the config `TableLayout`; the draw polls the file stamp on the wall
     * clock (a paused table or an open modal keep polling) and offers the document also when the stamp did not change, so an unsaved edit
     * of the Config Editor shows. Only a spec that passes `CheckTableLayoutSpec` replaces the active one; a refusal is logged once per
     * distinct text.
     *
     * **The regions** of the layout just built (`MakeTableRegions`) go to the canvases of `kRegionNodes`. The `Table` node is first in the
     * scene, so they are set before those canvases lay out in the same frame; a node the scene lacks is looked for again at the next draw.
     *
     * **No peeking.** While the hand is played only the viewer's concealed tiles (`TableSession::Viewer()`) are face up, none under the
     * hot-seat cover. When the hand is over, and all the time while no seat is a person (a watched match), all four hands are face up: a
     * choice of the screen only, the computers still decide from `MakeTableView`. The marks follow the viewer, so a spectator has none.
     * The highlight behind a hand is the seat whose turn it is; in a claim window that is the discarder, never the seat asked to claim
     * first, which would tell a person that this computer holds a claim.
     *
     * **Marks and flights.** The marks that blink run on `SceneUpdateEvent` time, which stops with the table pause, and change no game
     * state. A new `TableSnapshot` against the last one (`PlanFlights`) makes one batch of flights, whoever made the moves (a click, a
     * computer, the control API). A flight carries no face: the slot is drawn by the same code and the same face rule at an interpolated
     * rectangle, so nothing is face up that the slot would not show, and the marks of the slot follow it. The frame of a declared calling
     * hand and the keeps-waiting bars read public data (`PlayerHand::callingDeclared`), never a tile; the bars do not depend on the
     * `Hints` switch.
     *
     * **Playing.** A person acts while `TableSession::ActorIsPerson()` and no cover is up; a refused action changes nothing. The tap regime
     * (`UsesTapToSelect`) turns the first click on a tile into a selection that holds only in the discard turn it was made in.
     *
     * **Buttons.** The canvas `Actions` and `ActionsCompact` hold the same buttons by name and exactly one is shown (`NeedsCompactActions`);
     * `LegalActions` decides every frame which buttons show and the answers come as `InputEvents::ActionEvent`s.
     *
     * **Pace.** While a computer has to act the component plays one step every `kComputerStepSeconds` of `SceneUpdateEvent` time, and
     * nothing while a person acts, the cover is up, the table is paused or the hand is over. A claim window and the discard after it are
     * one pause, so between computers the time from a discard to the next is the same whether or not a window opened; a person who acts
     * next gets the turn at once when no window opened and after the pause when one did.
     */
    class MahjongTableComponent : public IComponent {
        BE_CLASS(MahjongTableComponent)
    public:
        MahjongTableComponent() = default;
        ~MahjongTableComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// A computer seat's decision is being computed in the background (only a search seat; a decision with no search is never "thinking").
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

        /// The tiles of the viewer's hand framed by the last `OnDraw` for the offered chow (`ChowTilesTaken`); none under the cover, with no
        /// chow on offer or with several and the pointer on none.
        [[nodiscard]] const ChowTakes& GetChowTakes() const noexcept {
            return _chowTakes;
        }

        /// Draws with `texture` (made from pixels, `IRenderer::CreateTexture()`) instead of the atlas from the file; a test hands one.
        void SetAtlasTexture(IntrusivePtr<ITexture> texture);

        /// The same for the emblem atlas of the seat plates (`Table/EmblemAtlas.h`); an empty texture draws the plates without emblems.
        void SetEmblemTexture(IntrusivePtr<ITexture> texture);

        /// The index (in the viewer's concealed tiles) of the tile drawn raised by the last `OnDraw`; the hints draw their marks on its `RaisedRect`.
        [[nodiscard]] Optional<int> SelectedHandIndex() const noexcept {
            return _drawnSelected;
        }

        /// The capacities of the scratch lists the panels are meshed into (`DrawPanel`): a steady frame leaves both as they were.
        struct PanelScratch {
            size_t vertices = 0;
            size_t indices = 0;

            [[nodiscard]] bool operator==(const PanelScratch&) const = default;
        };
        [[nodiscard]] PanelScratch PanelScratchCapacity() const noexcept {
            return PanelScratch{_panelVertices.capacity(), _panelIndices.capacity()};
        }

        /// The layout of the last `OnDraw` in the view units of the scene; empty before the first draw and while nothing is drawn.
        [[nodiscard]] const TableLayout& GetLayout() const noexcept {
            return _shown.layout;
        }

        /// The length of a flight at `Normal` speed (the game's speed scales it); 0 or less plans no flight.
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

        /// Whether the score table is held back so that it does not cover the flying tile and the marks: a mahjong on a discard beat the claim
        /// of a person in a window this component settled itself (never one a control client settled), for `kClaimLostSeconds` of table time.
        /// The HUD shows `HandOver` only when this is false.
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

        /// The felt texture for `renderer`, made on first use and again for another renderer (`GetId()`) or when the felt colour of the palette
        /// is not the one it was made with; null when the renderer gave none.
        [[nodiscard]] ITexture* FeltTexture(IRenderer& renderer);
        /// Reads `_palette` and the back set of the current theme again when the version of the themes moved: one integer compare in a steady frame.
        void RefreshPalette();
        /// Frees the felt texture, with the GPU idle when the renderer is the one that made it.
        void ReleaseFelt();

        /// Offers the config `TableLayout` to `OfferTableLayoutSpec` (the node is never kept: a reload invalidates it); a refusal is logged once
        /// per distinct text. False when the game has no such config.
        bool OfferTableLayoutConfig();
        void LoadTableLayoutSpec();
        void PollTableLayoutSpec();

        /// The game when a person has to act and can (Playing, a person to act, no cover), else null.
        [[nodiscard]] MahjongGame* PersonToAct() const;
        /// Whether a click on a hand tile only selects it (`UsesTapToSelect`): `_touchSeen`, or a hand tile under the touch minimum in the
        /// window of the last draw (the window as it is now before any draw).
        [[nodiscard]] bool TapToSelect() const;
        /// The size part of the regime for the renderer's output as it is now; `OnDraw` keeps it.
        [[nodiscard]] bool SmallHandTileNow() const;
        /// What a selection must still be to count: the regime on, a person to act in the discard turn, hand and match it was made in, at the
        /// session version it was made at, and the tile at its index still the one selected.
        [[nodiscard]] bool SelectionHolds(const MahjongGame& game) const;
        /// Drops the selection that no longer holds (any, with no game).
        void RefreshSelection(const MahjongGame* game);
        /// A click on a hand tile: `StepTap` says what it does. Needs `PersonToAct()`.
        void TapHandTile(MahjongGame& game, const Optional<int>& hit);
        /// What the person to act at @p actor may do now: `LegalActions` of the hand, in a room the server's own list (`OnlineTable::actions`,
        /// without `next_hand`: a display hand lists nothing). Valid until the next call.
        [[nodiscard]] const Vector<HandAction>& LegalFor(const MahjongGame& game, Seat actor);
        /// Plays @p action for the person to act: through the session, or in a room as the first entry of the server's list that equals it
        /// (`OnlineClient::Act`). An action the server does not list is refused (`NotYourTurn`), which is a click that is out of date.
        HandStatus Perform(MahjongGame& game, const HandAction& action);
        /// The buttons for what the person to act can do now (all hidden when none can); clears `_declareNext` when `Declare` is not offered.
        [[nodiscard]] ActionButtons CurrentButtons(const MahjongGame& game);
        /// The turn the `Declare` flag is set on: the acting seat and the discards so far (every turn follows a discard, so a control client
        /// that played for the person ends the turn too).
        struct DeclareTurn {
            Seat seat = kNoSeat;
            size_t discards = 0;

            bool operator==(const DeclareTurn&) const = default;
        };
        [[nodiscard]] static DeclareTurn TurnOf(const MahjongGame& game);
        /// The hand whose `Moves()` the `_movesSeen` entries belong to; a new match is not the old one however many moves it has made.
        struct SeenHand {
            const MahjongGame* game = nullptr;
            int handsPlayed = -1;
            size_t settled = 0;
            uint64_t starts = 0;  // `TableSession::StartCount()`

            bool operator==(const SeenHand&) const = default;
        };
        [[nodiscard]] static SeenHand SeenHandOf(const MahjongGame& game);
        /// Starts the hold of the score table (`HandOverHeld`) when the claim window settled since `serialBefore` ended the hand with a
        /// mahjong that beat the claim of a person; called only after an action the component carried out itself.
        void HoldScoreTableIfBeaten(const MahjongGame& game, int serialBefore);
        /// The pace of the computers, once a frame: begins a step at the start of its pause and applies it at the pause end (when a running
        /// search is slower, when it arrives, never earlier); the rest of a claim window and the discard after it follow with no pause.
        /// Drops the step in progress when it can no longer be applied.
        void PlayComputers(MahjongGame& game, float deltaTime);
        /// Forgets the step in progress: the task is cancelled and nothing is applied. The pause already spent stays when `Version()` or
        /// `StartCount()` moved, so the next frame begins the step again without waiting twice.
        void DropComputerStep();
        /// Begins the next step of the actor and hands it to `_thinking`; false when there is none.
        bool BeginComputerStep(TableSession& session, const HandModel& hand);
        /// Sets `selectedTileText` to the name of `tile` (empty for none), only when the tile or the language changed.
        void PublishSelectedText(Tile tile);
        /// The name on hover, from the layout just built: `tileHoverText` and the plate `TileHoverPlate`, shown exactly while the pointer is
        /// over a face-up tile (`Table/TileHover.h`). `game` null: nothing is hovered.
        void PublishHover(const MahjongGame* game, float viewWidth, float viewHeight);
        /// Publishes the regions of `layout` to the canvases of `kRegionNodes`, only when a rectangle or a name moved; a withdrawn name is removed.
        void PublishRegions(const TableLayout& layout);
        /// Pushes the buttons, their tile pictures, the hint of the hovered button, the choice between `Actions` and `ActionsCompact` and the
        /// cover to the canvases, only what changed.
        void PublishScreen(const MahjongGame* game);

        /// The entries of `Moves()` not seen before the frame, from index `first`; `restarted` when the hand, match or game is not the one
        /// seen before (what is on the table then is not news).
        struct NewMoves {
            bool restarted = false;
            size_t first = 0;
            bool setLaid = false;  // a set was laid among them: its highlight has just started
        };

        /// Starts the highlight of the set that the unseen `Moves()` laid (the last one that made a set, so a claim and the discard after it
        /// in one step still show the set) and the marks of the claims that lost in the window settled since the last frame (by its serial).
        NewMoves FollowMoves(const MahjongGame& game);

        /// Plans the flights of `_shown` against `before`, the frame drawn last: a new batch ends the running flights at their targets; a
        /// restart, another viewer, view size, shape or layout profile starts with none.
        void PlanBatch(const TableSnapshot& before, const NewMoves& news, const HandModel& hand);

        /// A panel (a seat plate, the centre panel) is one `DrawGeometry` into the member scratch lists: `DrawPanel` meshes shadow, fill, border
        /// and highlight of `rect` (with `turn`, also the turn tint and frame); `DrawPanelTint` and `DrawPanelFrame` add one more layer over a
        /// plate. `unit` is the table's `LayoutScale`: radius and widths are in design units.
        void DrawPanel(IRenderer& renderer, const Rect& rect, float unit, bool turn);
        void DrawPanelTint(IRenderer& renderer, const Rect& rect, float unit, const Color& color);
        void DrawPanelFrame(IRenderer& renderer, const Rect& rect, float unit, float width, const Color& color);
        /// Empties the scratch lists and gives them their first capacity, so only the first frame allocates them.
        void BeginPanelMesh();

        [[nodiscard]] float Seconds(float baseSeconds) const noexcept {
            return ScaledSeconds(baseSeconds, _speed);
        }

        /// Ends the running flights where they are; a highlight that waited for them starts.
        void EndFlights();

        ResourceHandle<ITexture> _atlas;
        ResourceHandle<ITexture> _emblems;               // `Table/EmblemAtlas.h`
        IntrusivePtr<ITexture> _felt;                    // the felt made from pixels (`Table/TableLook.h`); empty when the renderer gave none
        uint64_t _feltRenderer = 0;                      // `IRenderer::GetId()` of the renderer `_felt` was asked of, 0 for none yet
        Color _feltColor;                                // the felt colour `_felt` was made with
        TablePalette _palette;                           // the colours the table paints in, of the theme at `_paletteVersion`
        uint64_t _paletteVersion = 0;                    // `UIThemes::GetVersion()` that `_palette` was read at; 0 for never
        TableSnapshot _shown;                            // what the last `OnDraw` drew
        TableSnapshot _previous;                         // `OnDraw` swaps the two and refills one, so their lists keep their capacity
        TableInput _input;                               // kept for the capacity of its lists
        Vector<TileFlight> _flights;                     // the tiles in the air, flying to slots of `_shown.layout`
        float _flightElapsed = 0.0f;                     // seconds since the batch of `_flights` was planned
        float _flightSeconds = kTileFlightSeconds;       // at Normal speed: `Seconds(_flightSeconds)` is the length of a flight
        AnimationSpeed _speed = kDefaultAnimationSpeed;  // the game's speed as of the last update or draw
        ActionButtons _published;                        // what the canvases show; the scene file starts with every button hidden
        /// The back and the wall stack of the theme at `_paletteVersion` (`RefreshPalette`).
        const TileBackSet* _backSet = &kTileBackSets[0];
        /// What the buttons were made from: `LegalActions` allocates, and the model moves only through the session, which bumps its version.
        struct ButtonsKey {
            const MahjongGame* game = nullptr;
            bool online = false;  // the display session of a room has versions of its own
            uint64_t version = 0;
            uint64_t starts = 0;
            Seat actor = kNoSeat;

            bool operator==(const ButtonsKey&) const = default;
        };
        Optional<ButtonsKey> _offeredKey;     // the key of `_offered`; none while no person acts
        ActionButtons _offered;               // `MakeActionButtons` of the person to act, made again only when the key moves
        Vector<HandAction> _legal;            // `LegalFor`, a member so that it keeps its capacity
        Optional<ButtonsKey> _keepsKey;       // the key `_keeps` was made for; none while the keeps-waiting bars are not asked for
        Array<bool, kTileIdCount> _keeps{};   // `KeepsWaitingDiscards` of the viewer's own turn
        Optional<ButtonKind> _hoveredButton;  // the shown button of `Actions` under the pointer at the last publication
        ChowTakes _chowTakes;
        StringView _hintKey;               // the key of the sentence in `actionHint`: a literal of `SpecOf`, which lives as long as the program
        uint64_t _hintStringsVersion = 0;  // the string table version that sentence was looked up at
        bool _handoffShown = false;
        bool _compactActions = false;  // the scene file starts with `Actions` shown
        bool _declareNext = false;
        bool _touchSeen = false;             // a click made by a touch or a pen was seen: the tap regime stays on from then
        Optional<bool> _drawnSmallHandTile;  // `SmallHandTileNow()` of the last draw: what the regime reads between draws
        Optional<int> _selected;             // the selected hand tile (an index of the concealed tiles) and what it holds for:
        Tile _selectedTile;                  // the tile,
        DeclareTurn _selectedTurn;           // the turn,
        uint64_t _selectedVersion = 0;       // the session version
        SeenHand _selectedHand;              // and the hand and match
        Optional<int> _drawnSelected;        // the tile the last `OnDraw` raised
        bool _selectedLabelShown = false;    // the label `SelectedTile` is shown
        Tile _textTile;                      // the tile `selectedTileText` names now (invalid for none) and the string table version it was worded at
        uint64_t _textStringsVersion = 0;
        float _pointerX = 0.0f;  // the pointer in output pixels, valid while `_pointerSeen`; converted to the view when used, so a resized
        float _pointerY = 0.0f;  // window keeps it where it is on the screen
        bool _pointerSeen = false;
        bool _hoverShown = false;  // the plate `TileHoverPlate` is shown
        Tile _hoverTile;           // the tile `tileHoverText` names now (invalid for none) and the string table version it was worded at
        uint64_t _hoverStringsVersion = 0;
        DangerMark _hoverDanger = DangerMark::None;  // the danger mark `tileHoverDanger` words now
        DeclareTurn _declareTurn;                    // the turn `_declareNext` was set on
        float _computerClock = 0.0f;                 // seconds a computer has been waiting to play
        /// The step of a computer begun and not applied yet: the session `Version()` and `StartCount()` it was begun at.
        struct StepInProgress {
            uint64_t version = 0;
            uint64_t starts = 0;
        };
        Optional<StepInProgress> _step;
        ComputerThinking _thinking;  // runs the search of `_step`; ends with the component
        /// After the first step of a pause that was in a claim window: the other computers of the window and the discard after it go
        /// without another pause. `_burstSerial` is the window serial before the first step (for `HoldScoreTableIfBeaten`).
        enum class Burst : uint8_t { None, Window, Discard };
        Burst _burst = Burst::None;
        bool _firstInWindow = false;  // the step in progress is the first of its pause and began in a claim window
        int _burstSerial = 0;
        SeenHand _seenHand;
        size_t _movesSeen = 0;
        Seat _highlightSeat = kNoSeat;  // the set that blinks: the meld `_highlightMeld` of this seat
        int _highlightMeld = -1;
        float _highlightLeft = 0.0f;           // seconds of the highlight left, 0 for none
        bool _highlightWaits = false;          // the tiles of the set are still in the air: the highlight does not run and is not drawn until they land
        int _windowSeen = 0;                   // the serial of the last claim window looked at (`ClaimWindowResult::serial`), 0 for none
        Seat _lostWinner = kNoSeat;            // the seat that won the window whose losers are marked
        Array<bool, kSeatCount> _lostSeats{};  // the seats whose claim lost in it
        float _lostLeft = 0.0f;                // seconds of the marks of that window left, 0 for none
        float _handOverHoldLeft = 0.0f;        // seconds the score table is still held back, 0 for none
        SeenHand _handOverHoldOf;              // the hand it is held for: a hold is void in any other

        Vector<GeometryVertex> _panelVertices;  // the scratch of `DrawPanel`: cleared before each panel, never made again
        Vector<int> _panelIndices;

        Vector<TableRegion> _regions;                                              // the regions of the last layout (refilled every draw, for its capacity)
        Vector<TableRegion> _publishedRegions;                                     // what the canvases of `_regionCanvases` hold,
        Vector<PoolString> _regionNames;                                           // and the interned names of them, in the same order
        Array<IntrusivePtr<UICanvasComponent>, kRegionNodeCount> _regionCanvases;  // the canvases of `kRegionNodes`, null for a node the scene lacks

        Optional<std::filesystem::file_time_type> _layoutStamp;  // the stamp of the config `TableLayout` file at the last look, none when it has none
        std::chrono::steady_clock::time_point _layoutPolled;     // the last look
        String _layoutRefused;                                   // the text of the refusal logged last, empty when the last offer was not refused
        static MahjongTableComponent* s_active;
    };

}  // namespace BECore::Mahjong
