#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/Wall.h>

namespace BECore::Mahjong {

    /// Where a hand stands. A draw is automatic, so a seat only ever sees a phase it can act in. `Discard`: `CurrentSeat()` holds one
    /// tile too many. `Claim` / `RobKong`: a window is open on `PendingTile()`; `CurrentSeat()` is the discarder / the kong's owner.

    CORE_ENUM(Phase, uint8_t, Discard, Claim, RobKong, Finished)

    /// Why an action was refused. A refused action changes nothing.
    CORE_ENUM(HandError, uint8_t,
              WrongPhase,      // the action does not belong to the phase the hand is in
              NotYourTurn,     // the seat is not the one to act (or is not a seat)
              TileNotInHand,   // the tile is not among the concealed tiles
              NotWaiting,      // a calling hand is declared with a discard that leaves a hand that is not waiting
              AlreadyCalling,  // the seat has declared its calling hand already
              NotCalling,      // winning needs the calling-hand declaration
              CannotWin,       // the declaration is made but the hand is not a winning one (or the tile was not drawn)
              IllegalClaim,    // the claim is not possible for the seat (claim window)
              AlreadyDecided,  // the seat has claimed or passed already (claim window)
              IllegalKong,     // no four equal concealed tiles to declare, or no open pung of that kind with the fourth tile in hand
              BadSeatWinds     // `Begin` was given seat winds without exactly one East
    )

    /// Tiles a seat holds for a claim on a discard (a chow takes two other tiles too).
    inline constexpr int kPungTilesFromHand = 2;
    inline constexpr int kKongTilesFromHand = 3;

    /// What a seat claims a discard for.
    CORE_ENUM(ClaimKind, uint8_t, Chow, Pung, Kong, Mahjong)

    /// One way a seat can claim the pending discard. `chowFirst` is the lowest tile of the sequence for a chow (a seat may have
    /// several), invalid for the other kinds.
    struct ClaimOption {
        /// `ClaimOption{}` stands for "no claim"; its kind is never read.
        ClaimKind kind = ClaimKind::Chow;
        Tile chowFirst;

        bool operator==(const ClaimOption&) const = default;
    };

    /// The kinds of action a seat can take (`HandAction`).
    CORE_ENUM(ActionKind, uint8_t, Discard, DeclareWin, ConcealedKong, UpgradeKong, Claim, Pass)

    /**
     * @brief One thing a seat can do, as data: the item of `HandModel::LegalActions` and the argument of `HandModel::Apply`.
     *
     * Only the fields of `kind` count (`tile` and `declareCalling` for `Discard`, `tile` for the kongs, `claim` and `chowFirst` for
     * `Claim`); `LegalActions` leaves the others empty, so two equal actions compare equal.
     */
    struct HandAction {
        ActionKind kind = ActionKind::Pass;
        Tile tile;
        bool declareCalling = false;
        ClaimKind claim = ClaimKind::Chow;
        Tile chowFirst;

        bool operator==(const HandAction&) const = default;

        [[nodiscard]] static HandAction MakeDiscard(Tile tile, bool declareCalling = false) {
            return {ActionKind::Discard, tile, declareCalling, ClaimKind::Chow, Tile{}};
        }
        [[nodiscard]] static HandAction MakeDeclareWin() {
            return {ActionKind::DeclareWin, Tile{}, false, ClaimKind::Chow, Tile{}};
        }
        [[nodiscard]] static HandAction MakeConcealedKong(Tile tile) {
            return {ActionKind::ConcealedKong, tile, false, ClaimKind::Chow, Tile{}};
        }
        [[nodiscard]] static HandAction MakeUpgradeKong(Tile tile) {
            return {ActionKind::UpgradeKong, tile, false, ClaimKind::Chow, Tile{}};
        }
        [[nodiscard]] static HandAction MakeClaim(ClaimKind claim, Tile chowFirst = Tile{}) {
            return {ActionKind::Claim, Tile{}, false, claim, chowFirst};
        }
        [[nodiscard]] static HandAction MakePass() {
            return {ActionKind::Pass, Tile{}, false, ClaimKind::Chow, Tile{}};
        }
    };

    /// Where a seat stands in the claim window.
    CORE_ENUM(ClaimStatus, uint8_t, NotInWindow, Waiting, Passed, Claimed)

    /// A win or a drawn hand.
    CORE_ENUM(ResultKind, uint8_t, Win, Drawn)

