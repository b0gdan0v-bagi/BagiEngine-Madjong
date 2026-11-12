#pragma once

#include "MahjongRoomsJudge.h"

#include <BECore/Rooms/ManualClock.h>
#include <BECore/Rooms/TestRandom.h>
#include <Games/SeatedGameRegistry.h>
#include <Mahjong/Online/MahjongSeatedGame.h>
#include <Mahjong/Online/OnlineClient.h>
#include <RoomClient/RoomTransport.h>
#include <Rooms/RoomHub.h>
#include <Rooms/RoomLimits.h>
#include <deque>
#include <gtest/gtest.h>
#include <map>
#include <memory>
#include <vector>

/**
 * A small world for `OnlineClient`s of the game: a `RoomHub` over `MahjongSeatedGame` (registered as `mahjong`), a clock the test moves,
 * and in-memory transports for `RoomClient`s. No socket, no thread and no `RoomServerModule`: a call of a transport into the hub is a
 * direct call, and what the hub sends is queued for the client's next `PollEvent`. The engine has such a world of its own for its demo
 * games (`engine/tests/BECore/Rooms/HubTransport.h`); this one is the game's, with the game in the registry and the judge of
 * `MahjongRoomsJudge.h` looking at every text a client receives (a seat sees its own hand and nothing of the others').
 */
namespace BECore::Mahjong::Tests {

    /// One connection between a client and the hub of a `MahjongHubWorld`.
    struct HubLink {
        ConnectionId id = 0;
        /// The hub knows the connection and nobody has closed it.
        bool open = false;
        bool closedQueued = false;
        /// The seat the receiver holds, for the judge: from its `joined`, then from the client (a seat it chose).
        int judgeSeat = -1;
        /// What the client polls.
        std::deque<RoomTransportEvent> toClient;

        void QueueText(eastl::string_view text) {
            RoomTransportEvent event;
            event.kind = RoomTransportEventKind::Text;
            event.text = eastl::string(text);
            toClient.push_back(std::move(event));
        }

        /// The one `Closed` of the connection; a second call does nothing.
        void QueueClosed(uint16_t code, eastl::string_view reason) {
            if (closedQueued) {
                return;
            }
            closedQueued = true;
            RoomTransportEvent event;
            event.kind = RoomTransportEventKind::Closed;
            event.closeCode = code;
            event.reason = eastl::string(reason);
            toClient.push_back(std::move(event));
        }
    };

    /// The connections one client made, in order (a reconnect makes a new one).
    struct HubClientLinks {
        std::vector<std::shared_ptr<HubLink>> links;

        [[nodiscard]] HubLink& Last() {
            return *links.back();
        }
    };

    class MahjongHubWorld {
    public:
        static constexpr std::chrono::seconds kGrace{30};
        static constexpr std::chrono::minutes kAbandoned{10};

        MahjongHubWorld() : _hub(MakeRegistry(), MakeSettings(), BECore::Tests::MakeSequence(kRandomSeed), MakeSink()) {}

        MahjongHubWorld(const MahjongHubWorld&) = delete;
        MahjongHubWorld& operator=(const MahjongHubWorld&) = delete;

        /// No computer delay, the usual grace; the rate limit out of the way (a game of many small messages is not what it is for).
        static RoomHubSettings MakeSettings() {
            constexpr double kUnlimitedRate = 1.0e9;
            constexpr int kUnlimitedBurst = 1000000000;
            RoomHubSettings settings;
            settings.timings = RoomTimings{kGrace, kAbandoned, std::chrono::milliseconds(0)};
            settings.rateBurst = kUnlimitedBurst;
            settings.ratePerSecond = kUnlimitedRate;
            return settings;
        }

        [[nodiscard]] RoomHub& Hub() {
            return _hub;
        }

        [[nodiscard]] RoomTime Now() const {
            return _clock.Now();
        }

        /// Moves the clock and ticks the hub (computers move, the grace runs out).
        void Advance(std::chrono::milliseconds step) {
            _clock.Advance(step);
            _hub.Tick(_clock.Now());
        }

        /// The live game of the one room of the test, or null.
        [[nodiscard]] MahjongSeatedGame* Game() const {
            return _game;
        }

        [[nodiscard]] RoomsJudgeLog& Judge() {
            return _judge;
        }

        /// The server is down: a new connection is refused (the transport's `Closed`, 1006).
        void SetRefusing(bool refusing) {
            _refusing = refusing;
        }

