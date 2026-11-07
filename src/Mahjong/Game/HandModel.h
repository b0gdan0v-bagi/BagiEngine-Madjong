#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/Deal.h>
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
     * - `Claim`: a discard is pending and the other seats decide (the claim window; not in this model yet).
     * - `RobKong`: an upgraded kong is pending and the other seats may only claim mahjong (not in this model yet).
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
              AlreadyDecided   // the seat has claimed or passed already (claim window)
    )

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
     * Where the next steps join (the claim window, the kongs): `AfterDiscard` is the one place that decides what follows a discard,
     * and `Receive` is the one place that gives a seat a tile and ends a hand that cannot.
     */
    class HandModel {
    public:
        /// Starts a hand on `wall`: deals 13 tiles to each seat (East first, `DealHands`) and gives East the first tile. `seatWinds` is
        /// indexed by seat and holds East exactly once; `settings.flowers` says whether bonus tiles are replaced.
        void Begin(const MahjongSettings& settings, Wall wall, const SeatWinds& seatWinds, Wind roundWind);

        [[nodiscard]] Phase GetPhase() const noexcept {
            return _phase;
        }
        /// The seat that acts in `Phase::Discard`: the one that holds the extra tile.
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

    private:
        /// Gives `seat` its tile: `first` (the draw that was due, nullopt when there was none), then replacements from the loose
        /// reserve while the tile is a bonus tile. `loose` says that `first` came from the reserve. Ends the hand as drawn when no
        /// tile is left; otherwise `seat` holds the tile and the phase is `Discard`.
        void Receive(Seat seat, std::optional<Tile> first, bool loose);
        /// The regular draw of `seat` (from the live wall).
        void StartTurn(Seat seat);
        /// What follows the discard of `tile` by `seat`. Now: the next seat draws. (The claim window opens here.)
        void AfterDiscard(Seat seat, Tile tile);

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
        std::optional<HandResult> _result;
    };

}  // namespace BECore::Mahjong