    CORE_ENUM(WinSource, uint8_t, Discard, SelfDraw, RobbedKong)

    /**
     * @brief The outcome of a hand, with everything scoring needs.
     *
     * A drawn hand has no winner (`winner` and `from` are `kNoSeat`). `from` is the discarder (`Discard`), the kong's owner
     * (`RobbedKong`) or the winner itself (`SelfDraw`). `loose`: the tile came from the loose reserve. `lastLiveTile` only for a
     * self-drawn win on the last live-wall tile: not for a discard, a loose tile (even one whose refill emptied the live wall) or a
     * robbed kong.
     */
    struct HandResult {
        ResultKind kind = ResultKind::Drawn;
        Seat winner = kNoSeat;
        Tile winningTile;
        WinSource source = WinSource::SelfDraw;
        Seat from = kNoSeat;
        bool loose = false;
        bool lastLiveTile = false;
    };

    /// A tile in the shared discard pile, in the order discarded; a tile that is claimed leaves the pile.
    struct DiscardEntry {
        Tile tile;
        Seat seat = 0;
    };

    /// What kind of move a `HandMove` records (`HandModel::Moves()`). The three claims are carried-out ones.
    CORE_ENUM(MoveKind, uint8_t, Discard, ClaimChow, ClaimPung, ClaimKong, ConcealedKong, UpgradeKong)

    /// The `meld` of a `HandMove` that has no meld (a discard).
    inline constexpr int kNoMeld = -1;

    /**
     * @brief One accepted move of the hand, public data only (`HandModel::Moves()`).
     *
     * `meld` indexes `GetHand(seat).melds` (the new meld of a claim, the kong, the pung that grows); `meld` is `kNoMeld` and `from`
     * is `kNoSeat` where they do not apply. A `ConcealedKong` keeps `tile` invalid: it is not public. An `UpgradeKong` is logged when
     * accepted, also when the kong is then robbed.
     */
    struct HandMove {
        MoveKind kind = MoveKind::Discard;
        Seat seat = 0;
        Tile tile;
        Seat from = kNoSeat;
        int meld = kNoMeld;
        bool declaredCalling = false;
    };

    /// A claim that lost in a claim window to a better one (`ClaimWindowResult::beaten`).
    struct LostClaim {
        Seat seat = 0;
        ClaimKind kind = ClaimKind::Chow;
        Tile chowFirst;  // the lowest tile of the sequence for a chow, invalid for the other kinds
    };

    /**
     * @brief How the last `Phase::Claim` window was settled (`HandModel::LastClaimWindow()`), public data only: at a table every claim
     *        is said aloud. `Moves()` holds carried-out claims only, so a claim that lost is only here.
     *
     * `serial` counts the windows settled in this hand from 1, so a front end tells a new result from one it has seen. `winner` is
     * `kNoSeat` when all passed (`winningKind` is then not read). `beaten`: every seat that claimed and did not win, in seat order.
     */
    struct ClaimWindowResult {
        int serial = 0;
        Seat from = kNoSeat;
        Tile tile;
        Seat winner = kNoSeat;
        ClaimKind winningKind = ClaimKind::Chow;
        Vector<LostClaim> beaten;
    };

    /**
     * @brief Everything a `HandModel` keeps, as plain data (`HandModel::Capture`, `HandModel::Restore`).
     *
     * For the search player and the tests: the rules never build a hand from it. The wall is the three lists in draw order (the front
     * of `loose` is the next loose draw, the front of `live` the next regular draw); how it was broken is not kept. The fields are
     * those of the model of the same name; `east` has to be the seat that holds East in `seatWinds`, `claims` holds only the seats
     * whose status is `Claimed` (the others stay `ClaimOption{}`), `result` is set exactly when the phase is `Finished`.
     */
    struct HandState {
        MahjongSettings settings;
        SeatWinds seatWinds{};
        Wind roundWind = Wind::East;
        Seat east = 0;
        Vector<Tile> loose;
        Vector<Tile> live;
        Vector<Tile> dead;
        Array<PlayerHand, kSeatCount> hands;
        Vector<DiscardEntry> discards;
        Vector<HandMove> moves;
        Phase phase = Phase::Finished;
        Seat current = 0;
        Tile drawnTile;
        bool drawnLoose = false;
        Array<ClaimStatus, kSeatCount> claimStatus{};
        Array<ClaimOption, kSeatCount> claims;
        Tile pendingTile;
        Optional<HandResult> result;
        Optional<ClaimWindowResult> lastWindow;
        int windowSerial = 0;
    };

