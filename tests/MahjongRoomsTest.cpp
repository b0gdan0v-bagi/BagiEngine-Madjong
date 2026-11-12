#include "MahjongRoomsJudge.h"

#include <BECore/Control/Json.h>
#include <EASTL/sort.h>
#include <Mahjong/MahjongStateJson.h>
#include <Mahjong/Online/MahjongSeatedGame.h>
#include <Rooms/RoomErrors.h>
#include <Rooms/RoomHub.h>
#include <Rooms/RoomProtocol.h>
#include <gtest/gtest.h>

/**
 * `MahjongSeatedGame` in rooms: a `RoomHub` (in memory, a clock the test moves, no computer delay) plays whole hands and matches, and
 * every text it sends to every client goes through the judge of `MahjongRoomsJudge.h`. The harness is this file's own (a map from a
 * connection to what it received); the engine's one is in the engine's test target.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        constexpr milliseconds kTick{10};
        constexpr int kTickLimit = 60000;
        constexpr uint64_t kNever = std::numeric_limits<uint64_t>::max();
        constexpr int kPersonSeats = 2;
        constexpr int kStaleEvery = 40;
        constexpr int kStallSeconds = 5;
        constexpr int kGraceSeconds = 30;
        constexpr int kWarmUpVersion = 20;
        constexpr double kUnlimitedRate = 1.0e9;
        constexpr int kUnlimitedBurst = 1000000000;
        constexpr const char* kShortMatch = "{\"hands\": 8, \"level\": 1}";

        std::string Std(const eastl::string& text) {
            return std::string(text.c_str(), text.size());
        }

        std::string Dump(const Json& json) {
            return Std(DumpJson(json));
        }

        Json Parse(const char* text) {
            return ParseJson(text).value();
        }

        std::string StringAt(const Json& object, const char* key) {
            const auto found = object.is_object() ? object.find(key) : object.end();
            return found != object.end() && found->is_string() ? found->get<std::string>() : std::string();
        }

        int64_t IntAt(const Json& object, const char* key, int64_t fallback = -1000) {
            const auto found = object.is_object() ? object.find(key) : object.end();
            return found != object.end() && found->is_number_integer() ? found->get<int64_t>() : fallback;
        }

        constexpr uint64_t kMixIncrement = 0x9E3779B97F4A7C15ULL;
        constexpr uint64_t kMixFirstMultiplier = 0xBF58476D1CE4E5B9ULL;
        constexpr uint64_t kMixSecondMultiplier = 0x94D049BB133111EBULL;
        constexpr int kMixFirstShift = 30;
        constexpr int kMixSecondShift = 27;
        constexpr int kMixLastShift = 31;

        /// A deterministic stream of well-mixed words (splitmix64)
        RandomFn MakeRandom(uint64_t seed) {
            auto state = eastl::make_shared<uint64_t>(seed);
            return [state]() -> uint64_t {
                uint64_t z = (*state += kMixIncrement);
                z = (z ^ (z >> kMixFirstShift)) * kMixFirstMultiplier;
                z = (z ^ (z >> kMixSecondShift)) * kMixSecondMultiplier;
                return z ^ (z >> kMixLastShift);
            };
        }

        /// A game whose view gives something away, to prove that the judge and the equal-views test can fail.
        enum class Leak : uint8_t {
            None,
            /// The neighbour's concealed tiles in a key of their own
            ExtraKey,
            /// The neighbour's concealed tiles in the place of the own `hand` (the schema cannot see it, the equal views can)
            NeighbourAsOwn
        };

        class LeakyMahjongSeatedGame : public MahjongSeatedGame {
        public:
            explicit LeakyMahjongSeatedGame(Leak leak) : _leak(leak) {}

            [[nodiscard]] std::expected<Json, ControlError> GetSeatView(int seat) const override {
                std::expected<Json, ControlError> view = MahjongSeatedGame::GetSeatView(seat);
                if (!view || !view->contains("hand")) {
                    return view;
                }
                const Json tiles = TilesToJson(GetMatch().Hand().GetHand(NextSeat(seat)).concealed);
                if (_leak == Leak::ExtraKey) {
                    (*view)["neighbourHand"] = tiles;
                } else {
                    (*view)["hand"] = tiles;
                }
                return view;
            }

        private:
            Leak _leak;
        };

        struct Received {
            std::string text;
            Json json;
            std::string type;
        };

        /// One fake connection: what the hub sent it and what it learned from `joined`.
        struct Client {
            ConnectionId id = 0;
            std::vector<Received> received;
            bool closed = false;
            std::string room;
            int seat = -1;
            std::string token;
            /// The version of the view this client last answered, so that it answers each view once
            uint64_t actedVersion = kNever;

            [[nodiscard]] size_t Count(std::string_view type) const {
                size_t count = 0;
                for (const Received& message : received) {
                    count += message.type == type ? 1 : 0;
                }
                return count;
            }

            [[nodiscard]] const Received* Last(std::string_view type) const {
                for (size_t index = received.size(); index > 0; --index) {
                    if (received[index - 1].type == type) {
                        return &received[index - 1];
                    }
                }
                return nullptr;
            }

            [[nodiscard]] std::string LastErrorCode() const {
                const Received* error = Last("error");
                return error ? StringAt(error->json, "code") : std::string();
            }

            [[nodiscard]] std::string Types() const {
                std::string types;
                for (const Received& message : received) {
                    types += (types.empty() ? "" : ",") + message.type;
                }
                return types;
            }

            /// The state object of the last view ({} before the first)
            [[nodiscard]] Json State() const {
                const Received* view = Last("view");
                const Json* state = view ? FindJsonMember(view->json, "view") : nullptr;
                return state ? *state : Json::object();
            }

            [[nodiscard]] std::string Phase() const {
                return StringAt(State(), "phase");
            }

            [[nodiscard]] uint64_t ViewVersion() const {
                const Received* view = Last("view");
                return view ? static_cast<uint64_t>(IntAt(view->json, "version", 0)) : 0;
            }

            [[nodiscard]] Json Actions() const {
                const Received* view = Last("view");
                const Json* actions = view ? FindJsonMember(view->json, "actions") : nullptr;
                return actions && actions->is_array() ? *actions : Json::array();
            }
        };

        /// A `RoomHub` over `MahjongSeatedGame` with fake clients. Every text the hub sends is judged against the seat the client holds.
        class RoomsHarness {
        public:
            explicit RoomsHarness(Leak leak = Leak::None, uint64_t randomSeed = 42) : _hub(_registry, MakeSettings(), MakeRandom(randomSeed), MakeSink()) {
                const auto factory = [this, leak]() -> eastl::unique_ptr<ISeatedGame> {
                    if (leak == Leak::None) {
                        return eastl::make_unique<Tracked<MahjongSeatedGame>>(this);
                    }
                    return eastl::make_unique<Tracked<LeakyMahjongSeatedGame>>(this, leak);
                };
                EXPECT_TRUE(_registry.Register(MahjongSeatedGame::Info(), factory));
            }

            /// No computer delay, the usual grace; the rate limit out of the way (a game of many small messages is not what it is for)
            static RoomHubSettings MakeSettings() {
                RoomHubSettings settings;
                settings.timings = RoomTimings{seconds(kGraceSeconds), std::chrono::minutes(10), milliseconds(0)};
                settings.rateBurst = kUnlimitedBurst;
                settings.ratePerSecond = kUnlimitedRate;
                return settings;
            }

            RoomHub& Hub() {
                return _hub;
            }

            /// The live game of the one room of the test, or null
            [[nodiscard]] MahjongSeatedGame* Game() const {
                return _game;
            }

            [[nodiscard]] RoomsJudgeLog& Judge() {
                return _judge;
            }

            void SetFailOnOffence(bool fail) {
                _failOnOffence = fail;
            }

            void Tick(milliseconds step = kTick) {
                _now += step;
                _hub.Tick(_now);
            }

            ConnectionId Connect() {
                const ConnectionId id = _nextConnection++;
                Client client;
                client.id = id;
                _clients.emplace(id, std::move(client));
                _hub.OnConnected(id, _now);
                return id;
            }

            void Disconnect(ConnectionId id) {
                _hub.OnDisconnected(id, _now);
            }

            Client& At(ConnectionId id) {
                return _clients[id];
            }

            void SendText(ConnectionId id, const std::string& text) {
                _hub.OnMessage(id, eastl::string_view(text.data(), text.size()), _now);
            }

            void Send(ConnectionId id, const ClientMessage& message) {
                const eastl::string text = SerializeClientMessage(message);
                _hub.OnMessage(id, text, _now);
            }

            void Create(ConnectionId id, const char* name, const char* options = kShortMatch) {
                Send(id, CreateRoomMessage{eastl::string(kSeatedGameName.data(), kSeatedGameName.size()), eastl::string(name), Parse(options)});
            }

            void JoinByName(ConnectionId id, const std::string& code, const char* name) {
                JoinRoomMessage message;
                message.code = eastl::string(code.c_str());
                message.name = eastl::string(name);
                Send(id, message);
            }

            void JoinByToken(ConnectionId id, const std::string& code, const std::string& token) {
                JoinRoomMessage message;
                message.code = eastl::string(code.c_str());
                message.token = eastl::string(token.c_str());
                Send(id, message);
            }

            void Start(ConnectionId id) {
                Send(id, StartMessage{});
            }

            /// A room of @p people clients (the host first), not started; the seats left over are the computers' on `start`.
            std::vector<ConnectionId> MakeLobby(int people, const char* options = kShortMatch) {
                std::vector<ConnectionId> ids;
                const ConnectionId host = Connect();
                Create(host, "P0", options);
                ids.push_back(host);
                for (int index = 1; index < people; ++index) {
                    const ConnectionId guest = Connect();
                    JoinByName(guest, At(host).room, ("P" + std::to_string(index)).c_str());
                    ids.push_back(guest);
                }
                return ids;
            }

            /// A person plays the first action its last view lists, once per view. @p withNextHand false leaves the end screen alone.
            /// True when it sent something.
            bool Act(ConnectionId id, bool withNextHand = true) {
                Client& client = At(id);
                const Json actions = client.Actions();
                if (actions.empty() || client.ViewVersion() == client.actedVersion) {
                    return false;
                }
                if (!withNextHand && StringAt(actions[0], "type") == "next_hand") {
                    return false;
                }
                client.actedVersion = client.ViewVersion();
                ActionMessage message;
                message.version = client.ViewVersion();
                message.action = actions[0];
                Send(id, message);
                return true;
            }

        private:
            template <typename GameType>
            class Tracked : public GameType {
            public:
                template <typename... Args>
                explicit Tracked(RoomsHarness* harness, Args&&... args) : GameType(std::forward<Args>(args)...), _harness(harness) {
                    _harness->_game = this;
                }

                ~Tracked() override {
                    if (_harness->_game == this) {
                        _harness->_game = nullptr;
                    }
                }

            private:
                RoomsHarness* _harness;
            };

            HubSink MakeSink() {
                HubSink sink;
                sink.send = [this](ConnectionId id, eastl::string text) { OnSent(id, Std(text)); };
                sink.close = [this](ConnectionId id, eastl::string) { At(id).closed = true; };
                return sink;
            }

            void OnSent(ConnectionId id, std::string text) {
                Client& client = At(id);
                Received message;
                message.text = text;
                const auto parsed = ParseJson(eastl::string_view(text.data(), text.size()));
                if (parsed) {
                    message.json = *parsed;
                    message.type = StringAt(message.json, "type");
                }
                // The seat of a client is what its `joined` said, never what a later message claims
                if (message.type == "joined") {
                    client.room = StringAt(message.json, "room");
                    client.seat = static_cast<int>(IntAt(message.json, "seat"));
                    client.token = StringAt(message.json, "token");
                }
                for (const std::string& offence : _judge.Text(text, client.seat)) {
                    if (_failOnOffence) {
                        ADD_FAILURE() << "seat " << client.seat << ": " << offence << "\n" << text;
                    }
                }
                if (message.type == "left") {
                    client.room.clear();
                    client.seat = -1;
                }
                client.received.push_back(std::move(message));
            }

            // The hub (and with it the game) is destroyed before the members its game unregisters from
            MahjongSeatedGame* _game = nullptr;
            std::map<ConnectionId, Client> _clients;
            RoomsJudgeLog _judge;
            SeatedGameRegistry _registry;
            RoomHub _hub;
            RoomTime _now{seconds(1000)};
            ConnectionId _nextConnection = 1;
            bool _failOnOffence = true;
        };

        /// Ticks the clock and lets the persons play until @p done; false when it does not come in time.
        template <typename Done>
        bool RunUntil(RoomsHarness& harness, const std::vector<ConnectionId>& persons, Done done, bool withNextHand = true) {
            for (int step = 0; step < kTickLimit; ++step) {
                if (done()) {
                    return true;
                }
                harness.Tick();
                for (const ConnectionId id : persons) {
                    harness.Act(id, withNextHand);
                }
            }
            return done();
        }

        bool AllInPhase(RoomsHarness& harness, const std::vector<ConnectionId>& persons, const char* phase) {
            for (const ConnectionId id : persons) {
                if (harness.At(id).Phase() != phase) {
                    return false;
                }
            }
            return true;
        }

        void ExpectNoErrors(RoomsHarness& harness, const std::vector<ConnectionId>& persons) {
            for (const ConnectionId id : persons) {
                EXPECT_EQ(harness.At(id).Count("error"), 0u) << harness.At(id).LastErrorCode();
            }
        }

        void ExpectJudgedClean(RoomsHarness& harness) {
            EXPECT_GT(harness.Judge().Texts(), 0u);
            EXPECT_EQ(harness.Judge().NonText(), 0u);
            EXPECT_TRUE(harness.Judge().Offences().empty()) << harness.Judge().Offences().front();
        }

        /// The own concealed tiles of the seat as the game really holds them
        Json OwnTilesOf(const RoomsHarness& harness, int seat) {
            return TilesToJson(harness.Game()->GetMatch().Hand().GetHand(seat).concealed);
        }

        /// Everything the game says to one seat, as one text: a refused message must leave it as it was
        std::string SeatText(const RoomsHarness& harness, int seat) {
            return std::to_string(harness.Game()->GetStateVersion()) + Dump(harness.Game()->GetSeatView(seat).value()) + Dump(harness.Game()->GetSeatActions(seat).value());
        }

    }  // namespace

    // ----- (a) two people and two computers -----

    TEST(MahjongRoomsPlayTest, TwoPeopleAndTwoComputersPlayAHandThenAShortMatchAndEveryTextIsJudged) {
        RoomsHarness harness;
        const std::vector<ConnectionId> people = harness.MakeLobby(kPersonSeats);
        harness.Start(people[0]);
        ASSERT_NE(harness.Game(), nullptr);
        for (const ConnectionId id : people) {
            EXPECT_EQ(harness.At(id).Count("view"), 1u) << "the start sends each person its view";
        }

        // the first hand to its end; the people leave the end screen alone
        ASSERT_TRUE(RunUntil(harness, people, [&] { return AllInPhase(harness, people, "hand_over"); }, false));
        for (const ConnectionId id : people) {
            EXPECT_TRUE(harness.At(id).State().contains("result"));
            EXPECT_EQ(harness.At(id).Actions(), Json::array({Parse("{\"type\":\"next_hand\"}")}));
        }
        // the computers are ready at once and do not take the end screen from the people
        for (int step = 0; step < 20; ++step) {
            harness.Tick();
        }
        EXPECT_EQ(harness.At(people[0]).State()["ready"], Json::array({false, false, true, true}));
        EXPECT_TRUE(AllInPhase(harness, people, "hand_over"));
        EXPECT_EQ(harness.Game()->GetMatch().History().size(), 0u);

        // both read it and are ready: the hand is settled and the next one is dealt
        ASSERT_TRUE(RunUntil(harness, people, [&] { return harness.Game()->GetMatch().History().size() == 1u && AllInPhase(harness, people, "playing"); }));
        EXPECT_EQ(harness.At(people[0]).State()["ready"], Json::array({false, false, false, false}));
        EXPECT_FALSE(harness.At(people[0]).State().contains("result"));

        // the rest of the match
        ASSERT_TRUE(RunUntil(harness, people, [&] { return harness.Game()->IsFinished(); }));
        harness.Tick();
        for (const ConnectionId id : people) {
            EXPECT_EQ(harness.At(id).Phase(), "match_over");
            EXPECT_TRUE(IntAt(harness.At(id).State(), "handsPlayed") == IntAt(harness.At(id).State(), "handsTotal"));
            const Received* view = harness.At(id).Last("view");
            ASSERT_NE(view, nullptr);
            EXPECT_TRUE(view->json["finished"].get<bool>());
        }
        ExpectNoErrors(harness, people);
        ExpectJudgedClean(harness);
    }

    // ----- (b) four people -----

    TEST(MahjongRoomsPlayTest, FourPeopleWithStaleActionsPlayAHandAndOnlyTheStaleOnesAreRefused) {
        RoomsHarness harness;
        const std::vector<ConnectionId> people = harness.MakeLobby(kSeatCount);
        harness.Start(people[0]);
        ASSERT_NE(harness.Game(), nullptr);

        size_t stale = 0;
        for (int step = 0; step < kTickLimit && !AllInPhase(harness, people, "hand_over"); ++step) {
            harness.Tick();
            for (const ConnectionId id : people) {
                harness.Act(id, false);
            }
            if (step % kStaleEvery == 0) {
                // an action of an old view: the first action of the last one, a version too low
                const ConnectionId id = people[static_cast<size_t>(step / kStaleEvery) % people.size()];
                ActionMessage message;
                message.version = harness.At(id).ViewVersion() - 1;
                message.action = harness.At(id).Actions().empty() ? Parse("{\"type\":\"pass\"}") : harness.At(id).Actions()[0];
                const uint64_t before = harness.Game()->GetStateVersion();
                harness.Send(id, ActionMessage{message});
                EXPECT_EQ(harness.At(id).LastErrorCode(), "stale_action");
                EXPECT_EQ(harness.Game()->GetStateVersion(), before) << "a stale action changes nothing";
                ++stale;
            }
        }
        ASSERT_TRUE(AllInPhase(harness, people, "hand_over"));
        EXPECT_GT(stale, 0u);
        size_t errors = 0;
        for (const ConnectionId id : people) {
            for (const Received& message : harness.At(id).received) {
                if (message.type == "error") {
                    EXPECT_EQ(StringAt(message.json, "code"), "stale_action");
                    ++errors;
                }
            }
        }
        EXPECT_EQ(errors, stale);
        // all four ready by hand: the next hand is dealt
        ASSERT_TRUE(RunUntil(harness, people, [&] { return harness.Game()->GetMatch().History().size() == 1u && AllInPhase(harness, people, "playing"); }));
        ExpectJudgedClean(harness);
    }

    // ----- (c) a person drops -----

    namespace {

        /// Ticks until the game waits for @p seat (it has actions) while the other persons answer; false when it does not come.
        bool RunUntilSeatWaits(RoomsHarness& harness, const std::vector<ConnectionId>& others, int seat) {
            for (int step = 0; step < kTickLimit; ++step) {
                if (!harness.Game()->GetSeatActions(seat).value().empty()) {
                    return true;
                }
                harness.Tick();
                for (const ConnectionId id : others) {
                    harness.Act(id, false);
                }
            }
            return false;
        }

        void ExpectNothingMovesFor(RoomsHarness& harness, const std::vector<ConnectionId>& others, int seconds_) {
            const uint64_t before = harness.Game()->GetStateVersion();
            for (int second = 0; second < seconds_; ++second) {
                harness.Tick(seconds(1));
                for (const ConnectionId id : others) {
                    harness.Act(id, false);
                }
            }
            EXPECT_EQ(harness.Game()->GetStateVersion(), before) << "nothing is played for a seat inside the grace";
        }

        /// The person comes back on a new connection with its token: `joined`, `lobby`, `view`, and the view is the seat's own.
        ConnectionId ReturnWithToken(RoomsHarness& harness, const std::string& room, const std::string& token, int seat) {
            const ConnectionId back = harness.Connect();
            harness.JoinByToken(back, room, token);
            EXPECT_EQ(harness.At(back).Types(), "joined,lobby,view");
            EXPECT_EQ(harness.At(back).seat, seat);
            const Received* view = harness.At(back).Last("view");
            EXPECT_NE(view, nullptr);
            if (view != nullptr) {
                EXPECT_EQ(IntAt(view->json, "seat"), seat);
                EXPECT_EQ(IntAt(harness.At(back).State(), "seat"), seat);
                EXPECT_EQ(harness.At(back).State()["hand"], OwnTilesOf(harness, seat));
                EXPECT_EQ(static_cast<uint64_t>(IntAt(view->json, "version")), harness.Game()->GetStateVersion());
            }
            return back;
        }

    }  // namespace

    TEST(MahjongRoomsPlayTest, APersonWhoDropsMidHandIsNotPlayedForInsideTheGraceAndAComputerPlaysAfterIt) {
        RoomsHarness harness;
        std::vector<ConnectionId> people = harness.MakeLobby(kPersonSeats);
        harness.Start(people[0]);
        ASSERT_NE(harness.Game(), nullptr);
        const std::string room = harness.At(people[1]).room;
        const std::string token = harness.At(people[1]).token;
        constexpr int kAway = 1;
        ASSERT_EQ(harness.At(people[1]).seat, kAway);
        ASSERT_TRUE(RunUntil(harness, people, [&] { return harness.Game()->GetStateVersion() >= kWarmUpVersion; }, false));

        harness.Disconnect(people[1]);
        ASSERT_TRUE(RunUntilSeatWaits(harness, {people[0]}, kAway));
        ExpectNothingMovesFor(harness, {people[0]}, kStallSeconds);

        // past the grace a computer plays the seat
        const uint64_t stalledAt = harness.Game()->GetStateVersion();
        harness.Tick(seconds(kGraceSeconds));
        for (int step = 0; step < kTickLimit && harness.Game()->GetStateVersion() == stalledAt; ++step) {
            harness.Tick();
        }
        EXPECT_GT(harness.Game()->GetStateVersion(), stalledAt) << "a computer plays the absent seat after the grace";

        // the person returns with the token, gets its own view, and is in control again
        const ConnectionId back = ReturnWithToken(harness, room, token, kAway);
        const std::vector<ConnectionId> playing = {people[0], back};
        ASSERT_TRUE(RunUntilSeatWaits(harness, {people[0]}, kAway));
        const uint64_t waiting = harness.Game()->GetStateVersion();
        harness.Tick();
        harness.Tick();
        EXPECT_EQ(harness.Game()->GetStateVersion(), waiting) << "the returned person's seat is no longer a computer's";
        ASSERT_TRUE(RunUntil(harness, playing, [&] { return AllInPhase(harness, playing, "hand_over"); }, false));
        ExpectNoErrors(harness, playing);
        ExpectJudgedClean(harness);
    }

    TEST(MahjongRoomsPlayTest, APersonWhoDropsOnTheEndScreenIsReadyForByAComputerAfterTheGrace) {
        RoomsHarness harness;
        const std::vector<ConnectionId> people = harness.MakeLobby(kPersonSeats);
        harness.Start(people[0]);
        ASSERT_NE(harness.Game(), nullptr);
        const std::string room = harness.At(people[1]).room;
        const std::string token = harness.At(people[1]).token;
        ASSERT_TRUE(RunUntil(harness, people, [&] { return AllInPhase(harness, people, "hand_over"); }, false));

        harness.Disconnect(people[1]);
        harness.Act(people[0]);  // the host is ready
        for (int step = 0; step < 20; ++step) {
            harness.Tick();
        }
        EXPECT_EQ(harness.At(people[0]).State()["ready"], Json::array({true, false, true, true}));
        ExpectNothingMovesFor(harness, {people[0]}, kStallSeconds);
        EXPECT_EQ(harness.Game()->GetMatch().History().size(), 0u);

        harness.Tick(seconds(kGraceSeconds));
        for (int step = 0; step < 20; ++step) {
            harness.Tick();
        }
        EXPECT_EQ(harness.Game()->GetMatch().History().size(), 1u) << "the computer's next_hand for the absent seat settles the hand";

        const ConnectionId back = ReturnWithToken(harness, room, token, 1);
        EXPECT_EQ(harness.At(back).Phase(), "playing");
        EXPECT_EQ(harness.At(back).State()["ready"], Json::array({false, false, false, false}));
        ExpectJudgedClean(harness);
    }

    // ----- (d) the judge can fail -----

    TEST(MahjongRoomsJudgeTest, AGameThatSendsTheNeighboursHandIsCaughtOnTheWire) {
        RoomsHarness harness(Leak::ExtraKey);
        harness.SetFailOnOffence(false);
        const std::vector<ConnectionId> people = harness.MakeLobby(kPersonSeats);
        harness.Start(people[0]);
        ASSERT_TRUE(RunUntil(harness, people, [&] { return AllInPhase(harness, people, "hand_over"); }, false));
        ASSERT_FALSE(harness.Judge().Offences().empty()) << "the judge must be able to fail";
        for (const std::string& offence : harness.Judge().Offences()) {
            EXPECT_NE(offence.find("neighbourHand"), std::string::npos) << offence;
        }
    }

    namespace {

        /// A `view` message of an honest game for @p seat, as the hub would send it
        std::string ViewTextOf(const MahjongSeatedGame& game, int seat) {
            return Std(MakeViewMessage("ABCDE", seat, game.GetStateVersion(), game.IsFinished(), game.GetSeatView(seat).value(), game.GetSeatActions(seat).value()));
        }

        std::string Mutated(const std::string& text, const std::function<void(Json&)>& change) {
            Json json = Parse(text.c_str());
            change(json);
            return Dump(json);
        }

        bool Says(const std::vector<std::string>& offences, const char* part) {
            return std::any_of(offences.begin(), offences.end(), [&](const std::string& offence) { return offence.find(part) != std::string::npos; });
        }

        MahjongSeatedGame StartedGame() {
            MahjongSeatedGame game;
            EXPECT_TRUE(game.Start(7, Parse(kShortMatch)).has_value());
            return game;
        }

        void PlayToHandOver(MahjongSeatedGame& game) {
            for (int step = 0; step < kTickLimit && PhaseOfMatch(game.GetMatch()) == SessionPhase::Playing; ++step) {
                for (int seat = 0; seat < kSeatCount; ++seat) {
                    if (const std::optional<Json> action = game.ChooseComputerAction(seat)) {
                        ASSERT_TRUE(game.PerformSeatAction(seat, *action).has_value());
                        break;
                    }
                }
            }
        }

    }  // namespace

    TEST(MahjongRoomsJudgeTest, HonestViewsPassTheJudgeAtEveryStageOfAHand) {
        MahjongSeatedGame game = StartedGame();
        for (int seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_TRUE(JudgeRoomMessage(ViewTextOf(game, seat), seat).empty()) << seat;
        }
        PlayToHandOver(game);
        ASSERT_EQ(PhaseOfMatch(game.GetMatch()), SessionPhase::HandOver);
        for (int seat = 0; seat < kSeatCount; ++seat) {
            const std::vector<std::string> offences = JudgeRoomMessage(ViewTextOf(game, seat), seat);
            EXPECT_TRUE(offences.empty()) << seat << ": " << (offences.empty() ? "" : offences.front());
        }
        EXPECT_TRUE(JudgeRoomMessage(Std(MakeViewMessage("ABCDE", 0, 0, false, Json::object(), Json::array())), 0).empty()) << "the view before the start is empty";
    }

    TEST(MahjongRoomsJudgeTest, TheJudgeFindsWhatItIsMeantToFindInHandMadeTexts) {
        MahjongSeatedGame game = StartedGame();
        const std::string honest = ViewTextOf(game, 0);
        ASSERT_TRUE(JudgeRoomMessage(honest, 0).empty());

        // another seat's tiles in a key of their own, a `hands` key, a wall
        EXPECT_TRUE(Says(JudgeRoomMessage(Mutated(honest, [](Json& json) { json["view"]["hands"] = Json::array(); }), 0), "'hands'"));
        EXPECT_TRUE(Says(JudgeRoomMessage(Mutated(honest, [](Json& json) { json["view"]["seats"][1]["concealed"] = Json::array({"1p", "2p"}); }), 0), "'concealed'"));
        EXPECT_TRUE(Says(JudgeRoomMessage(Mutated(honest, [](Json& json) { json["view"]["wall"] = Json::array({"1p"}); }), 0), "view.wall"));
        EXPECT_TRUE(Says(JudgeRoomMessage(Mutated(honest, [](Json& json) { json["hand"] = Json::array({"1p"}); }), 0), "'hand'"));
        // the own list shorter than the count says (another seat's list pasted in, a tile dropped)
        EXPECT_TRUE(Says(JudgeRoomMessage(Mutated(honest, [](Json& json) { json["view"]["hand"].erase(json["view"]["hand"].begin()); }), 0), "view.hand"));
        // a concealed meld of another seat
        const std::string melds = Mutated(honest, [](Json& json) {
            json["view"]["seats"][2]["melds"] =
                Json::array({Parse("{\"kind\":\"kong\",\"tiles\":[\"1p\",\"1p\",\"1p\",\"1p\"],\"concealed\":true,\"from\":null,\"fromDiscard\":false,\"claimed\":null}")});
        });
        EXPECT_TRUE(Says(JudgeRoomMessage(melds, 0), "concealed meld of another seat"));
        // a view of another seat than the client's
        EXPECT_TRUE(Says(JudgeRoomMessage(honest, 1), "seat of the client"));
        // `result` while the hand is played, copied from a finished hand
        MahjongSeatedGame finished = StartedGame();
        PlayToHandOver(finished);
        const Json result = finished.GetSeatView(0).value()["result"];
        EXPECT_TRUE(Says(JudgeRoomMessage(Mutated(honest, [&](Json& json) { json["view"]["result"] = result; }), 0), "view.result"));
        // the same result is fine when the hand is over, and not with a hand list that is not tiles
        const std::string over = ViewTextOf(finished, 0);
        EXPECT_TRUE(JudgeRoomMessage(over, 0).empty());
        EXPECT_FALSE(JudgeRoomMessage(Mutated(over, [](Json& json) { json["view"]["result"]["seats"][1]["concealed"] = 5; }), 0).empty());
        EXPECT_FALSE(JudgeRoomMessage(Mutated(over, [](Json& json) { json["view"]["result"]["extra"] = 1; }), 0).empty());
        // the other messages carry no list of tiles
        EXPECT_FALSE(JudgeRoomMessage("{\"v\":1,\"type\":\"joined\",\"room\":\"ABCDE\",\"player\":1,\"token\":\"x\",\"seat\":0,\"host\":true,\"hand\":[\"1p\"]}", 0).empty());
        EXPECT_FALSE(JudgeRoomMessage("{\"v\":1,\"type\":\"lobby\",\"room\":\"ABCDE\",\"game\":\"mahjong\",\"phase\":\"playing\",\"host\":0,\"seats\":[],\"hands\":[]}", 0).empty());
        EXPECT_FALSE(JudgeRoomMessage("{\"v\":1,\"type\":\"error\",\"code\":\"x\",\"message\":\"y\",\"hand\":[\"1p\"]}", 0).empty());
        EXPECT_FALSE(JudgeRoomMessage("{\"v\":1,\"type\":\"pong\",\"hand\":[\"1p\"]}", 0).empty());
        EXPECT_TRUE(JudgeRoomMessage("{\"v\":1,\"type\":\"pong\"}", 0).empty());
        // not JSON, not an object, no type, a type the protocol does not send
        EXPECT_FALSE(JudgeRoomMessage("not json", 0).empty());
        EXPECT_FALSE(JudgeRoomMessage("[1]", 0).empty());
        EXPECT_FALSE(JudgeRoomMessage("{\"v\":1}", 0).empty());
        EXPECT_FALSE(JudgeRoomMessage("{\"v\":1,\"type\":\"cheat\"}", 0).empty());
    }

    TEST(MahjongRoomsJudgeTest, TheLogCountsTextsAndNonTextFrames) {
        RoomsJudgeLog log;
        EXPECT_TRUE(log.Text("{\"v\":1,\"type\":\"pong\"}", 0).empty());
        EXPECT_FALSE(log.Text("{\"v\":1,\"type\":\"cheat\"}", 0).empty());
        log.NonTextFrame();
        EXPECT_EQ(log.Texts(), 2u);
        EXPECT_EQ(log.NonText(), 1u);
        EXPECT_EQ(log.Offences().size(), 1u);
    }

    // ----- (e) equal views -----

    namespace {

        /// The state of a hand with everything @p viewer cannot see shuffled: the concealed tiles of the other three seats and the
        /// three parts of the wall, the sizes as they were.
        HandState WithHiddenTilesShuffled(HandState state, Seat viewer, const RandomFn& rng) {
            std::vector<Tile> pool;
            const auto take = [&](const eastl::vector<Tile>& tiles) { pool.insert(pool.end(), tiles.begin(), tiles.end()); };
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat != viewer) {
                    take(state.hands[static_cast<size_t>(seat)].concealed);
                }
            }
            take(state.live);
            take(state.loose);
            take(state.dead);
            for (size_t index = pool.size(); index > 1; --index) {
                std::swap(pool[index - 1], pool[rng() % index]);
            }
            size_t next = 0;
            const auto fill = [&](eastl::vector<Tile>& tiles) {
                for (Tile& tile : tiles) {
                    tile = pool[next++];
                }
            };
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat != viewer) {
                    eastl::vector<Tile>& concealed = state.hands[static_cast<size_t>(seat)].concealed;
                    fill(concealed);
                    eastl::sort(concealed.begin(), concealed.end(), [](Tile a, Tile b) { return a.Id() < b.Id(); });
                }
            }
            fill(state.live);
            fill(state.loose);
            fill(state.dead);
            // the tile just drawn lies in the hand of the seat to act; it stays a tile of that hand
            const eastl::vector<Tile>& current = state.hands[static_cast<size_t>(state.current)].concealed;
            if (state.current != viewer && state.drawnTile.IsValid() && !current.empty()) {
                state.drawnTile = current.back();
            }
            return state;
        }

        struct TwinReport {
            size_t compared = 0;
            size_t refused = 0;
            size_t differing = 0;
            std::string firstDifference;
        };

        /// Plays @p handsToPlay hands with computers; at every step of a hand in progress, for every viewer, a twin of the game whose
        /// hidden tiles are shuffled must give the viewer the same view and the same actions.
        TwinReport CompareTwins(MahjongSeatedGame game, size_t handsToPlay) {
            TwinReport report;
            const RandomFn rng = MakeRandom(1234);
            for (int step = 0; step < kTickLimit && game.GetMatch().History().size() < handsToPlay; ++step) {
                if (PhaseOfMatch(game.GetMatch()) == SessionPhase::Playing) {
                    for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                        const HandState state = WithHiddenTilesShuffled(game.GetMatch().Hand().Capture(), viewer, rng);
                        std::expected<HandModel, eastl::string> restored = HandModel::Restore(state);
                        if (!restored) {
                            ++report.refused;
                            continue;
                        }
                        MahjongSeatedGame twin = game;
                        twin.GetMatch().Hand() = std::move(*restored);
                        ++report.compared;
                        const bool same = twin.GetSeatView(viewer).value() == game.GetSeatView(viewer).value() && twin.GetSeatActions(viewer).value() == game.GetSeatActions(viewer).value();
                        if (!same) {
                            if (report.differing++ == 0) {
                                report.firstDifference = "step " + std::to_string(step) + ", viewer " + std::to_string(viewer);
                            }
                        }
                    }
                }
                bool moved = false;
                for (int seat = 0; seat < kSeatCount && !moved; ++seat) {
                    if (const std::optional<Json> action = game.ChooseComputerAction(seat)) {
                        EXPECT_TRUE(game.PerformSeatAction(seat, *action).has_value());
                        moved = true;
                    }
                }
                if (!moved) {
                    break;
                }
            }
            return report;
        }

        constexpr size_t kTwinHands = 2;

    }  // namespace

    TEST(MahjongRoomsEqualViewsTest, TheViewOfASeatDoesNotDependOnTheTilesItCannotSee) {
        const TwinReport report = CompareTwins(StartedGame(), kTwinHands);
        EXPECT_GT(report.compared, 100u);
        EXPECT_LT(report.refused, report.compared) << "most twins are real positions";
        EXPECT_EQ(report.differing, 0u) << report.firstDifference;
    }

    TEST(MahjongRoomsEqualViewsTest, AGameThatShowsTheNeighboursTilesIsCaughtByTheTwins) {
        for (const Leak leak : {Leak::ExtraKey, Leak::NeighbourAsOwn}) {
            LeakyMahjongSeatedGame game(leak);
            ASSERT_TRUE(game.Start(7, Parse(kShortMatch)).has_value());
            // the twin is a copy of the leaky game, so the leak is in the comparison
            const RandomFn rng = MakeRandom(99);
            size_t differing = 0;
            for (int step = 0; step < 30; ++step) {
                const HandState state = WithHiddenTilesShuffled(game.GetMatch().Hand().Capture(), 0, rng);
                std::expected<HandModel, eastl::string> restored = HandModel::Restore(state);
                if (restored) {
                    LeakyMahjongSeatedGame twin = game;
                    twin.GetMatch().Hand() = std::move(*restored);
                    differing += twin.GetSeatView(0).value() == game.GetSeatView(0).value() ? 0 : 1;
                }
                for (int seat = 0; seat < kSeatCount; ++seat) {
                    if (const std::optional<Json> action = game.ChooseComputerAction(seat)) {
                        ASSERT_TRUE(game.PerformSeatAction(seat, *action).has_value());
                        break;
                    }
                }
            }
            EXPECT_GT(differing, 0u) << "leak " << static_cast<int>(leak);
        }
    }

    // ----- (f) hostile actions through the hub -----

    namespace {

        std::string ActionText(uint64_t version, const std::string& action) {
            return "{\"v\":1,\"type\":\"action\",\"version\":" + std::to_string(version) + ",\"action\":" + action + "}";
        }

        /// A tile kind the seat does not hold, in notation
        std::string UnheldTile(const RoomsHarness& harness, int seat) {
            const PlayerHand& own = harness.Game()->GetMatch().Hand().GetHand(seat);
            for (uint8_t id = 0; id < kKindCount; ++id) {
                const Tile tile{id};
                if (eastl::find(own.concealed.begin(), own.concealed.end(), tile) == own.concealed.end()) {
                    return TileToJson(tile).get<std::string>();
                }
            }
            return "zz";
        }

    }  // namespace

    TEST(MahjongRoomsHostileTest, HostileActionObjectsGetAStableErrorAndChangeNothing) {
        RoomsHarness harness;
        const std::vector<ConnectionId> people = harness.MakeLobby(kPersonSeats);
        harness.Start(people[0]);
        ASSERT_NE(harness.Game(), nullptr);

        // a person to act
        ConnectionId actor = 0;
        for (int step = 0; step < kTickLimit && actor == 0; ++step) {
            for (const ConnectionId id : people) {
                actor = harness.At(id).Actions().empty() ? actor : id;
            }
            if (actor == 0) {
                harness.Tick();
            }
        }
        ASSERT_NE(actor, 0u);
        const ConnectionId idler = actor == people[0] ? people[1] : people[0];
        const int seat = harness.At(actor).seat;
        const int idleSeat = harness.At(idler).seat;
        const uint64_t version = harness.At(actor).ViewVersion();
        const std::string unheld = UnheldTile(harness, seat);

        struct Attempt {
            std::string action;
            const char* code;
        };
        const std::vector<Attempt> attempts = {
            {"{}", "invalid_params"},
            {"{\"type\":7}", "invalid_params"},
            {"{\"type\":null}", "invalid_params"},
            {"{\"type\":{\"a\":{\"a\":[[[]]]}}}", "invalid_params"},
            {"{\"type\":\"dance\"}", "invalid_params"},
            {"{\"type\":\"discard\"}", "invalid_params"},
            {"{\"type\":\"discard\",\"tile\":42}", "invalid_params"},
            {"{\"type\":\"discard\",\"tile\":[\"1p\"]}", "invalid_params"},
            {"{\"type\":\"discard\",\"tile\":\"zz\"}", "invalid_params"},
            {"{\"type\":\"claim\",\"claim\":\"chow\"}", "invalid_params"},
            {"{\"type\":\"discard\",\"tile\":\"" + unheld + "\"}", "illegal_action"},
            {"{\"type\":\"concealed_kong\",\"tile\":\"" + unheld + "\"}", "illegal_action"},
            {"{\"type\":\"next_hand\"}", "illegal_action"},
            {"{\"type\":\"pass\"}", "illegal_action"},
            {"{\"type\":\"claim\",\"claim\":\"pung\"}", "illegal_action"},
            {"{\"type\":\"claim\",\"claim\":\"mahjong\"}", "illegal_action"},
        };
        for (const Attempt& attempt : attempts) {
            const std::string before = SeatText(harness, seat) + SeatText(harness, idleSeat);
            const size_t views = harness.At(actor).Count("view");
            harness.SendText(actor, ActionText(version, attempt.action));
            EXPECT_EQ(harness.At(actor).LastErrorCode(), attempt.code) << attempt.action;
            EXPECT_EQ(SeatText(harness, seat) + SeatText(harness, idleSeat), before) << attempt.action;
            EXPECT_EQ(harness.At(actor).Count("view"), views) << attempt.action << ": a refused action sends no view";
        }

        // not an object at all: the hub refuses the message before the game sees it
        for (const char* action : {"\"discard\"", "[]", "7", "null"}) {
            const std::string before = SeatText(harness, seat);
            harness.SendText(actor, ActionText(version, action));
            EXPECT_EQ(harness.At(actor).LastErrorCode(), "bad_message") << action;
            EXPECT_EQ(SeatText(harness, seat), before) << action;
        }

        // a listed action sent by the seat that has none
        const std::string idleBefore = SeatText(harness, idleSeat);
        harness.SendText(idler, ActionText(harness.At(idler).ViewVersion(), Dump(harness.At(actor).Actions()[0])));
        EXPECT_EQ(harness.At(idler).LastErrorCode(), "not_your_turn");
        EXPECT_EQ(SeatText(harness, idleSeat), idleBefore);

        // a listed action of a version the game has not reached: stale, and the view comes again
        const size_t views = harness.At(actor).Count("view");
        harness.SendText(actor, ActionText(version + 1, Dump(harness.At(actor).Actions()[0])));
        EXPECT_EQ(harness.At(actor).LastErrorCode(), "stale_action");
        EXPECT_EQ(harness.At(actor).Count("view"), views + 1);
        EXPECT_EQ(harness.Game()->GetStateVersion(), version);

        // after all that the game goes on
        EXPECT_TRUE(harness.Act(actor));
        EXPECT_GT(harness.Game()->GetStateVersion(), version);
        ExpectJudgedClean(harness);
    }

}  // namespace BECore::Mahjong::Tests
