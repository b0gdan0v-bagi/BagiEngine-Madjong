#pragma once

#include <BECore/UI/UIStrings.h>
#include <RoomClient/RoomClient.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomTypes.h>

namespace BECore::Mahjong {

    /// Codes the client can meet beyond those of `RoomErrors`, `RoomClientErrors` and `RoomClientCloseReasons`.
    namespace OnlineCodes {
        inline constexpr StringView kInvalidParams = "invalid_params";
        inline constexpr StringView kTooManyFailedJoins = "too_many_failed_joins";
        inline constexpr StringView kServerFull = "server_full";
        /// The server closes a connection in no room that sent nothing it counts (a `ping` is none) for its idle time: a list left on screen.
        inline constexpr StringView kIdle = "idle";
        /// Reasons of a `left` that the room itself sends.
        inline constexpr StringView kRoomClosed = "room_closed";
        inline constexpr StringView kRoomFinished = "room_finished";
    }  // namespace OnlineCodes

    /// Where the player can do nothing different, several codes share a key of `config/UIStrings.xml`.
    struct OnlineCodeWord {
        StringView code;
        StringView key;
    };

    /// For a code the table does not list (a newer server): the only `online.error.*` text with a placeholder, `{code}`.
    inline constexpr StringView kOnlineUnknownKey = "online.error.unknown";

    /// A code that the server or the client gains later is added here, with its words in both languages.
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
    [[nodiscard]] StringView OnlineErrorKey(StringView code);

    /// Whether @p code is shown at all: `stale_action` only is not (the fresh view follows and the table is right again).
    [[nodiscard]] bool IsSilentOnlineCode(StringView code);

    /// For a view the client will not read: another `format` (it needs a newer or an older game), and a broken view.
    inline constexpr StringView kOnlineViewFormatKey = "online.error.viewFormat";
    inline constexpr StringView kOnlineBadViewKey = "online.error.badView";

    inline constexpr StringView kOnlineConnectingKey = "online.status.connecting";
    inline constexpr StringView kOnlineReconnectingKey = "online.status.reconnecting";
    inline constexpr StringView kOnlineBrowsingKey = "online.status.browsing";
    inline constexpr StringView kOnlineNoRoomsKey = "online.rooms.empty";

    /// The host's line about his hints setting; the guest's line is `kOnlineWaitHostKey` with the host's choice when the lobby says it.
    inline constexpr StringView kOnlineLobbyHintsAllowedKey = "online.lobby.hintsAllowed";
    inline constexpr StringView kOnlineWaitHostKey = "online.waitHost";
    inline constexpr StringView kOnlineWaitHostHintsAllowedKey = "online.waitHost.hintsAllowed";
    inline constexpr StringView kOnlineWaitHostHintsOffKey = "online.waitHost.hintsOff";
    inline constexpr StringView kOnlineLobbyHintsOffKey = "online.lobby.hintsOff";

    inline constexpr StringView kOnlineCopyKey = "online.copy";
    inline constexpr StringView kOnlineCopiedKey = "online.copied";
    inline constexpr StringView kOnlineCopyFailedKey = "online.copyFailed";
    inline constexpr StringView kOnlinePasteKey = "online.paste";
    /// The note of the pad when a paste did not give a code: the text holds none, the clipboard is empty, the browser refused to be read.
    inline constexpr StringView kOnlinePasteNotCodeKey = "online.paste.notCode";
    inline constexpr StringView kOnlinePasteEmptyKey = "online.paste.empty";
    inline constexpr StringView kOnlinePasteRefusedKey = "online.paste.refused";

    /// Only `online.error.unknown` has a placeholder, filled with @p arg.
    [[nodiscard]] String OnlineNoticeText(const UIStrings& strings, StringView key, StringView arg = {});

    /**
     * @brief What a lobby seat says about its occupant: "Empty", "Computer", the name of a person, "{name} (you)", "{name} (away)".
     *
     * @p name is kept as it is (UTF-8, whatever the person typed). @p mine wins over @p away: a client never shows its own seat as away.
     * @param connected only for a person: false is "away".
     */
    [[nodiscard]] String OnlineSeatText(const UIStrings& strings, BECore::SeatKind kind, StringView name, bool mine, bool connected);

}  // namespace BECore::Mahjong
