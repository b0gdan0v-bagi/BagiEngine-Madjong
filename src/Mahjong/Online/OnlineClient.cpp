#include "OnlineClient.h"

#include <BECore/Control/Json.h>
#include <Mahjong/Online/MahjongSeatedGame.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Rooms/RoomCode.h>

namespace BECore::Mahjong {

    namespace {

        constexpr size_t kMaxNoticeArgBytes = 32;

        constexpr eastl::string_view kHandsKey = "hands";
        constexpr eastl::string_view kLevelKey = "level";

        constexpr eastl::string_view kSessionUrlKey = "url";
        constexpr eastl::string_view kSessionRoomKey = "room";
        constexpr eastl::string_view kSessionTokenKey = "token";

        /// The reason of `left` after the player's own `leave`: nothing to say.
        constexpr eastl::string_view kLeftReasonLeft = "left";

        /// A code from the server is shown in a notice only when it is plain: letters, digits and underscores, so a hostile server cannot put a text on the screen.
        eastl::string SafeArg(eastl::string_view code) {
            eastl::string arg;
            for (const char c : code) {
                const bool plain = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
                if (plain && arg.size() < kMaxNoticeArgBytes) {
                    arg.push_back(c);
                }
            }
            return arg;
        }

        /// The ends of a connection after which asking again would get the same answer (`RoomClient/CLAUDE.md`, "Where it stops"): the session is no use any more.
        bool EndsTheSession(eastl::string_view reason) {
            return reason == RoomClientCloseReasons::kRoomGone || reason == RoomClientCloseReasons::kReplaced || reason == RoomClientCloseReasons::kGaveUp;
        }

    }  // namespace

    OnlineClient::OnlineClient(RoomClientSettings settings) : _client(std::move(settings)) {}

    OnlineClient::~OnlineClient() = default;

    // ---- Commands ----------------------------------------------------------------------------------------------------------------

    bool OnlineClient::Report(const std::expected<void, ControlError>& result) {
        if (!result) {
            SetNoticeForCode(result.error().code);
            return false;
        }
        return true;
    }

    bool OnlineClient::Host(eastl::string_view url, eastl::string_view name, const OnlineRoomOptions& options) {
        const bool handsOk = options.hands == kShortMatchHands || options.hands == kFullMatchHands;
        const bool levelOk = options.level >= kMinLevel && options.level <= kMaxLevel;
        if (!handsOk || !levelOk) {
            SetNoticeForCode(OnlineCodes::kInvalidParams);
            return false;
        }
        const std::optional<eastl::string> sanitized = SanitizePlayerName(name);
        if (!sanitized) {
            SetNoticeForCode(RoomErrors::kBadName);
            return false;
        }
        const Json json = Json::object({{"hands", options.hands}, {"level", options.level}, {std::string(kHintsAllowedKey.data(), kHintsAllowedKey.size()), options.hintsAllowed}});
        const bool sent = Report(_client.Host(url, kSeatedGameName, *sanitized, json, options.isPublic));
        if (sent) {
            _requesting = true;
            _hosted = options;
            EndBrowsing();
            Bump();
        }
        Settle();
        return sent;
    }

    bool OnlineClient::Join(eastl::string_view url, eastl::string_view code, eastl::string_view name) {
        return SendJoin(url, code, name, false);
    }

    bool OnlineClient::JoinListed(eastl::string_view url, eastl::string_view code, eastl::string_view name) {
        return SendJoin(url, code, name, true);
    }

    bool OnlineClient::SendJoin(eastl::string_view url, eastl::string_view code, eastl::string_view name, bool fromList) {
        const std::optional<eastl::string> sanitized = SanitizePlayerName(name);
        if (!sanitized) {
            SetNoticeForCode(RoomErrors::kBadName);
            return false;
        }
        const bool sent = Report(_client.Join(url, code, *sanitized));
        if (sent) {
            _requesting = true;
            ForgetHosted();
            if (fromList && _browsing) {
                // The connection stays a browsing one: an error of this join asks the list again
                _joiningListed = true;
            } else {
                EndBrowsing();
            }
        }
        Settle();
        return sent;
    }

