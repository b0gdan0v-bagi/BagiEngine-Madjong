#pragma once

#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Online/OnlineTable.h>
#include <RoomClient/RoomClient.h>

namespace BECore::Mahjong {

    /// Where the player is in the life of an online game; the screens and the table switch on it.
    CORE_ENUM(OnlineStage, uint8_t,
              Idle,          // no connection and no room (or a request that was answered with an error)
              Connecting,    // a Host / Join was sent and `joined` has not come
              Lobby,         // in a room, before the first good view
              Game,          // in a room with a table to read (`Table()`)
              Reconnecting,  // the connection was lost (or `Resume` is under way); the lobby and the table stay readable, frozen
              Browsing       // a connection used to list the open rooms (opening, or open with no room): no room to show
    )

    /// What the host asks of the game when the room is made (`create_room options`, read by `MahjongSeatedGame::Start`).
    struct OnlineRoomOptions {
        /// `kShortMatchHands` or `kFullMatchHands`.
        int hands = kFullMatchHands;
        /// The level of every computer seat, `kMinLevel` .. `kMaxLevel`.
        int level = LevelNumber(kDefaultLevel);
        /// Whether the players of the room may use hints and the coach (the host decides; the room keeps it).
        bool hintsAllowed = false;
        /// Whether the room is listed for the players who browse (`Browse`); false: it is joined by its code only.
        bool isPublic = false;
    };

    /// One open room of a list the server sent, with the facts a screen needs. A fact the listing does not carry is none.
    struct OnlineListedRoom {
        /// The code to join with.
        eastl::string code;
        /// The host's name (UTF-8, as the server kept it): untrusted text, shown as given and never used as a format.
        eastl::string host;
        /// Seats a person can still take.
        int free = 0;
        std::optional<int> hands;
        std::optional<int> level;
        std::optional<bool> hintsAllowed;
    };

    /// The last list of open rooms, for the `Rooms` page.
    struct OnlineRoomList {
        /// An answer has come since `Browse` started this connection (a list that was re-asked keeps the old rooms until the new answer).
        bool loaded = false;
        /// A list was asked for and neither an answer nor a refusal has come.
        bool pending = false;
        /// The server cut the list at its cap: more rooms are open.
        bool more = false;
        eastl::vector<OnlineListedRoom> rooms;
    };

    /// One seat of the lobby, with the facts a screen needs so that it reads no `RoomClient`.
    struct OnlineLobbySeat {
        BECore::SeatKind kind = BECore::SeatKind::Empty;
        /// A person's name (UTF-8, as the server kept it); empty for the other kinds.
        eastl::string name;
        bool connected = false;
        /// This client's seat.
        bool mine = false;
        bool host = false;
        /// A person whose connection is down: the room holds the seat for a while.
        bool away = false;
    };

    struct OnlineLobby {
        /// The code of the room; empty when there is no lobby to show.
        eastl::string room;
        RoomPhase phase = RoomPhase::Lobby;
        eastl::array<OnlineLobbySeat, kSeatCount> seats;
        /// This client is the host of a room that has not started.
        bool canStart = false;
        /// How many seats a person holds.
        int humans = 0;
        /// Whether the host allowed hints and the coach, from the public options of the lobby; none when the lobby does not say
        /// (a server from before the lobby carried options).
        std::optional<bool> hintsAllowed;
    };

    /// One thing for a screen to say: a key of `config/UIStrings.xml` (`OnlineWords.h`), the code for the one key that names it, and a
    /// serial so a screen shows each notice once (0 is none yet).
    struct OnlineNotice {
        eastl::string_view key;
        eastl::string arg;
        uint32_t serial = 0;
    };

    /**
     * @brief The model of an online game over one `RoomClient`: the screens read it and send commands, nothing here draws.
     *
     * A component calls `Update()` once per frame and redraws when `Version()` moved. Commands return whether they were sent; one that
     * cannot be sent makes a notice (`wrong_state`, a bad name, a code that cannot be a room code) and a server's refusal comes later as
     * a notice from `Update()`. The model keeps no game: `Table()` is the last good view of the server read by `ReadOnlineView`, and
     * `Act(i)` sends the server's own i-th action. Rules and reasons: `Online/CLAUDE.md`.
     *
     * Not copyable, one thread.
     */
    class OnlineClient {
    public:
        explicit OnlineClient(RoomClientSettings settings = {});
        ~OnlineClient();

