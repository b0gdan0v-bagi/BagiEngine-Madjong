#include <BECore/Control/Json.h>
#include <BECore/Rooms/FakeRoomTransport.h>
#include <BECore/Rooms/ManualClock.h>
#include <Mahjong/MahjongStateJson.h>
#include <Mahjong/Online/MahjongSeatedGame.h>
#include <Mahjong/Online/OnlineClient.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>

/**
 * `OnlineClient` (`Online/OnlineClient.h`): the model over a `RoomClient`, driven by a scripted server (the engine's fake transport) and a manual clock. The views come from the real
 * `MahjongSeatedGame`, so the table the model reads is the one the adapter gives. Plain C++: no scene, no `Application`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        using BECore::Tests::FakeLink;
        using BECore::Tests::FakeTransports;
        using BECore::Tests::ManualClock;
        using std::chrono::milliseconds;

        constexpr eastl::string_view kUrl = "ws://127.0.0.1:9000";
        constexpr eastl::string_view kRoom = "ABCDE";
        constexpr eastl::string_view kToken = "00112233445566778899aabbccddeeff";
        constexpr uint64_t kSeed = 23;
        constexpr milliseconds kBackoff(100);
        constexpr milliseconds kGiveUp(5000);
        constexpr int kStepLimit = 20000;
        constexpr int kOtherFormat = 2;

        Json Parse(const char* text) {
            return ParseJson(text).value();
        }

        MahjongSeatedGame Started() {
            MahjongSeatedGame game;
            EXPECT_TRUE(game.Start(kSeed, Parse("{\"hands\": 8, \"level\": 1}")).has_value());
            return game;
        }

        /// Plays the computer's choice for whichever seat has one, until the hand is over.
        void PlayToHandOver(MahjongSeatedGame& game) {
            for (int step = 0; step < kStepLimit && PhaseOfMatch(game.GetMatch()) == SessionPhase::Playing; ++step) {
                bool moved = false;
                for (int seat = 0; seat < kSeatCount && !moved; ++seat) {
                    if (const std::optional<Json> action = game.ChooseComputerAction(seat)) {
                        ASSERT_TRUE(game.PerformSeatAction(seat, *action).has_value());
                        moved = true;
                    }
                }
                ASSERT_TRUE(moved);
            }
            ASSERT_EQ(PhaseOfMatch(game.GetMatch()), SessionPhase::HandOver);
        }

        eastl::string ViewText(const MahjongSeatedGame& game, int seat) {
            return MakeViewMessage(kRoom, seat, game.GetStateVersion(), false, game.GetSeatView(seat).value(), game.GetSeatActions(seat).value());
        }

        eastl::string LobbyText(int hostSeat, const eastl::vector<LobbySeat>& seats, RoomPhase phase = RoomPhase::Lobby) {
            LobbySnapshot lobby;
            lobby.room = kRoom;
            lobby.game = kSeatedGameName;
            lobby.phase = phase;
            lobby.hostSeat = hostSeat;
            lobby.seats = seats;
            return MakeLobbyMessage(lobby);
        }

        eastl::vector<LobbySeat> HostAlone() {
            return {{0, BECore::SeatKind::Human, "Ann", true}, {1, BECore::SeatKind::Empty, {}, false}, {2, BECore::SeatKind::Empty, {}, false}, {3, BECore::SeatKind::Empty, {}, false}};
        }

        eastl::vector<LobbySeat> AnnAndBob() {
            return {{0, BECore::SeatKind::Human, "Ann", true}, {1, BECore::SeatKind::Human, "Bob", true}, {2, BECore::SeatKind::Computer, {}, false}, {3, BECore::SeatKind::Computer, {}, false}};
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.c_str(), text.size());
        }

        class OnlineClientTest : public ::testing::Test {
        protected:
            OnlineClientTest() {
                MakeClient();
            }

            void MakeClient() {
                RoomClientSettings settings;
                settings.transportFactory = _transports.Factory();
                settings.pingInterval = milliseconds(0);
                settings.silenceLimit = milliseconds(0);
                settings.backoffStart = kBackoff;
                settings.backoffMax = kBackoff;
                settings.reconnectGiveUp = kGiveUp;
                _client = std::make_unique<OnlineClient>(settings);
            }

            void Pump() {
                _client->Update(_clock.Now());
            }

            void Advance(milliseconds step) {
                _clock.Advance(step);
                Pump();
            }

            FakeLink& Link() {
                return _transports.Last();
            }

            void Receive(eastl::string_view text) {
                Link().PushText(text);
                Pump();
            }

            void ReceiveError(eastl::string_view code, eastl::string_view ref) {
                Receive(MakeErrorMessage(code, "text of the server", ref));
            }

            /// Host a room: connected, told the room, alone in the lobby on seat 0.
            void EnterAsHost() {
                ASSERT_TRUE(_client->Host(kUrl, "Ann", {8, 1}));
                Link().PushOpened();
                Pump();
                Link().PushText(MakeJoinedMessage(kRoom, 1, kToken, 0, true));
                Link().PushText(LobbyText(0, HostAlone()));
                Pump();
            }

            /// Host, start with Ann and Bob and two computers, and see the first view of seat 0.
            void EnterAGame(const MahjongSeatedGame& game) {
                EnterAsHost();
                ASSERT_EQ(_client->Stage(), OnlineStage::Lobby);
                Link().PushText(LobbyText(0, AnnAndBob(), RoomPhase::Playing));
                Link().PushText(ViewText(game, 0));
                Pump();
                ASSERT_EQ(_client->Stage(), OnlineStage::Game);
            }

            /// The message the client sent as number @p index, parsed.
            template <typename T>
            T Sent(size_t index) {
                auto parsed = Link().ParseSent(index);
                EXPECT_TRUE(parsed.has_value());
                const T* message = parsed ? std::get_if<T>(&*parsed) : nullptr;
                EXPECT_NE(message, nullptr);
                return message != nullptr ? *message : T();
            }

            ManualClock _clock;
            FakeTransports _transports;
            std::unique_ptr<OnlineClient> _client;
        };

        const char* KeyOf(const OnlineClient& client) {
            return client.Notice().key.data();
        }

    }  // namespace

    // ---- Host and Join ------------------------------------------------------------------------------------------------------------

    TEST_F(OnlineClientTest, AFreshClientIsIdleWithNothingToShow) {
        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        EXPECT_FALSE(_client->HasTable());
        EXPECT_EQ(_client->Notice().serial, 0u);
        EXPECT_FALSE(_client->SessionToKeep().IsValid());
        EXPECT_EQ(_client->SessionRevision(), 0u);
        EXPECT_TRUE(_client->Lobby().room.empty());
    }

    TEST_F(OnlineClientTest, HostCreatesTheRoomAndTheHostAloneSeesThreeEmptySeats) {
        ASSERT_TRUE(_client->Host(kUrl, "Ann", {8, 1}));
        EXPECT_EQ(_client->Stage(), OnlineStage::Connecting);
        Link().PushOpened();
        Pump();
        EXPECT_EQ(_client->Stage(), OnlineStage::Connecting);

        ASSERT_EQ(Link().sent.size(), 1u);
        const CreateRoomMessage create = Sent<CreateRoomMessage>(0);
        EXPECT_EQ(create.game, kSeatedGameName);
        EXPECT_EQ(create.name, "Ann");
        EXPECT_EQ(GetJsonInt(create.options, "hands"), 8);
        EXPECT_EQ(GetJsonInt(create.options, "level"), 1);

        const uint64_t before = _client->Version();
        Link().PushText(MakeJoinedMessage(kRoom, 1, kToken, 0, true));
        Link().PushText(LobbyText(0, HostAlone()));
        Pump();

        EXPECT_GT(_client->Version(), before);
        EXPECT_EQ(_client->Stage(), OnlineStage::Lobby);
        EXPECT_EQ(_client->Room(), kRoom);
        EXPECT_EQ(_client->Seat(), 0);
        EXPECT_TRUE(_client->IsHost());
        const OnlineLobby& lobby = _client->Lobby();
        EXPECT_EQ(lobby.room, kRoom);
        EXPECT_EQ(lobby.phase, RoomPhase::Lobby);
        EXPECT_TRUE(lobby.canStart);
        EXPECT_EQ(lobby.humans, 1);
        EXPECT_EQ(lobby.seats[0].kind, BECore::SeatKind::Human);
        EXPECT_EQ(lobby.seats[0].name, "Ann");
        EXPECT_TRUE(lobby.seats[0].mine);
        EXPECT_TRUE(lobby.seats[0].host);
        EXPECT_FALSE(lobby.seats[0].away);
        for (int seat = 1; seat < kSeatCount; ++seat) {
            EXPECT_EQ(lobby.seats[static_cast<size_t>(seat)].kind, BECore::SeatKind::Empty) << seat;
            EXPECT_TRUE(_client->CanChoose(seat)) << seat;
        }
        EXPECT_FALSE(_client->CanChoose(0));
        EXPECT_EQ(_client->SessionToKeep().room, kRoom);
        EXPECT_EQ(_client->SessionToKeep().token, kToken);
        EXPECT_EQ(_client->SessionToKeep().url, kUrl);
        EXPECT_EQ(_client->SessionRevision(), 1u);
    }

    TEST_F(OnlineClientTest, JoinTakesALowerCaseCodeAndAGuestCannotStart) {
        ASSERT_TRUE(_client->Join(kUrl, "abcde", "Bob"));
        Link().PushOpened();
        Pump();
        const JoinRoomMessage join = Sent<JoinRoomMessage>(0);
        EXPECT_EQ(join.code, kRoom);
        EXPECT_EQ(join.name, "Bob");

        Link().PushText(MakeJoinedMessage(kRoom, 2, kToken, 1, false));
        Link().PushText(LobbyText(0, AnnAndBob()));
        Pump();
        EXPECT_EQ(_client->Stage(), OnlineStage::Lobby);
        EXPECT_FALSE(_client->IsHost());
        EXPECT_FALSE(_client->Lobby().canStart);
        EXPECT_TRUE(_client->Lobby().seats[1].mine);
        EXPECT_TRUE(_client->Lobby().seats[0].host);
        EXPECT_EQ(_client->Lobby().humans, 2);
    }

    TEST_F(OnlineClientTest, TheLobbyShowsAPersonWhoseConnectionIsDownAsAway) {
        EnterAsHost();
        eastl::vector<LobbySeat> seats = AnnAndBob();
        seats[1].connected = false;
        Receive(LobbyText(0, seats));

        EXPECT_TRUE(_client->Lobby().seats[1].away);
        EXPECT_FALSE(_client->Lobby().seats[1].connected);
        EXPECT_FALSE(_client->Lobby().seats[0].away);
        EXPECT_EQ(_client->Lobby().seats[2].kind, BECore::SeatKind::Computer);
        EXPECT_TRUE(_client->Lobby().seats[2].name.empty());
    }

    TEST_F(OnlineClientTest, TheHostLeavingInTheLobbyMovesTheHostToTheOtherPlayer) {
        ASSERT_TRUE(_client->Join(kUrl, "ABCDE", "Bob"));
        Link().PushOpened();
        Pump();
        Link().PushText(MakeJoinedMessage(kRoom, 2, kToken, 1, false));
        Link().PushText(LobbyText(0, AnnAndBob()));
        Pump();
        ASSERT_FALSE(_client->IsHost());

        eastl::vector<LobbySeat> alone = AnnAndBob();
        alone[0] = {0, BECore::SeatKind::Empty, {}, false};
        Receive(LobbyText(1, alone));
        EXPECT_TRUE(_client->IsHost());
        EXPECT_TRUE(_client->Lobby().canStart);
        EXPECT_TRUE(_client->Lobby().seats[1].host);
    }

    // ---- Commands that cannot be sent ---------------------------------------------------------------------------------------------

    TEST_F(OnlineClientTest, ACodeThatCannotBeARoomCodeAndANameTheServerWouldRefuseMakeNoConnection) {
        EXPECT_FALSE(_client->Join(kUrl, "no!", "Bob"));
        EXPECT_STREQ(KeyOf(*_client), "online.error.unknownRoom");
        EXPECT_EQ(_client->Notice().serial, 1u);

        EXPECT_FALSE(_client->Join(kUrl, "ABCDE", ""));
        EXPECT_STREQ(KeyOf(*_client), "online.error.badName");
        EXPECT_FALSE(_client->Host(kUrl, "\t", {}));
        EXPECT_STREQ(KeyOf(*_client), "online.error.badName");

        EXPECT_EQ(_transports.Count(), 0u);
        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
    }

    TEST_F(OnlineClientTest, OptionsOutsideWhatTheGameTakesMakeNoConnection) {
        EXPECT_FALSE(_client->Host(kUrl, "Ann", {9, 5}));
        EXPECT_FALSE(_client->Host(kUrl, "Ann", {8, 0}));
        EXPECT_FALSE(_client->Host(kUrl, "Ann", {16, 11}));
        EXPECT_EQ(_transports.Count(), 0u);
        EXPECT_EQ(_client->Notice().serial, 3u);
        EXPECT_STREQ(KeyOf(*_client), "online.error.server");
        EXPECT_TRUE(_client->Host(kUrl, "Ann", {16, 10}));
    }

    TEST_F(OnlineClientTest, ACommandThatDoesNotFitTheStateIsANoticeAndOneOutsideItsStageIsNot) {
        EXPECT_FALSE(_client->Start());
        EXPECT_FALSE(_client->ChooseSeat(1));
        EXPECT_FALSE(_client->Act(0));
        EXPECT_EQ(_client->Notice().serial, 0u);

        EnterAsHost();
        const uint32_t serial = _client->Notice().serial;
        EXPECT_FALSE(_client->Host(kUrl, "Ann"));
        EXPECT_EQ(_client->Notice().serial, serial + 1);
        EXPECT_STREQ(KeyOf(*_client), "online.error.notNow");
        EXPECT_FALSE(_client->Act(0));
        EXPECT_FALSE(_client->ChooseSeat(kSeatCount));
        EXPECT_FALSE(_client->ChooseSeat(-1));
        EXPECT_EQ(_client->Notice().serial, serial + 1);
    }

    // ---- Seats and start ----------------------------------------------------------------------------------------------------------

    TEST_F(OnlineClientTest, ChooseSeatTakesTheSeatAtOnceAndARefusalGivesTheOldOneBackWithANotice) {
        EnterAsHost();
        ASSERT_TRUE(_client->ChooseSeat(1));
        const ChooseSeatMessage choose = Sent<ChooseSeatMessage>(Link().sent.size() - 1);
        EXPECT_EQ(choose.seat, 1);
        EXPECT_EQ(_client->Seat(), 1);
        EXPECT_TRUE(_client->Lobby().seats[1].mine);
        EXPECT_FALSE(_client->Lobby().seats[0].mine);

        ReceiveError(RoomErrors::kSeatTaken, "choose_seat");
        EXPECT_EQ(_client->Seat(), 0);
        EXPECT_TRUE(_client->Lobby().seats[0].mine);
        EXPECT_FALSE(_client->Lobby().seats[1].mine);
        EXPECT_STREQ(KeyOf(*_client), "online.error.seatTaken");
        EXPECT_EQ(_client->Stage(), OnlineStage::Lobby);
    }

    TEST_F(OnlineClientTest, AViewOfAnEmptyObjectBeforeTheStartIsNotATable) {
        EnterAsHost();
        const uint32_t serial = _client->Notice().serial;
        Receive(MakeViewMessage(kRoom, 0, 0, false, Json::object(), Json::array()));

        EXPECT_EQ(_client->Stage(), OnlineStage::Lobby);
        EXPECT_FALSE(_client->HasTable());
        EXPECT_EQ(_client->Notice().serial, serial);
    }

    TEST_F(OnlineClientTest, StartIsSentAndTheFirstViewMakesTheTableAndTheStageGame) {
        EnterAsHost();
        ASSERT_TRUE(_client->Start());
        EXPECT_EQ(Link().sent.size(), 2u);
        EXPECT_TRUE(std::holds_alternative<StartMessage>(*Link().ParseSent(1)));

        const MahjongSeatedGame game = Started();
        Link().PushText(LobbyText(0, AnnAndBob(), RoomPhase::Playing));
        Link().PushText(ViewText(game, 0));
        Pump();

        EXPECT_EQ(_client->Stage(), OnlineStage::Game);
        ASSERT_TRUE(_client->HasTable());
        const OnlineTable& table = _client->Table();
        EXPECT_EQ(table.seat, 0);
        EXPECT_EQ(table.format, kOnlineViewFormat);
        EXPECT_EQ(table.version, game.GetStateVersion());
        EXPECT_EQ(table.actions.size(), game.GetSeatActions(0).value().size());
        EXPECT_FALSE(table.hand.empty());
        EXPECT_FALSE(_client->Lobby().canStart);
        EXPECT_EQ(_client->Lobby().phase, RoomPhase::Playing);
    }

    // ---- Acting -------------------------------------------------------------------------------------------------------------------

    TEST_F(OnlineClientTest, ActSendsTheListedActionStampedWithTheVersionOfTheView) {
        MahjongSeatedGame game = Started();
        EnterAGame(game);
        const size_t sentBefore = Link().sent.size();

        ASSERT_FALSE(_client->Table().actions.empty());
        EXPECT_FALSE(_client->Act(_client->Table().actions.size()));
        ASSERT_TRUE(_client->Act(0));
        ASSERT_EQ(Link().sent.size(), sentBefore + 1);
        const ActionMessage sent = Sent<ActionMessage>(sentBefore);
        EXPECT_EQ(sent.version, game.GetStateVersion());
        EXPECT_EQ(sent.action, game.GetSeatActions(0).value()[0]);
    }

    TEST_F(OnlineClientTest, AStaleActionIsSilentAndTheNextViewIsShown) {
        MahjongSeatedGame game = Started();
        EnterAGame(game);
        const uint32_t serial = _client->Notice().serial;
        const uint64_t firstVersion = _client->Table().version;

        ReceiveError(RoomErrors::kStaleAction, "action");
        EXPECT_EQ(_client->Notice().serial, serial);
        EXPECT_EQ(_client->Stage(), OnlineStage::Game);

        ASSERT_TRUE(game.PerformSeatAction(0, game.GetSeatActions(0).value()[0]).has_value());
        Receive(ViewText(game, 0));
        EXPECT_GT(_client->Table().version, firstVersion);
        EXPECT_EQ(_client->Table().version, game.GetStateVersion());
    }

    TEST_F(OnlineClientTest, AnErrorOfTheGameIsShownAsItsWord) {
        MahjongSeatedGame game = Started();
        EnterAGame(game);
        ReceiveError(RoomErrors::kIllegalAction, "action");
        EXPECT_STREQ(KeyOf(*_client), "online.error.notNow");
        ReceiveError("from_a_newer_server", "action");
        EXPECT_EQ(_client->Notice().key, kOnlineUnknownKey);
        EXPECT_EQ(_client->Notice().arg, "from_a_newer_server");
        ReceiveError("a hostile text <b>", "action");
        EXPECT_EQ(_client->Notice().arg, "ahostiletextb");
    }

    TEST_F(OnlineClientTest, NextHandIsListedInAFinishedHandAndTheReadyFlagsComeWithTheView) {
        MahjongSeatedGame game = Started();
        PlayToHandOver(game);
        EnterAGame(game);

        const OnlineTable& table = _client->Table();
        EXPECT_EQ(table.phase, SessionPhase::HandOver);
        ASSERT_EQ(table.actions.size(), 1u);
        EXPECT_TRUE(table.actions[0].nextHand);
        ASSERT_TRUE(table.result.has_value());
        EXPECT_FALSE(table.ready[1]);

        ASSERT_TRUE(game.PerformSeatAction(1, NextHandActionToJson()).has_value());
        Receive(ViewText(game, 0));
        EXPECT_TRUE(_client->Table().ready[1]);
        EXPECT_FALSE(_client->Table().ready[0]);

        const size_t sentBefore = Link().sent.size();
        ASSERT_TRUE(_client->Act(0));
        EXPECT_EQ(Sent<ActionMessage>(sentBefore).action, NextHandActionToJson());
    }

    // ---- Errors of a request ------------------------------------------------------------------------------------------------------

    TEST_F(OnlineClientTest, ARefusedJoinIsItsWordAndTheStageGoesBackToIdleWithTheConnectionClosed) {
        const eastl::string_view codes[] = {RoomErrors::kUnknownRoom, RoomErrors::kRoomFull, RoomErrors::kGameStarted, RoomErrors::kBadName};
        for (const eastl::string_view code : codes) {
            const size_t links = _transports.Count();
            ASSERT_TRUE(_client->Join(kUrl, "ABCDE", "Bob")) << code.data();
            EXPECT_EQ(_client->Stage(), OnlineStage::Connecting);
            Link().PushOpened();
            Pump();
            EXPECT_EQ(_client->Stage(), OnlineStage::Connecting);
            ReceiveError(code, "join_room");

            EXPECT_EQ(_client->Stage(), OnlineStage::Idle) << code.data();
            EXPECT_EQ(_client->Notice().key, OnlineErrorKey(code)) << code.data();
            EXPECT_NE(_client->Notice().key, kOnlineUnknownKey) << code.data();
            EXPECT_TRUE(Link().closeCalled) << code.data();
            EXPECT_EQ(_transports.Count(), links + 1);
            EXPECT_FALSE(_client->SessionToKeep().IsValid());
        }
        EXPECT_EQ(_client->Notice().serial, 4u);
    }

    TEST_F(OnlineClientTest, AConnectionThatDoesNotOpenIsAClosedWithTheConnectFailedWord) {
        _transports.FailNextOpen("tls_unsupported");
        EXPECT_FALSE(_client->Host(kUrl, "Ann"));
        EXPECT_STREQ(KeyOf(*_client), "online.error.connectFailed");
        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);

        ASSERT_TRUE(_client->Host(kUrl, "Ann"));
        Link().PushClosed(kRoomTransportAbnormalClosure, "connection failed");
        Pump();
        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        EXPECT_STREQ(KeyOf(*_client), "online.error.connectFailed");
    }

    // ---- Losing the connection ----------------------------------------------------------------------------------------------------

    TEST_F(OnlineClientTest, ALostConnectionFreezesTheTableAndTheReturnFindsTheGameAgain) {
        MahjongSeatedGame game = Started();
        EnterAGame(game);
        const uint32_t revision = _client->SessionRevision();
        const uint32_t serial = _client->Notice().serial;
        const size_t links = _transports.Count();

        Link().PushClosed();
        Pump();
        EXPECT_EQ(_client->Stage(), OnlineStage::Reconnecting);
        EXPECT_TRUE(_client->HasTable());
        EXPECT_EQ(_client->Table().version, game.GetStateVersion());
        EXPECT_EQ(_client->Lobby().room, kRoom);
        EXPECT_TRUE(_client->SessionToKeep().IsValid());
        EXPECT_FALSE(_client->Act(0));
        EXPECT_EQ(_client->Notice().serial, serial);

        Advance(kBackoff);
        ASSERT_EQ(_transports.Count(), links + 1);
        Link().PushOpened();
        Pump();
        const JoinRoomMessage join = Sent<JoinRoomMessage>(0);
        EXPECT_EQ(join.token, kToken);
        EXPECT_EQ(join.code, kRoom);

        // The game moved on while the player was away
        ASSERT_TRUE(game.PerformSeatAction(0, game.GetSeatActions(0).value()[0]).has_value());
        Link().PushText(MakeJoinedMessage(kRoom, 1, kToken, 0, true));
        Link().PushText(LobbyText(0, AnnAndBob(), RoomPhase::Playing));
        Pump();
        EXPECT_EQ(_client->Stage(), OnlineStage::Game);
        EXPECT_NE(_client->Table().version, game.GetStateVersion());
        Receive(ViewText(game, 0));
        EXPECT_EQ(_client->Table().version, game.GetStateVersion());
        EXPECT_EQ(_client->SessionRevision(), revision);
        EXPECT_EQ(_client->Notice().serial, serial);
        EXPECT_EQ(_client->Stage(), OnlineStage::Game);
    }

    TEST_F(OnlineClientTest, TheSessionIsForgottenWhenTheRoomIsGoneAndKeptWhenTheServerCannotBeReached) {
        // room_gone: the server does not know the token
        {
            MahjongSeatedGame game = Started();
            EnterAGame(game);
            const uint32_t revision = _client->SessionRevision();
            Link().PushClosed();
            Pump();
            Advance(kBackoff);
            Link().PushOpened();
            Pump();
            ReceiveError(RoomErrors::kBadToken, "join_room");

            EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
            EXPECT_STREQ(KeyOf(*_client), "online.error.roomGone");
            EXPECT_FALSE(_client->SessionToKeep().IsValid());
            EXPECT_GT(_client->SessionRevision(), revision);
            EXPECT_FALSE(_client->HasTable());
            EXPECT_TRUE(_client->Lobby().room.empty());
        }
        // replaced: another device took the seat
        {
            MakeClient();
            EnterAsHost();
            const uint32_t revision = _client->SessionRevision();
            const uint32_t serial = _client->Notice().serial;
            Receive(MakeLeftMessage(RoomClientCloseReasons::kReplaced));
            EXPECT_FALSE(_client->SessionToKeep().IsValid());
            EXPECT_GT(_client->SessionRevision(), revision);
            Link().PushClosed(1000, "replaced");
            Pump();
            EXPECT_STREQ(KeyOf(*_client), "online.error.replaced");
            EXPECT_EQ(_client->Notice().serial, serial + 1);
            EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        }
        // gave_up: the connection did not come back in time
        {
            MakeClient();
            EnterAsHost();
            const uint32_t revision = _client->SessionRevision();
            Link().PushClosed();
            Pump();
            Advance(kGiveUp);
            EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
            EXPECT_STREQ(KeyOf(*_client), "online.error.roomGone");
            EXPECT_FALSE(_client->SessionToKeep().IsValid());
            EXPECT_GT(_client->SessionRevision(), revision);
        }
        // connect_failed: the address cannot be reached at all, and the session stays for another try
        {
            MakeClient();
            EnterAsHost();
            const uint32_t revision = _client->SessionRevision();
            Link().PushClosed();
            Pump();
            _transports.FailNextOpen("tls_unsupported");
            Advance(kBackoff);
            EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
            EXPECT_STREQ(KeyOf(*_client), "online.error.connectFailed");
            EXPECT_TRUE(_client->SessionToKeep().IsValid());
            EXPECT_EQ(_client->SessionRevision(), revision);
        }
    }

    // ---- Leave, Disconnect, Resume ------------------------------------------------------------------------------------------------

    TEST_F(OnlineClientTest, LeaveForgetsTheSessionAtOnceAndTheConnectionIsClosedWhenTheServerAnswered) {
        EnterAsHost();
        const uint32_t revision = _client->SessionRevision();
        const uint32_t serial = _client->Notice().serial;
        ASSERT_TRUE(_client->Leave());
        EXPECT_FALSE(_client->SessionToKeep().IsValid());
        EXPECT_GT(_client->SessionRevision(), revision);
        EXPECT_TRUE(std::holds_alternative<LeaveMessage>(*Link().ParseSent(Link().sent.size() - 1)));

        Receive(MakeLeftMessage("left"));
        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        EXPECT_TRUE(Link().closeCalled);
        EXPECT_TRUE(_client->Lobby().room.empty());
        EXPECT_EQ(_client->Notice().serial, serial);
    }

    TEST_F(OnlineClientTest, TheRoomClosingUnderThePlayerIsTheRoomGoneWord) {
        EnterAsHost();
        Receive(MakeLeftMessage("room_closed"));
        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        EXPECT_STREQ(KeyOf(*_client), "online.error.roomGone");
        EXPECT_FALSE(_client->SessionToKeep().IsValid());
    }

    TEST_F(OnlineClientTest, DisconnectKeepsTheSessionAndSaysNothing) {
        MahjongSeatedGame game = Started();
        EnterAGame(game);
        const uint32_t revision = _client->SessionRevision();
        const uint32_t serial = _client->Notice().serial;

        _client->Disconnect();
        Pump();
        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        EXPECT_FALSE(_client->HasTable());
        EXPECT_TRUE(_client->SessionToKeep().IsValid());
        EXPECT_EQ(_client->SessionRevision(), revision);
        EXPECT_EQ(_client->Notice().serial, serial);
    }

    TEST_F(OnlineClientTest, LeavingWhileReconnectingEndsItWithNoNoticeAndNoSession) {
        EnterAsHost();
        Link().PushClosed();
        Pump();
        ASSERT_EQ(_client->Stage(), OnlineStage::Reconnecting);
        const uint32_t serial = _client->Notice().serial;

        ASSERT_TRUE(_client->Leave());
        Pump();
        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        EXPECT_FALSE(_client->SessionToKeep().IsValid());
        EXPECT_EQ(_client->Notice().serial, serial);
    }

    TEST_F(OnlineClientTest, ASessionGoesThroughJsonAndResumesInANewClient) {
        EnterAsHost();
        const eastl::string text = DumpJson(SessionToJson(_client->SessionToKeep()));
        EXPECT_NE(text.find(kRoom.data()), eastl::string::npos);

        MakeClient();
        const std::optional<RoomSession> session = SessionFromJson(ParseJson(text).value());
        ASSERT_TRUE(session.has_value());
        EXPECT_EQ(session->room, kRoom);
        EXPECT_EQ(session->token, kToken);
        EXPECT_EQ(session->url, kUrl);

        ASSERT_TRUE(_client->Resume(*session));
        EXPECT_EQ(_client->Stage(), OnlineStage::Reconnecting);
        EXPECT_TRUE(_client->SessionToKeep().IsValid());
        Link().PushOpened();
        Pump();
        EXPECT_EQ(Sent<JoinRoomMessage>(0).token, kToken);

        Link().PushText(MakeJoinedMessage(kRoom, 1, kToken, 0, true));
        Link().PushText(LobbyText(0, HostAlone()));
        Pump();
        EXPECT_EQ(_client->Stage(), OnlineStage::Lobby);
        EXPECT_EQ(_client->Seat(), 0);
        EXPECT_EQ(_client->SessionRevision(), 0u);
    }

    TEST_F(OnlineClientTest, AResumeOfARoomThatNoLongerExistsIsTheRoomGoneWord) {
        RoomSession session;
        session.url = kUrl;
        session.room = kRoom;
        session.token = kToken;
        ASSERT_TRUE(_client->Resume(session));
        Link().PushOpened();
        Pump();
        ReceiveError(RoomErrors::kUnknownRoom, "join_room");

        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        EXPECT_STREQ(KeyOf(*_client), "online.error.roomGone");
        EXPECT_FALSE(_client->SessionToKeep().IsValid());
        EXPECT_GT(_client->SessionRevision(), 0u);
    }

    TEST_F(OnlineClientTest, ASessionThatCannotBeOneIsRefusedWithoutAConnection) {
        RoomSession session;
        session.url = kUrl;
        session.room = kRoom;
        session.token = "short";
        EXPECT_FALSE(_client->Resume(session));
        EXPECT_STREQ(KeyOf(*_client), "online.error.roomGone");
        EXPECT_EQ(_transports.Count(), 0u);
        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        EXPECT_FALSE(_client->SessionToKeep().IsValid());
    }

    TEST(OnlineSessionJsonTest, ARoundTripKeepsTheSessionAndAnythingElseIsRefused) {
        RoomSession session;
        session.url = kUrl;
        session.room = "abcde";
        session.token = kToken;
        const std::optional<RoomSession> back = SessionFromJson(SessionToJson(session));
        ASSERT_TRUE(back.has_value());
        EXPECT_EQ(back->room, kRoom);

        const char* const refused[] = {
            "{}",
            "[]",
            "\"text\"",
            "{\"url\":\"ws://x\",\"token\":\"00112233445566778899aabbccddeeff\"}",
            "{\"url\":\"ws://x\",\"room\":\"ABCDE\"}",
            "{\"url\":\"ws://x\",\"room\":\"ABCDE\",\"token\":\"0011\"}",
            "{\"url\":\"ws://x\",\"room\":\"ABCDE\",\"token\":\"00112233445566778899AABBCCDDEEFF\"}",
            "{\"url\":\"ws://x\",\"room\":\"AB\",\"token\":\"00112233445566778899aabbccddeeff\"}",
            "{\"url\":\"\",\"room\":\"ABCDE\",\"token\":\"00112233445566778899aabbccddeeff\"}",
            "{\"url\":5,\"room\":\"ABCDE\",\"token\":\"00112233445566778899aabbccddeeff\"}",
            "{\"url\":\"ws://x\",\"room\":\"ABCDE\",\"token\":7}",
        };
        for (const char* text : refused) {
            EXPECT_FALSE(SessionFromJson(ParseJson(text).value()).has_value()) << text;
        }
    }

    // ---- Views the model will not read --------------------------------------------------------------------------------------------

    TEST_F(OnlineClientTest, AViewOfAnotherFormatIsRefusedInWordsTheClientClosesAndTheSessionIsForgotten) {
        MahjongSeatedGame game = Started();
        EnterAsHost();
        const uint32_t revision = _client->SessionRevision();
        Json view = game.GetSeatView(0).value();
        view["format"] = kOtherFormat;
        Link().PushText(LobbyText(0, AnnAndBob(), RoomPhase::Playing));
        Link().PushText(MakeViewMessage(kRoom, 0, game.GetStateVersion(), false, view, game.GetSeatActions(0).value()));
        Pump();

        EXPECT_EQ(_client->Stage(), OnlineStage::Idle);
        EXPECT_FALSE(_client->HasTable());
        EXPECT_STREQ(KeyOf(*_client), "online.error.viewFormat");
        EXPECT_TRUE(Link().closeCalled);
        EXPECT_FALSE(_client->SessionToKeep().IsValid());
        EXPECT_GT(_client->SessionRevision(), revision);
        const uint32_t serial = _client->Notice().serial;
        Pump();
        EXPECT_EQ(_client->Notice().serial, serial);
    }

    TEST_F(OnlineClientTest, AViewThatDoesNotParseKeepsTheLastGoodTableWithANotice) {
        MahjongSeatedGame game = Started();
        EnterAGame(game);
        const OnlineTable good = _client->Table();

        Json view = game.GetSeatView(0).value();
        view.erase("hand");
        Receive(MakeViewMessage(kRoom, 0, game.GetStateVersion() + 1, false, view, game.GetSeatActions(0).value()));

        EXPECT_STREQ(KeyOf(*_client), "online.error.badView");
        EXPECT_EQ(_client->Stage(), OnlineStage::Game);
        ASSERT_TRUE(_client->HasTable());
        EXPECT_EQ(_client->Table().hand, good.hand);
        EXPECT_EQ(_client->Table().version, good.version);
        EXPECT_TRUE(_client->SessionToKeep().IsValid());
        EXPECT_FALSE(Link().closeCalled);
    }

    // ---- Redraw by version --------------------------------------------------------------------------------------------------------

    TEST_F(OnlineClientTest, AFrameWithNothingToDoChangesNothingAndKeepsTheCapacities) {
        MahjongSeatedGame game = Started();
        EnterAGame(game);
        Receive(ViewText(game, 0));

        const uint64_t version = _client->Version();
        const size_t handCapacity = _client->Table().hand.capacity();
        const size_t actionCapacity = _client->Table().actions.capacity();
        const size_t discardCapacity = _client->Table().discards.capacity();
        const size_t nameCapacity = _client->Lobby().seats[0].name.capacity();
        for (int i = 0; i < 3; ++i) {
            Advance(milliseconds(16));
        }

        EXPECT_EQ(_client->Version(), version);
        EXPECT_EQ(_client->Table().hand.capacity(), handCapacity);
        EXPECT_EQ(_client->Table().actions.capacity(), actionCapacity);
        EXPECT_EQ(_client->Table().discards.capacity(), discardCapacity);
        EXPECT_EQ(_client->Lobby().seats[0].name.capacity(), nameCapacity);

        // The same view again is a read of the same size: the lists do not grow
        Receive(ViewText(game, 0));
        EXPECT_EQ(_client->Table().hand.capacity(), handCapacity);
        EXPECT_EQ(_client->Table().actions.capacity(), actionCapacity);
    }

    TEST_F(OnlineClientTest, EveryChangeMovesTheVersion) {
        uint64_t version = _client->Version();
        const auto moved = [&] {
            const bool grew = _client->Version() > version;
            version = _client->Version();
            return grew;
        };

        ASSERT_TRUE(_client->Host(kUrl, "Ann", {8, 1}));
        EXPECT_TRUE(moved());
        Link().PushOpened();
        Pump();
        Link().PushText(MakeJoinedMessage(kRoom, 1, kToken, 0, true));
        Pump();
        EXPECT_TRUE(moved());
        Receive(LobbyText(0, HostAlone()));
        EXPECT_TRUE(moved());
        Receive(LobbyText(0, AnnAndBob()));
        EXPECT_TRUE(moved());
        ReceiveError(RoomErrors::kNotHost, "start");
        EXPECT_TRUE(moved());
        Receive(LobbyText(0, AnnAndBob(), RoomPhase::Playing));
        EXPECT_TRUE(moved());
        Receive(ViewText(Started(), 0));
        EXPECT_TRUE(moved());
        Link().PushClosed();
        Pump();
        EXPECT_TRUE(moved());
    }

}  // namespace BECore::Mahjong::Tests