    bool OnlineClient::Browse(eastl::string_view url) {
        if (_stage == OnlineStage::Browsing) {
            if (_rooms.pending) {
                return false;
            }
            return Report(AskForRooms(url));
        }
        if (_stage != OnlineStage::Idle) {
            SetNoticeForCode(RoomClientErrors::kWrongState);
            return false;
        }
        const bool sent = Report(_client.ListRooms(url, kSeatedGameName));
        if (sent) {
            EndBrowsing();
            _browsing = true;
            _browseUrl.assign(url.data(), url.size());
            _rooms.pending = true;
            Bump();
        }
        Settle();
        return sent;
    }

    std::expected<void, ControlError> OnlineClient::AskForRooms(eastl::string_view url) {
        std::expected<void, ControlError> result = _client.ListRooms(url, kSeatedGameName);
        if (result) {
            _rooms.pending = true;
            Bump();
        }
        return result;
    }

    bool OnlineClient::Resume(const RoomSession& session) {
        const bool sent = Report(_client.Resume(session));
        if (sent) {
            ForgetHosted();
            // The caller has this session stored already, so the revision stays
            _kept = session;
        }
        Settle();
        return sent;
    }

    bool OnlineClient::ChooseSeat(int seat) {
        if (_stage != OnlineStage::Lobby || seat < 0 || seat >= kSeatCount) {
            return false;
        }
        const bool sent = Report(_client.ChooseSeat(seat));
        Settle();
        return sent;
    }

    bool OnlineClient::Start() {
        if (_stage != OnlineStage::Lobby) {
            return false;
        }
        return Report(_client.Start());
    }

    bool OnlineClient::Act(size_t index) {
        // The client already holds the actions of a view the model did not read; the screen still shows the table before it
        if (_stage != OnlineStage::Game || !_tableIsLatest || index >= _table.actions.size()) {
            return false;
        }
        const Json& actions = _client.GetActions();
        if (!actions.is_array() || index >= actions.size()) {
            return false;
        }
        return Report(_client.SendAction(actions[index]));
    }

    bool OnlineClient::Leave() {
        if (_stage == OnlineStage::Browsing) {
            EndBrowsing();
            CloseConnection();
            Settle();
            return true;
        }
        const bool wasReconnecting = _client.GetState() == RoomClientState::Reconnecting;
        const bool sent = Report(_client.Leave());
        if (sent) {
            ForgetSession();
            ForgetHosted();
            if (wasReconnecting) {
                // There was no connection to ask on: the client closed itself
                CloseConnection();
                ForgetView();
            }
        }
        Settle();
        return sent;
    }

    void OnlineClient::Disconnect() {
        CloseConnection();
        EndBrowsing();
        ForgetHosted();
        _requesting = false;
        _awaitingClose = false;
        ForgetView();
        Settle();
    }

    bool OnlineClient::CanChoose(int seat) const {
        if (_stage != OnlineStage::Lobby || _lobby.phase != RoomPhase::Lobby || seat < 0 || seat >= kSeatCount) {
            return false;
        }
        return _lobby.seats[static_cast<size_t>(seat)].kind == BECore::SeatKind::Empty;
    }

    // ---- Frame -------------------------------------------------------------------------------------------------------------------

    void OnlineClient::Update(RoomTime now) {
        _client.Poll(now);
        HandleEvents();
    }

    void OnlineClient::Update() {
        Update(std::chrono::steady_clock::now());
    }

    void OnlineClient::HandleEvents() {
        _events.clear();
        _client.DrainEvents(_events);
        for (const RoomClientEvent& event : _events) {
            if (!Handle(event)) {
                break;
            }
        }
        Settle();
    }

