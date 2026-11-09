#include "HandTestSupport.h"

#include <Mahjong/Game/MatchLog.h>
#include <gtest/gtest.h>

/**
 * The recorder of a match (`Game/MatchLog.h`) and its hook in `TableSession`: the events of a whole match in order, what is not
 * logged, a sink replaced mid-match, and the replay of a log on a fresh `Match`. Pure logic: no engine services, an in-memory `MatchLog`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeed = 41;
        /// The most hands a loop plays before it gives up (a match is 8 or 16 counted hands, a replayed draw adds some).
        constexpr int kHandLimitPerMatch = 200;
        /// The tiles on the table at the deal: East 14, the others 13.
        constexpr size_t kEastTiles = 14;
        constexpr size_t kOtherTiles = 13;

        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

        MahjongSettings ShortMatch() {
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            return settings;
        }

        /// A match, its session and a log on it.
        struct LoggedTable {
            Match match;
            TableSession session{match};
            MatchLog log;

            LoggedTable() {
                session.SetLogSink(&log);
            }

            void Start(const SeatSetup& seats, uint32_t seed = kSeed, const MahjongSettings& settings = ShortMatch(), ComputerLevel level = kDefaultLevel) {
                ASSERT_TRUE(session.StartMatch(settings, seed, seats, level).has_value());
            }

            /// The computers play every hand to the end; returns the steps taken.
            int PlayMatch() {
                int steps = 0;
                for (int hands = 0; session.GetPhase() != SessionPhase::MatchOver; ++hands) {
                    EXPECT_LT(hands, kHandLimitPerMatch) << "the match does not end";
                    if (hands >= kHandLimitPerMatch) {
                        break;
                    }
                    steps += session.RunComputers();
                    if (session.GetPhase() == SessionPhase::HandOver) {
                        EXPECT_TRUE(session.NextHand().has_value());
                    }
                }
                return steps;
            }
        };

        /// A match played to its end by four computers on `seed`, and its log.
        struct PlayedMatch {
            Match match;
            eastl::vector<MatchLogEvent> events;
            int steps = 0;
        };

        PlayedMatch Play(uint32_t seed, ComputerLevel level = kDefaultLevel) {
            LoggedTable table;
            table.Start(kAllComputers, seed, ShortMatch(), level);
            PlayedMatch played;
            played.steps = table.PlayMatch();
            played.events = table.log.Events();
            played.match = table.match;
            return played;
        }

        /// The first event of `kind` at or after `from`.
        size_t IndexOf(const eastl::vector<MatchLogEvent>& events, MatchLogKind kind, size_t from = 0) {
            for (size_t index = from; index < events.size(); ++index) {
                if (events[index].kind == kind) {
                    return index;
                }
            }
            return events.size();
        }

    }  // namespace

    TEST(MatchLogTest, AMatchOfComputersIsLoggedInOrder) {
        const PlayedMatch played = Play(kSeed);
        const eastl::vector<MatchLogEvent>& events = played.events;
        ASSERT_GT(events.size(), 3u);
        EXPECT_EQ(events.front().kind, MatchLogKind::Start);
        EXPECT_EQ(events.back().kind, MatchLogKind::End);
        EXPECT_EQ(events.front().seed, kSeed);
        EXPECT_EQ(events.front().settings.handsPerMatch, kShortMatchHands);
        EXPECT_EQ(events.front().seats, kAllComputers);

        // Start, then per hand: Hand, Action..., Settlement; End last. Hand numbers follow the counted hands.
        MatchProgress progress{0, events[1].east};
        const MahjongSettings settings = ShortMatch();
        size_t hands = 0;
        size_t index = 1;
        while (index + 1 < events.size()) {
            const MatchLogEvent& hand = events[index];
            ASSERT_EQ(hand.kind, MatchLogKind::Hand) << "event " << index;
            EXPECT_EQ(hand.number, progress.handsPlayed) << "event " << index;
            EXPECT_EQ(hand.east, progress.east) << "event " << index;
            EXPECT_EQ(hand.roundWind, RoundWindOf(progress.handsPlayed, settings)) << "event " << index;
            EXPECT_EQ(hand.winds, MakeSeatWinds(progress.east)) << "event " << index;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(hand.concealed[static_cast<size_t>(seat)].size(), seat == hand.east ? kEastTiles : kOtherTiles) << "event " << index << " seat " << seat;
            }
            ++index;
            while (index < events.size() && events[index].kind == MatchLogKind::Action) {
                EXPECT_EQ(events[index].by, SeatKind::Computer) << "event " << index;
                EXPECT_TRUE(IsSeat(events[index].seat)) << "event " << index;
                ++index;
            }
            ASSERT_LT(index, events.size());
            ASSERT_EQ(events[index].kind, MatchLogKind::Settlement) << "event " << index;
            progress = NextProgress(progress, events[index].settlement.kind, events[index].settlement.winner, settings);
            ++hands;
            ++index;
        }
        EXPECT_EQ(index, events.size() - 1) << "the End is the last event, after the last Settlement";
        EXPECT_EQ(hands, played.match.History().size());
        EXPECT_GE(hands, static_cast<size_t>(kShortMatchHands));
        EXPECT_EQ(events.back().scores, played.match.Scores());
        EXPECT_EQ(events.back().standings, played.match.Standings());
        EXPECT_EQ(Sum(events.back().scores), kSeatCount * kExpectedStartingScore);
    }

    TEST(MatchLogTest, TheActionsLoggedAreTheStepsOfTheComputers) {
        const PlayedMatch played = Play(kSeed);
        int actions = 0;
        for (const MatchLogEvent& event : played.events) {
            actions += event.kind == MatchLogKind::Action ? 1 : 0;
        }
        EXPECT_GT(played.steps, 0);
        EXPECT_EQ(actions, played.steps);
    }

    TEST(MatchLogTest, TheReplayGivesTheSameMatch) {
        const PlayedMatch played = Play(kSeed);
        Match replayed;
        const ReplayStatus status = ReplayMatchLog(played.events, replayed);
        ASSERT_TRUE(status.has_value()) << "stopped at event " << status.error().index << ": " << Name(status.error().problem);
        EXPECT_EQ(replayed.GetPhase(), MatchPhase::Finished);
        EXPECT_EQ(replayed.Scores(), played.match.Scores());
        ASSERT_EQ(replayed.History().size(), played.match.History().size());
        for (size_t hand = 0; hand < replayed.History().size(); ++hand) {
            const HandSettlement& got = replayed.History()[hand];
            const HandSettlement& want = played.match.History()[hand];
            EXPECT_EQ(got.kind, want.kind) << "hand " << hand;
            EXPECT_EQ(got.winner, want.winner) << "hand " << hand;
            EXPECT_EQ(got.scores, want.scores) << "hand " << hand;
            EXPECT_EQ(got.deltas, want.deltas) << "hand " << hand;
        }
    }

    TEST(MatchLogTest, TheStartEventHoldsTheLevelOfTheSession) {
        EXPECT_EQ(Play(kSeed).events.front().level, kDefaultLevel);
        for (int index = 0; index < kLevelCount; ++index) {
            const ComputerLevel level = static_cast<ComputerLevel>(index);
            const PlayedMatch played = Play(kSeed, level);
            ASSERT_FALSE(played.events.empty());
            EXPECT_EQ(played.events.front().kind, MatchLogKind::Start);
            EXPECT_EQ(played.events.front().level, level) << Name(level);
        }
    }

    TEST(MatchLogTest, TheStartsOfTwoLevelsAreUnequal) {
        const MatchLogEvent normal = Play(kSeed, ComputerLevel::Normal).events.front();
        const MatchLogEvent easy = Play(kSeed, ComputerLevel::Easy).events.front();
        EXPECT_FALSE(normal == easy);
        MatchLogEvent same = normal;
        EXPECT_TRUE(normal == same);
        same.level = ComputerLevel::Easy;
        EXPECT_FALSE(normal == same);
    }

    TEST(MatchLogTest, ALogMadeAtEveryLevelReplaysFromItsActions) {
        // The replay needs no computer player and so no level: the actions are in the log.
        for (int index = 0; index < kLevelCount; ++index) {
            const ComputerLevel level = static_cast<ComputerLevel>(index);
            const PlayedMatch played = Play(kSeed, level);
            Match replayed;
            const ReplayStatus status = ReplayMatchLog(played.events, replayed);
            ASSERT_TRUE(status.has_value()) << Name(level) << ": stopped at event " << status.error().index << ": " << Name(status.error().problem);
            EXPECT_EQ(replayed.GetPhase(), MatchPhase::Finished) << Name(level);
            EXPECT_EQ(replayed.Scores(), played.match.Scores()) << Name(level);
        }
    }

    TEST(MatchLogTest, AChangedLogIsReportedAtTheFirstEventThatDisagrees) {
        const PlayedMatch played = Play(kSeed);

        // An action the hand cannot take: East has declared nothing, so no win.
        eastl::vector<MatchLogEvent> swapped = played.events;
        const size_t action = IndexOf(swapped, MatchLogKind::Action);
        ASSERT_LT(action, swapped.size());
        swapped[action].action = HandAction::MakeDeclareWin();
        Match first;
        ReplayStatus status = ReplayMatchLog(swapped, first);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().index, action);
        EXPECT_EQ(status.error().problem, ReplayProblem::ActionRefused);

        // Another seed deals another hand: the first Hand event no longer matches.
        eastl::vector<MatchLogEvent> reseeded = played.events;
        reseeded[0].seed += 1;
        Match second;
        status = ReplayMatchLog(reseeded, second);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().index, 1u);
        EXPECT_EQ(status.error().problem, ReplayProblem::HandMismatch);

        // A settlement that pays otherwise.
        eastl::vector<MatchLogEvent> paid = played.events;
        const size_t settlement = IndexOf(paid, MatchLogKind::Settlement);
        ASSERT_LT(settlement, paid.size());
        paid[settlement].settlement.deltas[0] += 10;
        Match third;
        status = ReplayMatchLog(paid, third);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().index, settlement);
        EXPECT_EQ(status.error().problem, ReplayProblem::SettlementMismatch);

        // The final scores of the End event.
        eastl::vector<MatchLogEvent> ended = played.events;
        ended.back().scores[1] += 10;
        Match fourth;
        status = ReplayMatchLog(ended, fourth);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().index, ended.size() - 1);
        EXPECT_EQ(status.error().problem, ReplayProblem::EndMismatch);
    }

    TEST(MatchLogTest, ALogWithoutAStartOrWithBadSettingsDoesNotReplay) {
        Match match;
        ReplayStatus status = ReplayMatchLog({}, match);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().index, 0u);
        EXPECT_EQ(status.error().problem, ReplayProblem::NoStart);

        const PlayedMatch played = Play(kSeed);
        eastl::vector<MatchLogEvent> headless(played.events.begin() + 1, played.events.end());
        status = ReplayMatchLog(headless, match);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().problem, ReplayProblem::NoStart);

        eastl::vector<MatchLogEvent> bad = played.events;
        bad[0].settings.handsPerMatch = kUnplayableHandsPerMatch;
        status = ReplayMatchLog(bad, match);
        ASSERT_FALSE(status.has_value());
        EXPECT_EQ(status.error().index, 0u);
        EXPECT_EQ(status.error().problem, ReplayProblem::BadSettings);
    }

    TEST(MatchLogTest, ADifferentSeedGivesADifferentLog) {
        const PlayedMatch a = Play(kSeed);
        const PlayedMatch b = Play(kSeed + 1);
        EXPECT_FALSE(a.events == b.events);
        EXPECT_EQ(Play(kSeed).events, a.events) << "the same seed gives the same log";
    }

    TEST(MatchLogTest, APersonsAcceptedActionIsLoggedWithBy) {
        LoggedTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start(SeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer}));
        table.session.RunComputers();
        const size_t before = table.log.Events().size();
        // Seat 0 is a person; whoever is East, the computers play on until seat 0 is asked.
        ASSERT_TRUE(table.session.ActorIsPerson());
        const Seat actor = table.session.Actor();
        const HandAction action = table.match.Hand().LegalActions(actor).front();
        ASSERT_TRUE(table.session.PerformPerson(action).has_value());
        ASSERT_EQ(table.log.Events().size(), before + 1);
        const MatchLogEvent& event = table.log.Events().back();
        EXPECT_EQ(event.kind, MatchLogKind::Action);
        EXPECT_EQ(event.by, SeatKind::Person);
        EXPECT_EQ(event.seat, actor);
        EXPECT_EQ(event.action, action);
    }

    TEST(MatchLogTest, RefusedAndSilentCallsLogNothing) {
        LoggedTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start(SeatSetup{SeatKind::Person, SeatKind::Person, SeatKind::Computer, SeatKind::Computer}));
        ASSERT_EQ(table.log.Events().size(), 2u) << "a start logs the Start and the first Hand";
        table.session.RunComputers();
        const size_t logged = table.log.Events().size();

        // A bad start: refused, and not logged (nor the old match disturbed).
        MahjongSettings bad;
        bad.handsPerMatch = kUnplayableHandsPerMatch;
        EXPECT_FALSE(table.session.StartMatch(bad, kSeed, kAllComputers).has_value());
        // An action the hand refuses.
        ASSERT_TRUE(table.session.ActorIsPerson());
        EXPECT_FALSE(table.session.PerformPerson(HandAction::MakeDeclareWin()).has_value());
        // A computer step while a person acts.
        EXPECT_FALSE(table.session.StepComputer().has_value());
        // The hand is not over.
        EXPECT_FALSE(table.session.NextHand().has_value());
        // A cover that goes: nothing happens at the table.
        table.session.Reveal();
        EXPECT_EQ(table.log.Events().size(), logged);
    }

    TEST(MatchLogTest, ASinkCanBeReplacedOrRemovedInTheMiddleOfAMatch) {
        LoggedTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kAllComputers));
        ASSERT_TRUE(table.session.StepComputer().has_value());
        const size_t first = table.log.Events().size();
        ASSERT_EQ(first, 3u);

        table.session.SetLogSink(nullptr);
        EXPECT_EQ(table.session.GetLogSink(), nullptr);
        ASSERT_TRUE(table.session.StepComputer().has_value());
        EXPECT_EQ(table.log.Events().size(), first) << "no sink, no event";

        MatchLog other;
        table.session.SetLogSink(&other);
        ASSERT_TRUE(table.session.StepComputer().has_value());
        EXPECT_EQ(table.log.Events().size(), first);
        ASSERT_EQ(other.Events().size(), 1u);
        EXPECT_EQ(other.Events().front().kind, MatchLogKind::Action);

        // The hand goes on to its end and the next one without a sink in the way.
        table.session.SetLogSink(nullptr);
        EXPECT_NO_FATAL_FAILURE(table.PlayMatch());
        EXPECT_EQ(table.session.GetPhase(), SessionPhase::MatchOver);
    }

    TEST(MatchLogTest, ASessionWithoutASinkPlaysAsBefore) {
        Match match;
        TableSession session(match);
        ASSERT_TRUE(session.StartMatch(ShortMatch(), kSeed, kAllComputers).has_value());
        EXPECT_GT(session.RunComputers(), 0);
        EXPECT_EQ(session.GetLogSink(), nullptr);
    }

}  // namespace BECore::Mahjong::Tests