        OnlineClient(const OnlineClient&) = delete;
        OnlineClient& operator=(const OnlineClient&) = delete;

        // ---- Commands ----

        /// Creates a room of Mahjong on the server at @p url. @p options wrong (hands not 8 or 16, level outside 1..10) or a name the server
        /// would refuse: a notice and no connection.
        bool Host(eastl::string_view url, eastl::string_view name, const OnlineRoomOptions& options = {});
        /// Enters the room @p code (any case). A code that cannot be one is `unknown_room` at once; a bad name is `bad_name`.
        bool Join(eastl::string_view url, eastl::string_view code, eastl::string_view name);
        /// Starts browsing the open rooms of @p url (from `Idle`: opens a connection) or asks again on the open one (from `Browsing`; nothing
        /// while an ask is still unanswered). Any other stage is refused with the `wrong_state` notice. The list is the answer to `Rooms()`.
        /// The model never asks by itself: a screen presses Refresh.
        bool Browse(eastl::string_view url);
        /// `Join` of a code from the list: on the connection that lists. A refusal (`unknown_room`, `room_full`, `game_started`, any error)
        /// keeps the connection, makes its notice and asks the list again, so the player is back on a list without the gone room.
        bool JoinListed(eastl::string_view url, eastl::string_view code, eastl::string_view name);
        /// Comes back into the room of a session that an earlier run kept. `invalid_session` for one that cannot be a room and a token.
        bool Resume(const RoomSession& session);
        /// Asks for a seat, in the lobby only (no notice elsewhere). `CanChoose` says whether the lobby shows the seat free.
        bool ChooseSeat(int seat);
        /// The host starts the game, in the lobby only.
        bool Start();
        /// Sends `Table().actions[index]` as the server listed it, stamped with the version of the view. Only in `Game` (no notice
        /// elsewhere, so a click during a reconnect does nothing) and not after a view that was not read, until a good one comes (the
        /// screen shows an older table than the client holds); the client never resends it.
        bool Act(size_t index);
        /// Leaves the room and forgets the session. The connection is closed by the model once the server answered. While `Browsing` it ends
        /// the browsing and closes the connection.
        bool Leave();
        /// Closes the connection and keeps the session; the room, lobby and table are forgotten. No notice.
        void Disconnect();

        // ---- Frame ----

        /// Polls the client against @p now and reads what came. Allocates nothing when nothing came.
        void Update(RoomTime now);
        void Update();

        // ---- State ----

        [[nodiscard]] OnlineStage Stage() const {
            return _stage;
        }

        /// Grows when the stage, the lobby, the table, the notice or the session to keep changes; a component redraws on a moved version only.
        [[nodiscard]] uint64_t Version() const {
            return _version;
        }

        [[nodiscard]] const OnlineLobby& Lobby() const {
            return _lobby;
        }

        /// Whether `Table()` holds a view: the last good one, kept through a reconnect and a bad view.
        [[nodiscard]] bool HasTable() const {
            return _hasTable;
        }

        [[nodiscard]] const OnlineTable& Table() const {
            return _table;
        }

        /// The latest notice; `serial` 0 before the first.
        [[nodiscard]] const OnlineNotice& Notice() const {
            return _notice;
        }

        /// The session the game should keep to `Resume` later (a secret: the token). Invalid when there is none.
        [[nodiscard]] const RoomSession& SessionToKeep() const {
            return _kept;
        }

        /// Grows when `SessionToKeep()` should be saved (a new one) or erased (it was forgotten).
        [[nodiscard]] uint32_t SessionRevision() const {
            return _sessionRevision;
        }

        [[nodiscard]] const eastl::string& Room() const {
            return _client.GetRoom();
        }

        [[nodiscard]] int Seat() const {
            return _client.GetSeat();
        }

        [[nodiscard]] bool IsHost() const {
            return _client.IsHost();
        }

        /// Whether the lobby shows @p seat free to take, in a lobby that has not started.
        [[nodiscard]] bool CanChoose(int seat) const;

