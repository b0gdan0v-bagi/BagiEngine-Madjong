#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/Wall.h>

namespace BECore::Mahjong {

    /// No seat (the winner of a drawn hand).
    inline constexpr Seat kNoSeat = -1;

    /**
     * @brief Where a hand stands. A draw is automatic, so a seat only ever sees one of these.
     *
     * - `Discard`: the current seat holds one tile too many (it has just drawn, or just claimed) and acts.
     * - `Claim`: a discard is pending and the other seats decide (the claim window; `CurrentSeat()` is the discarder).
     * - `RobKong`: an upgraded kong is pending and the other seats may only claim mahjong or pass (`CurrentSeat()` is the kong's
     *   owner, the tile is `PendingTile()`).
     * - `Finished`: `Result()` holds the outcome.
     */
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
              IllegalKong      // no four equal concealed tiles to declare, or no open pung of that kind with the fourth tile in hand
    )

    /// Tiles a seat holds for a claim on a discard: two equal ones for a pung, three for a kong (a chow takes two other tiles too).
    inline constexpr int kPungTilesFromHand = 2;
    inline constexpr int kKongTilesFromHand = 3;

    /// What a seat claims a discard for. A chow takes the two other tiles of a sequence, a pung two equal tiles, a kong three, mahjong
    /// completes the hand.
    CORE_ENUM(ClaimKind, uint8_t, Chow, Pung, Kong, Mahjong)

    /// One way a seat can claim the pending discard. `chowFirst` is the lowest tile of the sequence for a chow (a seat may have
    /// several), invalid for the other kinds.
    struct ClaimOption {
        /// A placeholder: `ClaimOption{}` stands for "no claim" and its kind is never read, so the default is just the first kind.
        ClaimKind kind = ClaimKind::Chow;
        Tile chowFirst;

        bool operator==(const ClaimOption&) const = default;
    };

    /// Where a seat stands in the claim window: not in it, still to decide, or decided (passed or claimed).
    CORE_ENUM(ClaimStatus, uint8_t, NotInWindow, Waiting, Passed, Claimed)

    /// A win or a drawn hand.
    CORE_ENUM(ResultKind, uint8_t, Win, Drawn)

    /// How the winning tile came: from a discard, from the seat's own draw, or from the kong another seat was adding to.
    CORE_ENUM(WinSource, uint8_t, Discard, SelfDraw, RobbedKong)

    /**
     * @brief The outcome of a hand, with everything stage 3 (scoring) needs.
     *
     * A drawn hand has `kind == Drawn` and no winner (`winner` and `from` are `kNoSeat`). For a win: `winner` completed the hand with
     * `winningTile`; `source` says how; `from` is the seat that discarded the tile (`Discard`), the seat that owns the kong
     * (`RobbedKong`) or the winner itself (`SelfDraw`). `loose` is set when the tile came from the loose reserve (after a bonus tile
     * or a kong); `lastLiveTile` when the live wall was empty after it (the last tile before the dead wall, a discard after it
     * included).
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

    /// What an action returns: nothing, or why it was refused.
    using HandStatus = std::expected<void, HandError>;

    /**
     * @brief One hand of mahjong from the deal to a win or a drawn hand. Plain C++: no scene, no renderer, no random numbers (the
     *        wall is built outside).
     *
     * `Begin` deals and gives East the first tile; from then on every transition that ends in a draw performs it, so the model is
     * always waiting for an action or `Finished`. A drawn bonus tile (when the set has them) is laid open at once and replaced from
     * the loose reserve, again and again; an empty live wall when a draw is due, or an empty reserve when a replacement is due, ends
     * the hand as `Drawn`.
     *
     * Actions return `HandStatus` and never assert on a bad input: a refused action leaves the model exactly as it was. Nothing is
     * compulsory: a seat that could declare a win may discard instead.
     *
     * The claim window: a discard opens `Phase::Claim` only when at least one other seat has a legal claim (`LegalClaims`), otherwise
     * the next seat draws at once. Each seat in the window claims once (`Claim`) or passes (`Pass`); when every one has decided the
     * best claim wins: mahjong, then pung or kong, then chow; two mahjong claims go to the seat farther counter-clockwise from the
     * discarder. A chow is only for the seat after the discarder. A claimed tile leaves the pile and goes into an open meld (or into
     * the winning hand); after a chow or pung the claimer discards, after a kong it draws a loose tile first, and the turn that follows
     * goes to the seat after the claimer. If all pass, the seat after the discarder draws.
     *
     * The kongs: besides the open kong of a claim, a seat on its turn declares a concealed kong from four equal tiles of its hand
     * (`DeclareConcealedKong`) or upgrades an open pung with the fourth tile (`UpgradeKong`). Each draws a loose tile (the reserve is
     * refilled from the end of the live wall) and the seat then discards. An upgraded kong can be robbed, only to win: when another
     * seat has declared a calling hand that the tile completes, `Phase::RobKong` opens and those seats `Claim(Mahjong)` or `Pass`;
     * the robber farthest counter-clockwise from the kong's owner wins, taking the tile (the pung stays a pung in the owner's hand);
     * if nobody robs, the kong is completed. A win on a tile from the loose reserve has `HandResult::loose`.
     *
     * Where the steps join: `AfterDiscard` is the one place that decides what follows a discard, and `Receive` is the one place that
     * gives a seat a tile and ends a hand that cannot.
     */
    class HandModel {
    public:
        /// Starts a hand on `wall`: deals 13 tiles to each seat (East first, `DealHands`) and gives East the first tile. `seatWinds` is
        /// indexed by seat and holds East exactly once; `settings.flowers` says whether bonus tiles are replaced.
        void Begin(const MahjongSettings& settings, Wall wall, const SeatWinds& seatWinds, Wind roundWind);

        [[nodiscard]] Phase GetPhase() const noexcept {
            return _phase;
        }
        /// The seat that acts in `Phase::Discard`: the one that holds the extra tile. In `Phase::Claim` it is the discarder; the
        /// pending tile is `Discards().back()`.
        [[nodiscard]] Seat CurrentSeat() const noexcept {
            return _current;
        }
        /// The hand of a seat (0..3).
        [[nodiscard]] const PlayerHand& GetHand(Seat seat) const noexcept {
            return _hands[seat];
        }
        /// The shared pile in the order discarded.
        [[nodiscard]] const eastl::vector<DiscardEntry>& Discards() const noexcept {
            return _discards;
        }
        [[nodiscard]] const Wall& GetWall() const noexcept {
            return _wall;
        }
        /// The outcome once the phase is `Finished`.
        [[nodiscard]] const std::optional<HandResult>& Result() const noexcept {
            return _result;
        }
        [[nodiscard]] const MahjongSettings& GetSettings() const noexcept {
            return _settings;
        }
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

        /**
         * @brief The current seat discards `tile`; the hand goes on to the next seat's draw.
         *
         * `declareCalling` declares the calling hand with this discard: the 13 tiles that stay have to be waiting (`WinningTiles` of
         * the seat, its melds' copies counted), otherwise `NotWaiting`; a seat that has declared cannot declare again
         * (`AlreadyCalling`). The declaration stores the number of discards the seat had made before this one in
         * `callingAtDiscardCount`, so "declared in the first hand" is `== 0`. Errors: `WrongPhase`, `NotYourTurn`, `TileNotInHand`.
         */
        HandStatus Discard(Seat seat, Tile tile, bool declareCalling = false);

        /**
         * @brief The current seat wins with the tile it has just drawn (a self-draw win).
         *
         * Needs the calling-hand declaration (`NotCalling`) and a winning shape (`CannotWin`, which also covers a seat that did not
         * draw its tile, such as after a claim). Ends the hand with `WinSource::SelfDraw`.
         */
        HandStatus DeclareWin(Seat seat);

        /**
         * @brief The current seat declares a concealed kong: four equal tiles of its hand become a concealed `Meld`.
         *
         * The seat then draws a loose tile (a bonus tile is laid open and replaced, as everywhere) and discards; the declaration is
         * never compulsory, four equal tiles left in the hand are no kong. Errors: `WrongPhase`, `NotYourTurn`, `IllegalKong` (the
         * hand does not hold four of `tile`).
         */
        HandStatus DeclareConcealedKong(Seat seat, Tile tile);

        /**
         * @brief The current seat adds the fourth tile of an open pung of `tile` to it, making an upgraded kong.
         *
         * When another seat has declared a calling hand that `tile` completes, `Phase::RobKong` opens and the kong waits for those
         * seats (`Claim(seat, ClaimKind::Mahjong)` or `Pass`); otherwise the kong is completed at once: the meld becomes a kong, the
         * seat draws a loose tile and discards. Errors: `WrongPhase`, `NotYourTurn`, `IllegalKong` (no open pung of `tile`, or the
         * fourth tile is not in the concealed hand).
         */
        HandStatus UpgradeKong(Seat seat, Tile tile);

        /**
         * @brief A seat in the claim window (or in the robbing window) claims the pending tile.
         *
         * `chowFirst` is the lowest tile of the chosen sequence (only for `ClaimKind::Chow`). The claim is stored; once every seat in
         * the window has decided, the best claim is carried out (see the class comment). Errors: `WrongPhase`; `NotYourTurn` (not a
         * seat, the discarder or the kong's owner, or a seat with no legal claim); `AlreadyDecided`; `NotCalling` / `CannotWin` for a
         * mahjong claim without the declaration or without a winning hand; `IllegalClaim` for any other claim the seat cannot make
         * (in `Phase::RobKong` that is every claim but mahjong).
         */
        HandStatus Claim(Seat seat, ClaimKind kind, Tile chowFirst = Tile{});

        /// A seat in the window lets the tile go (a kong is then completed). Always allowed while the seat has not decided. Errors:
        /// `WrongPhase`, `NotYourTurn`, `AlreadyDecided`.
        HandStatus Pass(Seat seat);

        /// The seats in the window that have not decided yet, in seat order. Empty outside `Phase::Claim` and `Phase::RobKong`.
        [[nodiscard]] eastl::vector<Seat> PendingClaimSeats() const;
        /// The claims `seat` can make on the pending tile (a pass is always possible besides). Empty for a seat that is not in the
        /// window or has decided, and outside `Phase::Claim` and `Phase::RobKong`; in `Phase::RobKong` at most a mahjong claim.
        [[nodiscard]] eastl::vector<ClaimOption> LegalClaims(Seat seat) const;
        /// The tile the window is about: the discard in `Phase::Claim`, the kong's fourth tile in `Phase::RobKong`.
        [[nodiscard]] Tile PendingTile() const noexcept {
            return _pendingTile;
        }
        [[nodiscard]] ClaimStatus GetClaimStatus(Seat seat) const noexcept {
            return _claimStatus[seat];
        }

    private:
        /// Gives `seat` its tile: `first` (the draw that was due, nullopt when there was none), then replacements from the loose
        /// reserve while the tile is a bonus tile. `loose` says that `first` came from the reserve. Ends the hand as drawn when no
        /// tile is left; otherwise `seat` holds the tile and the phase is `Discard`.
        void Receive(Seat seat, std::optional<Tile> first, bool loose);
        /// The regular draw of `seat` (from the live wall).
        void StartTurn(Seat seat);
        /// What follows the discard of `tile` by `seat`: the claim window when a seat can claim, otherwise the next seat draws.
        void AfterDiscard(Seat seat, Tile tile);
        /// Opens the window of `phase` (`Claim` or `RobKong`) when some seat other than `from` has a legal claim on `tile`; false
        /// when none has.
        bool OpenWindow(Phase phase, Seat from, Tile tile);
        /// Carries out the best claim once every seat in the window has decided (or deals on when all passed).
        void ResolveClaims();
        /// The seat whose claim wins: the best priority, a tie going to the seat farther counter-clockwise from `_current`;
        /// `kNoSeat` when nobody claimed.
        [[nodiscard]] Seat BestClaimant() const;
        /// Ends the robbing window: `robber` takes `_pendingTile` from the kong of `owner` and wins, or (`kNoSeat`) the kong is completed.
        void ResolveRobbery(Seat owner, Seat robber);
        /// Turns the open pung of `tile` of `seat` into a kong with the fourth tile of its hand, then the loose-tile draw.
        void CompleteKong(Seat seat, Tile tile);
        /// Lays `claimer`'s claimed meld: removes the tiles it uses from its hand and puts the meld down.
        void ExposeClaim(Seat claimer, const ClaimOption& claim, Seat from, Tile tile);

        void FinishDrawn();
        void FinishWin(Seat winner, Tile tile, WinSource source, Seat from, bool loose);

        MahjongSettings _settings;
        SeatWinds _seatWinds{};
        Wind _roundWind = Wind::East;
        Seat _east = 0;
        Wall _wall;
        eastl::array<PlayerHand, kSeatCount> _hands;
        eastl::vector<DiscardEntry> _discards;
        Phase _phase = Phase::Finished;
        Seat _current = 0;
        /// The tile `_current` has just drawn (invalid when the extra tile is not a draw, e.g. after a claim) and whether it came from
        /// the loose reserve.
        Tile _drawnTile;
        bool _drawnLoose = false;
        /// The claim window: where each seat stands and what the seats that claimed asked for.
        eastl::array<ClaimStatus, kSeatCount> _claimStatus{};
        eastl::array<ClaimOption, kSeatCount> _claims;
        /// The tile the open window is about (`PendingTile()`).
        Tile _pendingTile;
        std::optional<HandResult> _result;
    };

}  // namespace BECore::Mahjong