        /// The network breaks the connection: the hub is told, the client sees a `Closed` (1006).
        void Sever(HubLink& link) {
            if (!link.open) {
                return;
            }
            link.open = false;
            _hub.OnDisconnected(link.id, _clock.Now());
            link.QueueClosed(kRoomTransportAbnormalClosure, "connection lost");
        }

        /// The factory of one client's transports; their links are appended to @p links.
        [[nodiscard]] RoomTransportFactory Factory(HubClientLinks& links);

        /// The transport calls these.
        void Connect(const std::shared_ptr<HubLink>& link);
        void Deliver(HubLink& link, eastl::string_view text);
        void Disconnect(HubLink& link, uint16_t code, eastl::string_view reason);

    private:
        static constexpr uint64_t kRandomSeed = 11;

        /// The game of the room, with a hook so that the test can reach it.
        class TrackedGame final : public MahjongSeatedGame {
        public:
            explicit TrackedGame(MahjongHubWorld& world) : _world(world) {
                _world._game = this;
            }

            ~TrackedGame() override {
                if (_world._game == this) {
                    _world._game = nullptr;
                }
            }

        private:
            MahjongHubWorld& _world;
        };

        SeatedGameRegistry& MakeRegistry() {
            const bool registered = _registry.Register(MahjongSeatedGame::Info(), [this]() -> eastl::unique_ptr<ISeatedGame> { return eastl::make_unique<TrackedGame>(*this); });
            EXPECT_TRUE(registered);
            return _registry;
        }

        HubSink MakeSink() {
            HubSink sink;
            sink.send = [this](ConnectionId id, eastl::string text) {
                if (const auto found = _byId.find(id); found != _byId.end()) {
                    OnSent(*found->second, text);
                }
            };
            sink.close = [this](ConnectionId id, eastl::string reason) {
                const auto found = _byId.find(id);
                if (found == _byId.end()) {
                    return;
                }
                found->second->open = false;
                found->second->QueueClosed(GetCloseCode(reason), reason);
            };
            return sink;
        }

        /// Judges the text for the seat its receiver holds, then queues it.
        void OnSent(HubLink& link, const eastl::string& text) {
            const std::string_view view(text.data(), text.size());
            const auto parsed = ParseJson(text);
            const eastl::string type = parsed ? GetJsonString(*parsed, "type").value_or(eastl::string()) : eastl::string();
            // The seat of a client is what its `joined` said, then what the client chose; never what a later message claims
            if (parsed && type == "joined") {
                link.judgeSeat = static_cast<int>(GetJsonInt(*parsed, "seat").value_or(-1));
            }
            _judge.Text(view, link.judgeSeat);
            if (parsed && type == "left") {
                link.judgeSeat = -1;
            }
            link.QueueText(text);
        }

        /// What `RoomWebSocketServer` tells the client when the hub ends its connection.
        static uint16_t GetCloseCode(const eastl::string& reason) {
            constexpr uint16_t kPolicyViolation = 1008;
            constexpr uint16_t kTryAgainLater = 1013;
            if (reason == "rate_limited" || reason == "too_many_failed_joins") {
                return kPolicyViolation;
            }
            return reason == "server_full" ? kTryAgainLater : kRoomTransportNormalClosure;
        }

        // The registry is destroyed after the hub, which holds a reference to it.
        MahjongSeatedGame* _game = nullptr;
        RoomsJudgeLog _judge;
        BECore::Tests::ManualClock _clock;
        SeatedGameRegistry _registry;
        RoomHub _hub;
        std::map<ConnectionId, std::shared_ptr<HubLink>> _byId;
        ConnectionId _nextId = 1;
        bool _refusing = false;
    };

    /// An `IRoomTransport` that talks to a `MahjongHubWorld`.
    class MahjongHubTransport final : public IRoomTransport {
    public:
        MahjongHubTransport(MahjongHubWorld& world, std::shared_ptr<HubLink> link) : _world(world), _link(std::move(link)) {}

        ~MahjongHubTransport() override {
            // Letting go of a connection ends it, as closing the socket does
            if (_link->open) {
                _world.Disconnect(*_link, kRoomTransportAbnormalClosure, "dropped");
            }
        }

        std::expected<void, eastl::string> Open(eastl::string_view /*url*/) override {
            _world.Connect(_link);
            return {};
        }

        bool SendText(eastl::string_view text) override {
            if (!_link->open) {
                return false;
            }
            _world.Deliver(*_link, text);
            return true;
        }

