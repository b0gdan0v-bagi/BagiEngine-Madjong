#pragma once

#include <BECore/Control/Json.h>
#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong {

    /*
     * The events of a match log as JSON, one object per line (JSON lines), key `event`. `Game/MatchLog.h` has the plain data;
     * this is its text form, written by `MatchLogWriter`.
     *
     * ```
     * {"event":"start","version":1,"seed":N,"settings":{"handsPerMatch","flowers","replayDrawnHand","limit","startingScore"},"seats":"PCCC"}
     * {"event":"hand","number":handsPlayed,"east":s,"roundWind":"East","winds":["East",...],"deal":[{"concealed":["1p",...],"bonus":[...]} x4]}
     * {"event":"action","seat":s,"by":"person"|"computer","action":{...}}
     * {"event":"settlement","kind":"win"|"drawn","winner":s|null,"east":s,"roundWind":"..","scores":[4],"deltas":[4],"seats":[{"breakdown":{...}} x4]}
     * {"event":"end","scores":[4],"standings":[seats, best first]}
     * ```
     * Tiles are the notation of `Tile.h` (`5p`, `Ew`, `Rd`, `F1`), winds the names of `Wind` (`East` ...), seats the numbers 0..3 and the
     * `seats` letters of `start` are those of the control `reset` (P a person, C a computer). The `action` is exactly the object the control
     * API lists (`ActionToJson`), the `breakdown` the one of its `result` (`BreakdownToJson`; the winner's lines are in the winner's
     * breakdown). The deal lists East's 14 tiles: nothing is hidden, the log is the owner's.
     *
     * Reading back takes everything but the `breakdown`s of a settlement, which are there for the human reader (the scores and changes
     * they sum up to are in the line). Reading is checked: a missing or wrong-typed member, an unknown `event`, a bad tile, seat letter
     * or wind name is an error text that names the member; nothing aborts.
     */

    /// The `version` of the `start` line; a log with another version is not read.
    inline constexpr int kMatchLogFormatVersion = 1;

    /// One event as the object of its line.
    [[nodiscard]] Json EventToJson(const MatchLogEvent& event);

    /// The event of one parsed line, or the error text naming the member that is wrong.
    [[nodiscard]] std::expected<MatchLogEvent, eastl::string> EventFromJson(const Json& json);

    /// A whole log file: lines of JSON, blank lines skipped. The error names the line number (counted from 1) and what is wrong with it.
    [[nodiscard]] std::expected<eastl::vector<MatchLogEvent>, eastl::string> ParseMatchLog(std::string_view lines);

}  // namespace BECore::Mahjong
