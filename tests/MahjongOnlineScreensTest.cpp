#include "MahjongHubTransport.h"
#include "StringsTestSupport.h"

#include <Mahjong/Online/OnlineScreens.h>
#include <Mahjong/Online/OnlineServerUrl.h>
#include <Mahjong/Online/OnlineWords.h>
#include <Rooms/RoomErrors.h>
#include <gtest/gtest.h>

/**
 * `OnlineScreens` (`Online/OnlineScreens.h`): the pages of the online panel over `OnlineClient`s on a real `RoomHub` with `MahjongSeatedGame` (the in-memory world of
 * `MahjongHubTransport.h`): the player's choices become client commands, and the page, the notice and the status follow the client. No scene.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        using std::chrono::milliseconds;

        constexpr eastl::string_view kUrl = "ws://hub.test";
        constexpr milliseconds kTick{10};
        constexpr int kStepLimit = 20000;
        constexpr int kSettleSteps = 10;
        constexpr int kSeats = 4;
        constexpr int kHostSeat = 0;

        std::string Std(eastl::string_view text) {
            return std::string(text.data(), text.size());
        }

        /// One person at one screen: the client on a world, the pages over it and a string table of its own.
        struct Person {
            explicit Person(MahjongHubWorld& world) : player(world) {
                EXPECT_TRUE(LoadShippedStrings(strings).empty());
                EXPECT_TRUE(strings.SetLanguage("en"_intern));
                screens.Refresh(strings);
            }

            Person(const Person&) = delete;
            Person& operator=(const Person&) = delete;

            [[nodiscard]] OnlineClient& Client() {
                return player.Client();
            }

            void Update() {
                player.Update();
                screens.Sync(player.Client());
                screens.Refresh(strings);
            }

            /// Types @p text into @p field from its form (the Entry page and back).
            void Fill(OnlineField field, const char* text) {
                ASSERT_TRUE(screens.FocusField(field));
                screens.Refresh(strings);
                while (screens.Backspace()) {
                }
                for (const char* c = text; *c != '\0'; ++c) {
                    ASSERT_TRUE(screens.Type(eastl::string_view(c, 1))) << *c;
                }
                ASSERT_TRUE(screens.Done());
            }

            HubPlayer player;
            UIStrings strings;
            OnlineScreens screens;
        };

        using People = std::vector<Person*>;

        void Step(MahjongHubWorld& world, const People& people) {
            world.Advance(kTick);
            for (Person* person : people) {
                person->Update();
            }
        }

        template <typename Done>
        bool RunUntil(MahjongHubWorld& world, const People& people, Done done) {
            for (int count = 0; count < kStepLimit; ++count) {
                if (done()) {
                    return true;
                }
                Step(world, people);
            }
            return done();
        }

        void Settle(MahjongHubWorld& world, const People& people) {
            for (int count = 0; count < kSettleSteps; ++count) {
                Step(world, people);
            }
        }

        bool InStage(Person& person, OnlineStage stage) {
            return person.Client().Stage() == stage;
        }

        /// The host fills the Host form and creates the room; true when it is in its lobby.
        bool HostRoom(MahjongHubWorld& world, Person& host, const char* name = "Ann") {
            host.screens.Open(host.Client());
            EXPECT_TRUE(host.screens.ShowHost());
            host.Fill(OnlineField::Name, name);
            EXPECT_TRUE(host.screens.Create(host.Client(), kUrl));
            return RunUntil(world, {&host}, [&] { return InStage(host, OnlineStage::Lobby); });
        }

        /// The host starts the game and waits for its table: a seat is held for a person who drops only in a started game (in a lobby it is freed at once).
        bool StartGame(MahjongHubWorld& world, Person& host) {
            EXPECT_TRUE(host.screens.Start(host.Client()));
            return RunUntil(world, {&host}, [&] { return host.Client().HasTable(); });
        }

        /// A person types a name and the code and enters; the caller waits.
        bool EnterRoom(Person& person, const eastl::string& code, const char* name) {
            person.screens.Open(person.Client());
            EXPECT_TRUE(person.screens.ShowJoin());
            person.Fill(OnlineField::Name, name);
            person.Fill(OnlineField::Code, Std(code).c_str());
            return person.screens.Enter(person.Client(), kUrl);
        }

        std::string NoticeOf(const Person& person) {
            return Std(person.screens.NoticeKey());
        }

        std::string KeyOf(eastl::string_view code) {
            return Std(OnlineErrorKey(code));
        }

    }  // namespace

    TEST(MahjongOnlineScreens, TheMenuOffersHostAndJoinAndTheHostFormCreatesARoomWithItsCode) {
        MahjongHubWorld world;
        Person host(world);
        host.screens.Open(host.Client());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Menu);
        EXPECT_FALSE(host.screens.CanReturn());

        ASSERT_TRUE(host.screens.ShowHost());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Host);
        EXPECT_EQ(Std(host.screens.Name().Text()), "Player") << "the name starts as the default one of the language";
        EXPECT_TRUE(host.screens.CanCreate());

        ASSERT_TRUE(host.screens.Create(host.Client(), kUrl));
        EXPECT_EQ(host.screens.Page(), OnlinePage::Lobby) << "connecting shows the lobby page with a status";
        EXPECT_EQ(Std(host.screens.StatusKey()), Std(kOnlineConnectingKey));

        ASSERT_TRUE(RunUntil(world, {&host}, [&] { return InStage(host, OnlineStage::Lobby); }));
        EXPECT_EQ(host.screens.Page(), OnlinePage::Lobby);
        EXPECT_TRUE(host.screens.StatusKey().empty());
        EXPECT_TRUE(host.screens.NoticeKey().empty());
        EXPECT_EQ(host.Client().Room().size(), RoomLimits::kRoomCodeLength);
        EXPECT_EQ(host.Client().Seat(), kHostSeat);
        EXPECT_TRUE(host.Client().IsHost());
    }

    TEST(MahjongOnlineScreens, TheHostOptionsAreCheckedAndReachTheGame) {
        MahjongHubWorld world;
        Person host(world);
        host.screens.Open(host.Client());
        EXPECT_FALSE(host.screens.SetHands(8)) << "only on the Host form";
        ASSERT_TRUE(host.screens.ShowHost());
        const OnlineRoomOptions defaults;
        EXPECT_EQ(host.screens.Options().hands, defaults.hands);
        EXPECT_EQ(host.screens.Options().level, defaults.level);

        EXPECT_FALSE(host.screens.SetHands(12));
        EXPECT_FALSE(host.screens.SetLevel(0));
        EXPECT_FALSE(host.screens.SetLevel(kMaxLevel + 1));
        EXPECT_EQ(host.screens.Options().hands, defaults.hands);
        EXPECT_TRUE(host.screens.SetHands(kShortMatchHands));
        EXPECT_TRUE(host.screens.SetLevel(kMinLevel));
        EXPECT_EQ(host.screens.Options().hands, kShortMatchHands);
        EXPECT_EQ(host.screens.Options().level, kMinLevel);

        ASSERT_TRUE(host.screens.Create(host.Client(), kUrl));
        ASSERT_TRUE(RunUntil(world, {&host}, [&] { return InStage(host, OnlineStage::Lobby); }));
        ASSERT_TRUE(host.screens.Start(host.Client()));
        ASSERT_TRUE(RunUntil(world, {&host}, [&] { return host.Client().HasTable(); }));
        EXPECT_EQ(host.Client().Table().handsTotal, kShortMatchHands);
        EXPECT_EQ(host.screens.Page(), OnlinePage::Closed) << "the table of a started game is the scene's: the panel closes";
        EXPECT_EQ(host.Client().Stage(), OnlineStage::Game);
    }

    TEST(MahjongOnlineScreens, JoinByCodeWithANameAndALowerCaseCodeSeatsThePlayer) {
        MahjongHubWorld world;
        Person host(world);
        Person guest(world);
        ASSERT_TRUE(HostRoom(world, host));

        eastl::string lower = host.Client().Room();
        for (char& c : lower) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        ASSERT_TRUE(EnterRoom(guest, lower, "Bob"));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Lobby);
        ASSERT_TRUE(RunUntil(world, {&host, &guest}, [&] { return InStage(guest, OnlineStage::Lobby); }));
        EXPECT_EQ(guest.Client().Room(), host.Client().Room());
        EXPECT_EQ(Std(guest.screens.Code().Text()), Std(host.Client().Room())) << "the code is folded to upper case as it is typed";
        EXPECT_NE(guest.Client().Seat(), kHostSeat);
        EXPECT_TRUE(guest.screens.NoticeKey().empty());
        Settle(world, {&host, &guest});
        const OnlineLobbySeat& seat = host.Client().Lobby().seats[static_cast<size_t>(guest.Client().Seat())];
        EXPECT_EQ(Std(seat.name), "Bob");
    }

    TEST(MahjongOnlineScreens, AWrongCodeLeavesThePlayerOnTheJoinFormWithTheNoticeOnce) {
        MahjongHubWorld world;
        Person guest(world);
        ASSERT_TRUE(EnterRoom(guest, "ABCDE", "Bob"));
        ASSERT_TRUE(RunUntil(world, {&guest}, [&] { return InStage(guest, OnlineStage::Idle) && guest.screens.Page() != OnlinePage::Lobby; }));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Join);
        EXPECT_EQ(NoticeOf(guest), KeyOf(RoomErrors::kUnknownRoom));
        EXPECT_EQ(Std(guest.screens.Code().Text()), "ABCDE") << "the form keeps what was typed";
        EXPECT_TRUE(guest.screens.StatusKey().empty());

        // Shown once: a frame with nothing new changes nothing; the next move of the player clears it
        const uint64_t version = guest.screens.Version();
        Settle(world, {&guest});
        EXPECT_EQ(guest.screens.Version(), version);
        EXPECT_EQ(NoticeOf(guest), KeyOf(RoomErrors::kUnknownRoom));
        ASSERT_TRUE(guest.screens.FocusField(OnlineField::Code));
        EXPECT_TRUE(guest.screens.NoticeKey().empty());
        ASSERT_TRUE(guest.screens.Done());
        Settle(world, {&guest});
        EXPECT_TRUE(guest.screens.NoticeKey().empty()) << "the same notice is not shown again";
    }

    TEST(MahjongOnlineScreens, AFullRoomAStartedGameAndATakenSeatEachGiveTheirNotice) {
        MahjongHubWorld world;
        Person host(world);
        Person a(world);
        Person b(world);
        Person c(world);
        Person late(world);
        ASSERT_TRUE(HostRoom(world, host));
        const People all = {&host, &a, &b, &c, &late};
        ASSERT_TRUE(EnterRoom(a, host.Client().Room(), "Ann2"));
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(a, OnlineStage::Lobby); }));
        ASSERT_TRUE(EnterRoom(b, host.Client().Room(), "Bob"));
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(b, OnlineStage::Lobby); }));
        ASSERT_TRUE(EnterRoom(c, host.Client().Room(), "Cy"));
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(c, OnlineStage::Lobby); }));

        // The room is full
        ASSERT_TRUE(EnterRoom(late, host.Client().Room(), "Zed"));
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(late, OnlineStage::Idle) && late.screens.Page() != OnlinePage::Lobby; }));
        EXPECT_EQ(late.screens.Page(), OnlinePage::Join);
        EXPECT_EQ(NoticeOf(late), KeyOf(RoomErrors::kRoomFull));

        // The game started
        ASSERT_TRUE(host.screens.Start(host.Client()));
        ASSERT_TRUE(RunUntil(world, all, [&] { return host.Client().HasTable(); }));
        ASSERT_TRUE(late.screens.Enter(late.Client(), kUrl));
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(late, OnlineStage::Idle) && late.screens.Page() != OnlinePage::Lobby; }));
        EXPECT_EQ(late.screens.Page(), OnlinePage::Join);
        EXPECT_EQ(NoticeOf(late), KeyOf(RoomErrors::kGameStarted));
    }

    TEST(MahjongOnlineScreens, ASeatTakenBetweenTheViewAndTheClickIsANoticeInTheLobby) {
        MahjongHubWorld world;
        Person host(world);
        Person a(world);
        Person b(world);
        ASSERT_TRUE(HostRoom(world, host));
        const People all = {&host, &a, &b};
        ASSERT_TRUE(EnterRoom(a, host.Client().Room(), "Ann2"));
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(a, OnlineStage::Lobby); }));
        ASSERT_TRUE(EnterRoom(b, host.Client().Room(), "Bob"));
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(b, OnlineStage::Lobby); }));
        Settle(world, all);

        const int wanted = kSeats - 1;
        ASSERT_TRUE(a.Client().CanChoose(wanted));
        ASSERT_TRUE(b.Client().CanChoose(wanted));
        EXPECT_TRUE(a.screens.Sit(a.Client(), wanted));
        EXPECT_TRUE(b.screens.Sit(b.Client(), wanted)) << "b has not seen a's move yet";
        Settle(world, all);
        EXPECT_EQ(a.Client().Seat(), wanted);
        EXPECT_NE(b.Client().Seat(), wanted);
        EXPECT_EQ(b.screens.Page(), OnlinePage::Lobby);
        EXPECT_EQ(NoticeOf(b), KeyOf(RoomErrors::kSeatTaken));
        EXPECT_TRUE(a.screens.NoticeKey().empty());

        // A seat the lobby does not show free is not asked for
        EXPECT_FALSE(b.screens.Sit(b.Client(), wanted));
        EXPECT_FALSE(b.screens.Sit(b.Client(), kSeats));
    }

    TEST(MahjongOnlineScreens, ANameThatCannotBeSentSendsNothingAndSaysWhy) {
        MahjongHubWorld world;
        Person host(world);
        host.screens.Open(host.Client());
        ASSERT_TRUE(host.screens.ShowHost());
        host.Fill(OnlineField::Name, " ");
        EXPECT_FALSE(host.screens.CanCreate());
        EXPECT_FALSE(host.screens.Create(host.Client(), kUrl));
        EXPECT_EQ(host.screens.Page(), OnlinePage::Host);
        EXPECT_EQ(NoticeOf(host), KeyOf(RoomErrors::kBadName));
        EXPECT_TRUE(host.player.Links().links.empty()) << "no connection was made";
        EXPECT_EQ(host.Client().Stage(), OnlineStage::Idle);

        ASSERT_TRUE(host.screens.Back());
        ASSERT_TRUE(host.screens.ShowJoin());
        host.Fill(OnlineField::Name, "Bob");
        host.Fill(OnlineField::Code, "AB");
        EXPECT_FALSE(host.screens.CanEnter());
        EXPECT_FALSE(host.screens.Enter(host.Client(), kUrl));
        EXPECT_EQ(host.screens.Page(), OnlinePage::Join);
        EXPECT_EQ(NoticeOf(host), KeyOf(RoomErrors::kUnknownRoom));
        EXPECT_TRUE(host.player.Links().links.empty());
    }

    TEST(MahjongOnlineScreens, BackDoneAndEscapeFollowThePages) {
        MahjongHubWorld world;
        Person host(world);
        EXPECT_FALSE(host.screens.Back()) << "closed: nothing to go back from";
        host.screens.Open(host.Client());
        EXPECT_TRUE(host.screens.IsOpen());

        // Entry to its form, a form to the Menu, the Menu closes
        ASSERT_TRUE(host.screens.ShowJoin());
        ASSERT_TRUE(host.screens.FocusField(OnlineField::Code));
        EXPECT_EQ(host.screens.Page(), OnlinePage::Entry);
        EXPECT_EQ(host.screens.Focus(), OnlineField::Code);
        EXPECT_TRUE(host.screens.Back());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Join);
        ASSERT_TRUE(host.screens.FocusField(OnlineField::Name));
        EXPECT_TRUE(host.screens.Done());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Join);
        EXPECT_FALSE(host.screens.Done()) << "Done is for the Entry page";
        EXPECT_TRUE(host.screens.Back());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Menu);

        // The Host form has no code field, and the Entry page returns to the form it came from
        ASSERT_TRUE(host.screens.ShowHost());
        EXPECT_FALSE(host.screens.FocusField(OnlineField::Code));
        ASSERT_TRUE(host.screens.FocusField(OnlineField::Name));
        EXPECT_TRUE(host.screens.Done());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Host);
        EXPECT_TRUE(host.screens.Back());
        EXPECT_TRUE(host.screens.Back());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Closed);
        EXPECT_FALSE(host.screens.IsOpen());

        // The Lobby ignores it: a stray key must not give up a seat
        ASSERT_TRUE(HostRoom(world, host));
        EXPECT_FALSE(host.screens.Back());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Lobby);
        EXPECT_TRUE(host.Client().Stage() == OnlineStage::Lobby);
        // ... but the panel can be closed and opened again over the same room
        host.screens.Close();
        EXPECT_EQ(host.screens.Page(), OnlinePage::Closed);
        host.screens.Open(host.Client());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Lobby);
    }

    TEST(MahjongOnlineScreens, ALostConnectionShowsTheStatusAndKeepsTheLobby) {
        MahjongHubWorld world;
        Person host(world);
        Person guest(world);
        ASSERT_TRUE(HostRoom(world, host));
        ASSERT_TRUE(EnterRoom(guest, host.Client().Room(), "Bob"));
        const People both = {&host, &guest};
        ASSERT_TRUE(RunUntil(world, both, [&] { return InStage(guest, OnlineStage::Lobby); }));
        Settle(world, both);

        world.SetRefusing(true);
        guest.player.Sever();
        Settle(world, both);
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Lobby);
        EXPECT_EQ(Std(guest.screens.StatusKey()), Std(kOnlineReconnectingKey));
        EXPECT_EQ(guest.Client().Room(), host.Client().Room()) << "the lobby stays readable";
        EXPECT_TRUE(guest.screens.NoticeKey().empty());

        // A lobby frees the seat of a person who dropped, so a server that comes back says the room is gone: the Menu, with the notice, and nothing to return to
        world.SetRefusing(false);
        ASSERT_TRUE(RunUntil(world, both, [&] { return guest.screens.Page() == OnlinePage::Menu; }));
        EXPECT_TRUE(guest.screens.StatusKey().empty());
        EXPECT_EQ(NoticeOf(guest), KeyOf(RoomErrors::kBadToken));
        EXPECT_FALSE(guest.screens.CanReturn());
    }

    TEST(MahjongOnlineScreens, ALostConnectionInAStartedGameLeavesTheTableToTheScene) {
        MahjongHubWorld world;
        Person host(world);
        Person guest(world);
        ASSERT_TRUE(HostRoom(world, host));
        ASSERT_TRUE(EnterRoom(guest, host.Client().Room(), "Bob"));
        const People both = {&host, &guest};
        ASSERT_TRUE(RunUntil(world, both, [&] { return InStage(guest, OnlineStage::Lobby); }));
        ASSERT_TRUE(host.screens.Start(host.Client()));
        ASSERT_TRUE(RunUntil(world, both, [&] { return guest.Client().HasTable(); }));

        guest.player.Sever();
        Settle(world, both);
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Closed) << "the frozen table is the scene's too";
        ASSERT_TRUE(RunUntil(world, both, [&] { return InStage(guest, OnlineStage::Game); }));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Closed);
        EXPECT_TRUE(guest.screens.StatusKey().empty());
        EXPECT_TRUE(guest.screens.NoticeKey().empty());
    }

    TEST(MahjongOnlineScreens, LeaveForgetsTheSessionAndGoesToTheMenu) {
        MahjongHubWorld world;
        Person host(world);
        ASSERT_TRUE(HostRoom(world, host));
        EXPECT_TRUE(host.Client().SessionToKeep().IsValid());
        ASSERT_TRUE(host.screens.Leave(host.Client()));
        ASSERT_TRUE(RunUntil(world, {&host}, [&] { return host.screens.Page() == OnlinePage::Menu; }));
        EXPECT_FALSE(host.Client().SessionToKeep().IsValid());
        EXPECT_FALSE(host.screens.CanReturn());
        EXPECT_TRUE(host.screens.NoticeKey().empty()) << "a leave the player asked for says nothing";
    }

    TEST(MahjongOnlineScreens, GivingUpAnAttemptToConnectReturnsToTheForm) {
        MahjongHubWorld world;
        Person guest(world);
        world.SetRefusing(true);
        ASSERT_TRUE(EnterRoom(guest, "ABCDE", "Bob"));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Lobby);
        ASSERT_TRUE(guest.screens.Leave(guest.Client()));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Join);
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Idle);

        // A server that refuses ends the attempt on its own, with the notice
        ASSERT_TRUE(guest.screens.Enter(guest.Client(), kUrl));
        ASSERT_TRUE(RunUntil(world, {&guest}, [&] { return guest.screens.Page() == OnlinePage::Join; }));
        EXPECT_FALSE(guest.screens.NoticeKey().empty());
    }

    TEST(MahjongOnlineScreens, ReturnOffersTheKeptSessionAndResumeBringsThePlayerBack) {
        MahjongHubWorld world;
        Person first(world);
        ASSERT_TRUE(HostRoom(world, first));
        ASSERT_TRUE(StartGame(world, first));
        const eastl::string room = first.Client().Room();
        const RoomSession session = first.Client().SessionToKeep();
        ASSERT_TRUE(session.IsValid());

        // The same client: a closed connection keeps the session
        first.Client().Disconnect();
        first.screens.Open(first.Client());
        EXPECT_EQ(first.screens.Page(), OnlinePage::Menu);
        ASSERT_TRUE(first.screens.CanReturn());
        EXPECT_EQ(first.screens.ReturnRoom(), room);

        // A new run: the game stored the room, the client has nothing yet
        Person second(world);
        second.screens.SetStoredRoom(Std(room).c_str());
        second.screens.Open(second.Client());
        EXPECT_EQ(second.screens.Page(), OnlinePage::Menu);
        ASSERT_TRUE(second.screens.CanReturn());
        EXPECT_EQ(second.screens.ReturnRoom(), room);
        EXPECT_FALSE(second.screens.Resume(second.Client(), RoomSession{})) << "a session that is not a room is not entered";
        EXPECT_EQ(second.Client().Stage(), OnlineStage::Idle);
        second.screens.Open(second.Client());

        ASSERT_TRUE(second.screens.Resume(second.Client(), session));
        EXPECT_EQ(second.screens.Page(), OnlinePage::Lobby);
        EXPECT_EQ(Std(second.screens.StatusKey()), Std(kOnlineReconnectingKey));
        ASSERT_TRUE(RunUntil(world, {&second}, [&] { return second.Client().HasTable(); }));
        EXPECT_EQ(second.screens.Page(), OnlinePage::Closed) << "the table of the room is the scene's";
        EXPECT_TRUE(second.screens.StatusKey().empty());
        EXPECT_TRUE(second.screens.NoticeKey().empty());
        EXPECT_EQ(second.Client().Room(), room);
        EXPECT_EQ(second.Client().Seat(), kHostSeat);
        EXPECT_FALSE(second.screens.CanReturn());

        // The stored room does not outlive the session of the client: a leave forgets it
        ASSERT_TRUE(second.Client().Leave());
        ASSERT_TRUE(RunUntil(world, {&second}, [&] { return second.Client().Stage() == OnlineStage::Idle; }));
        EXPECT_FALSE(second.screens.CanReturn());
    }

    TEST(MahjongOnlineScreens, AStoredRoomThatIsGoneSaysSoAndIsForgotten) {
        MahjongHubWorld world;
        Person first(world);
        ASSERT_TRUE(HostRoom(world, first));
        ASSERT_TRUE(StartGame(world, first));
        const RoomSession session = first.Client().SessionToKeep();
        first.Client().Disconnect();
        // Nobody is in the room: it closes after the abandoned time
        world.Advance(std::chrono::minutes(11));

        Person second(world);
        second.screens.SetStoredRoom(Std(session.room).c_str());
        second.screens.Open(second.Client());
        ASSERT_TRUE(second.screens.CanReturn());
        ASSERT_TRUE(second.screens.Resume(second.Client(), session));
        ASSERT_TRUE(RunUntil(world, {&second}, [&] { return InStage(second, OnlineStage::Idle) && second.screens.Page() != OnlinePage::Lobby; }));
        EXPECT_EQ(second.screens.Page(), OnlinePage::Menu);
        const std::string key = NoticeOf(second);
        EXPECT_TRUE(key == KeyOf(RoomErrors::kUnknownRoom) || key == KeyOf(RoomErrors::kBadToken)) << key;
        EXPECT_FALSE(second.screens.CanReturn());
        EXPECT_FALSE(second.Client().SessionToKeep().IsValid());
    }

    TEST(MahjongOnlineScreens, ThePadChoosesTheKeyByIndexAndFollowsTheLanguage) {
        MahjongHubWorld world;
        Person person(world);
        person.screens.Open(person.Client());
        ASSERT_TRUE(person.screens.ShowJoin());
        EXPECT_TRUE(person.screens.Pad().empty()) << "the pad exists on the Entry page only";
        EXPECT_FALSE(person.screens.PressKey(0));

        ASSERT_TRUE(person.screens.FocusField(OnlineField::Code));
        person.screens.Refresh(person.strings);
        ASSERT_EQ(person.screens.Pad().size(), RoomLimits::kRoomCodeAlphabet.size());
        EXPECT_TRUE(person.screens.PressKey(0));
        EXPECT_TRUE(person.screens.PressKey(person.screens.Pad().size() - 1));
        EXPECT_FALSE(person.screens.PressKey(person.screens.Pad().size()));
        EXPECT_EQ(Std(person.screens.Code().Text()), "A9");
        EXPECT_TRUE(person.screens.Backspace());
        EXPECT_EQ(Std(person.screens.Code().Text()), "A");
        for (int count = 0; count < 10; ++count) {
            person.screens.PressKey(1);
        }
        EXPECT_EQ(person.screens.Code().Text().size(), RoomLimits::kRoomCodeLength);
        ASSERT_TRUE(person.screens.Done());
        EXPECT_TRUE(person.screens.Pad().empty());

        // The name: the pad is the letters of the language; a default name follows the language until the player edits it
        ASSERT_TRUE(person.strings.SetLanguage("ru"_intern));
        person.screens.Refresh(person.strings);
        EXPECT_EQ(Std(person.screens.Name().Text()), Std(TextEntry::DefaultName(person.strings)));
        ASSERT_TRUE(person.screens.FocusField(OnlineField::Name));
        person.screens.Refresh(person.strings);
        const uint64_t version = person.screens.Version();
        EXPECT_EQ(Std(person.screens.Pad().front()), "\xD0\x90");
        ASSERT_TRUE(person.screens.PressKey(1));
        EXPECT_GT(person.screens.Version(), version);
        ASSERT_TRUE(person.strings.SetLanguage("en"_intern));
        person.screens.Refresh(person.strings);
        EXPECT_EQ(Std(person.screens.Pad().front()), "A") << "the pad is made again for the language";
        EXPECT_NE(Std(person.screens.Name().Text()), Std(TextEntry::DefaultName(person.strings))) << "an edited name stays";
    }

    TEST(MahjongOnlineScreens, AStoredNameStaysAndAFrameWithNothingNewChangesNothing) {
        MahjongHubWorld world;
        Person person(world);
        person.screens.SetStoredName("Kira");
        person.screens.SetStoredName("no! way");
        ASSERT_TRUE(person.strings.SetLanguage("ru"_intern));
        person.screens.Refresh(person.strings);
        EXPECT_EQ(Std(person.screens.Name().Text()), "Kira");
        person.screens.Open(person.Client());
        ASSERT_TRUE(person.screens.ShowHost());
        ASSERT_TRUE(person.screens.FocusField(OnlineField::Name));
        person.screens.Refresh(person.strings);

        const uint64_t version = person.screens.Version();
        for (int count = 0; count < kSettleSteps; ++count) {
            Step(world, {&person});
        }
        EXPECT_EQ(person.screens.Version(), version);
    }

    TEST(MahjongOnlineServerUrl, TheVariableWinsOnTheDesktopAndNowhereElse) {
        const eastl::string standard(kDefaultOnlineServerUrl.data(), kDefaultOnlineServerUrl.size());
        EXPECT_EQ(OnlineServerUrl(false, nullptr), standard);
        EXPECT_EQ(OnlineServerUrl(false, ""), standard) << "an empty variable is no variable";
        EXPECT_EQ(OnlineServerUrl(false, "ws://example.test:9"), eastl::string("ws://example.test:9"));
        EXPECT_EQ(OnlineServerUrl(true, "ws://example.test:9"), standard) << "the browser has no environment";
        EXPECT_EQ(OnlineServerUrl(true, nullptr), standard);
        EXPECT_EQ(Std(standard).rfind("ws", 0), 0u);
    }

}  // namespace BECore::Mahjong::Tests
