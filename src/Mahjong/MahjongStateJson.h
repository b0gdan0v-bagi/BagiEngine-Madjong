#pragma once

#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/MahjongGameControl.h>

namespace BECore::Mahjong {

    /// What differs between the two users of the table state: the control API (`MahjongGameControl`, over a `TableSession`) and the
    /// online view (`MahjongSeatedGame`, over a `Match` alone). They share the keys, so a change here is a change of the wire format
    /// (`src/Mahjong/Online/CLAUDE.md`, "A view"). Pointers are borrowed for the call.
    struct StateJsonContext {
        const Match& match;
        /// The phase of the session, or `PhaseOfMatch(match)` for a user with no `TableSession`.
        SessionPhase phase = SessionPhase::Playing;
        uint64_t version = 0;
        /// The seat the view is made for; `kNoSeat` is a spectator (no own tiles, `seat` and `seatWind` null).
        Seat viewSeat = kNoSeat;
        /// The kind of each seat for the `kind` key of `seats[i]`; null leaves the key out (online: the lobby says who is a person).
        const SeatSetup* kinds = nullptr;
        /// The level of each seat for the `level` and `levels` keys; null leaves both out. `level` is `FirstComputerLevel(kinds, levels)`
        /// (without `kinds` every seat counts as a computer).
        const SeatLevels* levels = nullptr;
    };

    /// The phase by the rule of `TableSession::GetPhase`, for a user with no session.
    [[nodiscard]] SessionPhase PhaseOfMatch(const Match& match) noexcept;

    /// "playing" | "hand_over" | "match_over".
    [[nodiscard]] StringView PhaseName(SessionPhase phase);

    /// The open melds of a seat as the state lists them.
    [[nodiscard]] Json MeldsToJson(const Vector<Meld>& melds);

    /// What a finished hand came to, all four hands open: the `result` of the state.
    [[nodiscard]] Json ResultToJson(const HandModel& hand);

    /// The settlement `next_hand` answers: the outcome and the money, not the breakdowns (they are in `result`).
    [[nodiscard]] Json SettlementToJson(const HandSettlement& settlement);

    /// One entry of `seats`. `view` is the view of `context.viewSeat` (of any seat for a spectator, whose own tiles are not read); the
    /// melds of the viewing seat are its own, with the concealed kongs. Reads of the other hands: their size only.
    [[nodiscard]] Json SeatToJson(const StateJsonContext& context, const TableView& view, Seat seat);

    /// The whole state object from `view`; `kind`, `level` and `levels` only when the context has them, `result` when `context.phase` is
    /// not `Playing`.
    [[nodiscard]] Json TableStateToJson(const StateJsonContext& context, const TableView& view);

}  // namespace BECore::Mahjong
