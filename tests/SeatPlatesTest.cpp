#include "HandTestSupport.h"
#include "ShippedTableLayout.h"
#include "StringsTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/SeatPlates.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/UiPalette.h>
#include <cmath>
#include <gtest/gtest.h>
#include <set>

/**
 * The plates of the seats (`Table/SeatPlates.h`): which seat lies at each side of the table, and what its plate says (the name and the
 * turn, the kind, the level and the declared hand; the wind, the score and the dealer are the centre panel's, `CenterPanelTest`). Pure logic over a `Match` and its `TableSession`; the shipped words
 * are loaded into a `UIStrings` of the test, so no engine services are needed. The expectations are worked out here from the match and the session, not taken from the function under test.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeed = 31;
        constexpr int kActionLimit = 4000;
        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        constexpr SeatSetup kTwoPeople{SeatKind::Person, SeatKind::Computer, SeatKind::Person, SeatKind::Computer};

        UIStrings& Words() {
            static UIStrings strings;
            [[maybe_unused]] static const bool loaded = [] {
                EXPECT_TRUE(LoadShippedStrings(strings).empty());
                EXPECT_TRUE(strings.SetLanguage("en"_intern));
                return true;
            }();
            return strings;
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.data(), text.size());
        }

        struct Table {
            Match match;
            TableSession session{match};

            void Start(const SeatSetup& seats, uint32_t seed = kSeed, int hands = kShortMatchHands) {
                MahjongSettings settings;
                settings.handsPerMatch = hands;
                ASSERT_TRUE(session.StartMatch(settings, seed, seats).has_value());
            }

            void Start(const SeatSetup& seats, const SeatLevels& levels) {
                MahjongSettings settings;
                settings.handsPerMatch = kShortMatchHands;
                ASSERT_TRUE(session.StartMatch(settings, kSeed, seats, levels).has_value());
            }
        };

        SeatSetup PersonAt(Seat seat) {
            SeatSetup setup = kAllComputers;
            setup[static_cast<size_t>(seat)] = SeatKind::Person;
            return setup;
        }

        /// The name a plate shows: "Seat 2", and the word `seat.you` after it for the person at a table with one person.
        eastl::string ExpectedName(const UIStrings& strings, Seat seat, bool you) {
            const eastl::string number = Format("{}", seat + 1);
            eastl::string name = Tr(strings, "seat.short", {{"n", number}});
            if (you) {
                name += Tr(strings, "seat.you");
            }
            return name;
        }

        /// Checks the four plates against the match and the session, with `bottom` the seat the table shows at the bottom.
        void ExpectPlates(const UIStrings& strings, const Table& table, Seat bottom) {
            const auto plates = MakeSeatPlates(strings, table.session, table.match);
            const Seat turn = table.session.GetPhase() == SessionPhase::Playing ? table.match.Hand().CurrentSeat() : kNoSeat;
            int people = 0;
            for (const SeatKind kind : table.session.Seats()) {
                people += kind == SeatKind::Person ? 1 : 0;
            }
            int turns = 0;
            for (int position = 0; position < kSeatCount; ++position) {
                const SeatPlateText& plate = plates[static_cast<size_t>(position)];
                const Seat seat = (bottom + position) % kSeatCount;
                ASSERT_EQ(plate.seat, seat) << "position " << position;
                EXPECT_EQ(plate.seat, SeatAtPosition(bottom, position));
                const bool you = people == 1 && table.session.Seats()[static_cast<size_t>(seat)] == SeatKind::Person;
                EXPECT_EQ(Std(plate.name), Std(ExpectedName(strings, seat, you))) << "position " << position;
                EXPECT_EQ(plate.turn, seat == turn) << "position " << position;
                EXPECT_EQ(plate.name.find('{'), eastl::string::npos);
                turns += plate.turn ? 1 : 0;
            }
            EXPECT_EQ(turns, turn == kNoSeat ? 0 : 1);
        }

        /// The seat at the bottom, worked out here: the viewer, else the person that is to act, else the first person, else seat 0.
        Seat ExpectedBottom(const Table& table) {
            if (IsSeat(table.session.Viewer())) {
                return table.session.Viewer();
            }
            if (table.session.GetPhase() == SessionPhase::Playing && table.session.ActorIsPerson()) {
                return table.session.Actor();
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (table.session.Seats()[static_cast<size_t>(seat)] == SeatKind::Person) {
                    return seat;
                }
            }
            return 0;
        }

        /// A computer table steps until `stop(table)` is true; false when the match ended or the limit was reached first.
        template <class Stop>
        bool PlayComputersUntil(Table& table, Stop stop) {
            for (int actions = 0; actions < kActionLimit; ++actions) {
                if (stop(table)) {
                    return true;
                }
                if (table.session.GetPhase() == SessionPhase::HandOver) {
                    EXPECT_TRUE(table.session.NextHand().has_value());
                } else if (table.session.GetPhase() == SessionPhase::MatchOver) {
                    return false;
                } else {
                    EXPECT_TRUE(table.session.StepComputer().has_value());
                }
            }
            return false;
        }

    }  // namespace

    TEST(SeatPlatesTest, ThePersonOfAOnePersonTableIsAtTheBottomWithTheWordYou) {
        for (Seat person = 0; person < kSeatCount; ++person) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(PersonAt(person)));
            EXPECT_EQ(BottomSeat(table.session), person);
            ASSERT_NO_FATAL_FAILURE(ExpectPlates(Words(), table, person));
            const auto plates = MakeSeatPlates(Words(), table.session, table.match);
            EXPECT_EQ(plates[0].seat, person);
            for (int position = 0; position < kSeatCount; ++position) {
                const bool hasYou = Std(plates[static_cast<size_t>(position)].name) == Std(ExpectedName(Words(), person, true));
                EXPECT_EQ(hasYou, position == 0) << "person " << person << " position " << position;
            }
        }
    }

    TEST(SeatPlatesTest, ASpectatorSeesSeatZeroAtTheBottomAndNoYou) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        EXPECT_EQ(BottomSeat(table.session), 0);
        ASSERT_NO_FATAL_FAILURE(ExpectPlates(Words(), table, 0));
        for (const SeatPlateText& plate : MakeSeatPlates(Words(), table.session, table.match)) {
            EXPECT_EQ(plate.name.find(Std(Tr(Words(), "seat.you")).c_str()), eastl::string::npos) << Std(plate.name);
        }
    }

    TEST(SeatPlatesTest, WithTwoPeopleThePlatesFollowTheViewerAndHaveNoYou) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kTwoPeople));
        MahjongRng rng(kSeed);
        std::set<Seat> bottoms;
        int covered = 0;
        for (int actions = 0; actions < kActionLimit && table.session.GetPhase() == SessionPhase::Playing; ++actions) {
            if (!table.session.ActorIsPerson()) {
                ASSERT_TRUE(table.session.StepComputer().has_value());
                continue;
            }
            // Under the cover the table waits for the person who is to act: that seat is at the bottom, and it is the same after Reveal
            if (table.session.HandoffPending()) {
                ++covered;
                EXPECT_EQ(table.session.Viewer(), kNoSeat);
                ASSERT_NO_FATAL_FAILURE(ExpectPlates(Words(), table, table.session.Actor()));
                table.session.Reveal();
            }
            EXPECT_EQ(BottomSeat(table.session), table.session.Actor());
            ASSERT_NO_FATAL_FAILURE(ExpectPlates(Words(), table, ExpectedBottom(table)));
            bottoms.insert(BottomSeat(table.session));
            ASSERT_TRUE(table.session.PerformPerson(Choose(table.match.Hand().LegalActions(table.session.Actor()), Mode::NeverWin, rng)).has_value());
        }
        EXPECT_GT(covered, 0);
        EXPECT_EQ(bottoms.size(), 2u) << "the bottom seat never moved to the other person";
        // Both people keep the bottom after their action, and nobody is "you" among two
        for (const SeatPlateText& plate : MakeSeatPlates(Words(), table.session, table.match)) {
            EXPECT_EQ(Std(plate.name).find("You"), std::string::npos);
        }
    }

    TEST(SeatPlatesTest, TheTurnIsOnTheSeatWhoseTurnItIsAndOnTheDiscarderInAClaimWindow) {
        int claimWindows = 0;
        for (uint32_t seed = 1; seed <= 6; ++seed) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers, seed));
            for (int actions = 0; actions < kActionLimit && table.session.GetPhase() == SessionPhase::Playing; ++actions) {
                const HandModel& hand = table.match.Hand();
                ASSERT_NO_FATAL_FAILURE(ExpectPlates(Words(), table, 0));
                const auto plates = MakeSeatPlates(Words(), table.session, table.match);
                for (const SeatPlateText& plate : plates) {
                    EXPECT_EQ(plate.turn, plate.seat == hand.CurrentSeat());
                }
                if (hand.GetPhase() == Phase::Claim) {
                    ++claimWindows;
                    // The turn is the discarder's, never the seat that is asked first to claim
                    const Seat asked = table.session.Actor();
                    ASSERT_NE(asked, hand.CurrentSeat());
                    EXPECT_FALSE(plates[static_cast<size_t>(asked)].turn);
                    EXPECT_TRUE(plates[static_cast<size_t>(hand.CurrentSeat())].turn);
                }
                ASSERT_TRUE(table.session.StepComputer().has_value());
            }
        }
        EXPECT_GT(claimWindows, 0) << "no claim window seen";
    }

    TEST(SeatPlatesTest, NoPlateHasTheTurnWhenTheHandOrTheMatchIsOver) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers, kSeed, kShortMatchHands));
        ASSERT_TRUE(PlayComputersUntil(table, [](const Table& t) { return t.session.GetPhase() == SessionPhase::HandOver; }));
        ASSERT_NO_FATAL_FAILURE(ExpectPlates(Words(), table, 0));
        for (const SeatPlateText& plate : MakeSeatPlates(Words(), table.session, table.match)) {
            EXPECT_FALSE(plate.turn);
        }
        EXPECT_EQ(TurnSeat(table.session, table.match.Hand()), kNoSeat);
        EXPECT_FALSE(PlayComputersUntil(table, [](const Table& t) { return false; }));
        ASSERT_EQ(table.session.GetPhase(), SessionPhase::MatchOver);
        ASSERT_NO_FATAL_FAILURE(ExpectPlates(Words(), table, 0));
        for (const SeatPlateText& plate : MakeSeatPlates(Words(), table.session, table.match)) {
            EXPECT_FALSE(plate.turn);
        }
    }

    TEST(SeatPlatesTest, TheKindOfAPlateIsTheKindOfItsSeatAtEverySetup) {
        const SeatSetup setups[] = {kAllComputers, kTwoPeople, PersonAt(0), PersonAt(3), {SeatKind::Computer, SeatKind::Person, SeatKind::Person, SeatKind::Person}};
        for (const SeatSetup& setup : setups) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(setup));
            for (const SeatPlateText& plate : MakeSeatPlates(Words(), table.session, table.match)) {
                EXPECT_EQ(plate.kind, setup[static_cast<size_t>(plate.seat)]) << "seat " << plate.seat;
            }
        }
    }

    TEST(SeatPlatesTest, TheLevelOfAPlateIsTheLevelOfItsSeat) {
        // Every seat a level of its own, the numbers apart, and a table where the seats at the bottom differ
        const SeatLevels levelSets[] = {{{{1}, {4}, {7}, {10}}}, {{{10}, {9}, {2}, {5}}}, UniformLevels(ComputerLevel{3})};
        for (const SeatLevels& levels : levelSets) {
            for (Seat person = 0; person < kSeatCount; ++person) {
                Table table;
                ASSERT_NO_FATAL_FAILURE(table.Start(PersonAt(person), levels));
                for (const SeatPlateText& plate : MakeSeatPlates(Words(), table.session, table.match)) {
                    EXPECT_EQ(LevelNumber(plate.level), LevelNumber(levels[static_cast<size_t>(plate.seat)])) << "person " << person << " seat " << plate.seat;
                }
            }
        }
    }

    TEST(SeatPlatesTest, CallingIsOnTheSeatsThatDeclaredAndStaysAtTheEndOfTheHand) {
        int declaredPlates = 0;
        int seatsWithout = 0;
        for (uint32_t seed = 1; seed <= 8; ++seed) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers, seed));
            for (int actions = 0; actions < kActionLimit && table.session.GetPhase() != SessionPhase::MatchOver; ++actions) {
                for (const SeatPlateText& plate : MakeSeatPlates(Words(), table.session, table.match)) {
                    const bool declared = table.match.Hand().GetHand(plate.seat).callingDeclared;
                    EXPECT_EQ(plate.calling, declared) << "seed " << seed << " seat " << plate.seat;
                    declaredPlates += plate.calling ? 1 : 0;
                    seatsWithout += plate.calling ? 0 : 1;
                }
                if (table.session.GetPhase() == SessionPhase::HandOver) {
                    ASSERT_TRUE(table.session.NextHand().has_value());
                } else {
                    ASSERT_TRUE(table.session.StepComputer().has_value());
                }
            }
        }
        EXPECT_GT(declaredPlates, 0) << "no seat ever declared";
        EXPECT_GT(seatsWithout, 0);
    }

    TEST(SeatPlatesTest, AtTheHandsEndTheCallingStaysOnTheSeatThatDeclared) {
        bool seen = false;
        for (uint32_t seed = 1; seed <= 12 && !seen; ++seed) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers, seed));
            ASSERT_TRUE(PlayComputersUntil(table, [](const Table& t) { return t.session.GetPhase() == SessionPhase::HandOver; }));
            for (const SeatPlateText& plate : MakeSeatPlates(Words(), table.session, table.match)) {
                EXPECT_EQ(plate.calling, table.match.Hand().GetHand(plate.seat).callingDeclared);
                seen = seen || plate.calling;
            }
        }
        EXPECT_TRUE(seen) << "no hand ended with a declared seat";
    }

    TEST(SeatPlatesTest, TheEmblemBoxAndTheNameBoxLieInsideEveryPlateOfEveryShapeAndApart) {
        const TableLayoutSpec& spec = ShippedTableLayoutSpec();
        struct ViewSize {
            float width;
            float height;
        };
        const ViewSize views[] = {
            {kViewWidth, kViewHeight}, {kWideViewWidth, kViewHeight}, {kNarrowViewWidth, kNarrowViewHeight}, {kPortraitViewWidth, kPortraitViewHeight}, {1000.0f, 400.0f}, {640.0f, 480.0f},
            {390.0f, 844.0f}};
        constexpr float kTolerance = 0.001f;
        const auto OnTheGrid = [](float units) { return std::fabs(units - std::round(units / kGridUnit) * kGridUnit) < kTolerance; };
        for (const ViewSize& view : views) {
            const float unit = LayoutScale(view.width, view.height);
            for (Seat viewer = 0; viewer < kSeatCount; ++viewer) {
                TableInput input;
                input.viewer = viewer;
                const TableLayout layout = MakeTableLayout(input, spec, view.width, view.height);
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    const Rect& plate = layout.zones.seatPlates[static_cast<size_t>(seat)];
                    ASSERT_GT(plate.w, 0.0f) << view.width << "x" << view.height;
                    const Rect emblem = EmblemBox(plate, unit);
                    const Rect name = NameBox(plate, unit);
                    for (const Rect& box : {emblem, name}) {
                        EXPECT_GE(box.x, plate.x - kTolerance);
                        EXPECT_GE(box.y, plate.y - kTolerance);
                        EXPECT_LE(box.x + box.w, plate.x + plate.w + kTolerance);
                        EXPECT_LE(box.y + box.h, plate.y + plate.h + kTolerance);
                    }
                    // The emblem lies above the name, below the dealer bar, and the two do not overlap
                    EXPECT_LE(emblem.y + emblem.h, name.y + kTolerance) << view.width << "x" << view.height << " seat " << seat;
                    EXPECT_GE(emblem.y, plate.y + kDealerMarkHeight * unit - kTolerance);
                    // Both are on the grid of the design, in plate units
                    EXPECT_TRUE(OnTheGrid((emblem.y - plate.y) / unit));
                    EXPECT_TRUE(OnTheGrid((name.y - plate.y) / unit));
                    EXPECT_TRUE(OnTheGrid(emblem.h / unit));
                    EXPECT_TRUE(OnTheGrid(name.h / unit));
                }
            }
        }
    }

    TEST(SeatPlatesTest, TheWordsFollowTheLanguageOfTheTable) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(PersonAt(2)));
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        std::set<std::string> names;
        ForEachLanguage(strings, [&](PoolString language) {
            ASSERT_NO_FATAL_FAILURE(ExpectPlates(strings, table, 2));
            for (const SeatPlateText& plate : MakeSeatPlates(strings, table.session, table.match)) {
                EXPECT_FALSE(plate.name.empty()) << language.ToStringView().data();
                names.insert(Std(plate.name));
            }
        });
        // Four seats in each of the two languages, and no word is shared between them
        EXPECT_EQ(names.size(), 2u * kSeatCount);
    }

    TEST(SeatPlatesTest, ThePlateColoursAreTheHudPanelTheGoldOfTheTurnAndADealerMarkThatIsNotGold) {
        EXPECT_TRUE(SameColor(kSeatPlate, PaletteColor("hudPanel")));
        // The turn is gold: the frame is the gold of the other marks (the same red, green and blue as the turn mark) with an alpha of its own
        EXPECT_EQ(kSeatTurnFrame.r, kTurnMark.r);
        EXPECT_EQ(kSeatTurnFrame.g, kTurnMark.g);
        EXPECT_EQ(kSeatTurnFrame.b, kTurnMark.b);
        EXPECT_GT(kSeatTurnFrame.a, kTurnMark.a);
        EXPECT_GT(kSeatTurnFrameWidth, 0.0f);
        // The dealer mark is a colour of its own, gold in none of the channels, and opaque
        EXPECT_FALSE(SameColor(Color{kDealerMark.r, kDealerMark.g, kDealerMark.b, kSeatTurnFrame.a}, kSeatTurnFrame));
        EXPECT_LT(kDealerMark.g, kTurnMark.g / 2);
        EXPECT_LT(kDealerMark.b, kTurnMark.b);
        EXPECT_EQ(kDealerMark.a, 255);
        EXPECT_GT(kDealerMarkHeight, 0.0f);
        EXPECT_LT(kDealerMarkHeight, kSeatPlateH);
    }

}  // namespace BECore::Mahjong::Tests