    bool OnlineClient::Handle(const RoomClientEvent& event) {
        switch (event.kind) {
            case RoomClientEventKind::Connected:
                break;
            case RoomClientEventKind::Joined:
                OnJoined();
                break;
            case RoomClientEventKind::Resumed:
                OnResumed();
                break;
            case RoomClientEventKind::LobbyChanged:
                _lobbyDirty = true;
                break;
            case RoomClientEventKind::ViewChanged:
                return OnView();
            case RoomClientEventKind::Error:
                OnError(event);
                break;
            case RoomClientEventKind::Left:
                OnLeft(event);
                break;
            case RoomClientEventKind::ConnectionLost:
                // The state says the rest: Reconnecting when the client will retry, a Closed follows when not
                break;
            case RoomClientEventKind::Closed:
                OnClosed(event);
                break;
            case RoomClientEventKind::RoomList:
                OnRoomList();
                break;
        }
        return true;
    }

    void OnlineClient::OnJoined() {
        _requesting = false;
        EndBrowsing();
        ForgetView();
        KeepSession(_client.GetSession());
        Bump();
    }

    void OnlineClient::OnResumed() {
        _requesting = false;
        // The table and the lobby of before stay until the new ones come; the token is the same
        KeepSession(_client.GetSession());
        Bump();
    }

    bool OnlineClient::OnView() {
        const Json& view = _client.GetView();
        if (!_client.HasView() || !view.is_object() || view.empty()) {
            // The room says `view {}` while nobody has started the game: there is no table yet
            return true;
        }
        const std::expected<void, OnlineViewError> read = ReadOnlineView(view, _client.GetActions(), _table, _scratch);
        if (read) {
            _hasTable = true;
            _tableIsLatest = true;
            Bump();
            return true;
        }
        if (read.error().kind == OnlineViewErrorKind::Format) {
            // Reconnecting would give the same answer
            SetNotice(kOnlineViewFormatKey);
            ForgetSession();
            CloseConnection();
            ForgetView();
            return false;
        }
        LOG_WARNING(Format("Online: a view was not read: {}", read.error().text.c_str()));
        _tableIsLatest = false;
        SetNotice(kOnlineBadViewKey);
        return true;
    }

    void OnlineClient::OnError(const RoomClientEvent& event) {
        if (event.ref == RoomMessageTypes::kListRooms) {
            // A refused list is no answer to a join: the connection and the last list stay
            _rooms.pending = false;
            SetNoticeForCode(event.code);
            return;
        }
        // The client counts any error as the answer to a request that waits for `joined`
        _requesting = false;
        if (!IsSilentOnlineCode(event.code)) {
            SetNoticeForCode(event.code);
        }
        if (_joiningListed) {
            // The room may be gone or full: the player gets the notice and a fresh list on the same connection
            _joiningListed = false;
            if (_browsing && !AskForRooms(_browseUrl)) {
                EndBrowsing();
            }
        }
    }

    void OnlineClient::OnRoomList() {
        if (!_browsing) {
            return;
        }
        const RoomListMessage& list = _client.GetRoomList();
        size_t count = 0;
        _rooms.rooms.resize(list.rooms.size());
        for (const RoomListing& listing : list.rooms) {
            if (listing.started) {
                continue;
            }
            OnlineListedRoom& room = _rooms.rooms[count++];
            room.code = listing.room;
            room.host = listing.host;
            room.free = std::max(0, listing.seats - listing.taken);
            room.hands.reset();
            room.level.reset();
            room.hintsAllowed = GetJsonBool(listing.options, kHintsAllowedKey);
            if (const std::optional<int64_t> hands = GetJsonInt(listing.options, kHandsKey); hands && (*hands == kShortMatchHands || *hands == kFullMatchHands)) {
                room.hands = static_cast<int>(*hands);
            }
            if (const std::optional<int64_t> level = GetJsonInt(listing.options, kLevelKey); level && *level >= kMinLevel && *level <= kMaxLevel) {
                room.level = static_cast<int>(*level);
            }
        }
        _rooms.rooms.resize(count);
        _rooms.loaded = true;
        _rooms.pending = false;
        _rooms.more = list.more;
        Bump();
    }

