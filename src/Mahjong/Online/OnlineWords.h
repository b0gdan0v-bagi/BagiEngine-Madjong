#pragma once

#include <BECore/UI/UIStrings.h>
#include <RoomClient/RoomClient.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomTypes.h>

namespace BECore::Mahjong {

    /// Codes the client of a room can meet beyond the room protocol's own (`RoomErrors`, `RoomClientErrors`, `RoomClientCloseReasons`): the game's `invalid_params`, the
    /// reasons a server closes a connection with and the reasons of a `left`.
    namespace OnlineCodes {
        inline constexpr eastl::string_view kInvalidParams = "invalid_params";
        inline constexpr eastl::string_view kTooManyFailedJoins = "too_many_failed_joins";
        inline constexpr eastl::string_view kServerFull = "server_full";
        /// The server closes a connection in no room that sent nothing it counts (a `ping` is none) for its idle time: a list the player left on screen.
        inline constexpr eastl::string_view kIdle = "idle";
        /// The reasons of a `left` that the room itself sends (the hub closed the room, or a finished game ran out its time).
        inline constexpr eastl::string_view kRoomClosed = "room_closed";
        inline constexpr eastl::string_view kRoomFinished = "room_finished";
    }  // namespace OnlineCodes

    /// One code and the key of `config/UIStrings.xml` that says it. Where the player can do nothing different, several codes share a key.
    struct OnlineCodeWord {
        eastl::string_view code;
        eastl::string_view key;
    };

    /// The key of a code no table below lists (a newer server): its text has the placeholder `{code}`; every other `online.error.*` key has none.
    inline constexpr eastl::string_view kOnlineUnknownKey = "online.error.unknown";

    /// Every code the client can show, with its key. A code that the server or the client gains later is added here, with its words in both languages.
    inline constexpr OnlineCodeWord kKnownOnlineCodes[] = {
        // the room protocol (`RoomErrors`)
        {RoomErrors::kBadMessage, "online.error.server"},
        {RoomErrors::kUnsupportedVersion, "online.error.viewFormat"},
        {RoomErrors::kMessageTooLarge, "online.error.server"},
        {RoomErrors::kRateLimited, "online.error.rateLimited"},
        {RoomErrors::kUnknownGame, "online.error.server"},
        {RoomErrors::kUnknownRoom, "online.error.unknownRoom"},
        {RoomErrors::kRoomFull, "online.error.roomFull"},
        {RoomErrors::kNotInRoom, "online.error.notNow"},
        {RoomErrors::kAlreadyInRoom, "online.error.notNow"},
        {RoomErrors::kNotHost, "online.error.notHost"},
        {RoomErrors::kGameStarted, "online.error.gameStarted"},
        {RoomErrors::kNotStarted, "online.error.notNow"},
        {RoomErrors::kSeatTaken, "online.error.seatTaken"},
        {RoomErrors::kInvalidSeat, "online.error.notNow"},
        {RoomErrors::kStaleAction, "online.error.notNow"},
        {RoomErrors::kIllegalAction, "online.error.notNow"},
        {RoomErrors::kNotYourTurn, "online.error.notNow"},
        {RoomErrors::kBadName, "online.error.badName"},
        {RoomErrors::kBadToken, "online.error.roomGone"},
        {RoomErrors::kTooManyRooms, "online.error.busy"},
        {RoomErrors::kInternalError, "online.error.server"},
        // the game
        {OnlineCodes::kInvalidParams, "online.error.server"},
        // the commands of the client (`RoomClientErrors`)
        {RoomClientErrors::kWrongState, "online.error.notNow"},
        {RoomClientErrors::kTransportUnavailable, "online.error.connectFailed"},
        {RoomClientErrors::kConnectFailed, "online.error.connectFailed"},
        {RoomClientErrors::kBadServerMessage, "online.error.server"},
        {RoomClientErrors::kInvalidSession, "online.error.roomGone"},
        // why a connection ended for good (`RoomClientCloseReasons`; `rate_limited` is a server code above)
        {RoomClientCloseReasons::kRoomGone, "online.error.roomGone"},
        {RoomClientCloseReasons::kReplaced, "online.error.replaced"},
        {RoomClientCloseReasons::kGaveUp, "online.error.roomGone"},
        {RoomClientCloseReasons::kConnectTimeout, "online.error.noAnswer"},
        {OnlineCodes::kTooManyFailedJoins, "online.error.rateLimited"},
        {OnlineCodes::kServerFull, "online.error.busy"},
        {OnlineCodes::kIdle, "online.error.idle"},
        // why the room let the player go (`left`)
        {OnlineCodes::kRoomClosed, "online.error.roomGone"},
        {OnlineCodes::kRoomFinished, "online.error.roomGone"},
    };

