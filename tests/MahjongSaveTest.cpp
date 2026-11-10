#include "HandTestSupport.h"

#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/Game/TableSession.h>
#include <gtest/gtest.h>

/**
 * Resuming a match from its log (`TableSession::ResumeMatch`, `ReplayMatchLog` with an echo; `Game/MatchLog.h`, `Game/TableSession.h`):
 * a match played to some step by people and computers is recorded, its events (without the `Hand` and `End` events, as a saved match
 * keeps them) are resumed into a second session, and the second session is where the first was; it plays on to the end of the match
 * and echoes a whole log; a log that does not replay is refused and changes nothing. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeeds[] = {1, 7, 42, 700};
        /// The steps of the checkpoints "in the middle of a hand".
        constexpr int kMidHandSteps[] = {1, 17, 60};
        /// More steps than a match of people and computers takes (a hand is a few hundred).
        constexpr int kStepLimit = 40000;

        constexpr SeatSetup kPCCC{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        constexpr SeatSetup kPPCC{SeatKind::Person, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        constexpr SeatSetup kCCCC{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        constexpr SeatSetup kCCCP{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Person};
        constexpr SeatSetup kSetups[] = {kPCCC, kPPCC, kCCCC, kCCCP};
        constexpr ComputerLevel kLevels[] = {ComputerLevel::Normal, ComputerLevel::Easy};

        MahjongSettings ShortMatch() {
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            return settings;
        }

        /// A match, its session and a log on it, and the chooser of the people at the table.
        struct Table {
            Match match;
            TableSession session{match};
            MatchLog log;
            MahjongRng rng{1};

            Table() {
                session.SetLogSink(&log);
            }

            void Start(uint32_t seed, const SeatSetup& seats, ComputerLevel level) {
                ASSERT_TRUE(session.StartMatch(ShortMatch(), seed, seats, level).has_value());
                rng = MahjongRng(seed);
            }

            /// One thing happens: a person acts (by the "seek win" chooser), a computer steps, or a hand is settled. False at the
            /// end of the match, and when a call was refused (reported).
            bool Advance() {
                switch (session.GetPhase()) {
                    case SessionPhase::MatchOver:
                        return false;
                    case SessionPhase::HandOver:
                        if (!session.NextHand()) {
                            ADD_FAILURE() << "NextHand was refused";
                            return false;
                        }
                        return true;
                    case SessionPhase::Playing:
                        break;
                }
                if (session.ActorIsPerson()) {
                    const eastl::vector<HandAction> legal = match.Hand().LegalActions(session.Actor());
                    if (legal.empty() || !session.PerformPerson(Choose(legal, Mode::SeekWin, rng))) {
                        ADD_FAILURE() << "the action of the person was refused";
                        return false;
                    }
                    return true;
                }
                if (!session.StepComputer()) {
                    ADD_FAILURE() << "the step of the computer was refused";
                    return false;
                }
                return true;
            }

            /// Plays to the end of the match; the steps taken.
            int PlayOn() {
                int steps = 0;
                while (steps < kStepLimit && Advance()) {
                    ++steps;
                }
                EXPECT_EQ(session.GetPhase(), SessionPhase::MatchOver) << "the match does not end";
                return steps;
            }
        };

        /// What a resumed session must show as the session it was recorded from.
        struct Observation {
            SessionPhase phase = SessionPhase::MatchOver;
            Seat actor = kNoSeat;
            SeatScores scores{};
            size_t history = 0;
            int handsPlayed = 0;
            Seat east = kNoSeat;
            std::string hand;
            ComputerLevel level = kDefaultLevel;
            SeatSetup seats = kDefaultSeatSetup;
        };

        Observation Observe(const Table& table) {
            Observation seen;
            seen.phase = table.session.GetPhase();
            seen.actor = table.session.Actor();
            seen.scores = table.match.Scores();
            seen.history = table.match.History().size();
            seen.handsPlayed = table.match.HandsPlayed();
            seen.east = table.match.East();
            seen.hand = Snapshot(table.match.Hand());
            seen.level = table.session.Level();
            seen.seats = table.session.Seats();
            return seen;
        }

        void ExpectSame(const Observation& want, const Observation& got, const std::string& where) {
            EXPECT_EQ(Name(got.phase), Name(want.phase)) << where;
            EXPECT_EQ(got.actor, want.actor) << where;
            EXPECT_EQ(got.scores, want.scores) << where;
            EXPECT_EQ(got.history, want.history) << where;
            EXPECT_EQ(got.handsPlayed, want.handsPlayed) << where;
            EXPECT_EQ(got.east, want.east) << where;
            EXPECT_EQ(got.hand, want.hand) << where;
            EXPECT_EQ(Name(got.level), Name(want.level)) << where;
            EXPECT_EQ(got.seats, want.seats) << where;
        }

        /// The events a saved match keeps: no `Hand` and no `End` (the replay deals again).
        eastl::vector<MatchLogEvent> WithoutHandsAndEnd(const eastl::vector<MatchLogEvent>& events) {
            eastl::vector<MatchLogEvent> kept;
            for (const MatchLogEvent& event : events) {
                if (event.kind != MatchLogKind::Hand && event.kind != MatchLogKind::End) {
                    kept.push_back(event);
                }
            }
            return kept;
        }

        /// A moment of a recorded match: what the session showed, and the log up to there.
        struct Checkpoint {
            std::string name;
            Observation seen;
            eastl::vector<MatchLogEvent> full;
        };

        /// Whether the hand `HandOver` waits for is the last one of the match.
        bool SettlesTheMatch(const Table& table) {
            const HandSettlement settled = SettleHand(table.match.Hand());
            const MatchProgress next = NextProgress(MatchProgress{table.match.HandsPlayed(), table.match.East()}, settled.kind, settled.winner, table.match.GetSettings());
            return IsMatchOver(next.handsPlayed, table.match.GetSettings());
        }

        /// Plays a match of `seats` at `level` on `seed` and keeps a checkpoint at the first deal, after 1, 17 and 60 steps, in the
        /// first claim window, at the first and at the last score table, at the start of the second hand and at the end.
        eastl::vector<Checkpoint> RecordMatch(uint32_t seed, const SeatSetup& seats, ComputerLevel level) {
            eastl::vector<Checkpoint> points;
            Table table;
            table.Start(seed, seats, level);
            if (table.session.GetPhase() != SessionPhase::Playing) {
                return points;
            }
            bool claim = false;
            bool handOver = false;
            bool lastHandOver = false;
            bool secondHand = false;
            const auto keep = [&](const std::string& name) { points.push_back(Checkpoint{name + " (seed " + std::to_string(seed) + ")", Observe(table), table.log.Events()}); };
            keep("first deal");
            for (int steps = 1; steps < kStepLimit; ++steps) {
                if (!table.Advance()) {
                    break;
                }
                for (const int mid : kMidHandSteps) {
                    if (steps == mid) {
                        keep("after " + std::to_string(steps) + " steps");
                    }
                }
                const SessionPhase phase = table.session.GetPhase();
                if (!claim && phase == SessionPhase::Playing && table.match.Hand().GetPhase() == Phase::Claim) {
                    claim = true;
                    keep("claim window");
                }
                if (!handOver && phase == SessionPhase::HandOver) {
                    handOver = true;
                    keep("first score table");
                }
                if (!lastHandOver && phase == SessionPhase::HandOver && SettlesTheMatch(table)) {
                    lastHandOver = true;
                    keep("last score table");
                }
                if (!secondHand && phase == SessionPhase::Playing && table.match.History().size() == 1) {
                    secondHand = true;
                    keep("start of the second hand");
                }
            }
            EXPECT_EQ(table.session.GetPhase(), SessionPhase::MatchOver);
            keep("finished match");
            return points;
        }

        std::string Label(const Checkpoint& point, const SeatSetup& seats, ComputerLevel level) {
            std::string text = point.name + " ";
            for (const SeatKind kind : seats) {
                text += kind == SeatKind::Person ? 'P' : 'C';
            }
            return text + " " + Name(level);
        }

        /// A started session in the middle of a match, to see what a refused resume leaves.
        struct Busy {
            Table table;

            Busy() {
                table.Start(9, kPCCC, ComputerLevel::Easy);
                for (int step = 0; step < 6; ++step) {
                    EXPECT_TRUE(table.Advance());
                }
            }

            std::string State() const {
                const TableSession& session = table.session;
                std::string text = Snapshot(table.match.Hand()) + " scores";
                for (const int score : table.match.Scores()) {
                    text += " " + std::to_string(score);
                }
                text += " hands " + std::to_string(table.match.HandsPlayed()) + " version " + std::to_string(session.Version());
                text += " viewer " + std::to_string(session.Viewer()) + " level " + Name(session.Level()) + " phase " + Name(session.GetPhase());
                text += " actor " + std::to_string(session.Actor()) + " seats ";
                for (const SeatKind kind : session.Seats()) {
                    text += kind == SeatKind::Person ? 'P' : 'C';
                }
                return text;
            }
        };

        /// The log of a whole match of four computers, with its `Hand` and `End` events.
        eastl::vector<MatchLogEvent> WholeLog(uint32_t seed = 3) {
            Table table;
            table.Start(seed, kCCCC, ComputerLevel::Normal);
            table.PlayOn();
            return table.log.Events();
        }

        size_t IndexOf(const eastl::vector<MatchLogEvent>& events, MatchLogKind kind, size_t from = 0) {
            for (size_t index = from; index < events.size(); ++index) {
                if (events[index].kind == kind) {
                    return index;
                }
            }
            return events.size();
        }

    }  // namespace

    TEST(MahjongSaveTest, AResumedSessionIsWhereTheRecordedOneWas) {
        for (const uint32_t seed : kSeeds) {
            for (const SeatSetup& seats : kSetups) {
                for (const ComputerLevel level : kLevels) {
                    const eastl::vector<Checkpoint> points = RecordMatch(seed, seats, level);
                    ASSERT_GE(points.size(), 7u);
                    for (const Checkpoint& point : points) {
                        const std::string where = Label(point, seats, level);
                        Table resumed;
                        const ReplayStatus status = resumed.session.ResumeMatch(WithoutHandsAndEnd(point.full), seats, level);
                        ASSERT_TRUE(status.has_value()) << where << ": stopped at event " << status.error().index << ": " << Name(status.error().problem);
                        ExpectSame(point.seen, Observe(resumed), where);
                        EXPECT_GT(resumed.session.Version(), 0u) << where;
                    }
                }
            }
        }
    }

    TEST(MahjongSaveTest, TheSinkOfAResumedSessionGetsTheWholeLog) {
        for (const uint32_t seed : kSeeds) {
            for (const SeatSetup& seats : {kPCCC, kCCCC}) {
                const eastl::vector<Checkpoint> points = RecordMatch(seed, seats, ComputerLevel::Normal);
                for (const Checkpoint& point : points) {
                    const std::string where = Label(point, seats, ComputerLevel::Normal);
                    Table resumed;
                    ASSERT_TRUE(resumed.session.ResumeMatch(WithoutHandsAndEnd(point.full), seats, ComputerLevel::Normal).has_value()) << where;
                    // Exactly what the live session logged: the Hand and End events the log lacked come from the match.
                    EXPECT_TRUE(resumed.log.Events() == point.full) << where << ": " << resumed.log.Events().size() << " events, " << point.full.size() << " expected";
                    Match replayed;
                    ASSERT_TRUE(ReplayMatchLog(resumed.log.Events(), replayed).has_value()) << where;
                    EXPECT_EQ(replayed.Scores(), resumed.match.Scores()) << where;
                    EXPECT_EQ(replayed.History().size(), resumed.match.History().size()) << where;
                    EXPECT_EQ(Snapshot(replayed.Hand()), Snapshot(resumed.match.Hand())) << where;
                }
            }
        }
    }

    TEST(MahjongSaveTest, AResumedTablePlaysOnToTheEndOfTheMatch) {
        for (const uint32_t seed : kSeeds) {
            for (const SeatSetup& seats : kSetups) {
                const ComputerLevel level = seed % 2 == 0 ? ComputerLevel::Normal : ComputerLevel::Easy;
                const eastl::vector<Checkpoint> points = RecordMatch(seed, seats, level);
                for (const Checkpoint& point : points) {
                    const std::string where = Label(point, seats, level);
                    Table resumed;
                    ASSERT_TRUE(resumed.session.ResumeMatch(WithoutHandsAndEnd(point.full), seats, level).has_value()) << where;
                    resumed.rng = MahjongRng(seed + 1);
                    // Every action of the computers is listed and accepted (Advance reports a refusal), and the points add up.
                    resumed.PlayOn();
                    EXPECT_EQ(Sum(resumed.match.Scores()), kSeatCount * kExpectedStartingScore) << where;
                    EXPECT_EQ(resumed.match.GetPhase(), MatchPhase::Finished) << where;
                    EXPECT_EQ(resumed.log.Events().back().kind, MatchLogKind::End) << where;
                    // The log of the resumed match is a whole log of the match it ended as.
                    Match replayed;
                    ASSERT_TRUE(ReplayMatchLog(resumed.log.Events(), replayed).has_value()) << where;
                    EXPECT_EQ(replayed.Scores(), resumed.match.Scores()) << where;
                }
            }
        }
    }

    TEST(MahjongSaveTest, TheViewerAndTheCoverFollowTheSeatsOfTheResumedTable) {
        for (const uint32_t seed : kSeeds) {
            for (const SeatSetup& seats : kSetups) {
                const eastl::vector<Checkpoint> points = RecordMatch(seed, seats, ComputerLevel::Normal);
                for (const Checkpoint& point : points) {
                    const std::string where = Label(point, seats, ComputerLevel::Normal);
                    Table resumed;
                    ASSERT_TRUE(resumed.session.ResumeMatch(WithoutHandsAndEnd(point.full), seats, ComputerLevel::Normal).has_value()) << where;
                    const int persons = resumed.session.PersonCount();
                    if (persons == 1) {
                        Seat only = kNoSeat;
                        for (Seat seat = 0; seat < kSeatCount; ++seat) {
                            only = seats[static_cast<size_t>(seat)] == SeatKind::Person ? seat : only;
                        }
                        EXPECT_EQ(resumed.session.Viewer(), only) << where;
                        EXPECT_FALSE(resumed.session.HandoffPending()) << where;
                    } else {
                        EXPECT_EQ(resumed.session.Viewer(), kNoSeat) << where;
                        // Two people: the cover is up exactly while a person has to act; Reveal takes it away.
                        const bool covered = persons >= 2 && resumed.session.GetPhase() == SessionPhase::Playing && resumed.session.ActorIsPerson();
                        EXPECT_EQ(resumed.session.HandoffPending(), covered) << where;
                        if (covered) {
                            resumed.session.Reveal();
                            EXPECT_FALSE(resumed.session.HandoffPending()) << where;
                            EXPECT_EQ(resumed.session.Viewer(), resumed.session.Actor()) << where;
                        }
                    }
                }
            }
        }
    }

    TEST(MahjongSaveTest, AResumeReplacesAMatchInProgress) {
        Busy busy;
        const eastl::vector<MatchLogEvent> log = WholeLog();
        const eastl::vector<MatchLogEvent> cut(log.begin(), log.begin() + static_cast<ptrdiff_t>(IndexOf(log, MatchLogKind::Settlement)));
        const uint64_t before = busy.table.session.Version();
        const size_t logged = busy.table.log.Events().size();
        ASSERT_TRUE(busy.table.session.ResumeMatch(WithoutHandsAndEnd(cut), kCCCC, ComputerLevel::Normal).has_value());
        EXPECT_GT(busy.table.session.Version(), before);
        EXPECT_EQ(busy.table.session.Seats(), kCCCC);
        EXPECT_EQ(Name(busy.table.session.Level()), Name(ComputerLevel::Normal));
        EXPECT_EQ(busy.table.session.Viewer(), kNoSeat);
        EXPECT_EQ(busy.table.session.GetPhase(), SessionPhase::HandOver);
        EXPECT_GT(busy.table.log.Events().size(), logged) << "the resumed log reached the sink, from its Start";
        EXPECT_EQ(busy.table.log.Events()[logged].kind, MatchLogKind::Start);
        busy.table.PlayOn();
        EXPECT_EQ(Sum(busy.table.match.Scores()), kSeatCount * kExpectedStartingScore);
    }

    TEST(MahjongSaveTest, ALogThatIsNotAMatchIsRefusedAndNothingChanges) {
        const eastl::vector<MatchLogEvent> whole = WholeLog();
        ASSERT_GT(whole.size(), 10u);
        const size_t firstAction = IndexOf(whole, MatchLogKind::Action);
        const size_t firstSettlement = IndexOf(whole, MatchLogKind::Settlement);
        ASSERT_LT(firstAction, whole.size());
        ASSERT_LT(firstSettlement, whole.size());

        struct Case {
            std::string name;
            eastl::vector<MatchLogEvent> events;
            size_t index;
            ReplayProblem problem;
        };
        eastl::vector<Case> cases;

        // An action of the wrong seat.
        eastl::vector<MatchLogEvent> wrongSeat = WithoutHandsAndEnd(whole);
        const size_t savedAction = IndexOf(wrongSeat, MatchLogKind::Action);
        wrongSeat[savedAction].seat = NextSeat(wrongSeat[savedAction].seat);
        cases.push_back({"wrong seat", wrongSeat, savedAction, ReplayProblem::ActionRefused});

        // A tile that the seat does not hold.
        eastl::vector<MatchLogEvent> wrongTile = WithoutHandsAndEnd(whole);
        {
            const eastl::vector<Tile>& held = whole[1].concealed[static_cast<size_t>(wrongTile[savedAction].seat)];
            Tile missing{0};
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (eastl::find(held.begin(), held.end(), Tile{id}) == held.end()) {
                    missing = Tile{id};
                    break;
                }
            }
            wrongTile[savedAction].action = HandAction::MakeDiscard(missing, false);
        }
        cases.push_back({"tile not held", wrongTile, savedAction, ReplayProblem::ActionRefused});

        // A settlement that pays otherwise, late in the log.
        eastl::vector<MatchLogEvent> paid = WithoutHandsAndEnd(whole);
        const size_t savedSettlement = IndexOf(paid, MatchLogKind::Settlement);
        paid[savedSettlement].settlement.deltas[0] += 10;
        cases.push_back({"other deltas", paid, savedSettlement, ReplayProblem::SettlementMismatch});

        // A Hand event that the deal does not make.
        eastl::vector<MatchLogEvent> redealt = whole;
        redealt[1].concealed[0].clear();
        cases.push_back({"other deal", redealt, 1, ReplayProblem::HandMismatch});

        cases.push_back({"empty", {}, 0, ReplayProblem::NoStart});
        cases.push_back({"no start", eastl::vector<MatchLogEvent>(whole.begin() + 1, whole.end()), 0, ReplayProblem::NoStart});
        eastl::vector<MatchLogEvent> bad = WithoutHandsAndEnd(whole);
        bad[0].settings.handsPerMatch = kUnplayableHandsPerMatch;
        cases.push_back({"bad settings", bad, 0, ReplayProblem::BadSettings});

        for (const Case& refused : cases) {
            Busy busy;
            const std::string before = busy.State();
            const size_t logged = busy.table.log.Events().size();
            const ReplayStatus status = busy.table.session.ResumeMatch(refused.events, kCCCC, ComputerLevel::Normal);
            ASSERT_FALSE(status.has_value()) << refused.name;
            EXPECT_EQ(status.error().index, refused.index) << refused.name;
            EXPECT_EQ(Name(status.error().problem), Name(refused.problem)) << refused.name;
            EXPECT_EQ(busy.State(), before) << refused.name << ": the session changed";
            EXPECT_EQ(busy.table.log.Events().size(), logged) << refused.name << ": the sink got an event";
            // And the table that was refused goes on as it was.
            EXPECT_TRUE(busy.table.Advance()) << refused.name;
        }
    }

    TEST(MahjongSaveTest, TheEchoOfAReplayDoesNotDependOnTheHandAndEndEventsOfTheLog) {
        const eastl::vector<MatchLogEvent> whole = WholeLog();
        Match first;
        MatchLog withHands;
        ASSERT_TRUE(ReplayMatchLog(whole, first, &withHands).has_value());
        Match second;
        MatchLog withoutHands;
        ASSERT_TRUE(ReplayMatchLog(WithoutHandsAndEnd(whole), second, &withoutHands).has_value());
        EXPECT_TRUE(withHands.Events() == whole);
        EXPECT_TRUE(withoutHands.Events() == whole);
        // The settlements echoed are the match's own, with their breakdowns, as a live session logs them.
        const size_t settlement = IndexOf(withoutHands.Events(), MatchLogKind::Settlement);
        ASSERT_LT(settlement, withoutHands.Events().size());
        EXPECT_EQ(withoutHands.Events()[settlement].settlement.breakdowns[0].won, whole[settlement].settlement.breakdowns[0].won);
        EXPECT_EQ(withoutHands.Events()[settlement].settlement.breakdowns[0].score, whole[settlement].settlement.breakdowns[0].score);
    }

    TEST(MahjongSaveTest, ReplayingWithoutAnEchoIsAsBefore) {
        const eastl::vector<MatchLogEvent> whole = WholeLog();
        Match plain;
        Match echoed;
        MatchLog sink;
        ASSERT_TRUE(ReplayMatchLog(whole, plain).has_value());
        ASSERT_TRUE(ReplayMatchLog(whole, echoed, &sink).has_value());
        EXPECT_EQ(plain.Scores(), echoed.Scores());
        EXPECT_EQ(plain.History().size(), echoed.History().size());
    }

}  // namespace BECore::Mahjong::Tests