    void OnlineClient::OnLeft(const RoomClientEvent& event) {
        _requesting = false;
        ForgetHosted();
        ForgetView();
        ForgetSession();
        if (event.code == RoomClientCloseReasons::kReplaced) {
            // The server closes next, and that close carries the notice
            _awaitingClose = true;
        } else if (event.code != kLeftReasonLeft) {
            SetNoticeForCode(event.code);
        }
    }

    void OnlineClient::OnClosed(const RoomClientEvent& event) {
        _requesting = false;
        EndBrowsing();
        ForgetHosted();
        _awaitingClose = false;
        ForgetView();
        if (EndsTheSession(event.code)) {
            ForgetSession();
        }
        if (event.code.empty() && event.message == OnlineCodes::kIdle) {
            // An ordinary close (1000) whose reason is the server's: a connection in no room that was left alone
            SetNoticeForCode(OnlineCodes::kIdle);
        } else if (event.code.empty()) {
            // A connection that did not open, or that the server ended without a reason
            SetNoticeForCode(RoomClientErrors::kConnectFailed);
        } else {
            SetNoticeForCode(event.code);
        }
    }

    // ---- Keeping the model up to date ---------------------------------------------------------------------------------------------

    void OnlineClient::Settle() {
        if (_client.GetState() == RoomClientState::Connected && !_requesting && !_awaitingClose && !_browsing) {
            // After an error of a request or a `left`: an open connection with nothing to do would be closed by the server some time later, with a notice nobody asked for
            CloseConnection();
        }
        SyncLobby();
        SyncStage();
    }

    void OnlineClient::SyncLobby() {
        const bool seatMoved = _client.GetSeat() != _lobbySeat || _client.IsHost() != _lobbyHost;
        if (_lobbyDirty || seatMoved) {
            RebuildLobby();
        }
    }

    void OnlineClient::RebuildLobby() {
        _lobbyDirty = false;
        _lobbySeat = _client.GetSeat();
        _lobbyHost = _client.IsHost();

        const LobbySnapshot& source = _client.GetLobby();
        _lobby.room = source.room;
        _lobby.phase = source.phase;
        _lobby.humans = 0;
        for (OnlineLobbySeat& seat : _lobby.seats) {
            seat.kind = BECore::SeatKind::Empty;
            seat.name.clear();
            seat.connected = false;
            seat.mine = false;
            seat.host = false;
            seat.away = false;
        }
        for (const LobbySeat& from : source.seats) {
            if (from.seat < 0 || from.seat >= kSeatCount) {
                continue;
            }
            OnlineLobbySeat& seat = _lobby.seats[static_cast<size_t>(from.seat)];
            const bool human = from.kind == BECore::SeatKind::Human;
            seat.kind = from.kind;
            if (human) {
                seat.name = from.name;
                ++_lobby.humans;
            }
            seat.connected = human && from.connected;
            seat.mine = from.seat == _lobbySeat;
            seat.host = from.seat == source.hostSeat;
            seat.away = human && !from.connected;
        }
        _lobby.canStart = _lobbyHost && source.phase == RoomPhase::Lobby;
        _lobby.hintsAllowed = GetJsonBool(source.options, kHintsAllowedKey);
        Bump();
    }

    OnlineStage OnlineClient::ComputeStage() const {
        switch (_client.GetState()) {
            case RoomClientState::Disconnected:
                return OnlineStage::Idle;
            case RoomClientState::Connecting:
                return _browsing && !_requesting ? OnlineStage::Browsing : OnlineStage::Connecting;
            case RoomClientState::Connected:
                if (_requesting) {
                    return OnlineStage::Connecting;
                }
                return _browsing ? OnlineStage::Browsing : OnlineStage::Idle;
            case RoomClientState::InRoom:
                return _hasTable ? OnlineStage::Game : OnlineStage::Lobby;
            case RoomClientState::Reconnecting:
                return OnlineStage::Reconnecting;
        }
        return OnlineStage::Idle;
    }

