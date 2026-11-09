#pragma once

#include <Games/IControllableGame.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    class MahjongGame;

    /// The seed of a `reset` that names none, until the first `reset` that does.
    inline constexpr uint32_t kFirstResetSeed = 1;

    /// What a client sent to `perform_action`: a seat's action in the hand, or `next_hand` (the settlement of a finished hand).
    struct ControlAction {
        bool nextHand = false;
        HandAction hand;
    };

    /// A tile as text in the notation of `Tile.h` ("5p", "Ew", "F1"); JSON null for an invalid tile.
    [[nodiscard]] Json TileToJson(Tile tile);

    /// A seat as a number; JSON null for `kNoSeat` (or any number that is no seat).
    [[nodiscard]] Json SeatOrNull(Seat seat);

    /// A list of tiles in the notation of `Tile.h`.
    [[nodiscard]] Json TilesToJson(const eastl::vector<Tile>& tiles);

    /// One number per seat (points, scores, changes).
    [[nodiscard]] Json SeatScoresToJson(const SeatScores& scores);

    /// {"won", "lines": [{"kind", "tile", "points", "doubles"}], "points", "doubles", "raw", "score", "limitHand": kind | null}: the
    /// breakdown of one seat in `result` of the state.
    [[nodiscard]] Json BreakdownToJson(const ScoreBreakdown& breakdown);

    /// The setup of "PCCC"-style letters (P a person, C a computer), nullopt unless it is exactly four of them; and back.
    [[nodiscard]] std::optional<SeatSetup> ReadSeats(const eastl::string& letters);
    [[nodiscard]] eastl::string SeatsToLetters(const SeatSetup& seats);

    /// The names of the levels for an error text: "easy, normal or hard" (`LevelName` of each, in the order of the enum).
    [[nodiscard]] eastl::string LevelNamesText();

    /// An action as the object `list_actions` gives: {"type": "discard", "tile", "declareCalling"}, {"type": "win"},
    /// {"type": "concealed_kong" | "upgrade_kong", "tile"}, {"type": "claim", "claim": "chow" | "pung" | "kong" | "mahjong", "first" for
    /// a chow}, {"type": "pass"}.
    [[nodiscard]] Json ActionToJson(const HandAction& action);

    /// {"type": "next_hand"}.
    [[nodiscard]] Json NextHandActionToJson();

    /**
     * @brief Reads an action object from a client. Everything about its shape is checked with the helpers of `BECore/Control/Json.h`.
     *
     * `invalid_params` for anything malformed: the object has no `type`, or an unknown one; a tile is missing, is not a string or is not in
     * the notation; `declareCalling` is not a boolean (absent is false); a claim has an unknown `claim`, or a chow has no `first`. A
     * well-formed action is not yet a legal one: the adapter checks it against `LegalActions`. The fields a kind ignores are ignored,
     * so the result equals what `LegalActions` lists (the factories of `HandAction` leave them empty).
     */
    [[nodiscard]] std::expected<ControlAction, ControlError> ActionFromJson(const Json& json);

    /**
     * @brief The external control API of Mahjong: the state of the table, the actions of the person seats, and a way to act.
     *
     * A client plays the **person seats** of the game's `TableSession`; after each of its actions the adapter calls `RunComputers()`, so
     * the computer seats answer at once (the pace of a screen is the table component's business). With no person seat a `reset` and
     * a `next_hand` play the hand to its end.
     *
     * State (`get_state`), built from `MakeTableView` so a client never sees what a computer player could not see: the view of the
     * person who has to act, else of the first person seat. With no person seat at all the client is a spectator: "seat",
     * "seatWind" and "hand" are empty (null, null, []) and `yourTurn` is false; the hands are in `result` once the hand is over.
     * ```
     * {"phase": "playing" | "hand_over" | "match_over",
     *  "seat": 0 | null, "seatWind": "East" | null, "roundWind": "East", "east": 2,
     *  "hand": ["1p", "2p", ...],                        the concealed tiles of "seat", sorted
     *  "turn": {"phase": "discard" | "claim" | "rob_kong" | "finished", "current": 1 | null, "pendingTile": "5p" | null, "yourTurn": true},
     *  "seats": [{"kind": "person" | "computer", "wind", "score", "concealedCount", "melds": [{"kind": "chow" | "pung" | "kong", "tiles",
     *              "concealed", "from": seat | null, "fromDiscard", "claimed"}], "concealedKongs", "bonus", "discardCount",
     *              "calling"} x 4],
     *  "discards": [{"tile", "seat"}, ...], "wall": live tiles left, "handsPlayed", "handsTotal", "version",
     *  "result": {...}}                                  only in hand_over and match_over
     * ```
     * `melds` of every seat are its open melds; the seat of the view also lists its concealed kongs (`concealed`: true). `yourTurn` says
     * that "seat" has actions to choose from (`turn.phase` only says what the hand waits for). In `hand_over` the scores are those
     * before the settlement; `result` has the outcome of the hand (`kind`, `winner`, `winningTile`, `source`, `from`, `loose`,
     * `lastLiveTile`), for every seat its tiles, melds, bonus tiles and `breakdown` ({`won`, `lines`: [{`kind`, `tile`, `points`,
     * `doubles`}], `points`, `doubles`, `raw`, `score`, `limitHand`}), and the `scores` and `deltas`. All four hands are public then.
     *
     * Actions (`list_actions`, each accepted by `perform_action` as it is): those of `ActionToJson` for the person seat that has to act
     * (`LegalActions`); {"type": "next_hand"} in `hand_over`; nothing in `match_over` or when a computer has to act. `perform_action`
     * answers {"applied": the action, "phase", "version"} and, for `next_hand`, "settlement": {`kind`, `winner`, `scores`, `deltas`} of
     * the hand that ended. `illegal_action` is a well-formed action that is not listed now; `invalid_params` a malformed one.
     *
     * `reset` takes {"seed": 0..4294967295, "seats": "PCCC" (four letters P or C), "hands": 8 | 16, "flowers": bool}, all optional: what
     * is left out keeps the setting of the game (the seed: the one of the last `reset`, or 1). The seats and the settings stay for the
     * next match. `set_state` is not supported. `no_active_model` before the first match was started.
     */
    class MahjongGameControl : public IControllableGame {
    public:
        explicit MahjongGameControl(MahjongGame& game) : _game(game) {}

        [[nodiscard]] std::expected<Json, ControlError> GetState() const override;
        [[nodiscard]] std::expected<Json, ControlError> GetActions() const override;
        std::expected<Json, ControlError> PerformAction(const Json& action) override;
        std::expected<void, ControlError> Reset(const Json& options) override;
        [[nodiscard]] uint64_t GetStateVersion() const override;

    private:
        MahjongGame& _game;
        uint32_t _lastSeed = kFirstResetSeed;
    };

}  // namespace BECore::Mahjong
