#include "HandTestSupport.h"

#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/Game/TableSession.h>
#include <gtest/gtest.h>

/**
 * `TableSession` (`Game/TableSession.h`): the start, `Actor` in a turn and in a claim window, `StepComputer` and `RunComputers` (a
 * computer plays, a person is waited for), `PerformPerson`, `NextHand` and the end of the match, the version, the hot-seat viewer, and
 * whole matches played by people (a chooser of `HandTestSupport.h`) against computers. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeed = 31;
        /// Far more actions than a hand of people and computers takes; a loop fails fast.
        constexpr int kActionLimitPerMatch = 20000;
        constexpr int kSeedsToSearch = 80;
        /// Actions of a loop that looks for a claim window of two seats, and of one that stops where a person must act.
        constexpr int kActionLimitSearch = 400;
        constexpr int kActionLimitPerHandStop = 60;
        /// Counted hands of a short match played by computers; a loop fails fast.
        constexpr int kHandLimitPerMatch = 200;
        constexpr int kTableTotal = kSeatCount * kExpectedStartingScore;

        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        constexpr SeatSetup kAllPeople{SeatKind::Person, SeatKind::Person, SeatKind::Person, SeatKind::Person};
        constexpr SeatSetup kTwoPeople{SeatKind::Person, SeatKind::Computer, SeatKind::Person, SeatKind::Computer};

        MahjongSettings ShortMatch() {
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            return settings;
        }

        /// A setup with one person at `seat`.
        SeatSetup PersonAt(Seat seat) {
            SeatSetup setup = kAllComputers;
            setup[static_cast<size_t>(seat)] = SeatKind::Person;
            return setup;
        }

        /// A match and its session, started.
        struct Table {
            Match match;
            TableSession session{match};

            void Start(const SeatSetup& seats, uint32_t seed = kSeed, const MahjongSettings& settings = MahjongSettings{}) {
                ASSERT_TRUE(session.StartMatch(settings, seed, seats).has_value());
            }

            void StartAt(ComputerLevel level, uint32_t seed = kSeed, const MahjongSettings& settings = ShortMatch()) {
                ASSERT_TRUE(session.StartMatch(settings, seed, kAllComputers, level).has_value());
            }
        };

        /// What a refused call must leave as it was: the hand, the scores, the count, the version, the viewer and the phase.
        std::string State(const Table& table) {
            std::string text = Snapshot(table.match.Hand()) + " | scores";
            for (const int score : table.match.Scores()) {
                text += " " + std::to_string(score);
            }
            text += " | hands " + std::to_string(table.match.HandsPlayed()) + " version " + std::to_string(table.session.Version());
            text += " viewer " + std::to_string(table.session.Viewer()) + " phase " + Name(table.session.GetPhase());
            for (const ComputerLevel level : table.session.Levels()) {
                text += " level " + Name(level);
            }
            return text;
        }

        /// A person takes the action of `mode` from the legal ones of the actor.
        HandStatus PersonActs(TableSession& session, const HandModel& hand, Mode mode, MahjongRng& rng) {
            const eastl::vector<HandAction> legal = hand.LegalActions(session.Actor());
            EXPECT_FALSE(legal.empty());
            return session.PerformPerson(Choose(legal, mode, rng));
        }

        /// A tile the seat does not hold.
        Tile TileNotHeldBy(const PlayerHand& hand) {
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (eastl::find(hand.concealed.begin(), hand.concealed.end(), Tile{id}) == hand.concealed.end()) {
                    return Tile{id};
                }
            }
            return Tile{};
        }

        /// Plays a whole match: the computers step, the people act with `mode`, a finished hand is settled. `beforePerson(actor)` runs
        /// before each action of a person. Checks that every call moves the version and that the points of the table add up.
        template <class Before>
        void PlayWholeMatch(Table& table, Mode mode, MahjongRng& rng, Before beforePerson) {
            TableSession& session = table.session;
            uint64_t version = session.Version();
            for (int actions = 0; session.GetPhase() != SessionPhase::MatchOver; ++actions) {
                ASSERT_LT(actions, kActionLimitPerMatch) << "the match does not end";
                if (session.GetPhase() == SessionPhase::HandOver) {
                    ASSERT_TRUE(session.NextHand().has_value());
                    EXPECT_EQ(Sum(table.match.Scores()), kTableTotal);
                } else if (session.ActorIsPerson()) {
                    ASSERT_NO_FATAL_FAILURE(beforePerson(session.Actor()));
                    ASSERT_TRUE(PersonActs(session, table.match.Hand(), mode, rng).has_value());
                } else {
                    ASSERT_TRUE(session.StepComputer().has_value());
                }
                ASSERT_GT(session.Version(), version);
                version = session.Version();
            }
        }

    }  // namespace

    TEST(TableSessionTest, ANewSessionIsOverAndStepsNothing) {
        Table table;
        EXPECT_EQ(table.session.GetPhase(), SessionPhase::MatchOver);
        EXPECT_EQ(table.session.Actor(), kNoSeat);
        EXPECT_FALSE(table.session.ActorIsPerson());
        EXPECT_EQ(table.session.Version(), 0u);
        EXPECT_EQ(table.session.Viewer(), kNoSeat);
        EXPECT_FALSE(table.session.HandoffPending());
        const StepResult step = table.session.StepComputer();
        ASSERT_FALSE(step.has_value());
        EXPECT_EQ(step.error(), HandError::WrongPhase);
        EXPECT_EQ(Error(table.session.PerformPerson(HandAction::MakePass())), HandError::WrongPhase);
        EXPECT_EQ(table.session.NextHand().error(), MatchError::NotInHand);
        EXPECT_EQ(table.session.RunComputers(), 0);
        EXPECT_FALSE(table.session.LastSettlement().has_value());
        EXPECT_EQ(table.session.Version(), 0u);
    }

    TEST(TableSessionTest, TheStartBeginsAMatchInPlayAndMovesTheVersion) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kDefaultSeatSetup));
        EXPECT_EQ(table.session.GetPhase(), SessionPhase::Playing);
        EXPECT_GT(table.session.Version(), 0u);
        EXPECT_EQ(table.session.Seats(), kDefaultSeatSetup);
        EXPECT_EQ(table.session.KindOf(0), SeatKind::Person);
        EXPECT_EQ(table.session.KindOf(3), SeatKind::Computer);
        EXPECT_EQ(table.session.KindOf(kNoSeat), SeatKind::Computer);
        EXPECT_EQ(table.session.KindOf(kSeatCount), SeatKind::Computer);
        EXPECT_EQ(table.session.PersonCount(), 1);
        EXPECT_EQ(table.session.Actor(), table.match.East());
        EXPECT_EQ(table.match.GetPhase(), MatchPhase::InHand);
    }

    TEST(TableSessionTest, ABadHandsPerMatchIsRefusedAndNothingChanges) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kDefaultSeatSetup));
        const std::string before = State(table);
        MahjongSettings bad;
        bad.handsPerMatch = kUnplayableHandsPerMatch;
        const MatchStatus refused = table.session.StartMatch(bad, kSeed + 1, kAllComputers);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), MatchError::BadSettings);
        EXPECT_EQ(State(table), before);
        EXPECT_EQ(table.session.Seats(), kDefaultSeatSetup);
    }

    TEST(TableSessionTest, EveryAcceptedStartCountsAndARefusedOneDoesNot) {
        Table table;
        EXPECT_EQ(table.session.StartCount(), 0u);
        ASSERT_NO_FATAL_FAILURE(table.Start(kDefaultSeatSetup));
        EXPECT_EQ(table.session.StartCount(), 1u);
        table.session.RunComputers();
        EXPECT_EQ(table.session.StartCount(), 1u) << "playing is no start";
        ASSERT_NO_FATAL_FAILURE(table.Start(kDefaultSeatSetup));
        EXPECT_EQ(table.session.StartCount(), 2u);
        MahjongSettings bad;
        bad.handsPerMatch = kUnplayableHandsPerMatch;
        ASSERT_FALSE(table.session.StartMatch(bad, kSeed + 1, kAllComputers).has_value());
        EXPECT_EQ(table.session.StartCount(), 2u);
    }

    TEST(TableSessionTest, TheLevelIsNormalByDefaultAndAStartKeepsTheOneItGets) {
        Table table;
        EXPECT_EQ(table.session.Levels(), UniformLevels(ComputerLevel::Normal)) << "before the first start";
        ASSERT_NO_FATAL_FAILURE(table.Start(kDefaultSeatSetup));
        EXPECT_EQ(table.session.Levels(), UniformLevels(kDefaultLevel)) << "a start with no level";
        for (const ComputerLevel level : kAllLevels) {
            ASSERT_NO_FATAL_FAILURE(table.StartAt(level));
            EXPECT_EQ(table.session.Levels(), UniformLevels(level)) << Name(level);
        }
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        EXPECT_EQ(table.session.Levels(), UniformLevels(kDefaultLevel)) << "the level is not kept from the match before";
    }

    TEST(TableSessionTest, ARefusedStartKeepsTheOldLevelAndTheMatch) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.StartAt(ComputerLevel::Easy));
        const std::string before = State(table);
        MahjongSettings bad;
        bad.handsPerMatch = kUnplayableHandsPerMatch;
        const MatchStatus refused = table.session.StartMatch(bad, kSeed + 1, kAllComputers, ComputerLevel::Normal);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), MatchError::BadSettings);
        EXPECT_EQ(table.session.Levels(), UniformLevels(ComputerLevel::Easy));
        EXPECT_EQ(State(table), before);
    }

    TEST(TableSessionTest, EachSeatKeepsTheLevelItWasStartedWithPersonSeatsToo) {
        Table table;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(table.session.LevelOf(seat), kDefaultLevel) << "before the first start";
        }
        const SeatLevels levels{ComputerLevel{1}, ComputerLevel{3}, ComputerLevel{6}, ComputerLevel{10}};
        ASSERT_TRUE(table.session.StartMatch(ShortMatch(), kSeed, kDefaultSeatSetup, levels).has_value());
        EXPECT_EQ(table.session.Levels(), levels) << "seat 0 is a person: its level is kept";
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(table.session.LevelOf(seat), levels[static_cast<size_t>(seat)]) << "seat " << seat;
        }
        EXPECT_EQ(table.session.LevelOf(kNoSeat), kDefaultLevel);
        EXPECT_EQ(table.session.LevelOf(kSeatCount), kDefaultLevel);
        ASSERT_TRUE(table.session.StartMatch(ShortMatch(), kSeed, PersonAt(2), levels).has_value());
        ASSERT_TRUE(table.session.StartMatch(ShortMatch(), kSeed, kAllPeople, levels).has_value());
        EXPECT_EQ(table.session.Levels(), levels);

        ASSERT_TRUE(table.session.StartMatch(ShortMatch(), kSeed, kAllComputers, ComputerLevel{4}).has_value());
        EXPECT_EQ(table.session.Levels(), UniformLevels(ComputerLevel{4})) << "the one-level start is UniformLevels";
        ASSERT_TRUE(table.session.StartMatch(ShortMatch(), kSeed, kAllComputers).has_value());
        EXPECT_EQ(table.session.Levels(), UniformLevels(kDefaultLevel)) << "a start with no level";
    }

    TEST(TableSessionTest, ARefusedStartOfSeatLevelsChangesNothing) {
        Table table;
        ASSERT_TRUE(table.session.StartMatch(ShortMatch(), kSeed, kAllComputers, SeatLevels{ComputerLevel{2}, ComputerLevel{4}, ComputerLevel{6}, ComputerLevel{8}}).has_value());
        const std::string before = State(table);
        MahjongSettings bad;
        bad.handsPerMatch = kUnplayableHandsPerMatch;
        const MatchStatus refused = table.session.StartMatch(bad, kSeed + 1, kDefaultSeatSetup, UniformLevels(ComputerLevel::Easy));
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), MatchError::BadSettings);
        EXPECT_EQ(table.session.Levels(), (SeatLevels{ComputerLevel{2}, ComputerLevel{4}, ComputerLevel{6}, ComputerLevel{8}}));
        EXPECT_EQ(State(table), before);
    }

    TEST(TableSessionTest, ASeatPlaysAtItsOwnLevelWhateverTheOthersPlay) {
        // A table whose seat 0 is Easy and the rest Normal plays like the session of that table: step by step, the session's actions
        // are those of a `ComputerTable` made with the same seed and levels.
        const SeatLevels levels{ComputerLevel::Easy, ComputerLevel::Normal, ComputerLevel::Normal, ComputerLevel::Normal};
        Table table;
        ASSERT_TRUE(table.session.StartMatch(ShortMatch(), kSeed, kAllComputers, levels).has_value());
        Match reference;
        ASSERT_TRUE(reference.Start(ShortMatch(), kSeed).has_value());
        ComputerTable players(kSeed, levels);
        for (int actions = 0; table.session.GetPhase() != SessionPhase::MatchOver; ++actions) {
            ASSERT_LT(actions, kActionLimitPerMatch);
            if (table.session.GetPhase() == SessionPhase::HandOver) {
                ASSERT_TRUE(table.session.NextHand().has_value());
                ASSERT_TRUE(reference.EndHand().has_value());
            } else {
                const StepResult own = table.session.StepComputer();
                const StepResult expected = players.Step(reference.Hand());
                ASSERT_TRUE(own.has_value());
                ASSERT_TRUE(expected.has_value());
                ASSERT_EQ(*own, *expected) << "action " << actions;
            }
        }
    }

    TEST(TableSessionTest, ResumeMatchSetsTheLevelsAndARefusedOneChangesNothing) {
        MatchLog log;
        {
            Table recorded;
            recorded.session.SetLogSink(&log);
            ASSERT_TRUE(recorded.session.StartMatch(ShortMatch(), kSeed, kAllComputers, ComputerLevel::Normal).has_value());
            constexpr int kRecordedSteps = 20;  // far fewer than a hand takes
            for (int actions = 0; actions < kRecordedSteps; ++actions) {
                ASSERT_TRUE(recorded.session.StepComputer().has_value());
            }
        }
        const SeatLevels levels{ComputerLevel{10}, ComputerLevel{2}, ComputerLevel{5}, ComputerLevel{1}};
        Table table;
        ASSERT_TRUE(table.session.StartMatch(ShortMatch(), kSeed + 5, kAllComputers, ComputerLevel::Easy).has_value());
        const std::string before = State(table);
        eastl::vector<MatchLogEvent> broken = log.Events();
        broken[0].settings.handsPerMatch = kUnplayableHandsPerMatch;
        const ReplayStatus refused = table.session.ResumeMatch(broken, kAllComputers, levels);
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(table.session.Levels(), UniformLevels(ComputerLevel::Easy));
        EXPECT_EQ(State(table), before);

        ASSERT_TRUE(table.session.ResumeMatch(log.Events(), kDefaultSeatSetup, levels).has_value());
        EXPECT_EQ(table.session.Levels(), levels) << "the person seat's level too";
        ASSERT_TRUE(table.session.ResumeMatch(log.Events(), kAllComputers, ComputerLevel{7}).has_value());
        EXPECT_EQ(table.session.Levels(), UniformLevels(ComputerLevel{7}));
    }

    TEST(TableSessionTest, TheExplicitNormalLevelPlaysLikeTheDefaultActionByAction) {
        Table byDefault;
        Table explicitNormal;
        ASSERT_NO_FATAL_FAILURE(byDefault.Start(kAllComputers, kSeed, ShortMatch()));
        ASSERT_NO_FATAL_FAILURE(explicitNormal.StartAt(ComputerLevel::Normal));
        for (int actions = 0; byDefault.session.GetPhase() != SessionPhase::MatchOver; ++actions) {
            ASSERT_LT(actions, kActionLimitPerMatch);
            ASSERT_EQ(State(byDefault), State(explicitNormal)) << "action " << actions;
            if (byDefault.session.GetPhase() == SessionPhase::HandOver) {
                ASSERT_TRUE(byDefault.session.NextHand().has_value());
                ASSERT_TRUE(explicitNormal.session.NextHand().has_value());
            } else {
                const StepResult a = byDefault.session.StepComputer();
                const StepResult b = explicitNormal.session.StepComputer();
                ASSERT_TRUE(a.has_value());
                ASSERT_TRUE(b.has_value());
                ASSERT_EQ(*a, *b) << "action " << actions;
            }
        }
        EXPECT_EQ(State(byDefault), State(explicitNormal));
    }

    TEST(TableSessionTest, EveryLevelPlaysAWholeMatchLegallyAndTheLogSaysTheLevelAndReplays) {
        // How the levels differ is the business of the computer player's tests; here the match is legal at each, the points add up, the
        // `Start` of the log carries the level and the log replays to the same match.
        for (const ComputerLevel level : kAllLevels) {
            Table table;
            MatchLog log;
            table.session.SetLogSink(&log);
            ASSERT_NO_FATAL_FAILURE(table.StartAt(level));
            EXPECT_EQ(table.session.Levels(), UniformLevels(level)) << Name(level);
            for (int actions = 0; table.session.GetPhase() != SessionPhase::MatchOver; ++actions) {
                ASSERT_LT(actions, kActionLimitPerMatch) << Name(level);
                if (table.session.GetPhase() == SessionPhase::HandOver) {
                    ASSERT_TRUE(table.session.NextHand().has_value()) << Name(level);
                } else {
                    ASSERT_TRUE(table.session.StepComputer().has_value()) << Name(level) << " action " << actions;
                }
            }
            EXPECT_EQ(table.match.HandsPlayed(), table.match.HandsTotal()) << Name(level);
            EXPECT_EQ(Sum(table.match.Scores()), kTableTotal) << Name(level);

            ASSERT_FALSE(log.Events().empty()) << Name(level);
            EXPECT_EQ(log.Events().front().kind, MatchLogKind::Start) << Name(level);
            EXPECT_EQ(log.Events().front().levels, UniformLevels(level)) << Name(level);
            EXPECT_EQ(log.Events().back().kind, MatchLogKind::End) << Name(level);
            Match replayed;
            const ReplayStatus status = ReplayMatchLog(log.Events(), replayed);
            ASSERT_TRUE(status.has_value()) << Name(level) << ": stopped at event " << status.error().index << ": " << Name(status.error().problem);
            EXPECT_EQ(replayed.Scores(), table.match.Scores()) << Name(level);
            EXPECT_EQ(replayed.History().size(), table.match.History().size()) << Name(level);
        }
    }

    TEST(TableSessionTest, TheActorIsTheDiscarderInATurnAndThePendingSeatsInAClaimWindow) {
        bool sawWindowOfTwo = false;
        for (uint32_t seed = 1; seed <= kSeedsToSearch && !sawWindowOfTwo; ++seed) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(kAllPeople, seed));
            MahjongRng rng(seed);
            for (int actions = 0; actions < kActionLimitSearch && table.session.GetPhase() == SessionPhase::Playing; ++actions) {
                const HandModel& hand = table.match.Hand();
                if (hand.GetPhase() == Phase::Discard) {
                    ASSERT_EQ(table.session.Actor(), hand.CurrentSeat());
                } else if (hand.PendingClaimSeats().size() >= 2) {
                    const eastl::vector<Seat> pending = hand.PendingClaimSeats();
                    ASSERT_EQ(table.session.Actor(), pending.front());
                    const Seat first = pending.front();
                    ASSERT_TRUE(table.session.PerformPerson(HandAction::MakePass()).has_value());
                    // The seat that passed is done; the next pending seat is asked.
                    if (hand.GetPhase() != Phase::Claim) {
                        continue;
                    }
                    EXPECT_NE(table.session.Actor(), first);
                    EXPECT_EQ(table.session.Actor(), hand.PendingClaimSeats().front());
                    EXPECT_TRUE(table.session.ActorIsPerson());
                    sawWindowOfTwo = true;
                    break;
                }
                ASSERT_TRUE(PersonActs(table.session, hand, Mode::NeverWin, rng).has_value());
            }
        }
        EXPECT_TRUE(sawWindowOfTwo) << "no claim window with two pending seats found";
    }

    TEST(TableSessionTest, AComputerIsSteppedAndAPersonIsNot) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        const Seat east = table.match.East();
        ASSERT_NO_FATAL_FAILURE(table.Start(PersonAt(east)));
        // The actor is a person: a step is refused and changes nothing.
        const std::string before = State(table);
        const StepResult refused = table.session.StepComputer();
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), HandError::NotYourTurn);
        EXPECT_EQ(State(table), before);

        // The actor is a computer: the step is one of its legal actions, applied, and moves the version once.
        ASSERT_NO_FATAL_FAILURE(table.Start(PersonAt(NextSeat(east))));
        const Seat actor = table.session.Actor();
        const eastl::vector<HandAction> legal = table.match.Hand().LegalActions(actor);
        const uint64_t version = table.session.Version();
        const StepResult step = table.session.StepComputer();
        ASSERT_TRUE(step.has_value());
        EXPECT_TRUE(Has(legal, *step));
        EXPECT_EQ(table.session.Version(), version + 1);
        EXPECT_NE(table.session.Actor(), kNoSeat);
    }

    TEST(TableSessionTest, RunComputersStopsWhereAPersonMustAct) {
        for (const SeatSetup& seats : {kDefaultSeatSetup, kTwoPeople, PersonAt(2)}) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(seats));
            MahjongRng rng(kSeed);
            for (int actions = 0; actions < kActionLimitPerHandStop && table.session.GetPhase() == SessionPhase::Playing; ++actions) {
                const uint64_t version = table.session.Version();
                const int steps = table.session.RunComputers();
                EXPECT_EQ(table.session.Version(), version + static_cast<uint64_t>(steps));
                if (table.session.GetPhase() != SessionPhase::Playing) {
                    break;
                }
                EXPECT_TRUE(table.session.ActorIsPerson());
                EXPECT_EQ(table.session.KindOf(table.session.Actor()), SeatKind::Person);
                EXPECT_EQ(table.session.RunComputers(), 0) << "it must not move again where a person has to act";
                ASSERT_TRUE(PersonActs(table.session, table.match.Hand(), Mode::NeverWin, rng).has_value());
            }
        }
    }

    TEST(TableSessionTest, WithNoPersonRunComputersEndsTheHand) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        const int steps = table.session.RunComputers();
        EXPECT_GT(steps, 0);
        EXPECT_LT(steps, kMaxComputerSteps);
        EXPECT_EQ(table.session.GetPhase(), SessionPhase::HandOver);
        EXPECT_EQ(table.match.Hand().GetPhase(), Phase::Finished);
        EXPECT_EQ(table.session.Actor(), kNoSeat);
        EXPECT_EQ(table.session.RunComputers(), 0);
        EXPECT_EQ(table.session.Viewer(), kNoSeat);
        EXPECT_FALSE(table.session.HandoffPending());
    }

    TEST(TableSessionTest, APersonActsOnlyForItsOwnSeatAndARefusedActionChangesNothing) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        const Seat east = table.match.East();
        // The actor is a computer: refused.
        ASSERT_NO_FATAL_FAILURE(table.Start(PersonAt(NextSeat(east))));
        std::string before = State(table);
        const eastl::vector<HandAction> legal = table.match.Hand().LegalActions(east);
        ASSERT_FALSE(legal.empty());
        EXPECT_EQ(Error(table.session.PerformPerson(legal.front())), HandError::NotYourTurn);
        EXPECT_EQ(State(table), before);

        // The actor is a person: an action that is not listed is refused with the error of the model, and nothing moves.
        ASSERT_NO_FATAL_FAILURE(table.Start(PersonAt(east)));
        before = State(table);
        const Tile missing = TileNotHeldBy(table.match.Hand().GetHand(east));
        EXPECT_EQ(Error(table.session.PerformPerson(HandAction::MakeDiscard(missing))), HandError::TileNotInHand);
        EXPECT_EQ(Error(table.session.PerformPerson(HandAction::MakePass())), HandError::WrongPhase);
        EXPECT_EQ(State(table), before);

        // A listed one is accepted and moves the version once.
        const uint64_t version = table.session.Version();
        const eastl::vector<HandAction> listed = table.match.Hand().LegalActions(east);
        ASSERT_TRUE(table.session.PerformPerson(listed.front()).has_value());
        EXPECT_EQ(table.session.Version(), version + 1);
    }

    TEST(TableSessionTest, NextHandNeedsAFinishedHandAndSettlesIt) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        const std::string before = State(table);
        const auto early = table.session.NextHand();
        ASSERT_FALSE(early.has_value());
        EXPECT_EQ(early.error(), MatchError::HandNotFinished);
        EXPECT_EQ(State(table), before);

        table.session.RunComputers();
        ASSERT_EQ(table.session.GetPhase(), SessionPhase::HandOver);
        const HandSettlement expected = SettleHand(table.match.Hand());
        const uint64_t version = table.session.Version();
        const auto settled = table.session.NextHand();
        ASSERT_TRUE(settled.has_value());
        EXPECT_EQ(settled->kind, expected.kind);
        EXPECT_EQ(settled->deltas, expected.deltas);
        EXPECT_EQ(table.session.Version(), version + 1);
        EXPECT_EQ(table.session.GetPhase(), SessionPhase::Playing);
        EXPECT_EQ(Sum(table.match.Scores()), kSeatCount * kExpectedStartingScore);
        ASSERT_TRUE(table.session.LastSettlement().has_value());
        EXPECT_EQ(table.session.LastSettlement()->deltas, expected.deltas);
        EXPECT_EQ(table.match.History().size(), 1u);
    }

    TEST(TableSessionTest, TheMatchEndsAfterHandsPerMatchCountedHands) {
        const MahjongSettings settings = ShortMatch();
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers, kSeed, settings));
        uint64_t version = table.session.Version();
        for (int hands = 0; table.session.GetPhase() != SessionPhase::MatchOver; ++hands) {
            ASSERT_LT(hands, kHandLimitPerMatch) << "the match does not end";
            table.session.RunComputers();
            ASSERT_EQ(table.session.GetPhase(), SessionPhase::HandOver);
            ASSERT_TRUE(table.session.NextHand().has_value());
            EXPECT_GT(table.session.Version(), version);
            version = table.session.Version();
            EXPECT_EQ(Sum(table.match.Scores()), kTableTotal);
        }
        EXPECT_EQ(table.match.GetPhase(), MatchPhase::Finished);
        EXPECT_EQ(table.match.HandsPlayed(), settings.handsPerMatch);
        EXPECT_EQ(table.session.Actor(), kNoSeat);
        const std::string before = State(table);
        const auto refused = table.session.NextHand();
        ASSERT_FALSE(refused.has_value());
        EXPECT_EQ(refused.error(), MatchError::NotInHand);
        EXPECT_EQ(Error(table.session.PerformPerson(HandAction::MakePass())), HandError::WrongPhase);
        EXPECT_EQ(State(table), before);
    }

    TEST(TableSessionTest, OnePersonIsAlwaysTheViewerAndNoPersonHasNone) {
        struct Case {
            SeatSetup seats;
            Seat viewer;
        };
        for (const Case& test : {Case{PersonAt(0), 0}, Case{PersonAt(3), 3}, Case{kAllComputers, kNoSeat}}) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(test.seats, kSeed, ShortMatch()));
            MahjongRng rng(kSeed);
            ASSERT_NO_FATAL_FAILURE(PlayWholeMatch(table, Mode::SeekWin, rng, [&](Seat) {
                EXPECT_EQ(table.session.Viewer(), test.viewer);
                EXPECT_FALSE(table.session.HandoffPending());
            }));
            EXPECT_EQ(table.session.Viewer(), test.viewer);
            EXPECT_FALSE(table.session.HandoffPending());
        }
    }

    TEST(TableSessionTest, TwoPeopleShareTheScreenBehindACover) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kTwoPeople, kSeed, ShortMatch()));
        MahjongRng rng(kSeed);
        Seat lastPerson = kNoSeat;
        int covers = 0;
        int sameTwice = 0;
        while (table.session.GetPhase() != SessionPhase::MatchOver) {
            if (table.session.GetPhase() == SessionPhase::HandOver) {
                ASSERT_TRUE(table.session.NextHand().has_value());
                continue;
            }
            if (!table.session.ActorIsPerson()) {
                ASSERT_TRUE(table.session.StepComputer().has_value());
                continue;
            }
            const Seat actor = table.session.Actor();
            // The cover is up exactly when this is not the person who acted last.
            ASSERT_EQ(table.session.HandoffPending(), actor != lastPerson) << "actor " << actor << " last " << lastPerson;
            if (table.session.HandoffPending()) {
                ++covers;
                EXPECT_EQ(table.session.Viewer(), kNoSeat);
                const uint64_t version = table.session.Version();
                table.session.Reveal();
                EXPECT_EQ(table.session.Version(), version + 1);
                table.session.Reveal();
                EXPECT_EQ(table.session.Version(), version + 1) << "revealing twice changes nothing more";
            } else {
                ++sameTwice;
            }
            EXPECT_EQ(table.session.Viewer(), actor);
            EXPECT_FALSE(table.session.HandoffPending());
            ASSERT_TRUE(PersonActs(table.session, table.match.Hand(), Mode::NeverWin, rng).has_value());
            lastPerson = actor;
        }
        EXPECT_GT(covers, 1) << "the screen was never handed over";
        EXPECT_GT(sameTwice, 0) << "the same person never acted twice in a row";
    }

    TEST(TableSessionTest, RevealDoesNothingWhereNoPersonActs) {
        Table table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        const uint64_t version = table.session.Version();
        table.session.Reveal();
        EXPECT_EQ(table.session.Version(), version);
        EXPECT_EQ(table.session.Viewer(), kNoSeat);
    }

    TEST(TableSessionTest, OneSeedAndSetupGiveTheSameMatchActionByAction) {
        Table first;
        Table second;
        ASSERT_NO_FATAL_FAILURE(first.Start(kDefaultSeatSetup, kSeed, ShortMatch()));
        ASSERT_NO_FATAL_FAILURE(second.Start(kDefaultSeatSetup, kSeed, ShortMatch()));
        MahjongRng firstRng(kSeed);
        MahjongRng secondRng(kSeed);
        for (int actions = 0; first.session.GetPhase() != SessionPhase::MatchOver; ++actions) {
            ASSERT_LT(actions, kActionLimitPerMatch);
            ASSERT_EQ(State(first), State(second)) << "action " << actions;
            if (first.session.GetPhase() == SessionPhase::HandOver) {
                ASSERT_TRUE(first.session.NextHand().has_value());
                ASSERT_TRUE(second.session.NextHand().has_value());
            } else if (first.session.ActorIsPerson()) {
                ASSERT_TRUE(PersonActs(first.session, first.match.Hand(), Mode::SeekWin, firstRng).has_value());
                ASSERT_TRUE(PersonActs(second.session, second.match.Hand(), Mode::SeekWin, secondRng).has_value());
            } else {
                ASSERT_TRUE(first.session.StepComputer().has_value());
                ASSERT_TRUE(second.session.StepComputer().has_value());
            }
        }
        EXPECT_EQ(State(first), State(second));
    }

    TEST(TableSessionTest, APersonWhoSeeksTheWinPlaysAWholeMatchAgainstThreeComputers) {
        for (const uint32_t seed : {kSeed, kSeed + 1}) {
            Table table;
            ASSERT_NO_FATAL_FAILURE(table.Start(kDefaultSeatSetup, seed));
            MahjongRng rng(seed);
            ASSERT_NO_FATAL_FAILURE(PlayWholeMatch(table, Mode::SeekWin, rng, [](Seat) {}));
            EXPECT_EQ(table.session.GetPhase(), SessionPhase::MatchOver);
            EXPECT_EQ(table.match.HandsPlayed(), table.match.HandsTotal());
            EXPECT_EQ(Sum(table.match.Scores()), kTableTotal);
            EXPECT_GE(table.match.History().size(), static_cast<size_t>(table.match.HandsTotal()));
        }
    }

}  // namespace BECore::Mahjong::Tests
