#include "MahjongHubTransport.h"

#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>
#include <set>

/**
 * Four `OnlineClient`s over a real `RoomHub` with `MahjongSeatedGame` (the in-memory world of `MahjongHubTransport.h`): the whole chain
 * from a click of the model to the adapter and back. Every text a client receives is judged (`MahjongRoomsJudge.h`), so a hand that
 * leaks fails here too. The clients play the first action their table lists, the way a person does in `MahjongRoomsTest.cpp`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;
        using Players = std::vector<HubPlayer*>;

        constexpr eastl::string_view kUrl = "ws://hub.test";
        constexpr milliseconds kTick{10};
        constexpr int kStepLimit = 60000;
        constexpr int kWarmUpVersion = 20;
        constexpr int kSettleSteps = 20;
        constexpr int kHostSeat = 0;

        OnlineRoomOptions ShortMatch(bool hintsAllowed = false) {
            return OnlineRoomOptions{kShortMatchHands, kMinLevel, hintsAllowed};
        }

        /// One step of the world: the clock moves and the hub ticks, every client reads what came, then every client plays.
        void Step(MahjongHubWorld& world, const Players& players, bool withNextHand, milliseconds step = kTick) {
            world.Advance(step);
            for (HubPlayer* player : players) {
                player->Update();
            }
            for (HubPlayer* player : players) {
                player->ActFirst(withNextHand);
            }
        }

        template <typename Done>
        bool RunUntil(MahjongHubWorld& world, const Players& players, Done done, bool withNextHand = true, milliseconds step = kTick) {
            for (int count = 0; count < kStepLimit; ++count) {
                if (done()) {
                    return true;
                }
                Step(world, players, withNextHand, step);
            }
            return done();
        }

        void Settle(MahjongHubWorld& world, const Players& players, int steps = kSettleSteps) {
            for (int count = 0; count < steps; ++count) {
                Step(world, players, false);
            }
        }

        bool InStage(const HubPlayer& player, OnlineStage stage) {
            return player.Client().Stage() == stage;
        }

        bool AllInPhase(const Players& players, SessionPhase phase) {
            for (const HubPlayer* player : players) {
                if (!InStage(*player, OnlineStage::Game) || player->Table().phase != phase) {
                    return false;
                }
            }
            return true;
        }

        /// The host makes a room and waits in its lobby.
        void HostRoom(MahjongHubWorld& world, HubPlayer& host, const char* name = "Ann", bool hintsAllowed = false) {
            ASSERT_TRUE(host.Client().Host(kUrl, name, ShortMatch(hintsAllowed)));
            ASSERT_TRUE(RunUntil(world, {&host}, [&] { return InStage(host, OnlineStage::Lobby); }));
        }

        /// The player enters the room of @p host by its code and waits in the lobby.
        void JoinRoom(MahjongHubWorld& world, HubPlayer& player, const HubPlayer& host, const char* name) {
            ASSERT_TRUE(player.Client().Join(kUrl, host.Client().Room(), name));
            ASSERT_TRUE(RunUntil(world, {&player}, [&] { return InStage(player, OnlineStage::Lobby); }));
        }

        void StartAndWait(MahjongHubWorld& world, HubPlayer& host, const Players& players) {
            ASSERT_TRUE(host.Client().Start());
            ASSERT_TRUE(RunUntil(world, players, [&] { return std::all_of(players.begin(), players.end(), [](const HubPlayer* player) { return player->Client().HasTable(); }); }, false));
            ASSERT_NE(world.Game(), nullptr);
        }

        void ExpectJudgedClean(MahjongHubWorld& world) {
            EXPECT_GT(world.Judge().Texts(), 0u);
            EXPECT_EQ(world.Judge().NonText(), 0u);
            EXPECT_TRUE(world.Judge().Offences().empty()) << world.Judge().Offences().front();
        }

        void ExpectNoNotice(const HubPlayer& player) {
            EXPECT_EQ(player.Client().Notice().serial, 0u) << std::string(player.Client().Notice().key.data(), player.Client().Notice().key.size());
        }

        /// What the table of @p seat holds is what the adapter gives that seat now (the caller has checked that the versions agree).
        void ExpectTableIsTheAdapterView(const OnlineTable& table, const MahjongSeatedGame& game, int seat) {
            const Json view = game.GetSeatView(seat).value();
            const Json actions = game.GetSeatActions(seat).value();
            OnlineTable expected;
            const std::expected<void, OnlineViewError> read = ReadOnlineView(view, actions, expected);
            ASSERT_TRUE(read.has_value()) << "the adapter's own view must read";

            EXPECT_EQ(table.format, kOnlineViewFormat);
            EXPECT_EQ(table.hintsAllowed, expected.hintsAllowed);
            EXPECT_TRUE(table.phase == expected.phase);
            EXPECT_EQ(table.seat, seat);
            EXPECT_EQ(table.seat, expected.seat);
            EXPECT_EQ(table.east, expected.east);
            EXPECT_TRUE(table.seatWind == expected.seatWind);
            EXPECT_TRUE(table.roundWind == expected.roundWind);
            EXPECT_TRUE(table.hand == expected.hand);
            EXPECT_EQ(table.hand.size(), game.GetMatch().Hand().GetHand(seat).concealed.size());
            EXPECT_TRUE(table.turn.phase == expected.turn.phase);
            EXPECT_EQ(table.turn.current, expected.turn.current);
            EXPECT_TRUE(table.turn.pendingTile == expected.turn.pendingTile);
            EXPECT_EQ(table.turn.yourTurn, expected.turn.yourTurn);
            EXPECT_EQ(table.wall, expected.wall);
            EXPECT_EQ(table.handsPlayed, expected.handsPlayed);
            EXPECT_EQ(table.handsTotal, expected.handsTotal);
            EXPECT_EQ(table.version, game.GetStateVersion());
            EXPECT_TRUE(table.ready == expected.ready);
            for (size_t index = 0; index < table.seats.size(); ++index) {
                const OnlineSeat& have = table.seats[index];
                const OnlineSeat& want = expected.seats[index];
                EXPECT_TRUE(have.wind == want.wind) << "seat " << index;
                EXPECT_EQ(have.score, want.score) << "seat " << index;
                EXPECT_EQ(have.concealedCount, want.concealedCount) << "seat " << index;
                EXPECT_EQ(have.concealedKongs, want.concealedKongs) << "seat " << index;
                EXPECT_EQ(have.discardCount, want.discardCount) << "seat " << index;
                EXPECT_EQ(have.calling, want.calling) << "seat " << index;
                EXPECT_EQ(have.melds.size(), want.melds.size()) << "seat " << index;
                EXPECT_TRUE(have.bonus == want.bonus) << "seat " << index;
            }
            ASSERT_EQ(table.discards.size(), expected.discards.size());
            for (size_t index = 0; index < table.discards.size(); ++index) {
                EXPECT_TRUE(table.discards[index].tile == expected.discards[index].tile);
                EXPECT_EQ(table.discards[index].seat, expected.discards[index].seat);
            }
            ASSERT_EQ(table.actions.size(), actions.size());
            for (size_t index = 0; index < table.actions.size(); ++index) {
                const ControlAction& action = table.actions[index];
                EXPECT_EQ(action.nextHand ? NextHandActionToJson() : ActionToJson(action.hand), actions[index]) << "action " << index;
            }
            ASSERT_EQ(table.result.has_value(), expected.result.has_value());
            if (table.result) {
                EXPECT_TRUE(table.result->scores == expected.result->scores);
                EXPECT_TRUE(table.result->deltas == expected.result->deltas);
                EXPECT_TRUE(table.result->kind == expected.result->kind);
                EXPECT_EQ(table.result->winner, expected.result->winner);
            }
        }

        /// Both lobbies tell the same story about who is where, who is a person and who is the host.
        void ExpectLobbiesAgree(const HubPlayer& host, const HubPlayer& guest, int guestSeat) {
            const OnlineLobby& first = host.Client().Lobby();
            const OnlineLobby& second = guest.Client().Lobby();
            EXPECT_EQ(first.room, second.room);
            EXPECT_EQ(first.room, host.Client().Room());
            EXPECT_EQ(first.humans, 2);
            EXPECT_EQ(second.humans, 2);
            for (size_t seat = 0; seat < first.seats.size(); ++seat) {
                EXPECT_TRUE(first.seats[seat].kind == second.seats[seat].kind) << "seat " << seat;
                EXPECT_EQ(first.seats[seat].name, second.seats[seat].name) << "seat " << seat;
                EXPECT_EQ(first.seats[seat].host, second.seats[seat].host) << "seat " << seat;
                EXPECT_EQ(first.seats[seat].host, static_cast<int>(seat) == kHostSeat) << "seat " << seat;
            }
            EXPECT_TRUE(first.seats[kHostSeat].kind == BECore::SeatKind::Human);
            EXPECT_EQ(first.seats[kHostSeat].name, "Ann");
            EXPECT_TRUE(first.seats[static_cast<size_t>(guestSeat)].kind == BECore::SeatKind::Human);
            EXPECT_EQ(first.seats[static_cast<size_t>(guestSeat)].name, "Bob");
            EXPECT_TRUE(first.seats[kHostSeat].mine);
            EXPECT_FALSE(first.seats[static_cast<size_t>(guestSeat)].mine);
            EXPECT_TRUE(second.seats[static_cast<size_t>(guestSeat)].mine);
            EXPECT_FALSE(second.seats[kHostSeat].mine);
            EXPECT_TRUE(host.Client().IsHost());
            EXPECT_FALSE(guest.Client().IsHost());
        }

    }  // namespace

    // ----- (a) two people and two computers -----

    TEST(MahjongOnlineHubTest, TwoPeopleAndTwoComputersPlayAHandThroughTheirTableModelsAndSeeTheNextHand) {
        constexpr int kGuestSeat = 2;
        MahjongHubWorld world;
        HubPlayer host(world);
        HubPlayer guest(world);
        const Players players = {&host, &guest};

        ASSERT_NO_FATAL_FAILURE(HostRoom(world, host));
        EXPECT_TRUE(host.Client().Lobby().canStart);
        ASSERT_NO_FATAL_FAILURE(JoinRoom(world, guest, host, "Bob"));
        EXPECT_FALSE(guest.Client().Lobby().canStart);

        // the guest takes the third seat; both lobbies show it
        ASSERT_TRUE(guest.Client().CanChoose(kGuestSeat));
        ASSERT_TRUE(guest.Client().ChooseSeat(kGuestSeat));
        guest.Sync();
        ASSERT_TRUE(RunUntil(world, players, [&] { return host.Client().Lobby().seats[kGuestSeat].kind == BECore::SeatKind::Human; }));
        EXPECT_EQ(guest.Client().Seat(), kGuestSeat);
        EXPECT_FALSE(guest.Client().CanChoose(kGuestSeat));
        ExpectLobbiesAgree(host, guest, kGuestSeat);

        ASSERT_NO_FATAL_FAILURE(StartAndWait(world, host, players));
        EXPECT_EQ(host.Table().seat, kHostSeat);
        EXPECT_EQ(guest.Table().seat, kGuestSeat);
        EXPECT_EQ(host.Table().handsTotal, kShortMatchHands);
        for (const HubPlayer* player : players) {
            EXPECT_TRUE(InStage(*player, OnlineStage::Game));
            EXPECT_EQ(player->Table().hand.size(), static_cast<size_t>(player->Table().seats[static_cast<size_t>(player->Table().seat)].concealedCount));
        }
        EXPECT_TRUE(host.Client().Lobby().seats[1].kind == BECore::SeatKind::Computer);
        EXPECT_TRUE(host.Client().Lobby().seats[3].kind == BECore::SeatKind::Computer);

        // the first hand to its end; the people leave the end screen alone
        ASSERT_TRUE(RunUntil(world, players, [&] { return AllInPhase(players, SessionPhase::HandOver); }, false));
        for (const HubPlayer* player : players) {
            ASSERT_EQ(player->Table().actions.size(), 1u);
            EXPECT_TRUE(player->Table().actions.front().nextHand);
            EXPECT_TRUE(player->Table().result.has_value());
        }
        // the computers are ready at once and do not take the end screen from the people
        Settle(world, players);
        EXPECT_TRUE(AllInPhase(players, SessionPhase::HandOver));
        const eastl::array<bool, kSeatCount> peopleWait = {false, true, false, true};
        EXPECT_TRUE(host.Table().ready == peopleWait);
        EXPECT_TRUE(guest.Table().ready == peopleWait);

        // both read it and are ready: the next hand is dealt
        ASSERT_TRUE(RunUntil(world, players, [&] { return AllInPhase(players, SessionPhase::Playing) && host.Table().handsPlayed == 1 && guest.Table().handsPlayed == 1; }));
        for (const HubPlayer* player : players) {
            EXPECT_FALSE(player->Table().result.has_value());
            EXPECT_TRUE(player->Table().ready == (eastl::array<bool, kSeatCount>{}));
            ExpectNoNotice(*player);
        }
        ExpectJudgedClean(world);
    }

    // ----- (b) four people -----

    TEST(MahjongOnlineHubTest, TheTablesOfARoomSayWhatTheHostChoseAboutHints) {
        for (const bool allowed : {true, false}) {
            MahjongHubWorld world;
            HubPlayer host(world);
            HubPlayer guest(world);
            const Players players = {&host, &guest};
            ASSERT_NO_FATAL_FAILURE(HostRoom(world, host, "Ann", allowed));
            ASSERT_NO_FATAL_FAILURE(JoinRoom(world, guest, host, "Bob"));
            ASSERT_NO_FATAL_FAILURE(StartAndWait(world, host, players));
            ASSERT_TRUE(RunUntil(world, players, [&] { return world.Game()->GetStateVersion() > kWarmUpVersion; }));
            for (const HubPlayer* player : players) {
                EXPECT_EQ(player->Table().hintsAllowed, allowed) << "allowed " << allowed;
            }
            ExpectJudgedClean(world);
        }
    }

    TEST(MahjongOnlineHubTest, FourPeopleReadEveryViewTheAdapterGivesTheirSeatAtEveryVersionToTheFirstHandOver) {
        MahjongHubWorld world;
        HubPlayer host(world);
        HubPlayer second(world);
        HubPlayer third(world);
        HubPlayer fourth(world);
        const Players players = {&host, &second, &third, &fourth};

        ASSERT_NO_FATAL_FAILURE(HostRoom(world, host));
        for (HubPlayer* guest : {&second, &third, &fourth}) {
            ASSERT_NO_FATAL_FAILURE(JoinRoom(world, *guest, host, "Guest"));
        }
        ASSERT_TRUE(RunUntil(world, players, [&] { return host.Client().Lobby().humans == kSeatCount; }));
        ASSERT_NO_FATAL_FAILURE(StartAndWait(world, host, players));

        std::set<int> seats;
        for (const HubPlayer* player : players) {
            seats.insert(player->Client().Seat());
            EXPECT_EQ(player->Table().seat, player->Client().Seat());
        }
        EXPECT_EQ(seats.size(), static_cast<size_t>(kSeatCount));

        size_t checked = 0;
        const auto compareAll = [&] {
            for (const HubPlayer* player : players) {
                const OnlineTable& table = player->Table();
                if (table.version == world.Game()->GetStateVersion()) {
                    ExpectTableIsTheAdapterView(table, *world.Game(), player->Client().Seat());
                    ++checked;
                }
            }
        };
        // a step by hand: the clients have read what the hub sent and have not answered yet, so each table is the latest version
        for (int count = 0; count < kStepLimit && !AllInPhase(players, SessionPhase::HandOver); ++count) {
            world.Advance(kTick);
            for (HubPlayer* player : players) {
                player->Update();
            }
            compareAll();
            for (HubPlayer* player : players) {
                player->ActFirst(false);
            }
        }
        ASSERT_TRUE(AllInPhase(players, SessionPhase::HandOver));
        Settle(world, players);
        const size_t before = checked;
        compareAll();
        EXPECT_EQ(checked - before, players.size()) << "after the hand every client holds the version of the game";
        EXPECT_GT(checked, players.size() * 10);
        for (const HubPlayer* player : players) {
            EXPECT_TRUE(player->Table().phase == SessionPhase::HandOver);
            EXPECT_TRUE(player->Table().result.has_value());
        }
        ExpectJudgedClean(world);
    }

    // ----- (c) a person drops and comes back -----

    TEST(MahjongOnlineHubTest, AGuestWhoIsSeveredMidHandIsAwayThenReconnectsToTheSameSeatAndFindsALaterTable) {
        MahjongHubWorld world;
        HubPlayer host(world);
        HubPlayer guest(world);
        const Players players = {&host, &guest};
        ASSERT_NO_FATAL_FAILURE(HostRoom(world, host));
        ASSERT_NO_FATAL_FAILURE(JoinRoom(world, guest, host, "Bob"));
        ASSERT_NO_FATAL_FAILURE(StartAndWait(world, host, players));
        ASSERT_TRUE(RunUntil(world, players, [&] { return world.Game()->GetStateVersion() >= kWarmUpVersion; }, false));

        const int seat = guest.Client().Seat();
        ASSERT_GE(seat, 0);
        const RoomSession kept = guest.Client().SessionToKeep();
        ASSERT_TRUE(kept.IsValid());
        const uint64_t revision = guest.Client().SessionRevision();

        world.SetRefusing(true);
        guest.Sever();
        ASSERT_TRUE(RunUntil(world, players, [&] { return InStage(guest, OnlineStage::Reconnecting) && host.Client().Lobby().seats[static_cast<size_t>(seat)].away; }, false));
        EXPECT_TRUE(host.Client().Lobby().seats[static_cast<size_t>(seat)].kind == BECore::SeatKind::Human);
        EXPECT_FALSE(host.Client().Lobby().seats[static_cast<size_t>(seat)].connected);

        // the table of the guest is frozen and readable, and a click does nothing and says nothing
        const uint64_t frozenVersion = guest.Table().version;
        EXPECT_TRUE(guest.Client().HasTable());
        EXPECT_FALSE(guest.Client().Act(0));
        ExpectNoNotice(guest);

        // past the grace a computer plays the seat; the guest, who cannot connect, still holds the old table
        for (int second = 0; second < static_cast<int>(MahjongHubWorld::kGrace.count()) + 5; ++second) {
            Step(world, players, false, seconds(1));
        }
        EXPECT_GT(world.Game()->GetStateVersion(), frozenVersion) << "a computer plays the absent seat after the grace";
        EXPECT_TRUE(InStage(guest, OnlineStage::Reconnecting));
        EXPECT_EQ(guest.Table().version, frozenVersion);
        ExpectNoNotice(guest);

        // the server is back: same seat, same session, a later table
        world.SetRefusing(false);
        ASSERT_TRUE(RunUntil(world, players, [&] { return InStage(guest, OnlineStage::Game) && guest.Table().version > frozenVersion; }, false, milliseconds(100)));
        EXPECT_EQ(guest.Client().Seat(), seat);
        EXPECT_EQ(guest.Table().seat, seat);
        EXPECT_TRUE(guest.Client().SessionToKeep().token == kept.token) << "the token is the same";
        EXPECT_TRUE(guest.Client().SessionToKeep().room == kept.room);
        EXPECT_EQ(guest.Client().SessionRevision(), revision) << "nothing to save again";
        ASSERT_TRUE(RunUntil(world, players, [&] { return !host.Client().Lobby().seats[static_cast<size_t>(seat)].away; }, false));
        ASSERT_TRUE(RunUntil(world, players, [&] { return AllInPhase(players, SessionPhase::HandOver); }, false));
        ExpectJudgedClean(world);
    }

    // ----- (d) a new client resumes from the saved session -----

    TEST(MahjongOnlineHubTest, ANewClientThatResumesTheSavedSessionTakesTheSeatOfTheOneThatLeftTheApp) {
        MahjongHubWorld world;
        HubPlayer host(world);
        HubPlayer guest(world);
        ASSERT_NO_FATAL_FAILURE(HostRoom(world, host));
        ASSERT_NO_FATAL_FAILURE(JoinRoom(world, guest, host, "Bob"));
        ASSERT_NO_FATAL_FAILURE(StartAndWait(world, host, {&host, &guest}));
        ASSERT_TRUE(RunUntil(world, {&host, &guest}, [&] { return world.Game()->GetStateVersion() >= kWarmUpVersion; }, false));
        const int seat = guest.Client().Seat();

        // the app closes: the model keeps the session, the game stores it as JSON and reads it again after the restart
        guest.Client().Disconnect();
        const std::optional<RoomSession> stored = SessionFromJson(SessionToJson(guest.Client().SessionToKeep()));
        ASSERT_TRUE(stored.has_value());

        HubPlayer back(world);
        ASSERT_TRUE(back.Client().Resume(*stored));
        const Players players = {&host, &back};
        ASSERT_TRUE(RunUntil(world, players, [&] { return InStage(back, OnlineStage::Game); }, false));
        EXPECT_EQ(back.Client().Seat(), seat);
        EXPECT_EQ(back.Table().seat, seat);
        EXPECT_FALSE(back.Client().IsHost());
        EXPECT_TRUE(back.Client().SessionToKeep().IsValid());
        ASSERT_TRUE(RunUntil(world, players, [&] { return AllInPhase(players, SessionPhase::HandOver); }, false));
        ExpectNoNotice(back);
        ExpectJudgedClean(world);
    }

    TEST(MahjongOnlineHubTest, ASessionOfARoomThatNoLongerExistsEndsWithTheRoomGoneWordAndNoSession) {
        MahjongHubWorld world;
        HubPlayer host(world);
        ASSERT_NO_FATAL_FAILURE(HostRoom(world, host));
        const RoomSession stored = host.Client().SessionToKeep();
        ASSERT_TRUE(stored.IsValid());
        // alone in a lobby the host leaves the room with its connection: the room is gone
        host.Client().Disconnect();
        EXPECT_EQ(world.Hub().GetRoomCount(), 0u);

        HubPlayer back(world);
        ASSERT_TRUE(back.Client().Resume(stored));
        const uint32_t revision = back.Client().SessionRevision();
        ASSERT_TRUE(RunUntil(world, {&back}, [&] { return InStage(back, OnlineStage::Idle) && back.Client().Notice().serial != 0; }));
        EXPECT_EQ(back.Client().Notice().key, OnlineErrorKey("room_gone"));
        EXPECT_FALSE(back.Client().SessionToKeep().IsValid());
        EXPECT_NE(back.Client().SessionRevision(), revision) << "the game is told to erase the saved session";
        ExpectJudgedClean(world);
    }

    // ----- (e) the host leaves; a started room is not joined -----

    TEST(MahjongOnlineHubTest, AHostWhoLeavesTheLobbyPassesTheRoomOnAndAStartedRoomRefusesANewcomerWithItsWord) {
        MahjongHubWorld world;
        HubPlayer host(world);
        HubPlayer guest(world);
        ASSERT_NO_FATAL_FAILURE(HostRoom(world, host));
        ASSERT_NO_FATAL_FAILURE(JoinRoom(world, guest, host, "Bob"));
        const std::string room(guest.Client().Room().c_str(), guest.Client().Room().size());
        ASSERT_TRUE(RunUntil(world, {&host, &guest}, [&] { return guest.Client().Lobby().humans == 2; }));
        EXPECT_FALSE(guest.Client().IsHost());

        ASSERT_TRUE(host.Client().Leave());
        ASSERT_TRUE(RunUntil(world, {&host, &guest}, [&] { return InStage(host, OnlineStage::Idle) && guest.Client().IsHost(); }));
        EXPECT_FALSE(host.Client().SessionToKeep().IsValid());
        EXPECT_TRUE(guest.Client().Lobby().canStart);
        EXPECT_TRUE(guest.Client().Lobby().seats[static_cast<size_t>(guest.Client().Seat())].host);
        EXPECT_EQ(guest.Client().Lobby().humans, 1);
        EXPECT_EQ(std::string(guest.Client().Room().c_str()), room) << "the room goes on";

        // the new host starts a game alone; a newcomer finds the room started
        ASSERT_NO_FATAL_FAILURE(StartAndWait(world, guest, {&guest}));
        HubPlayer late(world);
        ASSERT_TRUE(late.Client().Join(kUrl, guest.Client().Room(), "Cy"));
        ASSERT_TRUE(RunUntil(world, {&guest, &late}, [&] { return InStage(late, OnlineStage::Idle) && late.Client().Notice().serial != 0; }, false));
        EXPECT_EQ(late.Client().Notice().key, OnlineErrorKey(RoomErrors::kGameStarted));
        EXPECT_FALSE(late.Client().SessionToKeep().IsValid());
        EXPECT_TRUE(InStage(guest, OnlineStage::Game));
        ExpectNoNotice(guest);
        ExpectJudgedClean(world);
    }

}  // namespace BECore::Mahjong::Tests
