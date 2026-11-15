#pragma once

#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Online/OnlineTable.h>
#include <RoomClient/RoomClient.h>

namespace BECore::Mahjong {

    /// Where the player is in the life of an online game; the screens and the table switch on it.
    CORE_ENUM(OnlineStage, uint8_t,
              Idle,  // also after a request that was answered with an error
              Connecting,
              Lobby,         // no good view yet
              Game,          // `Table()` is readable
              Reconnecting,  // also while `Resume` is under way; the lobby and the table stay readable, frozen
              Browsing       // a connection that lists the open rooms: no room to show
    )

    /// What the host asks of the game when the room is made (`create_room options`, read by `MahjongSeatedGame::Start`).
    struct OnlineRoomOptions {
        /// `kShortMatchHands` or `kFullMatchHands`.
        int hands = kFullMatchHands;
        /// The level of every computer seat, `kMinLevel` .. `kMaxLevel`.
        int level = LevelNumber(kDefaultLevel);
        /// The host decides for the whole room.
        bool hintsAllowed = false;
        /// Listed for `Browse`; false: joined by its code only.
        bool isPublic = false;
    };

    /// One open room of a list the server sent. A fact the listing does not carry is none.
    struct OnlineListedRoom {
        String code;
        /// The host's name (UTF-8, as the server kept it): untrusted text, shown as given and never used as a format.
        String host;
        /// Seats a person can still take.
        int free = 0;
        Optional<int> hands;
        Optional<int> level;
        Optional<bool> hintsAllowed;
    };

    /// The last list of open rooms, for the `Rooms` page.
    struct OnlineRoomList {
        /// An answer has come since `Browse` started this connection (a re-asked list keeps the old rooms until the new answer).
        bool loaded = false;
        /// Asked for, and neither an answer nor a refusal has come.
        bool pending = false;
        /// The server cut the list at its cap.
        bool more = false;
        Vector<OnlineListedRoom> rooms;
    };

    /// One seat of the lobby, copied so that a screen reads no `RoomClient`.
    struct OnlineLobbySeat {
        BECore::SeatKind kind = BECore::SeatKind::Empty;
        /// A person's name (UTF-8, as the server kept it); empty for the other kinds.
        String name;
        bool connected = false;
        bool mine = false;
        bool host = false;
        /// A person whose connection is down: the room holds the seat for a while.
        bool away = false;
    };

    struct OnlineLobby {
        /// The code of the room; empty when there is no lobby to show.
        String room;
        RoomPhase phase = RoomPhase::Lobby;
        Array<OnlineLobbySeat, kSeatCount> seats;
        /// This client is the host of a room that has not started.
        bool canStart = false;
        int humans = 0;
        /// From the public options of the lobby; none when it does not say (an older server).
        Optional<bool> hintsAllowed;
    };

    /// One thing for a screen to say: a key of `config/UIStrings.xml` (`OnlineWords.h`), the code for the one key that names it, and a
    /// serial so a screen shows each notice once (0: none yet).
    struct OnlineNotice {
        StringView key;
        String arg;
        uint32_t serial = 0;
    };

