#include "HandTestSupport.h"

#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MatchLogJson.h>
#include <Mahjong/Save/MatchSave.h>
#include <gtest/gtest.h>

/**
 * The text of a saved match, the prefs and the recorder (`Save/MatchSave.h`), and the way from a played match to a resumed one.
 *
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
        constexpr SeatSetup kPPPP{SeatKind::Person, SeatKind::Person, SeatKind::Person, SeatKind::Person};
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
            SeatLevels levels = UniformLevels(kDefaultLevel);
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
            seen.levels = table.session.Levels();
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
            EXPECT_EQ(got.levels, want.levels) << where;
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
            return text + " " + std::string(LevelText(level));
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
                text += " viewer " + std::to_string(session.Viewer()) + " levels " + std::string(LevelText(session.LevelOf(0))) + std::string(LevelText(session.LevelOf(1))) +
                        std::string(LevelText(session.LevelOf(2))) + std::string(LevelText(session.LevelOf(3))) + " phase " + Name(session.GetPhase());
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
        EXPECT_EQ(busy.table.session.Levels(), UniformLevels(ComputerLevel::Normal));
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

    namespace {

        /// A whole match (or its first `steps` steps) of `seats` on `seed`, played through a session with a recorder in front of a log.
        struct Recorded {
            Match match;
            TableSession session{match};
            MatchLog log;
            MatchRecorder recorder;

            Recorded(uint32_t seed, const SeatSetup& seats, ComputerLevel level, int steps) : Recorded(seed, seats, UniformLevels(level), steps) {}

            Recorded(uint32_t seed, const SeatSetup& seats, const SeatLevels& levels, int steps) {
                recorder.SetNext(&log);
                session.SetLogSink(&recorder);
                EXPECT_TRUE(session.StartMatch(ShortMatch(), seed, seats, levels).has_value());
                MahjongRng rng(seed);
                for (int step = 0; step < steps; ++step) {
                    if (session.GetPhase() == SessionPhase::MatchOver) {
                        break;
                    }
                    if (session.GetPhase() == SessionPhase::HandOver) {
                        EXPECT_TRUE(session.NextHand().has_value());
                    } else if (session.ActorIsPerson()) {
                        EXPECT_TRUE(session.PerformPerson(Choose(match.Hand().LegalActions(session.Actor()), Mode::SeekWin, rng)).has_value());
                    } else {
                        EXPECT_TRUE(session.StepComputer().has_value());
                    }
                }
            }
        };

        constexpr int kAllSteps = kStepLimit;

        std::string Plain(const eastl::string& text) {
            return std::string(text.c_str(), text.size());
        }

        bool Contains(const eastl::string& text, const char* part) {
            return Plain(text).find(part) != std::string::npos;
        }

        /// The text of the full match log (all five kinds with the breakdowns), for the size to be compared with.
        size_t FullLogSize(const eastl::vector<MatchLogEvent>& events) {
            size_t size = 0;
            for (const MatchLogEvent& event : events) {
                size += EventToJson(event).dump().size() + 1;
            }
            return size;
        }

        /// The error of a text that does not parse as a saved match.
        std::string ParseError(const std::string& text) {
            const auto parsed = ParseMatchSaveText(text);
            EXPECT_FALSE(parsed.has_value());
            return parsed ? std::string() : Plain(parsed.error());
        }

        bool Has(const std::string& text, const char* part) {
            return text.find(part) != std::string::npos;
        }

    }  // namespace

    TEST(MahjongSaveTest, TheTextOfAMatchReadsBackAsItsEvents) {
        struct Played {
            uint32_t seed;
            SeatSetup seats;
            ComputerLevel level;
            int steps;
        };
        for (const Played& played : {Played{3, kCCCC, ComputerLevel::Normal, kAllSteps}, Played{11, kPCCC, ComputerLevel::Easy, kAllSteps}, Played{5, kPPCC, ComputerLevel::Normal, 400}}) {
            const std::string where = "seed " + std::to_string(played.seed) + " " + Plain(SeatsToLetters(played.seats));
            const Recorded recorded(played.seed, played.seats, played.level, played.steps);
            const eastl::vector<MatchLogEvent>& all = recorded.log.Events();
            const eastl::string text = MatchSaveText(all);
            const auto parsed = ParseMatchSaveText(std::string_view(text.c_str(), text.size()));
            ASSERT_TRUE(parsed.has_value()) << where << ": " << (parsed ? "" : parsed.error().c_str());
            const eastl::vector<MatchLogEvent> want = WithoutHandsAndEnd(all);
            ASSERT_EQ(parsed->size(), want.size()) << where;
            for (size_t index = 0; index < want.size(); ++index) {
                EXPECT_TRUE((*parsed)[index] == want[index]) << where << ": event " << index;
            }
            EXPECT_EQ((*parsed)[0].seats, played.seats) << where;
            EXPECT_EQ((*parsed)[0].levels, UniformLevels(played.level)) << where;
            // The recorder keeps the same events.
            EXPECT_TRUE(recorded.recorder.Events() == want) << where;
            // Only the three kinds, no breakdowns, and smaller than the log file of the match.
            EXPECT_FALSE(Contains(text, "\"event\":\"hand\"")) << where;
            EXPECT_FALSE(Contains(text, "\"event\":\"end\"")) << where;
            EXPECT_FALSE(Contains(text, "breakdown")) << where;
            EXPECT_TRUE(Contains(text, "\"event\":\"settlement\"") == (IndexOf(want, MatchLogKind::Settlement) < want.size())) << where;
            EXPECT_LT(text.size(), FullLogSize(all)) << where;
            // And the text is a match that replays.
            Match replayed;
            ASSERT_TRUE(ReplayMatchLog(*parsed, replayed).has_value()) << where;
            EXPECT_EQ(replayed.Scores(), recorded.match.Scores()) << where;
        }
    }

    TEST(MahjongSaveTest, TheTextOfAMatchWithMixedLevelsReadsBackWithThemAndResumes) {
        // A person's seat keeps its level too (it plays again when the seat becomes a computer)
        const SeatLevels mixed{ComputerLevel{2}, ComputerLevel{6}, ComputerLevel::Easy, ComputerLevel{10}};
        for (const SeatSetup& seats : {kCCCC, kPCCC, kPPPP}) {
            const std::string where = Plain(SeatsToLetters(seats));
            const Recorded recorded(5, seats, mixed, 120);
            const eastl::string text = MatchSaveText(recorded.log.Events());
            const auto parsed = ParseMatchSaveText(std::string_view(text.c_str(), text.size()));
            ASSERT_TRUE(parsed.has_value()) << where << ": " << (parsed ? "" : parsed.error().c_str());
            EXPECT_EQ((*parsed)[0].levels, mixed) << where;

            Table resumed;
            ASSERT_TRUE(resumed.session.ResumeMatch(*parsed, seats, (*parsed)[0].levels).has_value()) << where;
            EXPECT_EQ(resumed.session.Levels(), mixed) << where;
            EXPECT_EQ(resumed.session.LevelOf(1), ComputerLevel{6}) << where;
        }
    }

    TEST(MahjongSaveTest, AMatchSavedBeforeTheLevelOfEachSeatStillResumes) {
        // The text of a version 3 save: its `start` has one `level`, the rest is as written now
        const Recorded recorded(3, kPCCC, ComputerLevel{3}, 120);
        const std::string current = Plain(MatchSaveText(recorded.log.Events()));
        const size_t firstBreak = current.find('\n');
        ASSERT_NE(firstBreak, std::string::npos);
        const std::string startLine = current.substr(0, firstBreak);
        ASSERT_NE(startLine.find("\"version\":5"), std::string::npos) << startLine;
        ASSERT_NE(startLine.find("\"levels\":[3,3,3,3]"), std::string::npos) << startLine;
        std::string old = startLine;
        old.replace(old.find("\"version\":5"), 11, "\"version\":3");
        old.replace(old.find("\"levels\":[3,3,3,3]"), 18, "\"level\":3");
        const std::string oldText = old + current.substr(firstBreak);

        const auto parsed = ParseMatchSaveText(oldText);
        ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
        EXPECT_EQ((*parsed)[0].levels, UniformLevels(ComputerLevel{3}));
        EXPECT_TRUE(*parsed == WithoutHandsAndEnd(recorded.log.Events()));
        Table resumed;
        ASSERT_TRUE(resumed.session.ResumeMatch(*parsed, kPCCC, (*parsed)[0].levels).has_value());
        EXPECT_EQ(resumed.session.Levels(), UniformLevels(ComputerLevel{3}));
        EXPECT_EQ(resumed.match.Scores(), recorded.match.Scores());
    }

    TEST(MahjongSaveTest, AMatchSavedBeforeTheOriginStillResumesAndItsOriginIsUnknown) {
        // The text of a version 4 save: its `start` has no `platform`, `build` and `gameVersion`
        Recorded recorded(3, kPCCC, ComputerLevel{3}, 120);
        const std::string current = Plain(MatchSaveText(recorded.log.Events()));
        const size_t firstBreak = current.find('\n');
        ASSERT_NE(firstBreak, std::string::npos);
        Json start = Json::parse(current.substr(0, firstBreak));
        start["version"] = 4;
        for (const char* member : {"platform", "build", "gameVersion"}) {
            ASSERT_EQ(start.erase(member), 1u) << member;
        }
        const std::string startLine = start.dump();
        const std::string oldText = startLine + current.substr(firstBreak);

        const auto parsed = ParseMatchSaveText(oldText);
        ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
        EXPECT_EQ((*parsed)[0].origin, MatchLogOrigin{});
        EXPECT_TRUE(*parsed == WithoutHandsAndEnd(recorded.log.Events())) << "the recorded match was made with no origin set";
        Table resumed;
        ASSERT_TRUE(resumed.session.ResumeMatch(*parsed, kPCCC, (*parsed)[0].levels).has_value());
        EXPECT_EQ(resumed.match.Scores(), recorded.match.Scores());
    }

    TEST(MahjongSaveTest, ATextThatIsNoSavedMatchIsRefusedWithItsLine) {
        const Recorded recorded(3, kCCCC, ComputerLevel::Normal, 60);
        const std::string text = Plain(MatchSaveText(recorded.log.Events()));
        const size_t firstBreak = text.find('\n');
        ASSERT_NE(firstBreak, std::string::npos);

        EXPECT_TRUE(Has(ParseError(""), "empty"));
        EXPECT_TRUE(Has(ParseError("\n \n"), "empty"));
        EXPECT_TRUE(Has(ParseError("not json\n"), "line 1"));
        EXPECT_TRUE(Has(ParseError(text + "{oops\n"), "not JSON"));
        // The first line is no start.
        const std::string withoutStart = text.substr(firstBreak + 1);
        EXPECT_TRUE(Has(ParseError(withoutStart), "line 1")) << ParseError(withoutStart);
        EXPECT_TRUE(Has(ParseError(withoutStart), "start"));
        // A hand line (from the full log) or an end line in the middle.
        const std::string handLine = EventToJson(recorded.log.Events()[1]).dump();
        const std::string afterHand = text.substr(0, firstBreak + 1) + handLine + "\n" + text.substr(firstBreak + 1);
        EXPECT_TRUE(Has(ParseError(afterHand), "line 2")) << ParseError(afterHand);
        EXPECT_TRUE(Has(ParseError(afterHand), "Hand event"));
        const std::string endLine = EventToJson(MatchLogEvent{.kind = MatchLogKind::End}).dump();
        EXPECT_TRUE(Has(ParseError(text + endLine + "\n"), "End event"));
        // A tile that is not one, on the first action (line 2), and a blank line before it is counted: the number is the physical line (3).
        const size_t tile = text.find("\"tile\":\"", firstBreak);
        ASSERT_NE(tile, std::string::npos);
        std::string badTile = text;
        badTile.replace(tile + 8, 2, "9z");
        EXPECT_TRUE(Has(ParseError(badTile), "line 2")) << ParseError(badTile);
        std::string withBlank = badTile;
        withBlank.insert(firstBreak + 1, "\n");
        EXPECT_TRUE(Has(ParseError(withBlank), "line 3")) << ParseError(withBlank);
        // More lines than the cap, and exactly the cap is not that error.
        std::string many;
        for (size_t line = 0; line <= kMaxSavedEvents; ++line) {
            many += "x\n";
        }
        EXPECT_TRUE(Has(ParseError(many), "more than"));
        EXPECT_FALSE(Has(ParseError(many.substr(2)), "more than"));
    }

    TEST(MahjongSaveTest, TheRecorderKeepsTheMatchAndPassesEverythingOn) {
        MatchRecorder recorder;
        MatchLog next;
        recorder.SetNext(&next);
        EXPECT_EQ(recorder.Counter(), 0u);
        EXPECT_FALSE(recorder.Finished());
        EXPECT_FALSE(recorder.IsRecording());

        const Recorded first(3, kCCCC, ComputerLevel::Normal, kAllSteps);
        const eastl::vector<MatchLogEvent>& all = first.log.Events();
        for (const MatchLogEvent& event : all) {
            recorder.OnEvent(event);
        }
        // Everything reached the next sink, the Hand and End events too.
        EXPECT_TRUE(next.Events() == all);
        EXPECT_TRUE(recorder.Events() == WithoutHandsAndEnd(all));
        EXPECT_TRUE(recorder.IsRecording());
        EXPECT_TRUE(recorder.Finished());
        // The counter: one per kept event, per Start and per End.
        EXPECT_EQ(recorder.Counter(), recorder.Events().size() + 1);

        // A second Start clears the match, the flag and keeps the counter growing.
        const uint64_t counter = recorder.Counter();
        recorder.OnEvent(all[0]);
        EXPECT_EQ(recorder.Events().size(), 1u);
        EXPECT_FALSE(recorder.Finished());
        EXPECT_GT(recorder.Counter(), counter);

        // The counter grows with an event, not between two.
        recorder.OnEvent(all[2]);
        EXPECT_EQ(recorder.Events().size(), 2u);
        EXPECT_EQ(recorder.Counter(), counter + 2);

        // Not recording: a match that begins keeps nothing, still forwards, and the flag changes nothing for the match in progress.
        recorder.SetRecording(false);
        recorder.OnEvent(all[2]);
        EXPECT_EQ(recorder.Events().size(), 3u) << "the match in progress is still kept";
        EXPECT_TRUE(recorder.IsRecording());
        const size_t forwarded = next.Events().size();
        recorder.OnEvent(all[0]);
        recorder.OnEvent(all[1]);
        recorder.OnEvent(all[2]);
        EXPECT_TRUE(recorder.Events().empty());
        EXPECT_FALSE(recorder.IsRecording());
        EXPECT_EQ(next.Events().size(), forwarded + 3);
        recorder.OnEvent(all.back());
        EXPECT_TRUE(recorder.Finished());
        EXPECT_TRUE(recorder.Events().empty());

        // Back on: the next Start records again.
        recorder.SetRecording(true);
        recorder.OnEvent(all[0]);
        EXPECT_TRUE(recorder.IsRecording());
        EXPECT_EQ(recorder.Events().size(), 1u);

        // No next sink is fine.
        recorder.SetNext(nullptr);
        recorder.OnEvent(all[2]);
        EXPECT_EQ(recorder.Events().size(), 2u);
    }

    TEST(MahjongSaveTest, ThePrefsRoundTripAndAreChecked) {
        for (const SeatSetup& seats : kSetups) {
            for (const ComputerLevel level : kLevels) {
                for (const int hands : {kFullMatchHands, kShortMatchHands}) {
                    const MahjongPrefsSave prefs = MakePrefs("ru", hands == kFullMatchHands, seats, hands, level);
                    const auto read = ReadPrefs(prefs);
                    ASSERT_TRUE(read.has_value()) << (read ? "" : read.error().c_str());
                    EXPECT_EQ(read->language, "ru");
                    EXPECT_EQ(read->hintsOn, hands == kFullMatchHands);
                    EXPECT_EQ(read->seats, seats);
                    EXPECT_EQ(read->handsPerMatch, hands);
                    EXPECT_EQ(read->levels, UniformLevels(level));
                }
            }
        }
        for (const ComputerLevel level : kAllLevels) {
            const MahjongPrefsSave prefs = MakePrefs("en", true, kDefaultSeatSetup, kFullMatchHands, level);
            EXPECT_EQ(prefs.level, eastl::string(LevelText(level).data(), LevelText(level).size()));
            const auto read = ReadPrefs(prefs);
            ASSERT_TRUE(read.has_value()) << LevelText(level);
            EXPECT_EQ(read->levels, UniformLevels(level)) << LevelText(level);
        }
        // A copy written before the scale of numbers holds the name of a level
        for (const auto& [name, level] : {std::pair{"easy", ComputerLevel::Easy}, std::pair{"normal", ComputerLevel::Normal}}) {
            MahjongPrefsSave prefs;
            prefs.level = name;
            const auto read = ReadPrefs(prefs);
            ASSERT_TRUE(read.has_value()) << name;
            EXPECT_EQ(read->levels, UniformLevels(level)) << name;
        }
        const MahjongPrefsSave defaults;
        EXPECT_EQ(defaults.level, "5");
        EXPECT_TRUE(ReadPrefs(defaults).has_value());
        EXPECT_TRUE(defaults == MakePrefs("en", true, kDefaultSeatSetup, kFullMatchHands, kDefaultLevel));
        EXPECT_FALSE(defaults == MakePrefs("ru", true, kDefaultSeatSetup, kFullMatchHands, kDefaultLevel));

        const auto refused = [&](const char* what, auto change, const char* word) {
            MahjongPrefsSave prefs;
            change(prefs);
            const auto read = ReadPrefs(prefs);
            ASSERT_FALSE(read.has_value()) << what;
            EXPECT_NE(Plain(read.error()).find(word), std::string::npos) << what << ": " << read.error().c_str();
        };
        refused("seats too short", [](MahjongPrefsSave& prefs) { prefs.seats = "PCC"; }, "seats");
        refused("seats letter", [](MahjongPrefsSave& prefs) { prefs.seats = "PCCX"; }, "seats");
        refused("level hard", [](MahjongPrefsSave& prefs) { prefs.level = "hard"; }, "level");
        refused("level capital", [](MahjongPrefsSave& prefs) { prefs.level = "Easy"; }, "level");
        refused("level 0", [](MahjongPrefsSave& prefs) { prefs.level = "0"; }, "level");
        refused("level 11", [](MahjongPrefsSave& prefs) { prefs.level = "11"; }, "level");
        refused("level 05", [](MahjongPrefsSave& prefs) { prefs.level = "05"; }, "level");
        refused("hands 12", [](MahjongPrefsSave& prefs) { prefs.hands = 12; }, "hands");
        refused("hands 0", [](MahjongPrefsSave& prefs) { prefs.hands = 0; }, "hands");
        refused("language empty", [](MahjongPrefsSave& prefs) { prefs.language.clear(); }, "language");
    }

    TEST(MahjongSaveTest, ThePrefsKeepALevelPerSeat) {
        const SeatLevels mixed{ComputerLevel{5}, ComputerLevel{2}, ComputerLevel{6}, ComputerLevel{10}};
        const MahjongPrefsSave prefs = MakePrefs("en", true, kPCCC, kFullMatchHands, mixed);
        EXPECT_EQ(prefs.levels, "5,2,6,10");
        EXPECT_EQ(prefs.level, "2") << "level is the lowest computer seat's, for a reader that knows one";
        const auto read = ReadPrefs(prefs);
        ASSERT_TRUE(read.has_value());
        EXPECT_EQ(read->levels, mixed);

        // A person's seat keeps its level in the text, and the lowest computer seat gives `level`
        const MahjongPrefsSave personFirst = MakePrefs("en", true, kPCCC, kFullMatchHands, SeatLevels{ComputerLevel{9}, ComputerLevel{3}, ComputerLevel{4}, ComputerLevel{5}});
        EXPECT_EQ(personFirst.levels, "9,3,4,5");
        EXPECT_EQ(personFirst.level, "3");

        // A table of one level leaves `levels` empty, so an old file and a new one of that table are the same state
        const MahjongPrefsSave uniform = MakePrefs("en", true, kPCCC, kFullMatchHands, UniformLevels(ComputerLevel{3}));
        EXPECT_TRUE(uniform.levels.empty());
        EXPECT_EQ(uniform.level, "3");
        EXPECT_EQ(ReadPrefs(uniform)->levels, UniformLevels(ComputerLevel{3}));
        // Nobody is a computer: `level` still carries the one level
        const MahjongPrefsSave people = MakePrefs("en", true, SeatSetup{SeatKind::Person, SeatKind::Person, SeatKind::Person, SeatKind::Person}, kFullMatchHands, UniformLevels(ComputerLevel{3}));
        EXPECT_EQ(ReadPrefs(people)->levels, UniformLevels(ComputerLevel{3}));

        // `levels` wins over `level`, even a `level` that would be refused
        MahjongPrefsSave wins;
        wins.level = "hard";
        wins.levels = "1,2,3,4";
        ASSERT_TRUE(ReadPrefs(wins).has_value());
        EXPECT_EQ(ReadPrefs(wins)->levels, (SeatLevels{ComputerLevel{1}, ComputerLevel{2}, ComputerLevel{3}, ComputerLevel{4}}));
        // An older copy has only `level`
        MahjongPrefsSave older;
        older.level = "3";
        EXPECT_EQ(ReadPrefs(older)->levels, UniformLevels(ComputerLevel{3}));

        for (const char* bad : {"5,5,5", "5,5,5,11", "a", "5,5,5,5,5", "5,5,5,", ",5,5,5", "5,5,0,5", "5, 5,5,5", "05,5,5,5", "5;5;5;5", "easy,5,5,5"}) {
            MahjongPrefsSave broken;
            broken.levels = bad;
            const auto refused = ReadPrefs(broken);
            ASSERT_FALSE(refused.has_value()) << bad;
            EXPECT_NE(Plain(refused.error()).find("levels"), std::string::npos) << bad << ": " << refused.error().c_str();
        }
    }

    TEST(MahjongSaveTest, AMatchRecordedParsedAndResumedIsTheSameTable) {
        // From the first deal, the middle of a hand and the end of the match.
        for (const int steps : {0, 33, 170, kAllSteps}) {
            for (const SeatSetup& seats : {kPCCC, kPPCC}) {
                const std::string where = std::to_string(steps) + " steps " + Plain(SeatsToLetters(seats));
                const Recorded recorded(13, seats, ComputerLevel::Easy, steps);
                const eastl::string text = MatchSaveText(recorded.recorder.Events());
                const auto parsed = ParseMatchSaveText(std::string_view(text.c_str(), text.size()));
                ASSERT_TRUE(parsed.has_value()) << where;
                Table resumed;
                const ReplayStatus status = resumed.session.ResumeMatch(*parsed, (*parsed)[0].seats, (*parsed)[0].levels);
                ASSERT_TRUE(status.has_value()) << where << ": event " << status.error().index << " " << Name(status.error().problem);
                Observation want;
                want.phase = recorded.session.GetPhase();
                want.actor = recorded.session.Actor();
                want.scores = recorded.match.Scores();
                want.history = recorded.match.History().size();
                want.handsPlayed = recorded.match.HandsPlayed();
                want.east = recorded.match.East();
                want.hand = Snapshot(recorded.match.Hand());
                want.levels = recorded.session.Levels();
                want.seats = recorded.session.Seats();
                ExpectSame(want, Observe(resumed), where);
                // And the resumed table's own text is the one it was resumed from.
                EXPECT_TRUE(MatchSaveText(resumed.log.Events()) == text) << where;
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
