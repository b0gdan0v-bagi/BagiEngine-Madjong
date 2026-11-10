#pragma once

#include <BECore/Control/Json.h>
#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong {

    /*
     * The events of a match log as JSON, one object per line (JSON lines), key `event`. `Game/MatchLog.h` has the plain data;
     * this is its text form, written by `MatchLogWriter`.
     *
     * ```
     * {"event":"start","version":3,"seed":N,"settings":{"handsPerMatch","flowers","replayDrawnHand","limit","startingScore"},"seats":"PCCC","level":5}
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
     * they sum up to are in the line). Reading is checked: a missing or wrong-typed member, an unknown `event`, a bad tile, seat letter,
     * wind name or level is an error text that names the member; nothing aborts.
     *
     * Versions: 3 (`kMatchLogFormatVersion`, `kMatchLogNumberLevelVersion`) writes `level`, the strength of the computer players, as its
     * number 1..10 (`LevelNumber`). Version 2 (`kMatchLogLevelVersion`) wrote it as a name, `easy` (read as 1) or `normal` (5), and a
     * `hard` of version 2 (`kMatchLogRetiredLevelName`) is read as 5. Version 1 files (`kMatchLogOldestVersion`) have no `level` and are
     * read as `Normal`, the only player there was. Any other version is refused.
     */

    /// The `version` of the `start` line that is written.
    inline constexpr int kMatchLogFormatVersion = 3;

    /// The oldest `version` that is still read; a log with a version outside `kMatchLogOldestVersion..kMatchLogFormatVersion` is not read.
    inline constexpr int kMatchLogOldestVersion = 1;

    /// The first `version` whose `start` line has `level`; an older one has none and reads as `Normal`.
    inline constexpr int kMatchLogLevelVersion = 2;
    static_assert(kMatchLogLevelVersion >= kMatchLogOldestVersion && kMatchLogLevelVersion <= kMatchLogFormatVersion);

    /// The first `version` whose `level` is a number 1..10, not a name.
    inline constexpr int kMatchLogNumberLevelVersion = 3;
    static_assert(kMatchLogNumberLevelVersion > kMatchLogLevelVersion && kMatchLogNumberLevelVersion <= kMatchLogFormatVersion);

    /// The name of the level `Hard` of the first slice of the computer levels. It played exactly like Normal and is gone, but files were
    /// written with it: a version 2 `start` that names it is read as `Normal`, not refused. Only version 2 has names.
    inline constexpr std::string_view kMatchLogRetiredLevelName = "hard";

    /// One event as the object of its line.
    [[nodiscard]] Json EventToJson(const MatchLogEvent& event);

    /// The event of one parsed line, or the error text naming the member that is wrong.
    [[nodiscard]] std::expected<MatchLogEvent, eastl::string> EventFromJson(const Json& json);

    /// A whole log file: lines of JSON, blank lines skipped. The error names the line number (counted from 1) and what is wrong with it.
    [[nodiscard]] std::expected<eastl::vector<MatchLogEvent>, eastl::string> ParseMatchLog(std::string_view lines);

}  // namespace BECore::Mahjong
