#pragma once

#include <Games/IControllableGame.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    class MahjongGame;

    /// The seed of a `reset` that names none until a `reset` names one.
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
    [[nodiscard]] Json TilesToJson(const Vector<Tile>& tiles);

    /// One number per seat (points, scores, changes).
    [[nodiscard]] Json SeatScoresToJson(const SeatScores& scores);

    /// The breakdown of one seat in `result` of the state.
    [[nodiscard]] Json BreakdownToJson(const ScoreBreakdown& breakdown);

    /// The setup of "PCCC"-style letters (P a person, C a computer), nullopt unless it is exactly four of them; and back.
    [[nodiscard]] Optional<SeatSetup> ReadSeats(const String& letters);
    [[nodiscard]] String SeatsToLetters(const SeatSetup& seats);

    /// The `levels` of a `reset` or a `set_state`: an array of four integers 1..10; nullopt for anything else (a number above INT64_MAX
    /// included, as `GetJsonInt` refuses it).
    [[nodiscard]] Optional<SeatLevels> ReadResetLevels(const Json& list);

    /// An action as the object `list_actions` gives (`type` and the fields of its kind: `src/Mahjong/CLAUDE.md` "Control API").
    [[nodiscard]] Json ActionToJson(const HandAction& action);

    /// {"type": "next_hand"}.
    [[nodiscard]] Json NextHandActionToJson();

    /**
     * @brief Reads an action object from a client with the checked helpers of `BECore/Control/Json.h`.
     *
     * `invalid_params` for anything malformed (`declareCalling` absent is false). A well-formed action is not yet a legal one: the
     * adapter checks it against `LegalActions`. The fields a kind ignores are ignored, so the result equals what `LegalActions` lists.
     */
    [[nodiscard]] std::expected<ControlAction, ControlError> ActionFromJson(const Json& json);

    /**
     * @brief The external control API of Mahjong: the state of the table, the actions of the person seats, and a way to act.
     *
     * A client plays the **person seats** of the game's `TableSession`; after each action the adapter calls `RunComputers()`. With no
     * person seat a `reset` and a `next_hand` play the hand to its end and the client is a spectator.
     *
     * `get_state` is built from `MakeTableView` (`MahjongStateJson.h`): the view of the person who has to act, else of the first person
     * seat, so a client never sees what a computer player could not. Only in `hand_over` and `match_over` are all four hands public
     * (`result`: the outcome `kind`, `winner`, `winningTile`, `source`, `from`, `loose`, `lastLiveTile`, then every seat's tiles and
     * `breakdown`, the `scores` and `deltas`). With no person seat (a spectator) `seat`, `seatWind` and `hand` are null, null, [] and
     * `yourTurn` is false. `perform_action` answers `applied`, `phase`, `version` and, for `next_hand`, `settlement` (`kind`, `winner`,
     * `scores`, `deltas` of the hand that ended). `yourTurn` says that "seat" has actions to choose from; `turn.phase` only says what the hand waits for.
     * `list_actions` gives `ActionToJson` of the `LegalActions` of the person seat that has to act, `next_hand` in `hand_over`, nothing
     * otherwise; `perform_action` accepts each as listed. `illegal_action` is a well-formed action that is not listed now,
     * `invalid_params` a malformed one (the message names the key). `reset` takes the options of `src/Mahjong/CLAUDE.md` "Control API",
     * all optional; anything invalid changes nothing. `set_state` starts a match from a position (`MahjongSetState.h`): the same rules
     * for the errors, and the match is saved but never uploaded. `no_active_model` before the first match and while
     * the game plays in an online room (`MahjongGame::IsOnlineTable()`: the adapter drives the local match only).
     */
    class MahjongGameControl : public IControllableGame {
    public:
        explicit MahjongGameControl(MahjongGame& game) : _game(game) {}

        [[nodiscard]] std::expected<Json, ControlError> GetState() const override;
        [[nodiscard]] std::expected<Json, ControlError> GetActions() const override;
        std::expected<Json, ControlError> PerformAction(const Json& action) override;
        std::expected<void, ControlError> Reset(const Json& options) override;
        std::expected<void, ControlError> SetState(const Json& state) override;
        [[nodiscard]] uint64_t GetStateVersion() const override;

    private:
        MahjongGame& _game;
        uint32_t _lastSeed = kFirstResetSeed;
    };

}  // namespace BECore::Mahjong
