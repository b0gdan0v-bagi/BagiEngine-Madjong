#pragma once

#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Online/OnlineTable.h>
#include <RoomClient/RoomClient.h>

namespace BECore::Mahjong {

    /// Where the player is in the life of an online game; the screens and the table switch on it.
    CORE_ENUM(OnlineStage, uint8_t,
              Idle,         // no connection and no room (or a request that was answered with an error)
              Connecting,   // a Host / Join was sent and `joined` has not come
              Lobby,        // in a room, before the first good view
              Game,         // in a room with a table to read (`Table()`)
              Reconnecting  // the connection was lost (or `Resume` is under way); the lobby and the table stay readable, frozen
    )

    /// What the host asks of the game when the room is made (`create_room options`, read by `MahjongSeatedGame::Start`).
    struct OnlineRoomOptions {
        /// `kShortMatchHands` or `kFullMatchHands`.
        int hands = kFullMatchHands;
        /// The level of every computer seat, `kMinLevel` .. `kMaxLevel`.
        int level = LevelNumber(kDefaultLevel);
        /// Whether the players of the room may use hints and the coach (the host decides; the room keeps it).
        bool hintsAllowed = false;
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
        /// Leaves the room and forgets the session. The connection is closed by the model once the server answered.
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
        /// `left {replaced}` came: the server's close follows and carries the notice.
        bool _awaitingClose = false;
    };

    /// The session as JSON for the store of the game (`url`, `room`, `token`: a secret, keep it where the platform keeps secrets).
    [[nodiscard]] Json SessionToJson(const RoomSession& session);

    /// The session of @p json; nullopt for anything that is not an object with a `url`, a `room` that can be a room code and a `token`
    /// of the form a room hands out. The room comes back in upper case.
    [[nodiscard]] std::optional<RoomSession> SessionFromJson(const Json& json);

}  // namespace BECore::Mahjong