    /// What an action returns: nothing, or why it was refused.
    using HandStatus = std::expected<void, HandError>;

    /**
     * @brief One hand of mahjong from the deal to a win or a drawn hand. Plain C++: no scene, no renderer, no random numbers (the
     *        wall is built outside). The rules in short: `Game/CLAUDE.md` ("HandModel").
     *
     * Every transition that ends in a draw performs it, so the model is always waiting for an action or `Finished`. Actions return
     * `HandStatus` and never assert on a bad input: a refused action leaves the model exactly as it was. Nothing is compulsory.
     *
     * A discard opens `Phase::Claim` only when some other seat has a legal claim (`LegalClaims`), otherwise the next seat draws at
     * once. The window is settled as soon as no seat still to decide could beat the best claim made, so such a seat is not asked.
     * The turn after a claim goes to the seat after the claimer (also after a kong, which draws a loose tile first).
     *
     * An upgraded kong can be robbed, only to win: `Phase::RobKong` opens when another seat has declared a calling hand that the tile
     * completes; the robber takes the tile and the pung stays a pung in the owner's hand.
     *
     * A model that was never begun is a finished, drawn hand, so a result exists exactly when the phase is `Finished`.
     *
     * Where the steps join: `AfterDiscard` is the one place that decides what follows a discard, and `Receive` is the one place that
     * gives a seat a tile and ends a hand that cannot.
     */
    class HandModel {
    public:
        /// Deals (`DealHands`) and gives East the first tile. `seatWinds` is indexed by seat and has to hold East exactly once, otherwise
        /// `BadSeatWinds` and the model stays as it was.
        HandStatus Begin(const MahjongSettings& settings, Wall wall, const SeatWinds& seatWinds, Wind roundWind);

        [[nodiscard]] Phase GetPhase() const noexcept {
            return _phase;
        }
        /// The seat that acts in `Phase::Discard`; in `Phase::Claim` the discarder, in `Phase::RobKong` the kong's owner.
        [[nodiscard]] Seat CurrentSeat() const noexcept {
            return _current;
        }
        /// The hand of a seat; `seat` has to be 0..3 (`IsSeat`).
        [[nodiscard]] const PlayerHand& GetHand(Seat seat) const noexcept {
            return _hands[seat];
        }
        /// The shared pile in the order discarded.
        [[nodiscard]] const Vector<DiscardEntry>& Discards() const noexcept {
            return _discards;
        }
        [[nodiscard]] const Wall& GetWall() const noexcept {
            return _wall;
        }
        /// The tile `seat` has just drawn (a bonus tile already replaced): invalid unless the phase is `Discard`, `seat` is
        /// `CurrentSeat()` and it really drew (not after the claim of a pung or a chow; after a kong it is the loose tile). A front end
        /// calls it only for the seat whose hand it shows face up.
        [[nodiscard]] Tile DrawnTile(Seat seat) const noexcept {
            return _phase == Phase::Discard && seat == _current ? _drawnTile : Tile{};
        }
        /// Every accepted move of this hand in order; `Begin` clears it. Passes, wins and bonus tiles are not in it.
        [[nodiscard]] const Vector<HandMove>& Moves() const noexcept {
            return _moves;
        }
        /// How the last `Phase::Claim` window was settled (the robbing window leaves it as it was); nullopt before the first one.
        /// `Begin` clears it.
        [[nodiscard]] const Optional<ClaimWindowResult>& LastClaimWindow() const noexcept {
            return _lastWindow;
        }
        /// The outcome once the phase is `Finished`.
        [[nodiscard]] const Optional<HandResult>& Result() const noexcept {
            return _result;
        }
        [[nodiscard]] const MahjongSettings& GetSettings() const noexcept {
            return _settings;
        }
        /// The wind of a seat; `seat` has to be 0..3 (`IsSeat`).
        [[nodiscard]] Wind SeatWind(Seat seat) const noexcept {
            return _seatWinds[seat];
        }
        [[nodiscard]] Wind RoundWind() const noexcept {
            return _roundWind;
        }
        /// The dealer.
        [[nodiscard]] Seat East() const noexcept {
            return _east;
        }

