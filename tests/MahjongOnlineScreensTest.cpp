#include "MahjongHubTransport.h"
#include "ShippedScene.h"
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

        /// The host makes a room that others can list (`SetPublic(true)`) or, with @p isPublic false, one by code only; true when it is in its lobby.
        bool HostRoomVisible(MahjongHubWorld& world, Person& host, const char* name, bool isPublic) {
            host.screens.Open(host.Client());
            EXPECT_TRUE(host.screens.ShowHost());
            host.Fill(OnlineField::Name, name);
            EXPECT_TRUE(host.screens.SetPublic(isPublic));
            EXPECT_TRUE(host.screens.Create(host.Client(), kUrl));
            return RunUntil(world, {&host}, [&] { return InStage(host, OnlineStage::Lobby); });
        }

        /// The person opens the panel, chooses `Open rooms` and waits for the first list.
        bool ListRooms(MahjongHubWorld& world, Person& person, const People& pumped) {
            person.screens.Open(person.Client());
            EXPECT_TRUE(person.screens.ShowRooms(person.Client(), kUrl));
            return RunUntil(world, pumped, [&] { return person.Client().Rooms().loaded; });
        }

        /// The person presses Refresh and waits for the answer.
        bool Refresh(MahjongHubWorld& world, Person& person, const People& pumped) {
            if (!person.screens.RefreshRooms(person.Client(), kUrl)) {
                return false;
            }
            return RunUntil(world, pumped, [&] { return !person.Client().Rooms().pending; });
        }

        std::string HostOfRow(Person& person, int row) {
            return Std(person.Client().Rooms().rooms[person.screens.RoomIndex(row)].host);
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
        EXPECT_FALSE(host.screens.Options().hintsAllowed) << "hints are not allowed unless the host says so";

        EXPECT_FALSE(host.screens.SetHands(12));
        EXPECT_FALSE(host.screens.SetLevel(0));
        EXPECT_FALSE(host.screens.SetLevel(kMaxLevel + 1));
        EXPECT_EQ(host.screens.Options().hands, defaults.hands);
        EXPECT_TRUE(host.screens.SetHands(kShortMatchHands));
        EXPECT_TRUE(host.screens.SetLevel(kMinLevel));
        EXPECT_TRUE(host.screens.SetHintsAllowed(true));
        EXPECT_TRUE(host.screens.Options().hintsAllowed);
        EXPECT_EQ(host.screens.Options().hands, kShortMatchHands);
        EXPECT_EQ(host.screens.Options().level, kMinLevel);

        ASSERT_TRUE(host.screens.Create(host.Client(), kUrl));
        ASSERT_TRUE(RunUntil(world, {&host}, [&] { return InStage(host, OnlineStage::Lobby); }));
        ASSERT_TRUE(host.screens.Start(host.Client()));
        ASSERT_TRUE(RunUntil(world, {&host}, [&] { return host.Client().HasTable(); }));
        EXPECT_EQ(host.Client().Table().handsTotal, kShortMatchHands);
        EXPECT_TRUE(host.Client().Table().hintsAllowed) << "the choice of the form reached the room";
        EXPECT_EQ(host.screens.Page(), OnlinePage::Closed) << "the table of a started game is the scene's: the panel closes";
        EXPECT_EQ(host.Client().Stage(), OnlineStage::Game);
    }

    TEST(MahjongOnlineScreens, TheHintsChoiceIsKeptOnlyOnTheHostFormAndAHostWhoDidNotChooseGetsARoomWithoutHints) {
        MahjongHubWorld world;
        Person host(world);
        host.screens.Open(host.Client());
        EXPECT_FALSE(host.screens.SetHintsAllowed(true)) << "the Menu has no such choice";
        ASSERT_TRUE(host.screens.ShowJoin());
        EXPECT_FALSE(host.screens.SetHintsAllowed(true)) << "nor has the Join form";
        EXPECT_FALSE(host.screens.Options().hintsAllowed);
        ASSERT_TRUE(host.screens.Back(host.Client()));
        ASSERT_TRUE(host.screens.ShowHost());
        const uint64_t before = host.screens.Version();
        EXPECT_TRUE(host.screens.SetHintsAllowed(false));
        EXPECT_EQ(host.screens.Version(), before) << "the same choice changes nothing";
        EXPECT_TRUE(host.screens.SetHintsAllowed(true));
        EXPECT_GT(host.screens.Version(), before);
        EXPECT_TRUE(host.screens.SetHintsAllowed(false));

        ASSERT_TRUE(host.screens.Create(host.Client(), kUrl));
        ASSERT_TRUE(RunUntil(world, {&host}, [&] { return InStage(host, OnlineStage::Lobby); }));
        ASSERT_NE(host.Client().HostedOptions(), nullptr);
        EXPECT_FALSE(host.Client().HostedOptions()->hintsAllowed);
        ASSERT_TRUE(host.screens.Start(host.Client()));
        ASSERT_TRUE(RunUntil(world, {&host}, [&] { return host.Client().HasTable(); }));
        EXPECT_FALSE(host.Client().Table().hintsAllowed);
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

        ASSERT_TRUE(host.screens.Back(host.Client()));
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
        EXPECT_FALSE(host.screens.Back(host.Client())) << "closed: nothing to go back from";
        host.screens.Open(host.Client());
        EXPECT_TRUE(host.screens.IsOpen());

        // Entry to its form, a form to the Menu, the Menu closes
        ASSERT_TRUE(host.screens.ShowJoin());
        ASSERT_TRUE(host.screens.FocusField(OnlineField::Code));
        EXPECT_EQ(host.screens.Page(), OnlinePage::Entry);
        EXPECT_EQ(host.screens.Focus(), OnlineField::Code);
        EXPECT_TRUE(host.screens.Back(host.Client()));
        EXPECT_EQ(host.screens.Page(), OnlinePage::Join);
        ASSERT_TRUE(host.screens.FocusField(OnlineField::Name));
        EXPECT_TRUE(host.screens.Done());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Join);
        EXPECT_FALSE(host.screens.Done()) << "Done is for the Entry page";
        EXPECT_TRUE(host.screens.Back(host.Client()));
        EXPECT_EQ(host.screens.Page(), OnlinePage::Menu);

        // The Host form has no code field, and the Entry page returns to the form it came from
        ASSERT_TRUE(host.screens.ShowHost());
        EXPECT_FALSE(host.screens.FocusField(OnlineField::Code));
        ASSERT_TRUE(host.screens.FocusField(OnlineField::Name));
        EXPECT_TRUE(host.screens.Done());
        EXPECT_EQ(host.screens.Page(), OnlinePage::Host);
        EXPECT_TRUE(host.screens.Back(host.Client()));
        EXPECT_TRUE(host.screens.Back(host.Client()));
        EXPECT_EQ(host.screens.Page(), OnlinePage::Closed);
        EXPECT_FALSE(host.screens.IsOpen());

        // The Lobby ignores it: a stray key must not give up a seat
        ASSERT_TRUE(HostRoom(world, host));
        EXPECT_FALSE(host.screens.Back(host.Client()));
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

    TEST(MahjongOnlineScreens, APasteFillsTheCodeOnTheJoinFormAndOnTheCodePadAndNowhereElse) {
        MahjongHubWorld world;
        Person person(world);
        person.screens.Open(person.Client());
        EXPECT_FALSE(person.screens.PasteCode("ab3k9")) << "the Menu takes no paste";
        ASSERT_TRUE(person.screens.ShowHost());
        EXPECT_FALSE(person.screens.PasteCode("ab3k9")) << "the Host form has no code";
        ASSERT_TRUE(person.screens.Back(person.Client()));
        ASSERT_TRUE(person.screens.ShowJoin());

        uint64_t version = person.screens.Version();
        EXPECT_TRUE(person.screens.PasteCode("  ab3k9 "));
        EXPECT_EQ(Std(person.screens.Code().Text()), "AB3K9");
        EXPECT_GT(person.screens.Version(), version);
        version = person.screens.Version();
        EXPECT_TRUE(person.screens.PasteCode("ab3k9"));
        EXPECT_EQ(person.screens.Version(), version) << "the same code is no change";

        // on the pad of the code the paste replaces what was typed; the pad of the name takes none
        ASSERT_TRUE(person.screens.FocusField(OnlineField::Code));
        person.screens.Refresh(person.strings);
        ASSERT_TRUE(person.screens.Backspace());
        ASSERT_TRUE(person.screens.PressKey(0));
        EXPECT_TRUE(person.screens.PasteCode("Join my room, the code is mm7nn"));
        EXPECT_EQ(Std(person.screens.Code().Text()), "MM7NN");
        ASSERT_TRUE(person.screens.Done());
        ASSERT_TRUE(person.screens.FocusField(OnlineField::Name));
        const std::string name = Std(person.screens.Name().Text());
        EXPECT_FALSE(person.screens.PasteCode("zz7zz"));
        EXPECT_EQ(Std(person.screens.Code().Text()), "MM7NN");
        EXPECT_EQ(Std(person.screens.Name().Text()), name);
        EXPECT_EQ(person.screens.PasteNote(), OnlinePasteNote::None);
        EXPECT_FALSE(person.screens.NoteNothingToPaste(ClipboardStatus::Empty));

        // on the form, which has no pad, a text with no code says so in the notice line
        ASSERT_TRUE(person.screens.Done());
        EXPECT_FALSE(person.screens.PasteCode("hello world"));
        EXPECT_EQ(Std(person.screens.NoticeKey()), Std(kOnlinePasteNotCodeKey));
        EXPECT_EQ(Std(person.screens.Code().Text()), "MM7NN");
        EXPECT_TRUE(person.screens.PasteCode("zz7zz"));
        EXPECT_TRUE(person.screens.NoticeKey().empty()) << "a paste that worked is a move";
    }

    TEST(MahjongOnlineScreens, ATextWithNoCodeChangesNothingAndTheNoteGoesAtTheNextMove) {
        MahjongHubWorld world;
        Person person(world);
        person.screens.Open(person.Client());
        ASSERT_TRUE(person.screens.ShowJoin());
        ASSERT_TRUE(person.screens.PasteCode("ab3k9"));
        ASSERT_TRUE(person.screens.FocusField(OnlineField::Code));
        person.screens.Refresh(person.strings);

        EXPECT_FALSE(person.screens.PasteCode("hello world"));
        EXPECT_EQ(Std(person.screens.Code().Text()), "AB3K9") << "a refused paste leaves the field as it was";
        EXPECT_EQ(person.screens.PasteNote(), OnlinePasteNote::NotACode);
        EXPECT_EQ(Std(person.screens.PasteNoteKey()), Std(kOnlinePasteNotCodeKey));
        const uint64_t version = person.screens.Version();
        EXPECT_FALSE(person.screens.PasteCode("hello again"));
        EXPECT_EQ(person.screens.Version(), version) << "the same note is no change";

        // the next key, Backspace, Done and a page move each clear it
        EXPECT_FALSE(person.screens.PressKey(0)) << "the code is full, but the key is a move all the same";
        EXPECT_EQ(person.screens.PasteNote(), OnlinePasteNote::None);
        EXPECT_TRUE(person.screens.NoteNothingToPaste(ClipboardStatus::Empty));
        EXPECT_EQ(Std(person.screens.PasteNoteKey()), Std(kOnlinePasteEmptyKey));
        EXPECT_TRUE(person.screens.Backspace());
        EXPECT_EQ(person.screens.PasteNote(), OnlinePasteNote::None);
        EXPECT_TRUE(person.screens.NoteNothingToPaste(ClipboardStatus::Refused));
        EXPECT_EQ(Std(person.screens.PasteNoteKey()), Std(kOnlinePasteRefusedKey));
        EXPECT_TRUE(person.screens.NoteNothingToPaste(ClipboardStatus::Unavailable));
        EXPECT_EQ(person.screens.PasteNote(), OnlinePasteNote::Refused);
        EXPECT_FALSE(person.screens.NoteNothingToPaste(ClipboardStatus::Ok));
        ASSERT_TRUE(person.screens.Done());
        EXPECT_EQ(person.screens.PasteNote(), OnlinePasteNote::None);
        EXPECT_TRUE(person.screens.PasteNoteKey().empty());

        ASSERT_TRUE(person.screens.FocusField(OnlineField::Code));
        EXPECT_FALSE(person.screens.PasteCode(""));
        ASSERT_TRUE(person.screens.Back(person.Client()));
        EXPECT_EQ(person.screens.PasteNote(), OnlinePasteNote::None);
    }

    TEST(MahjongOnlineScreens, TheCopyButtonCopiesTheCodeOfTheRoomOfTheLobbyAndForgetsItAtTheNextMove) {
        MahjongHubWorld world;
        Person host(world);
        host.screens.Open(host.Client());
        EXPECT_TRUE(host.screens.RoomCodeToCopy(host.Client()).empty());
        EXPECT_FALSE(host.screens.NoteCopy(true)) << "only the lobby copies";
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::None);

        ASSERT_TRUE(HostRoom(world, host));
        EXPECT_EQ(Std(host.screens.RoomCodeToCopy(host.Client())), Std(host.Client().Room()));
        EXPECT_EQ(host.screens.RoomCodeToCopy(host.Client()).size(), RoomLimits::kRoomCodeLength);
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::None);

        EXPECT_TRUE(host.screens.NoteCopy(true));
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::Copied);
        const uint64_t version = host.screens.Version();
        host.screens.NoteCopy(true);
        EXPECT_EQ(host.screens.Version(), version);
        Settle(world, {&host});
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::Copied) << "frames alone do not end it";
        EXPECT_EQ(host.screens.Version(), version);

        EXPECT_TRUE(host.screens.NoteCopy(false));
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::Failed);

        // a move of the player: a seat
        host.screens.NoteCopy(true);
        const int seat = host.Client().Seat() == 1 ? 2 : 1;
        ASSERT_TRUE(host.screens.Sit(host.Client(), seat));
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::None);

        // the panel left and opened again, and a closed one
        host.screens.NoteCopy(true);
        host.screens.Close();
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::None);
        EXPECT_TRUE(host.screens.RoomCodeToCopy(host.Client()).empty());
        host.screens.Open(host.Client());
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::None);
    }

    TEST(MahjongOnlineScreens, TheCopyStateEndsWithTheLobbyAndANewRoomStartsClean) {
        MahjongHubWorld world;
        Person host(world);
        ASSERT_TRUE(HostRoom(world, host));
        ASSERT_TRUE(host.screens.NoteCopy(true));

        ASSERT_TRUE(host.screens.Leave(host.Client()));
        ASSERT_TRUE(RunUntil(world, {&host}, [&] { return host.screens.Page() == OnlinePage::Menu; }));
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::None);
        ASSERT_TRUE(HostRoom(world, host));
        EXPECT_EQ(host.screens.CopyState(), OnlineCopyState::None) << "a new room starts with its own button";
        EXPECT_EQ(Std(host.screens.RoomCodeToCopy(host.Client())), Std(host.Client().Room()));
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

    namespace {

        /// The config as the game reads it: a root with a `webServer` attribute (`value` null: no attribute).
        OnlineServerConfig ConfigWith(const char* value) {
            const XmlConfig config = XmlConfig::Create();
            const eastl::string xml = value != nullptr ? eastl::string("<root webServer=\"") + value + "\"/>" : eastl::string("<root/>");
            EXPECT_TRUE(config.LoadFromString(eastl::string_view(xml.data(), xml.size())));
            return ReadOnlineServerConfig(config.GetRoot());
        }

        const eastl::string kStandardUrl(kDefaultOnlineServerUrl.data(), kDefaultOnlineServerUrl.size());

    }  // namespace

    TEST(MahjongOnlineServerUrl, ADesktopTakesTheVariableElseTheDefaultAndIgnoresTheConfig) {
        const OnlineServerConfig hosted = ConfigWith("wss://rooms.example.test");
        EXPECT_EQ(OnlineServerUrl(false, nullptr, hosted), kStandardUrl);
        EXPECT_EQ(OnlineServerUrl(false, "", hosted), kStandardUrl) << "an empty variable is no variable";
        EXPECT_EQ(OnlineServerUrl(false, "ws://example.test:9", hosted), eastl::string("ws://example.test:9"));
        EXPECT_EQ(OnlineServerUrl(false, nullptr, OnlineServerConfig{}), kStandardUrl);
        EXPECT_EQ(Std(kStandardUrl).rfind("ws", 0), 0u);
    }

    TEST(MahjongOnlineServerUrl, TheBrowserTakesTheConfigAndNothingElse) {
        EXPECT_EQ(OnlineServerUrl(true, nullptr, ConfigWith("wss://rooms.example.test")), eastl::string("wss://rooms.example.test"));
        EXPECT_EQ(OnlineServerUrl(true, "ws://example.test:9", ConfigWith("wss://rooms.example.test")), eastl::string("wss://rooms.example.test")) << "the browser has no environment";
        EXPECT_EQ(OnlineServerUrl(true, nullptr, ConfigWith("ws://localhost:8080")), kStandardUrl) << "a plain address is accepted";
        EXPECT_TRUE(OnlineServerUrl(true, nullptr, OnlineServerConfig{}).empty()) << "no localhost default in the browser";
        EXPECT_TRUE(OnlineServerUrl(true, "ws://example.test:9", OnlineServerConfig{}).empty());
    }

    TEST(MahjongOnlineServerUrl, OnlyAWebSocketAddressReadsAsSet) {
        EXPECT_EQ(ConfigWith("wss://rooms.example.test").webServer, eastl::string("wss://rooms.example.test"));
        EXPECT_EQ(ConfigWith("ws://localhost:8080").webServer, kStandardUrl);
        EXPECT_TRUE(ConfigWith(nullptr).webServer.empty());
        EXPECT_TRUE(ConfigWith("").webServer.empty());
        EXPECT_TRUE(ConfigWith("http://rooms.example.test").webServer.empty());
        EXPECT_TRUE(ConfigWith("https://rooms.example.test").webServer.empty());
        EXPECT_TRUE(ConfigWith("rooms.example.test").webServer.empty());
        EXPECT_TRUE(ReadOnlineServerConfig(XmlNode()).webServer.empty()) << "an invalid node reads as not set";
    }

    TEST(MahjongOnlineServerUrl, TheShippedConfigNamesASecureServer) {
        const XmlConfig config = XmlConfig::Create();
        ASSERT_TRUE(config.LoadFromFile(ShippedConfigDirectory() / (std::string(kOnlineConfigName.data(), kOnlineConfigName.size()) + ".xml")));
        const OnlineServerConfig shipped = ReadOnlineServerConfig(config.GetRoot());
        EXPECT_EQ(Std(shipped.webServer).rfind("wss://", 0), 0u) << "a typo in config/MahjongOnlineConfig.xml reads as not set: " << Std(shipped.webServer);
        // The browser build offers `Online` exactly when this is not empty (the component hides the button on an empty address) and connects to it
        EXPECT_EQ(OnlineServerUrl(true, nullptr, shipped), shipped.webServer);
        EXPECT_FALSE(OnlineServerUrl(true, nullptr, shipped).empty());
    }

    // ---- The Rooms page -----------------------------------------------------------------------------------------------------------

    TEST(MahjongOnlineScreens, TheMenuOpensTheRoomsPageAndItListsTheOpenRoomsInTheOrderTheyWereMadeAndNotThePrivateOne) {
        MahjongHubWorld world;
        Person first(world);
        Person hidden(world);
        Person second(world);
        Person guest(world);
        ASSERT_TRUE(HostRoomVisible(world, first, "Ann", true));
        ASSERT_TRUE(HostRoomVisible(world, hidden, "Hidden", false));
        ASSERT_TRUE(HostRoomVisible(world, second, "Cy", true));

        guest.screens.Open(guest.Client());
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Menu);
        ASSERT_TRUE(guest.screens.ShowRooms(guest.Client(), kUrl));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Rooms);
        EXPECT_EQ(guest.screens.StatusKey(), kOnlineBrowsingKey) << "until the first answer";
        EXPECT_EQ(guest.screens.RoomsOnPage(), 0);
        EXPECT_FALSE(guest.screens.RoomsEmpty());
        EXPECT_FALSE(guest.screens.ShowHost()) << "the Menu's choices are for the Menu";

        ASSERT_TRUE(RunUntil(world, {&guest}, [&] { return guest.Client().Rooms().loaded; }));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Rooms);
        EXPECT_TRUE(guest.screens.StatusKey().empty());
        EXPECT_EQ(guest.screens.RoomsPageCount(), 1);
        ASSERT_EQ(guest.screens.RoomsOnPage(), 2);
        EXPECT_EQ(HostOfRow(guest, 0), "Ann");
        EXPECT_EQ(HostOfRow(guest, 1), "Cy");
        EXPECT_FALSE(guest.screens.NextRoomsPage()) << "one page";
    }

    TEST(MahjongOnlineScreens, FiveRoomsAreThreeRowsAPageAndTheNextPageWrapsAround) {
        MahjongHubWorld world;
        std::vector<std::unique_ptr<Person>> hosts;
        const char* const names[] = {"Ann", "Bob", "Cy", "Di", "Ed"};
        for (const char* name : names) {
            hosts.push_back(std::make_unique<Person>(world));
            ASSERT_TRUE(HostRoomVisible(world, *hosts.back(), name, true)) << name;
        }
        Person guest(world);
        ASSERT_TRUE(ListRooms(world, guest, {&guest}));
        EXPECT_EQ(guest.screens.RoomsPageCount(), 2);
        EXPECT_EQ(guest.screens.RoomsPage(), 0);
        ASSERT_EQ(guest.screens.RoomsOnPage(), OnlineScreens::kRoomsPerPage);
        EXPECT_EQ(HostOfRow(guest, 0), "Ann");
        EXPECT_EQ(HostOfRow(guest, 2), "Cy");

        const uint64_t before = guest.screens.Version();
        ASSERT_TRUE(guest.screens.NextRoomsPage());
        EXPECT_GT(guest.screens.Version(), before);
        EXPECT_EQ(guest.screens.RoomsPage(), 1);
        ASSERT_EQ(guest.screens.RoomsOnPage(), 2);
        EXPECT_EQ(HostOfRow(guest, 0), "Di");
        EXPECT_EQ(HostOfRow(guest, 1), "Ed");

        // A join by a row of the second page takes that row's room
        ASSERT_TRUE(guest.screens.JoinRoom(guest.Client(), kUrl, 1));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Lobby);
        People all = {&guest};
        for (auto& host : hosts) {
            all.push_back(host.get());
        }
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(guest, OnlineStage::Lobby); }));
        EXPECT_EQ(guest.Client().Room(), hosts[4]->Client().Room());

        // The last page is followed by the first
        Person other(world);
        all.push_back(&other);
        ASSERT_TRUE(ListRooms(world, other, all));
        ASSERT_TRUE(other.screens.NextRoomsPage());
        EXPECT_EQ(other.screens.RoomsPage(), 1);
        ASSERT_TRUE(other.screens.NextRoomsPage());
        EXPECT_EQ(other.screens.RoomsPage(), 0);
    }

    TEST(MahjongOnlineScreens, AJoinByRowLandsInTheSameLobbyACodeGivesAndNeedsAName) {
        MahjongHubWorld world;
        Person host(world);
        Person guest(world);
        Person byCode(world);
        ASSERT_TRUE(HostRoomVisible(world, host, "Ann", true));
        ASSERT_TRUE(ListRooms(world, guest, {&host, &guest}));

        // An empty name is not sent; the list stays
        guest.Fill(OnlineField::Name, "");
        EXPECT_FALSE(guest.screens.JoinRoom(guest.Client(), kUrl, 0));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Rooms);
        EXPECT_EQ(NoticeOf(guest), KeyOf(RoomErrors::kBadName));
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Browsing);
        EXPECT_FALSE(guest.screens.JoinRoom(guest.Client(), kUrl, 1)) << "a row that is not there";
        EXPECT_FALSE(guest.screens.JoinRoom(guest.Client(), kUrl, -1));

        guest.Fill(OnlineField::Name, "Bob");
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Rooms) << "the Entry page returns to the list";
        ASSERT_TRUE(guest.screens.JoinRoom(guest.Client(), kUrl, 0));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Lobby);
        const People both = {&host, &guest};
        ASSERT_TRUE(RunUntil(world, both, [&] { return InStage(guest, OnlineStage::Lobby); }));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Lobby);
        EXPECT_TRUE(guest.screens.StatusKey().empty());

        ASSERT_TRUE(EnterRoom(byCode, host.Client().Room(), "Cy"));
        const People three = {&host, &guest, &byCode};
        ASSERT_TRUE(RunUntil(world, three, [&] { return InStage(byCode, OnlineStage::Lobby); }));
        Settle(world, three);
        EXPECT_EQ(guest.Client().Lobby().room, byCode.Client().Lobby().room);
        EXPECT_EQ(guest.Client().Lobby().seats[1].name, "Bob");
        EXPECT_EQ(byCode.Client().Lobby().seats[1].name, "Bob");
        EXPECT_EQ(byCode.screens.Page(), OnlinePage::Lobby);

        // Leaving that lobby is leaving a room: the Menu, not the list
        ASSERT_TRUE(guest.screens.Leave(guest.Client()));
        ASSERT_TRUE(RunUntil(world, three, [&] { return InStage(guest, OnlineStage::Idle); }));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Menu);
    }

    TEST(MahjongOnlineScreens, ARoomGoneBeforeTheJoinGivesTheNoticeAndTheListPageWithoutIt) {
        MahjongHubWorld world;
        Person gone(world);
        Person stays(world);
        Person guest(world);
        ASSERT_TRUE(HostRoomVisible(world, gone, "Gone", true));
        ASSERT_TRUE(HostRoomVisible(world, stays, "Stays", true));
        const People all = {&gone, &stays, &guest};
        ASSERT_TRUE(ListRooms(world, guest, all));
        ASSERT_EQ(guest.screens.RoomsOnPage(), 2);

        ASSERT_TRUE(gone.screens.Leave(gone.Client()));
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(gone, OnlineStage::Idle); }));
        ASSERT_TRUE(guest.screens.JoinRoom(guest.Client(), kUrl, 0));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Lobby) << "while it is being joined";
        ASSERT_TRUE(RunUntil(world, all, [&] { return !guest.screens.NoticeKey().empty() && guest.Client().Rooms().loaded && !guest.Client().Rooms().pending; }));
        EXPECT_EQ(guest.screens.Page(), OnlinePage::Rooms);
        EXPECT_EQ(NoticeOf(guest), KeyOf(RoomErrors::kUnknownRoom));
        EXPECT_EQ(guest.Client().Stage(), OnlineStage::Browsing);
        ASSERT_EQ(guest.screens.RoomsOnPage(), 1);
        EXPECT_EQ(HostOfRow(guest, 0), "Stays");

        // The player's next move clears the notice, and the room that is left can be joined
        ASSERT_TRUE(guest.screens.JoinRoom(guest.Client(), kUrl, 0));
        EXPECT_TRUE(guest.screens.NoticeKey().empty());
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(guest, OnlineStage::Lobby); }));
        EXPECT_EQ(guest.Client().Room(), stays.Client().Room());
    }

    TEST(MahjongOnlineScreens, AnEmptyListOffersToHostAndHostingFromItEndsTheBrowsing) {
        MahjongHubWorld world;
        Person person(world);
        Person other(world);
        ASSERT_TRUE(ListRooms(world, person, {&person}));
        EXPECT_TRUE(person.screens.RoomsEmpty());
        EXPECT_EQ(person.screens.StatusKey(), kOnlineNoRoomsKey);
        EXPECT_EQ(person.screens.RoomsOnPage(), 0);
        EXPECT_EQ(person.screens.RoomsPageCount(), 1);
        EXPECT_FALSE(person.screens.NextRoomsPage());

        ASSERT_TRUE(person.screens.HostFromRooms(person.Client()));
        EXPECT_EQ(person.screens.Page(), OnlinePage::Host);
        EXPECT_EQ(person.Client().Stage(), OnlineStage::Idle);
        EXPECT_TRUE(person.screens.StatusKey().empty());
        EXPECT_FALSE(person.screens.RoomsEmpty()) << "the list went with the browsing";
        EXPECT_FALSE(person.screens.HostFromRooms(person.Client())) << "Rooms page only";

        // The default of the form is a room by code only; Open room lists it for the next person
        EXPECT_FALSE(person.screens.Options().isPublic);
        ASSERT_TRUE(person.screens.SetPublic(true));
        EXPECT_TRUE(person.screens.Options().isPublic);
        person.Fill(OnlineField::Name, "Ann");
        ASSERT_TRUE(person.screens.Create(person.Client(), kUrl));
        const People both = {&person, &other};
        ASSERT_TRUE(RunUntil(world, both, [&] { return InStage(person, OnlineStage::Lobby); }));
        ASSERT_TRUE(ListRooms(world, other, both));
        ASSERT_EQ(other.screens.RoomsOnPage(), 1);
        EXPECT_EQ(HostOfRow(other, 0), "Ann");
    }

    TEST(MahjongOnlineScreens, CreateSendsTheVisibilityAndTheDefaultIsARoomNobodyLists) {
        MahjongHubWorld world;
        Person host(world);
        Person guest(world);
        host.screens.Open(host.Client());
        EXPECT_FALSE(host.screens.SetPublic(true)) << "the Host form only";
        ASSERT_TRUE(host.screens.ShowHost());
        const uint64_t version = host.screens.Version();
        EXPECT_TRUE(host.screens.SetPublic(false)) << "a choice that is already made is still a valid one";
        EXPECT_EQ(host.screens.Version(), version);
        host.Fill(OnlineField::Name, "Ann");
        ASSERT_TRUE(host.screens.Create(host.Client(), kUrl));
        const People both = {&host, &guest};
        ASSERT_TRUE(RunUntil(world, both, [&] { return InStage(host, OnlineStage::Lobby); }));
        ASSERT_TRUE(ListRooms(world, guest, both));
        EXPECT_TRUE(guest.screens.RoomsEmpty());
    }

    TEST(MahjongOnlineScreens, BackLeavesTheListWithNoConnectionAndEveryNewPageGoesBackWhereItCameFrom) {
        MahjongHubWorld world;
        Person host(world);
        Person person(world);
        ASSERT_TRUE(HostRoomVisible(world, host, "Ann", true));
        ASSERT_TRUE(ListRooms(world, person, {&host, &person}));
        EXPECT_EQ(world.Hub().GetConnectionCount(), 2u);

        // The name pad of the list returns to the list, with the list and the connection
        ASSERT_TRUE(person.screens.FocusField(OnlineField::Name));
        EXPECT_EQ(person.screens.Page(), OnlinePage::Entry);
        EXPECT_FALSE(person.screens.FocusField(OnlineField::Code));
        person.screens.Refresh(person.strings);
        ASSERT_TRUE(person.screens.PressKey(0));
        ASSERT_TRUE(person.screens.Back(person.Client()));
        EXPECT_EQ(person.screens.Page(), OnlinePage::Rooms);
        EXPECT_EQ(person.Client().Stage(), OnlineStage::Browsing);
        EXPECT_EQ(person.screens.RoomsOnPage(), 1);
        ASSERT_TRUE(person.screens.FocusField(OnlineField::Name));
        ASSERT_TRUE(person.screens.Done());
        EXPECT_EQ(person.screens.Page(), OnlinePage::Rooms);

        // Escape on the list: the Menu, and the connection is closed
        ASSERT_TRUE(person.screens.Back(person.Client()));
        EXPECT_EQ(person.screens.Page(), OnlinePage::Menu);
        EXPECT_EQ(person.Client().Stage(), OnlineStage::Idle);
        EXPECT_FALSE(person.Client().Rooms().loaded);
        Settle(world, {&host, &person});
        EXPECT_EQ(world.Hub().GetConnectionCount(), 1u) << "only the host's";
        EXPECT_EQ(person.screens.Page(), OnlinePage::Menu);

        // The Host form that the list offered goes back to the Menu
        ASSERT_TRUE(ListRooms(world, person, {&host, &person}));
        ASSERT_TRUE(person.screens.HostFromRooms(person.Client()));
        ASSERT_TRUE(person.screens.Back(person.Client()));
        EXPECT_EQ(person.screens.Page(), OnlinePage::Menu);
    }

    TEST(MahjongOnlineScreens, ARefreshAsksAgainOnceAtATimeAndAClosedConnectionIsOpenedAgainByIt) {
        MahjongHubWorld world;
        Person host(world);
        Person late(world);
        Person person(world);
        ASSERT_TRUE(HostRoomVisible(world, host, "Ann", true));
        ASSERT_TRUE(ListRooms(world, person, {&host, &late, &person}));
        ASSERT_EQ(person.screens.RoomsOnPage(), 1);

        ASSERT_TRUE(HostRoomVisible(world, late, "Bob", true));
        ASSERT_TRUE(person.screens.RefreshRooms(person.Client(), kUrl));
        EXPECT_FALSE(person.screens.RefreshRooms(person.Client(), kUrl)) << "an ask is in flight";
        ASSERT_TRUE(RunUntil(world, {&host, &late, &person}, [&] { return !person.Client().Rooms().pending; }));
        EXPECT_EQ(person.screens.RoomsOnPage(), 2);

        // The connection is gone (the server's idle time, a lost link): the page stays, and Refresh opens a new one
        person.Client().Disconnect();
        person.Update();
        EXPECT_EQ(person.screens.Page(), OnlinePage::Rooms);
        EXPECT_EQ(person.screens.RoomsOnPage(), 0);
        ASSERT_TRUE(Refresh(world, person, {&host, &late, &person}));
        EXPECT_EQ(person.screens.RoomsOnPage(), 2);
        EXPECT_EQ(person.Client().Stage(), OnlineStage::Browsing);

        // A panel hidden while browsing comes back on its list
        person.screens.Close();
        EXPECT_EQ(person.screens.Page(), OnlinePage::Closed);
        person.screens.Open(person.Client());
        EXPECT_EQ(person.screens.Page(), OnlinePage::Rooms);
        EXPECT_EQ(person.screens.RoomsOnPage(), 2);
    }

    TEST(MahjongOnlineScreens, AShorterRefreshedListClampsThePageAndAQuietFrameChangesNothing) {
        MahjongHubWorld world;
        std::vector<std::unique_ptr<Person>> hosts;
        People all;
        for (const char* name : {"Ann", "Bob", "Cy", "Di"}) {
            hosts.push_back(std::make_unique<Person>(world));
            ASSERT_TRUE(HostRoomVisible(world, *hosts.back(), name, true)) << name;
            all.push_back(hosts.back().get());
        }
        Person guest(world);
        all.push_back(&guest);
        ASSERT_TRUE(ListRooms(world, guest, all));
        ASSERT_EQ(guest.screens.RoomsPageCount(), 2);
        ASSERT_TRUE(guest.screens.NextRoomsPage());
        ASSERT_EQ(guest.screens.RoomsOnPage(), 1);

        ASSERT_TRUE(hosts[3]->screens.Leave(hosts[3]->Client()));
        ASSERT_TRUE(RunUntil(world, all, [&] { return InStage(*hosts[3], OnlineStage::Idle); }));
        ASSERT_TRUE(Refresh(world, guest, all));
        EXPECT_EQ(guest.screens.RoomsPageCount(), 1);
        EXPECT_EQ(guest.screens.RoomsPage(), 0);
        EXPECT_EQ(guest.screens.RoomsOnPage(), 3);

        Settle(world, all);
        const uint64_t version = guest.screens.Version();
        const uint64_t clientVersion = guest.Client().Version();
        Settle(world, all);
        EXPECT_EQ(guest.screens.Version(), version);
        EXPECT_EQ(guest.Client().Version(), clientVersion);
    }

}  // namespace BECore::Mahjong::Tests
