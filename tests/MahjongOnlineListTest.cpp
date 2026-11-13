#include "MahjongHubTransport.h"

#include <Mahjong/Online/OnlineWords.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>

/**
 * Browsing the open rooms with `OnlineClient` over a real `RoomHub` with `MahjongSeatedGame` (`MahjongHubTransport.h`): who is listed and with
 * what, a join from the list, a room that is gone or full by the time of the join, the limit of the list, and the end of browsing. Every text
 * a client receives is judged, a `rooms` list included.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        using std::chrono::milliseconds;
        using std::chrono::seconds;

        constexpr eastl::string_view kUrl = "ws://hub.test";
        constexpr milliseconds kTick{10};
        constexpr int kStepLimit = 2000;
        constexpr int kHostLevel = 3;
        constexpr int kFirstSeats = 4;
        constexpr int kListBurst = 5;
        constexpr seconds kPastIdle{40};

        OnlineRoomOptions OpenRoom(bool hintsAllowed = false, int hands = kShortMatchHands, int level = kHostLevel) {
            OnlineRoomOptions options;
            options.hands = hands;
            options.level = level;
            options.hintsAllowed = hintsAllowed;
            options.isPublic = true;
            return options;
        }

        class MahjongOnlineListTest : public ::testing::Test {
        protected:
            /// Steps the world until @p done or the step limit.
            template <typename Done>
            bool RunUntil(Done done, std::initializer_list<HubPlayer*> players) {
                for (int count = 0; count < kStepLimit; ++count) {
                    if (done()) {
                        return true;
                    }
                    _world.Advance(kTick);
                    for (HubPlayer* player : players) {
                        player->Update();
                    }
                }
                return done();
            }

            void Pump(HubPlayer& player) {
                player.Update();
            }

            bool HostRoom(HubPlayer& host, const OnlineRoomOptions& options, const char* name = "Ann") {
                if (!host.Client().Host(kUrl, name, options)) {
                    return false;
                }
                return RunUntil([&] { return host.Client().Stage() == OnlineStage::Lobby; }, {&host});
            }

            /// Starts browsing and waits for the first list.
            bool BrowseAndLoad(HubPlayer& player) {
                if (!player.Client().Browse(kUrl)) {
                    return false;
                }
                return RunUntil([&] { return player.Client().Rooms().loaded; }, {&player});
            }

            /// Asks again on the open connection and waits for the answer.
            bool Refresh(HubPlayer& player) {
                if (!player.Client().Browse(kUrl)) {
                    return false;
                }
                return RunUntil([&] { return !player.Client().Rooms().pending; }, {&player});
            }

            void ExpectJudgedClean() {
                EXPECT_GT(_world.Judge().Texts(), 0u);
                EXPECT_TRUE(_world.Judge().Offences().empty()) << _world.Judge().Offences().front();
            }

            static eastl::string_view NoticeKey(const HubPlayer& player) {
                return player.Client().Notice().key;
            }

            MahjongHubWorld _world;
        };

    }  // namespace

    TEST_F(MahjongOnlineListTest, ARoomMadeByCodeOnlyIsNotListedAndAnOpenOneIsWithTheFactsAGuestWants) {
        HubPlayer hidden(_world);
        HubPlayer open(_world);
        HubPlayer guest(_world);
        OnlineRoomOptions byCode = OpenRoom();
        byCode.isPublic = false;
        ASSERT_TRUE(HostRoom(hidden, byCode, "Hidden"));
        ASSERT_TRUE(HostRoom(open, OpenRoom(true, kShortMatchHands, kHostLevel), "{name}%s"));

        ASSERT_TRUE(BrowseAndLoad(guest));
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Browsing);
        const OnlineRoomList& list = guest.Client().Rooms();
        EXPECT_FALSE(list.more);
        EXPECT_FALSE(list.pending);
        ASSERT_EQ(list.rooms.size(), 1u);
        const OnlineListedRoom& room = list.rooms.front();
        EXPECT_EQ(room.code, open.Client().Room());
        EXPECT_EQ(room.host, "{name}%s");
        EXPECT_EQ(room.free, kFirstSeats - 1);
        EXPECT_EQ(room.hands, std::optional<int>(kShortMatchHands));
        EXPECT_EQ(room.level, std::optional<int>(kHostLevel));
        EXPECT_EQ(room.hintsAllowed, std::optional<bool>(true));
        // a guest browsing is in no room: nothing of a room to keep or to show
        EXPECT_FALSE(guest.Client().SessionToKeep().IsValid());
        EXPECT_TRUE(guest.Client().Room().empty());
        ExpectJudgedClean();
    }

    TEST_F(MahjongOnlineListTest, TwoOpenRoomsAreListedInTheOrderTheyWereMade) {
        HubPlayer first(_world);
        HubPlayer second(_world);
        HubPlayer guest(_world);
        ASSERT_TRUE(HostRoom(first, OpenRoom(), "First"));
        ASSERT_TRUE(HostRoom(second, OpenRoom(), "Second"));

        ASSERT_TRUE(BrowseAndLoad(guest));
        const OnlineRoomList& list = guest.Client().Rooms();
        ASSERT_EQ(list.rooms.size(), 2u);
        EXPECT_EQ(list.rooms[0].code, first.Client().Room());
        EXPECT_EQ(list.rooms[0].host, "First");
        EXPECT_EQ(list.rooms[1].code, second.Client().Room());
        EXPECT_EQ(list.rooms[1].host, "Second");
    }

    TEST_F(MahjongOnlineListTest, AnEmptyListIsLoadedAndSaysSo) {
        HubPlayer guest(_world);
        EXPECT_FALSE(guest.Client().Rooms().loaded);
        ASSERT_TRUE(BrowseAndLoad(guest));
        EXPECT_TRUE(guest.Client().Rooms().rooms.empty());
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Browsing);
    }

    TEST_F(MahjongOnlineListTest, TheStageIsBrowsingWhileTheConnectionOpensAndAJoinFromTheListLandsInTheLobbyACodeGives) {
        HubPlayer host(_world);
        HubPlayer listed(_world);
        HubPlayer byCode(_world);
        ASSERT_TRUE(HostRoom(host, OpenRoom(), "Ann"));

        ASSERT_TRUE(listed.Client().Browse(kUrl));
        EXPECT_EQ(listed.Client().Stage(), OnlineStage::Browsing);
        EXPECT_FALSE(listed.Client().Rooms().loaded);
        EXPECT_TRUE(listed.Client().Rooms().pending);
        ASSERT_TRUE(RunUntil([&] { return listed.Client().Rooms().loaded; }, {&listed}));
        ASSERT_EQ(listed.Client().Rooms().rooms.size(), 1u);

        const eastl::string code = listed.Client().Rooms().rooms.front().code;
        ASSERT_TRUE(listed.Client().JoinListed(kUrl, code, "Bob"));
        EXPECT_EQ(listed.Client().Stage(), OnlineStage::Connecting);
        ASSERT_TRUE(RunUntil([&] { return listed.Client().Stage() == OnlineStage::Lobby; }, {&host, &listed}));
        // the list is gone with the browsing; the room is the one that was listed, and the session is kept
        EXPECT_FALSE(listed.Client().Rooms().loaded);
        EXPECT_TRUE(listed.Client().Rooms().rooms.empty());
        EXPECT_EQ(listed.Client().Room(), host.Client().Room());
        EXPECT_TRUE(listed.Client().SessionToKeep().IsValid());

        // another guest, by the code: the same lobby
        ASSERT_TRUE(byCode.Client().Join(kUrl, host.Client().Room(), "Cy"));
        ASSERT_TRUE(RunUntil([&] { return byCode.Client().Stage() == OnlineStage::Lobby; }, {&host, &listed, &byCode}));
        const OnlineLobby& fromList = listed.Client().Lobby();
        const OnlineLobby& fromCode = byCode.Client().Lobby();
        EXPECT_EQ(fromList.room, fromCode.room);
        EXPECT_TRUE(fromList.phase == fromCode.phase);
        EXPECT_EQ(fromList.seats[0].name, "Ann");
        EXPECT_EQ(fromList.seats[1].name, "Bob");
        EXPECT_EQ(fromCode.seats[2].name, "Cy");
        EXPECT_FALSE(fromList.canStart);
        EXPECT_FALSE(listed.Client().IsHost());
        ExpectJudgedClean();
    }

    TEST_F(MahjongOnlineListTest, ARoomThatClosedBeforeTheJoinGivesTheNoticeAndAFreshListWithoutItOnTheSameConnection) {
        HubPlayer gone(_world);
        HubPlayer stays(_world);
        HubPlayer guest(_world);
        ASSERT_TRUE(HostRoom(gone, OpenRoom(), "Gone"));
        ASSERT_TRUE(HostRoom(stays, OpenRoom(), "Stays"));
        ASSERT_TRUE(BrowseAndLoad(guest));
        ASSERT_EQ(guest.Client().Rooms().rooms.size(), 2u);
        const eastl::string goneCode = gone.Client().Room();
        ASSERT_EQ(guest.Client().Rooms().rooms.front().code, goneCode);
        const size_t connections = _world.Hub().GetConnectionCount();

        // the host leaves the lobby and the room vanishes with its last member
        ASSERT_TRUE(gone.Client().Leave());
        ASSERT_TRUE(RunUntil([&] { return gone.Client().Stage() == OnlineStage::Idle; }, {&gone, &guest}));
        EXPECT_FALSE(_world.Hub().HasRoom(goneCode));

        ASSERT_TRUE(guest.Client().JoinListed(kUrl, goneCode, "Bob"));
        const uint32_t before = guest.Client().Notice().serial;
        ASSERT_TRUE(RunUntil([&] { return guest.Client().Notice().serial != before && guest.Client().Rooms().loaded && !guest.Client().Rooms().pending && guest.Client().Rooms().rooms.size() == 1; },
                             {&guest}));
        EXPECT_EQ(NoticeKey(guest), OnlineErrorKey(RoomErrors::kUnknownRoom));
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Browsing);
        ASSERT_EQ(guest.Client().Rooms().rooms.size(), 1u);
        EXPECT_EQ(guest.Client().Rooms().rooms.front().code, stays.Client().Room());
        // it is the connection that listed: nobody opened another
        EXPECT_LE(_world.Hub().GetConnectionCount(), connections);
        EXPECT_EQ(guest.Links().links.size(), 1u);
        ExpectJudgedClean();
    }

    TEST_F(MahjongOnlineListTest, ARoomThatFilledUpBeforeTheJoinGivesRoomFullAndAListWithoutIt) {
        HubPlayer host(_world);
        HubPlayer second(_world);
        HubPlayer third(_world);
        HubPlayer fourth(_world);
        HubPlayer guest(_world);
        ASSERT_TRUE(HostRoom(host, OpenRoom(), "Ann"));
        ASSERT_TRUE(second.Client().Join(kUrl, host.Client().Room(), "Bob"));
        ASSERT_TRUE(third.Client().Join(kUrl, host.Client().Room(), "Cy"));
        ASSERT_TRUE(RunUntil([&] { return second.Client().Stage() == OnlineStage::Lobby && third.Client().Stage() == OnlineStage::Lobby; }, {&host, &second, &third}));

        ASSERT_TRUE(BrowseAndLoad(guest));
        ASSERT_EQ(guest.Client().Rooms().rooms.size(), 1u);
        EXPECT_EQ(guest.Client().Rooms().rooms.front().free, 1);

        ASSERT_TRUE(fourth.Client().Join(kUrl, host.Client().Room(), "Di"));
        ASSERT_TRUE(RunUntil([&] { return fourth.Client().Stage() == OnlineStage::Lobby; }, {&host, &second, &third, &fourth}));

        ASSERT_TRUE(guest.Client().JoinListed(kUrl, host.Client().Room(), "Eve"));
        const uint32_t before = guest.Client().Notice().serial;
        ASSERT_TRUE(RunUntil([&] { return guest.Client().Notice().serial != before && !guest.Client().Rooms().pending; }, {&guest}));
        EXPECT_EQ(NoticeKey(guest), OnlineErrorKey(RoomErrors::kRoomFull));
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Browsing);
        EXPECT_TRUE(guest.Client().Rooms().loaded);
        EXPECT_TRUE(guest.Client().Rooms().rooms.empty());
    }

    TEST_F(MahjongOnlineListTest, AskingTooOftenGivesTheRateNoticeAndKeepsTheLastListAndTheConnection) {
        HubPlayer host(_world);
        HubPlayer guest(_world);
        ASSERT_TRUE(HostRoom(host, OpenRoom(), "Ann"));
        ASSERT_TRUE(BrowseAndLoad(guest));
        // the clock stands still: the burst of the server's list bucket is spent, then it refuses
        for (int ask = 1; ask < kListBurst; ++ask) {
            ASSERT_TRUE(guest.Client().Browse(kUrl)) << ask;
            guest.Update();
            EXPECT_TRUE(guest.Client().Rooms().loaded) << ask;
        }
        const uint32_t before = guest.Client().Notice().serial;
        ASSERT_TRUE(guest.Client().Browse(kUrl));
        EXPECT_TRUE(guest.Client().Rooms().pending);
        guest.Update();
        EXPECT_GT(guest.Client().Notice().serial, before);
        EXPECT_EQ(NoticeKey(guest), OnlineErrorKey(RoomErrors::kRateLimited));
        EXPECT_FALSE(guest.Client().Rooms().pending);
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Browsing);
        ASSERT_EQ(guest.Client().Rooms().rooms.size(), 1u);
        EXPECT_EQ(guest.Client().Rooms().rooms.front().code, host.Client().Room());

        // a while later the bucket has a token again
        _world.Advance(seconds(3));
        guest.Update();
        EXPECT_TRUE(Refresh(guest));
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Browsing);
    }

    TEST_F(MahjongOnlineListTest, AskAgainWhileAnAskIsUnansweredSendsNothing) {
        HubPlayer guest(_world);
        ASSERT_TRUE(guest.Client().Browse(kUrl));
        const uint64_t before = guest.Client().Version();
        EXPECT_FALSE(guest.Client().Browse(kUrl));
        EXPECT_EQ(guest.Client().Version(), before);
        EXPECT_EQ(guest.Client().Notice().serial, 0u);
    }

    TEST_F(MahjongOnlineListTest, BrowseIsRefusedOutsideIdleAndBrowsing) {
        HubPlayer host(_world);
        ASSERT_TRUE(HostRoom(host, OpenRoom(), "Ann"));
        EXPECT_FALSE(host.Client().Browse(kUrl));
        EXPECT_EQ(NoticeKey(host), OnlineErrorKey("wrong_state"));
        EXPECT_EQ(host.Client().Stage(), OnlineStage::Lobby);
    }

    TEST_F(MahjongOnlineListTest, DisconnectAndLeaveClearTheListAndCloseTheConnection) {
        HubPlayer host(_world);
        HubPlayer byDisconnect(_world);
        HubPlayer byLeave(_world);
        ASSERT_TRUE(HostRoom(host, OpenRoom(), "Ann"));
        const size_t connections = _world.Hub().GetConnectionCount();

        ASSERT_TRUE(BrowseAndLoad(byDisconnect));
        ASSERT_FALSE(byDisconnect.Client().Rooms().rooms.empty());
        EXPECT_EQ(_world.Hub().GetConnectionCount(), connections + 1);
        byDisconnect.Client().Disconnect();
        EXPECT_EQ(byDisconnect.Client().Stage(), OnlineStage::Idle);
        EXPECT_FALSE(byDisconnect.Client().Rooms().loaded);
        EXPECT_TRUE(byDisconnect.Client().Rooms().rooms.empty());
        EXPECT_EQ(_world.Hub().GetConnectionCount(), connections);

        ASSERT_TRUE(BrowseAndLoad(byLeave));
        EXPECT_EQ(_world.Hub().GetConnectionCount(), connections + 1);
        EXPECT_TRUE(byLeave.Client().Leave());
        EXPECT_EQ(byLeave.Client().Stage(), OnlineStage::Idle);
        EXPECT_FALSE(byLeave.Client().Rooms().loaded);
        EXPECT_TRUE(byLeave.Client().Rooms().rooms.empty());
        EXPECT_EQ(_world.Hub().GetConnectionCount(), connections);
        // nothing was said to the player who asked for it
        EXPECT_EQ(byLeave.Client().Notice().serial, 0u);
        EXPECT_EQ(byDisconnect.Client().Notice().serial, 0u);
    }

    TEST_F(MahjongOnlineListTest, ALostConnectionEndsBrowsingWithItsNoticeAndRefreshOpensANewOne) {
        HubPlayer host(_world);
        HubPlayer guest(_world);
        ASSERT_TRUE(HostRoom(host, OpenRoom(), "Ann"));
        ASSERT_TRUE(BrowseAndLoad(guest));

        guest.Sever();
        guest.Update();
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Idle);
        EXPECT_FALSE(guest.Client().Rooms().loaded);
        EXPECT_GT(guest.Client().Notice().serial, 0u);

        ASSERT_TRUE(BrowseAndLoad(guest));
        EXPECT_EQ(guest.Client().Rooms().rooms.size(), 1u);
        EXPECT_EQ(guest.Links().links.size(), 2u);
    }

    TEST_F(MahjongOnlineListTest, AListLeftOnScreenPastTheServersIdleTimeIsClosedWithItsOwnWordNotAnUnknownCode) {
        HubPlayer guest(_world);
        ASSERT_TRUE(BrowseAndLoad(guest));
        _world.Advance(kPastIdle);
        guest.Update();
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Idle);
        EXPECT_FALSE(guest.Client().Rooms().loaded);
        EXPECT_EQ(NoticeKey(guest), OnlineErrorKey(OnlineCodes::kIdle));
        EXPECT_NE(NoticeKey(guest), kOnlineUnknownKey);
    }

    TEST_F(MahjongOnlineListTest, HostingFromTheListUsesTheSameConnectionAndEndsBrowsing) {
        HubPlayer guest(_world);
        ASSERT_TRUE(BrowseAndLoad(guest));
        ASSERT_TRUE(guest.Client().Host(kUrl, "Ann", OpenRoom()));
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Connecting);
        EXPECT_FALSE(guest.Client().Rooms().loaded);
        ASSERT_TRUE(RunUntil([&] { return guest.Client().Stage() == OnlineStage::Lobby; }, {&guest}));
        EXPECT_EQ(guest.Links().links.size(), 1u);
        EXPECT_TRUE(guest.Client().IsHost());
    }

    TEST_F(MahjongOnlineListTest, IdleFramesLeaveTheVersionAlone) {
        HubPlayer host(_world);
        HubPlayer guest(_world);
        ASSERT_TRUE(HostRoom(host, OpenRoom(), "Ann"));
        ASSERT_TRUE(BrowseAndLoad(guest));
        const uint64_t version = guest.Client().Version();
        constexpr int kIdleFrames = 50;
        for (int frame = 0; frame < kIdleFrames; ++frame) {
            _world.Advance(kTick);
            guest.Update();
        }
        EXPECT_EQ(guest.Client().Version(), version);
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Browsing);
    }

}  // namespace BECore::Mahjong::Tests