        /// The whole state of the hand as data, for `Restore`.
        [[nodiscard]] HandState Capture() const;
        /**
         * @brief Builds a hand in any state from plain data. For the search player and the tests; the rules never call it.
         *
         * Refuses, with a text that names what is wrong, a state no play could reach (tile counts over the whole set, hand sizes for
         * the phase, a claim window that does not fit the pile or the claims, a result that does not fit the phase). Concealed tiles
         * are sorted. The model is then exactly the one that was captured: the same actions are legal and the same play follows.
         */
        [[nodiscard]] static std::expected<HandModel, String> Restore(HandState state);
        /**
         * @brief Builds a model that a client only shows (a table of an online room), from plain data, **without any check**: a tile
         *        may be invalid (a hand or a wall the client is not told). Only a front end builds one; the rules never do
         *        (`Game/CLAUDE.md`, "Display copies").
         *
         * A reader asks `Tile::IsValid` before it reads a tile of a hidden place. The model is **display only** (`IsDisplayOnly`, also
         * a copy of it): `Begin` and every action are refused with `WrongPhase`, `LegalActions` and `LegalClaims` are empty.
         */
        [[nodiscard]] static HandModel ForDisplay(HandState state);
        /// Whether the model was made by `ForDisplay`.
        [[nodiscard]] bool IsDisplayOnly() const noexcept {
            return _displayOnly;
        }

        /**
         * @brief The current seat discards `tile`; the hand goes on to the next seat's draw.
         *
         * `declareCalling` declares the calling hand with this discard: the 13 tiles that stay have to be waiting (`WinningTiles`, its
         * melds' copies counted), otherwise `NotWaiting`; once is enough (`AlreadyCalling`). It stores the seat's earlier discard
         * count in `callingAtDiscardCount`, so "declared in the first hand" is `== 0`. Errors: `WrongPhase`, `NotYourTurn`,
         * `TileNotInHand`.
         */
        HandStatus Discard(Seat seat, Tile tile, bool declareCalling = false);

        /**
         * @brief The current seat wins with the tile it has just drawn (a self-draw win).
         *
         * Needs the calling-hand declaration (`NotCalling`) and a winning shape (`CannotWin`, which also covers a seat that did not
         * draw its tile, such as after a claim).
         */
        HandStatus DeclareWin(Seat seat);

        /**
         * @brief The current seat declares a concealed kong: four equal tiles of its hand become a concealed `Meld`.
         *
         * The seat then draws a loose tile and discards. Errors: `WrongPhase`, `NotYourTurn`, `IllegalKong` (the hand does not hold
         * four of `tile`).
         */
        HandStatus DeclareConcealedKong(Seat seat, Tile tile);

        /**
         * @brief The current seat adds the fourth tile of an open pung of `tile` to it, making an upgraded kong.
         *
         * Opens `Phase::RobKong` when another seat can rob it, otherwise the kong is completed at once. Errors: `WrongPhase`, `NotYourTurn`, `IllegalKong` (no open pung of `tile`, or `tile`
         * is not the tile drawn from the wall this turn).
         */
        HandStatus UpgradeKong(Seat seat, Tile tile);

        /**
         * @brief A seat in the claim window (or in the robbing window) claims the pending tile.
         *
         * `chowFirst` is the lowest tile of the chosen sequence (only for `ClaimKind::Chow`). The claim is stored and carried out when
         * the window is decided (priority: `Game/CLAUDE.md`, "HandModel"). Errors, in this order: `WrongPhase`;
         * `NotYourTurn` (not a seat, the discarder or the kong's owner, or a seat that is not in the window because it has no legal
         * claim; this comes first for every kind of claim); `AlreadyDecided`; for a seat in the window, `NotCalling` / `CannotWin` for
         * a mahjong claim without the declaration or without a winning hand; `IllegalClaim` for any other claim the seat cannot make
         * (in `Phase::RobKong` that is every claim but mahjong).
         */
        HandStatus Claim(Seat seat, ClaimKind kind, Tile chowFirst = Tile{});

        /// Everything `seat` can do now, in this order: the discards (each distinct tile, also with the calling-hand declaration where
        /// `Discard` accepts it), `DeclareWin`, the kongs; or, in a window, the `LegalClaims` plus `Pass`. Empty for a seat that is
        /// not to act and for a number that is no seat.
        [[nodiscard]] Vector<HandAction> LegalActions(Seat seat) const;
        /// Carries out `action` for `seat` through the method of its kind, with its errors. An action that `LegalActions` would not
        /// list (once the fields its kind ignores are left empty) is refused.
        HandStatus Apply(Seat seat, const HandAction& action);