    void OnlineClient::SyncStage() {
        const OnlineStage stage = ComputeStage();
        if (stage != _stage) {
            _stage = stage;
            Bump();
        }
    }

    void OnlineClient::CloseConnection() {
        _client.Disconnect();
        _discarded.clear();
        _client.DrainEvents(_discarded);
        _discarded.clear();
    }

    void OnlineClient::ForgetView() {
        _hasTable = false;
        _tableIsLatest = false;
        _lobby = OnlineLobby();
        _lobbyDirty = false;
        _lobbySeat = -1;
        _lobbyHost = false;
        Bump();
    }

    void OnlineClient::ForgetHosted() {
        if (_hosted) {
            _hosted.reset();
            Bump();
        }
    }

    void OnlineClient::EndBrowsing() {
        const bool somethingToForget = _browsing || _joiningListed || _rooms.loaded || _rooms.pending || _rooms.more || !_rooms.rooms.empty();
        _browsing = false;
        _joiningListed = false;
        _browseUrl.clear();
        _rooms.loaded = false;
        _rooms.pending = false;
        _rooms.more = false;
        _rooms.rooms.clear();
        if (somethingToForget) {
            Bump();
        }
    }

    void OnlineClient::KeepSession(const RoomSession& session) {
        if (_kept.url == session.url && _kept.room == session.room && _kept.token == session.token) {
            return;
        }
        _kept = session;
        ++_sessionRevision;
        Bump();
    }

    void OnlineClient::ForgetSession() {
        if (_kept.url.empty() && _kept.room.empty() && _kept.token.empty()) {
            return;
        }
        _kept = RoomSession();
        ++_sessionRevision;
        Bump();
    }

    void OnlineClient::SetNotice(eastl::string_view key, eastl::string_view arg) {
        _notice.key = key;
        _notice.arg.assign(arg.data(), arg.size());
        ++_notice.serial;
        Bump();
    }

    void OnlineClient::SetNoticeForCode(eastl::string_view code) {
        const eastl::string_view key = OnlineErrorKey(code);
        if (key == kOnlineUnknownKey) {
            SetNotice(key, SafeArg(code));
        } else {
            SetNotice(key);
        }
    }

    void OnlineClient::Bump() {
        ++_version;
    }

    // ---- The session as JSON -----------------------------------------------------------------------------------------------------

    Json SessionToJson(const RoomSession& session) {
        Json json = Json::object();
        json[std::string(kSessionUrlKey.data(), kSessionUrlKey.size())] = std::string(session.url.data(), session.url.size());
        json[std::string(kSessionRoomKey.data(), kSessionRoomKey.size())] = std::string(session.room.data(), session.room.size());
        json[std::string(kSessionTokenKey.data(), kSessionTokenKey.size())] = std::string(session.token.data(), session.token.size());
        return json;
    }

    std::optional<RoomSession> SessionFromJson(const Json& json) {
        const std::optional<eastl::string> url = GetJsonString(json, kSessionUrlKey);
        const std::optional<eastl::string> room = GetJsonString(json, kSessionRoomKey);
        const std::optional<eastl::string> token = GetJsonString(json, kSessionTokenKey);
        if (!url || !room || !token || url->empty()) {
            return std::nullopt;
        }
        const std::optional<eastl::string> code = NormalizeRoomCode(*room);
        if (!code || !IsWellFormedToken(*token)) {
            return std::nullopt;
        }
        RoomSession session;
        session.url = *url;
        session.room = *code;
        session.token = *token;
        return session;
    }

}  // namespace BECore::Mahjong