        /// What this client asked for when it made the room it is in; null for a room it joined or resumed, and once the room is left, gone
        /// or the connection given up. The lobby does not carry the options, so only the host can say them there.
        [[nodiscard]] const OnlineRoomOptions* HostedOptions() const {
            return _hosted ? &*_hosted : nullptr;
        }

        /// The open rooms of the last answer; empty and not loaded outside `Browsing`. Refilled only when an answer comes (then `Version()` moves).
        [[nodiscard]] const OnlineRoomList& Rooms() const {
            return _rooms;
        }

        /// The raw client, for what the model does not copy (the breakdown lines of a result in `GetView()`).
        [[nodiscard]] const RoomClient& Client() const {
            return _client;
        }

    private:
        void HandleEvents();
        /// Returns false when the connection was closed by the model: what is queued behind belongs to it.
        bool Handle(const RoomClientEvent& event);
        void OnJoined();
        void OnResumed();
        bool OnView();
        void OnError(const RoomClientEvent& event);
        void OnLeft(const RoomClientEvent& event);
        void OnRoomList();
        void OnClosed(const RoomClientEvent& event);
        /// Closes a connection that has no room and no request, and brings the lobby and the stage up to date.
        void Settle();
        void SyncLobby();
        void SyncStage();
        void RebuildLobby();
        [[nodiscard]] OnlineStage ComputeStage() const;

        /// Closes the connection of the client and drops the `Closed` it queues (the model asked for it, so it has nothing to say).
        void CloseConnection();
        void ForgetView();
        void ForgetHosted();
        /// Ends browsing and clears the list (the connection is the caller's to close, or the model's `Settle()`).
        void EndBrowsing();
        /// Asks the server for the list on the connection that is open (`pending` until the answer or the refusal).
        std::expected<void, ControlError> AskForRooms(eastl::string_view url);
        bool SendJoin(eastl::string_view url, eastl::string_view code, eastl::string_view name, bool fromList);
        void KeepSession(const RoomSession& session);
        void ForgetSession();
        void SetNotice(eastl::string_view key, eastl::string_view arg = {});
        void SetNoticeForCode(eastl::string_view code);
        /// Turns an immediate refusal of a command into a notice; true when the command was sent.
        bool Report(const std::expected<void, ControlError>& result);
        void Bump();

        RoomClient _client;
        /// Members, so a frame keeps their capacity.
        eastl::vector<RoomClientEvent> _events;
        eastl::vector<RoomClientEvent> _discarded;
        OnlineTable _table;
        OnlineTable _scratch;
        bool _hasTable = false;
        /// `_table` is the latest view of the client: false after a view that was not read, until the next good one.
        bool _tableIsLatest = false;
        OnlineLobby _lobby;
        std::optional<OnlineRoomOptions> _hosted;
        bool _lobbyDirty = false;
        int _lobbySeat = -1;
        bool _lobbyHost = false;
        OnlineStage _stage = OnlineStage::Idle;
        uint64_t _version = 0;
        OnlineNotice _notice;
        RoomSession _kept;
        uint32_t _sessionRevision = 0;
        /// A Host / Join was sent and `joined` or an error has not come.
        bool _requesting = false;
        /// The connection is used to list the open rooms: `Settle()` keeps it open, and `ComputeStage()` says `Browsing` while it has no request.
        bool _browsing = false;
        /// The address of the browsing connection, for the next ask.
        eastl::string _browseUrl;
        /// The request that waits for `joined` is a join of a listed code: an error asks the list again.
        bool _joiningListed = false;
        OnlineRoomList _rooms;
        /// `left {replaced}` came: the server's close follows and carries the notice.
        bool _awaitingClose = false;
    };

    /// The session as JSON for the store of the game (`url`, `room`, `token`: a secret, keep it where the platform keeps secrets).
    [[nodiscard]] Json SessionToJson(const RoomSession& session);

    /// The session of @p json; nullopt for anything that is not an object with a `url`, a `room` that can be a room code and a `token`
    /// of the form a room hands out. The room comes back in upper case.
    [[nodiscard]] std::optional<RoomSession> SessionFromJson(const Json& json);

}  // namespace BECore::Mahjong
