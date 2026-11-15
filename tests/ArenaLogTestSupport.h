#pragma once

#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/MatchLogJson.h>
#include <gtest/gtest.h>

namespace BECore::Mahjong::Tests {

    inline constexpr SeatSetup kLogAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
    inline constexpr SeatSetup kLogPersonFirst{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
    /// More steps than any match takes (a hand has a few hundred actions at most); a guard against a loop that does not end.
    inline constexpr int kLogStepLimit = 100000;

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

    inline Vector<MatchLogEvent> PlayLog(const LogSpec& spec) {
        MahjongSettings settings;
        settings.handsPerMatch = spec.hands;
        Match match;
        TableSession session(match);
        session.SetLogOrigin(spec.origin);
        MatchLog log;
        session.SetLogSink(&log);
        EXPECT_TRUE(session.StartMatch(settings, spec.seed, spec.seats, spec.levels).has_value());
        Vector<ComputerPlayer> people;
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

    inline String LogText(const Vector<MatchLogEvent>& events) {
        String text;
        for (const MatchLogEvent& event : events) {
            text.append(DumpJson(EventToJson(event)));
            text.push_back('\n');
        }
        return text;
    }

    /// A log of a format before the origin: version 4, no `platform`, `build` or `gameVersion`.
    inline String LogTextVersionFour(const Vector<MatchLogEvent>& events) {
        constexpr int kOlderVersion = 4;
        Json start = EventToJson(events.front());
        start["version"] = kOlderVersion;
        start.erase("platform");
        start.erase("build");
        start.erase("gameVersion");
        String text = DumpJson(start);
        text.push_back('\n');
        const Vector<MatchLogEvent> rest(events.begin() + 1, events.end());
        text.append(LogText(rest));
        return text;
    }

    inline int CountEvents(const Vector<MatchLogEvent>& events, MatchLogKind kind) {
        int count = 0;
        for (const MatchLogEvent& event : events) {
            count += event.kind == kind ? 1 : 0;
        }
        return count;
    }

}  // namespace BECore::Mahjong::Tests