        /// A seat in the window lets the tile go (a kong is then completed). Errors: `WrongPhase`, `NotYourTurn`, `AlreadyDecided`.
        HandStatus Pass(Seat seat);

        /// The seats in the window that have not decided yet, in seat order. Empty outside `Phase::Claim` and `Phase::RobKong`.
        [[nodiscard]] Vector<Seat> PendingClaimSeats() const;
        /// Whether `seat` is one of `PendingClaimSeats()`, without making the list (a frame of the table asks it).
        [[nodiscard]] bool AwaitsClaim(Seat seat) const noexcept;
        /// The claims `seat` can make on the pending tile (a pass is always possible besides). Empty for a seat that is not in the
        /// window or has decided, and outside `Phase::Claim` and `Phase::RobKong`; in `Phase::RobKong` at most a mahjong claim.
        [[nodiscard]] Vector<ClaimOption> LegalClaims(Seat seat) const;
        /// The tile the window is about; invalid outside a window.
        [[nodiscard]] Tile PendingTile() const noexcept {
            return _pendingTile;
        }
        /// `NotInWindow` outside a window and for a number that is no seat.
        [[nodiscard]] ClaimStatus GetClaimStatus(Seat seat) const noexcept {
            return IsSeat(seat) ? _claimStatus[seat] : ClaimStatus::NotInWindow;
        }

    private:
        [[nodiscard]] HandStatus CheckOwnTurn(Seat seat) const;
        /// Gives `seat` its tile: `first` (the draw that was due, nullopt when there was none), then replacements from the loose
        /// reserve while the tile is a bonus tile. `loose` says that `first` came from the reserve. Ends the hand as drawn when no
        /// tile is left; otherwise `seat` holds the tile and the phase is `Discard`.
        void Receive(Seat seat, Optional<Tile> first, bool loose);
        void StartTurn(Seat seat);
        void AfterDiscard(Seat seat, Tile tile);
        /// False (no window) when no seat other than `from` has a legal claim on `tile`.
        bool OpenWindow(Phase phase, Seat from, Tile tile);
        void ResolveClaims();
        /// True when no seat still to decide could change the outcome (a chow cannot beat a mahjong claim, so it is not waited for).
        [[nodiscard]] bool WindowIsDecided() const;
        /// The seat whose claim wins: the best priority, a tie going to the seat farther counter-clockwise from `_current`;
        /// `kNoSeat` when nobody claimed.
        [[nodiscard]] Seat BestClaimant() const;
        /// Ends the robbing window: `robber` takes `tile` from the kong of `owner` and wins, or (`kNoSeat`) the kong is completed.
        void ResolveRobbery(Seat owner, Seat robber, Tile tile);
        /// Turns the open pung of `tile` of `seat` into a kong with the fourth tile of its hand (the drawn one), then the loose-tile
        /// draw. The seat holds both: `UpgradeKong` checked it, and no hand changes while the robbing window is open.
        void CompleteKong(Seat seat, Tile tile);
        void ExposeClaim(Seat claimer, const ClaimOption& claim, Seat from, Tile tile);

        void FinishDrawn();
        void FinishWin(Seat winner, Tile tile, WinSource source, Seat from, bool loose);

        MahjongSettings _settings;
        SeatWinds _seatWinds{};
        Wind _roundWind = Wind::East;
        Seat _east = 0;
        Wall _wall;
        Array<PlayerHand, kSeatCount> _hands;
        Vector<DiscardEntry> _discards;
        Vector<HandMove> _moves;
        Phase _phase = Phase::Finished;
        Seat _current = 0;
        /// The tile `_current` has just drawn (invalid when the extra tile is not a draw, e.g. after a claim).
        Tile _drawnTile;
        bool _drawnLoose = false;
        Array<ClaimStatus, kSeatCount> _claimStatus{};
        Array<ClaimOption, kSeatCount> _claims;
        Tile _pendingTile;
        Optional<ClaimWindowResult> _lastWindow;
        int _windowSerial = 0;
        /// Starts as a drawn hand: a model that was never begun is finished, and a finished hand has a result.
        Optional<HandResult> _result = HandResult{};
        bool _displayOnly = false;
    };

}  // namespace BECore::Mahjong
