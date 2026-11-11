#pragma once

#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/MatchLogJson.h>
#include <gtest/gtest.h>

/**
 * Match logs made in memory for the tests of the arena's log mode (`Arena/ArenaLogs.h`): a match played on a `TableSession` where
 * the seats that are `Person` are driven by a `ComputerPlayer` of the level the seat is given, so the log says `by: person` for
 * them and a test knows which player made every decision.
 */
namespace BECore::Mahjong::Tests {

    inline constexpr SeatSetup kLogAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
    inline constexpr SeatSetup kLogPersonFirst{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
    /// More steps than any match takes (a hand has a few hundred actions at most); a guard against a loop that does not end.
    inline constexpr int kLogStepLimit = 100000;

    /// What a played log is made of.
    struct LogSpec {
        uint32_t seed = 41;
        SeatSetup seats = kLogAllComputers;
        /// The level of every seat; a seat that is a person plays at its level through a `ComputerPlayer` seeded like the seat of a
        /// computer table (`SeatSeed`).
        SeatLevels levels = UniformLevels(kDefaultLevel);
        MatchLogOrigin origin;
        int hands = kShortMatchHands;
        /// Stop once this many hands are settled (0 = play to the end): a match given up. A match of that many hands is over, not given up.
        int stopAfterHands = 0;
    };

    inline eastl::vector<MatchLogEvent> PlayLog(const LogSpec& spec) {
        MahjongSettings settings;
        settings.handsPerMatch = spec.hands;
        Match match;
        TableSession session(match);
        session.SetLogOrigin(spec.origin);
        MatchLog log;
        session.SetLogSink(&log);
        EXPECT_TRUE(session.StartMatch(settings, spec.seed, spec.seats, spec.levels).has_value());
        eastl::vector<ComputerPlayer> people;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            people.emplace_back(SeatSeed(spec.seed, seat), spec.levels[static_cast<size_t>(seat)]);
        }
        int settled = 0;
        for (int steps = 0; session.GetPhase() != SessionPhase::MatchOver && steps < kLogStepLimit; ++steps) {
            if (session.GetPhase() == SessionPhase::HandOver) {
                EXPECT_TRUE(session.NextHand().has_value());
                ++settled;
                if (spec.stopAfterHands > 0 && settled >= spec.stopAfterHands) {
                    break;
                }
                continue;
            }
            session.RunComputers();
            if (session.GetPhase() == SessionPhase::Playing && session.ActorIsPerson()) {
                const Seat seat = session.Actor();
                EXPECT_TRUE(session.PerformPerson(people[static_cast<size_t>(seat)].Choose(MakeTableView(match.Hand(), seat))).has_value());
            }
        }
        return log.Events();
    }

    /// The text of a log: one JSON line per event, as `MatchLogWriter` writes them.
    inline eastl::string LogText(const eastl::vector<MatchLogEvent>& events) {
        eastl::string text;
        for (const MatchLogEvent& event : events) {
            text.append(DumpJson(EventToJson(event)));
            text.push_back('\n');
        }
        return text;
    }

    inline int CountEvents(const eastl::vector<MatchLogEvent>& events, MatchLogKind kind) {
        int count = 0;
        for (const MatchLogEvent& event : events) {
            count += event.kind == kind ? 1 : 0;
        }
        return count;
    }

}  // namespace BECore::Mahjong::Tests
