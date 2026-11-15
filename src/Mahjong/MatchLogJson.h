#pragma once

#include <BECore/Control/Json.h>
#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong {

    /*
     * The events of a match log as JSON, one object per line (JSON lines), key `event`: the text form of `Game/MatchLog.h`, written by
     * `MatchLogWriter`. The keys and the version history are in `src/Mahjong/CLAUDE.md`, "Match log".
     *
     * The log is the owner's: the deal lists East's 14 tiles and nothing is hidden, which is why it is not the view of a seat. The
     * `action` is exactly `ActionToJson` and the `breakdown` the one of `BreakdownToJson`.
     *
     * Reading back ignores the `breakdown`s of a settlement (they are for the human reader). Reading is checked: a missing or
     * wrong-typed member, an unknown `event`, a bad tile, seat letter, wind name or level is an error text that names the member;
     * nothing aborts. **Any `version` outside `kMatchLogOldestVersion..kMatchLogFormatVersion` is refused.** A `start` of an older
     * version is read as if the members added later were absent, and a member that its version does not have is not looked at.
     */

    /// The `version` of the `start` line that is written.
    inline constexpr int kMatchLogFormatVersion = 6;

    /// The oldest `version` that is still read.
    inline constexpr int kMatchLogOldestVersion = 1;

    /// The first `version` whose `start` line has `level` (a name); an older one has none and reads as `Normal`.
    inline constexpr int kMatchLogLevelVersion = 2;
    static_assert(kMatchLogLevelVersion >= kMatchLogOldestVersion && kMatchLogLevelVersion <= kMatchLogFormatVersion);

    /// The first `version` whose `level` is a number 1..10 for the whole table.
    inline constexpr int kMatchLogNumberLevelVersion = 3;
    static_assert(kMatchLogNumberLevelVersion > kMatchLogLevelVersion && kMatchLogNumberLevelVersion <= kMatchLogFormatVersion);

    /// The first `version` whose `start` line has `levels`, a level per seat, a person's seat too (kept for when it becomes a computer).
    inline constexpr int kMatchLogSeatLevelsVersion = 4;
    static_assert(kMatchLogSeatLevelsVersion > kMatchLogNumberLevelVersion && kMatchLogSeatLevelsVersion <= kMatchLogFormatVersion);

    /// The first `version` whose `start` line has `platform`, `build` and `gameVersion` (strings, empty when not known; `unknown` is a
    /// legal platform so that every event round-trips).
    inline constexpr int kMatchLogOriginVersion = 5;
    static_assert(kMatchLogOriginVersion > kMatchLogSeatLevelsVersion && kMatchLogOriginVersion <= kMatchLogFormatVersion);

    /// The first `version` whose `start` line may have `nickname`: written only when the tester typed one (`MatchLogOrigin::nickname`),
    /// at most `kMaxMatchLogNicknameBytes` bytes without a control character, else the line is refused.
    inline constexpr int kMatchLogNicknameVersion = 6;
    static_assert(kMatchLogNicknameVersion > kMatchLogOriginVersion && kMatchLogNicknameVersion <= kMatchLogFormatVersion);

    /// A level name that files of version 2 hold and no level has now: read as `Normal`, not refused.
    inline constexpr StringView kMatchLogRetiredLevelName = "hard";

    /// The `platform` text of a `start`: `desktop`, `web` or `unknown`.
    [[nodiscard]] StringView PlatformName(MatchPlatform platform);

    /// One event as the object of its line.
    [[nodiscard]] Json EventToJson(const MatchLogEvent& event);

    /// The event of one parsed line, or the error text naming the member that is wrong.
    [[nodiscard]] std::expected<MatchLogEvent, String> EventFromJson(const Json& json);

    /// A whole log file: lines of JSON, blank lines skipped. The error names the line number (counted from 1) and what is wrong with it.
    [[nodiscard]] std::expected<Vector<MatchLogEvent>, String> ParseMatchLog(StringView lines);

}  // namespace BECore::Mahjong