        void Close(uint16_t code, eastl::string_view reason) override {
            _world.Disconnect(*_link, code, reason);
        }

        void Abort() override {
            _world.Disconnect(*_link, kRoomTransportAbnormalClosure, "aborted");
        }

        bool PollEvent(RoomTransportEvent& event) override {
            if (_link->toClient.empty()) {
                return false;
            }
            event = std::move(_link->toClient.front());
            _link->toClient.pop_front();
            return true;
        }

    private:
        MahjongHubWorld& _world;
        std::shared_ptr<HubLink> _link;
    };

    inline RoomTransportFactory MahjongHubWorld::Factory(HubClientLinks& links) {
        return [this, &links]() -> eastl::unique_ptr<IRoomTransport> {
            auto link = std::make_shared<HubLink>();
            links.links.push_back(link);
            return eastl::make_unique<MahjongHubTransport>(*this, link);
        };
    }

    inline void MahjongHubWorld::Connect(const std::shared_ptr<HubLink>& link) {
        if (_refusing) {
            link->QueueClosed(kRoomTransportAbnormalClosure, "connection refused");
            return;
        }
        link->id = _nextId++;
        link->open = true;
        _byId[link->id] = link;
        _hub.OnConnected(link->id, _clock.Now());
        RoomTransportEvent opened;
        opened.kind = RoomTransportEventKind::Opened;
        link->toClient.push_back(std::move(opened));
    }

    inline void MahjongHubWorld::Deliver(HubLink& link, eastl::string_view text) {
        _hub.OnMessage(link.id, text, _clock.Now());
    }

    inline void MahjongHubWorld::Disconnect(HubLink& link, uint16_t code, eastl::string_view reason) {
        if (link.open) {
            link.open = false;
            _hub.OnDisconnected(link.id, _clock.Now());
        }
        // A connection that never opened has nothing else to say, and a closed one says its Closed once
        link.QueueClosed(code, reason);
    }

    /// One person of a test: an `OnlineClient` on the transports of a world, and the way it plays (the first action it is offered, once
    /// per view).
    class HubPlayer {
    public:
        explicit HubPlayer(MahjongHubWorld& world) : _world(world), _client(MakeSettings(world)) {}

        HubPlayer(const HubPlayer&) = delete;
        HubPlayer& operator=(const HubPlayer&) = delete;

        [[nodiscard]] OnlineClient& Client() {
            return _client;
        }

        [[nodiscard]] const OnlineClient& Client() const {
            return _client;
        }

        [[nodiscard]] const OnlineTable& Table() const {
            return _client.Table();
        }

        [[nodiscard]] HubClientLinks& Links() {
            return _links;
        }

        /// Polls the client against the world's clock and tells the judge which seat it holds.
        void Update() {
            _client.Update(_world.Now());
            Sync();
        }

        /// After a command: the seat a client chose is the seat the judge compares its views with.
        void Sync() {
            if (!_links.links.empty() && _client.Seat() >= 0) {
                _links.Last().judgeSeat = _client.Seat();
            }
        }

        /// The network breaks the connection.
        void Sever() {
            _world.Sever(_links.Last());
        }

        /// Sends the first action of the table once per version. @p withNextHand false leaves the end screen alone. True when it sent.
        bool ActFirst(bool withNextHand = true) {
            const OnlineTable& table = _client.Table();
            if (_client.Stage() != OnlineStage::Game || table.actions.empty() || table.version == _actedVersion) {
                return false;
            }
            if (!withNextHand && table.actions.front().nextHand) {
                return false;
            }
            _actedVersion = table.version;
            return _client.Act(0);
        }

    private:
        RoomClientSettings MakeSettings(MahjongHubWorld& world) {
            constexpr std::chrono::milliseconds kBackoff{100};
            constexpr std::chrono::milliseconds kBackoffMax{1000};
            RoomClientSettings settings;
            settings.transportFactory = world.Factory(_links);
            // The manual clock jumps by seconds: no heartbeat to answer
            settings.pingInterval = std::chrono::milliseconds(0);
            settings.silenceLimit = std::chrono::milliseconds(0);
            settings.backoffStart = kBackoff;
            settings.backoffMax = kBackoffMax;
            return settings;
        }

        static constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();

        MahjongHubWorld& _world;
        // The links come before the client: its factory appends to them
        HubClientLinks _links;
        OnlineClient _client;
        uint64_t _actedVersion = kNever;
    };

}  // namespace BECore::Mahjong::Tests