    /// The key that says @p code; `online.error.unknown` for a code that is not in `kKnownOnlineCodes`.
    [[nodiscard]] eastl::string_view OnlineErrorKey(eastl::string_view code);

    /// Whether @p code is shown at all: `stale_action` only is not (the fresh view follows and the table is right again).
    [[nodiscard]] bool IsSilentOnlineCode(eastl::string_view code);

    /// The keys for a view the client will not read: another `format` (it needs a newer or an older game) and a view that is broken.
    inline constexpr eastl::string_view kOnlineViewFormatKey = "online.error.viewFormat";
    inline constexpr eastl::string_view kOnlineBadViewKey = "online.error.badView";

    /// What the client is doing while it has no room to show.
    inline constexpr eastl::string_view kOnlineConnectingKey = "online.status.connecting";
    inline constexpr eastl::string_view kOnlineReconnectingKey = "online.status.reconnecting";

    /// The line the host reads in the lobby about the setting he made: hints and the coach allowed in the room, or not.
    inline constexpr eastl::string_view kOnlineLobbyHintsAllowedKey = "online.lobby.hintsAllowed";
    /// The guest's line of the lobby: waiting for the host, with the host's choice about hints when the lobby says it.
    inline constexpr eastl::string_view kOnlineWaitHostKey = "online.waitHost";
    inline constexpr eastl::string_view kOnlineWaitHostHintsAllowedKey = "online.waitHost.hintsAllowed";
    inline constexpr eastl::string_view kOnlineWaitHostHintsOffKey = "online.waitHost.hintsOff";
    inline constexpr eastl::string_view kOnlineLobbyHintsOffKey = "online.lobby.hintsOff";

    /// The lobby's copy button by what it did, and the Paste key of the code pad.
    inline constexpr eastl::string_view kOnlineCopyKey = "online.copy";
    inline constexpr eastl::string_view kOnlineCopiedKey = "online.copied";
    inline constexpr eastl::string_view kOnlineCopyFailedKey = "online.copyFailed";
    inline constexpr eastl::string_view kOnlinePasteKey = "online.paste";
    /// The note of the pad when a paste did not give a code: the text holds none, the clipboard is empty, the browser refused to be read.
    inline constexpr eastl::string_view kOnlinePasteNotCodeKey = "online.paste.notCode";
    inline constexpr eastl::string_view kOnlinePasteEmptyKey = "online.paste.empty";
    inline constexpr eastl::string_view kOnlinePasteRefusedKey = "online.paste.refused";

    /// The text of @p key in the current language. Only `online.error.unknown` has a placeholder (`{code}`, filled with @p arg).
    [[nodiscard]] eastl::string OnlineNoticeText(const UIStrings& strings, eastl::string_view key, eastl::string_view arg = {});

    /**
     * @brief What a lobby seat says about its occupant: "Empty", "Computer", the name of a person, "{name} (you)", "{name} (away)".
     *
     * @p name is kept as it is (UTF-8, whatever the person typed). @p mine wins over @p away: a client never shows its own seat as away.
     * @param connected only for a person: false is "away".
     */
    [[nodiscard]] eastl::string OnlineSeatText(const UIStrings& strings, BECore::SeatKind kind, eastl::string_view name, bool mine, bool connected);

}  // namespace BECore::Mahjong