    /**
     * @brief The model of an online game over one `RoomClient`: the screens read it and send commands, nothing here draws.
     *
     * A component calls `Update()` once per frame and redraws when `Version()` moved. A command that cannot be sent makes a notice; a
     * server's refusal comes later, from `Update()`. The model keeps no game: `Table()` is the last good view, and `Act(i)` sends the
     * server's own i-th action (a client reads `actions`, not `turn.yourTurn`). Rules and reasons: `src/Mahjong/Online/CLAUDE.md`
     * ("The client model").
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

        /// Creates a room on the server at @p url. Wrong @p options or a name the server would refuse: a notice and no connection.
        bool Host(StringView url, StringView name, const OnlineRoomOptions& options = {});
        /// Enters the room @p code (any case). A code that cannot be one is `unknown_room` at once; a bad name is `bad_name`.
        bool Join(StringView url, StringView code, StringView name);
        /// Starts browsing the open rooms of @p url (from `Idle`: opens a connection) or asks again on the open one (from `Browsing`; nothing
        /// while an ask is unanswered); any other stage gets the `wrong_state` notice. The model never polls: a screen presses Refresh.
        bool Browse(StringView url);
        /// `Join` of a listed code on the connection that lists. A refusal keeps that connection, makes its notice and asks the list again,
        /// so the player is back on a list without the gone room.
        bool JoinListed(StringView url, StringView code, StringView name);
        /// Comes back into the room of a session that an earlier run kept; `invalid_session` for one that cannot be a room and a token.
        bool Resume(const RoomSession& session);
        /// Asks for a seat, in the lobby only (no notice elsewhere).
        bool ChooseSeat(int seat);
        /// The host starts the game, in the lobby only.
        bool Start();
        /// Sends `Table().actions[index]` as the server listed it, stamped with the version of the view. Only in `Game`, and not after a
        /// view that was not read until a good one comes (the screen shows an older table than the client holds): no notice then, so a
        /// click during a reconnect does nothing. Never resent: a `stale_action` is silent and the fresh view follows.
        bool Act(size_t index);
        /// Leaves the room and forgets the session; the model closes the connection once the server answered. While `Browsing` it ends the
        /// browsing and closes the connection.
        bool Leave();
        /// Closes the connection and keeps the session; the room, lobby and table are forgotten. No notice.
        void Disconnect();

        // ---- Frame ----

        /// Once per frame. Allocates nothing when nothing came.
        void Update(RoomTime now);
        void Update();

        // ---- State ----

        [[nodiscard]] OnlineStage Stage() const {
            return _stage;
        }

        /// Grows when the stage, the lobby, the table, the notice or the session to keep changes: a component redraws on a moved version only.
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

        /// The session to `Resume` later; invalid when there is none. The token is a secret: never log or show it.
        [[nodiscard]] const RoomSession& SessionToKeep() const {
            return _kept;
        }

        /// Grows when `SessionToKeep()` should be saved (a new one) or erased (forgotten).
        [[nodiscard]] uint32_t SessionRevision() const {
            return _sessionRevision;
        }

        [[nodiscard]] const String& Room() const {
            return _client.GetRoom();
        }

        [[nodiscard]] int Seat() const {
            return _client.GetSeat();
        }

        [[nodiscard]] bool IsHost() const {
            return _client.IsHost();
        }

        /// Whether the lobby of a room that has not started shows @p seat free to take.
        [[nodiscard]] bool CanChoose(int seat) const;

        /// What this client asked for when it made the room it is in; null for a joined or resumed room and once the room is left or gone
        /// (the lobby does not carry the options, so only the host can say them).
        [[nodiscard]] const OnlineRoomOptions* HostedOptions() const {
            return _hosted ? &*_hosted : nullptr;
        }

        /// The open rooms of the last answer; empty outside `Browsing`. `Version()` moves when an answer refills it.
        [[nodiscard]] const OnlineRoomList& Rooms() const {
            return _rooms;
        }

        /// For what the model does not copy (the breakdown lines of a result in `GetView()`).
        [[nodiscard]] const RoomClient& Client() const {
            return _client;
        }

    private:
        void HandleEvents();
        /// False when the model closed the connection: what is queued behind belongs to it.
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

        /// Drops the `Closed` the close queues: the model asked for it, so it has nothing to say.
        void CloseConnection();
        void ForgetView();
        void ForgetHosted();
        /// The connection is the caller's to close, or `Settle()`'s.
        void EndBrowsing();
        std::expected<void, ControlError> AskForRooms(StringView url);
        bool SendJoin(StringView url, StringView code, StringView name, bool fromList);
        void KeepSession(const RoomSession& session);
        void ForgetSession();
        void SetNotice(StringView key, StringView arg = {});
        void SetNoticeForCode(StringView code);
        /// Turns an immediate refusal of a command into a notice; true when the command was sent.
        bool Report(const std::expected<void, ControlError>& result);
        void Bump();

        RoomClient _client;
        /// Members, so a frame keeps their capacity.
        Vector<RoomClientEvent> _events;
        Vector<RoomClientEvent> _discarded;
        OnlineTable _table;
        OnlineTable _scratch;
        bool _hasTable = false;
        /// `_table` is the latest view of the client: false after a view that was not read, until the next good one.
        bool _tableIsLatest = false;
        OnlineLobby _lobby;
        Optional<OnlineRoomOptions> _hosted;
        bool _lobbyDirty = false;
        int _lobbySeat = -1;
        bool _lobbyHost = false;
        OnlineStage _stage = OnlineStage::Idle;
        uint64_t _version = 0;
        OnlineNotice _notice;
        RoomSession _kept;
        uint32_t _sessionRevision = 0;
        /// A Host / Join waits for `joined` or an error.
        bool _requesting = false;
        /// `Settle()` keeps the connection open for listing; `ComputeStage()` says `Browsing` while it has no request.
        bool _browsing = false;
        String _browseUrl;
        /// The join that waits for `joined` is of a listed code: an error asks the list again.
        bool _joiningListed = false;
        OnlineRoomList _rooms;
        /// `left {replaced}` came: the server's close follows and carries the notice.
        bool _awaitingClose = false;
    };

    /// The session as JSON for the game's store (`url`, `room`, `token`: a secret).
    [[nodiscard]] Json SessionToJson(const RoomSession& session);

    /// Nullopt for anything that is not an object with a `url`, a `room` that can be a room code and a `token` of the form a room hands
    /// out. The room comes back in upper case.
    [[nodiscard]] Optional<RoomSession> SessionFromJson(const Json& json);

}  // namespace BECore::Mahjong
