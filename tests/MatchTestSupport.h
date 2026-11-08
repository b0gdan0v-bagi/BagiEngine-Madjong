#pragma once

#include "ScoreTestSupport.h"

#include <Mahjong/Game/Match.h>
#include <string>

/**
 * What the tests that play whole matches share (`MatchPlayoutTest`, `ComputerMatchTest`): the settings of match number `index`, and
 * `MatchAudit`, which checks the invariants of a match around every hand (the points of the table add up, the count and the deal moved
 * as `NextProgress` says, the settlement is well formed) and at the end (finished after exactly `handsPerMatch` counted hands, the
 * round winds a quarter each, the deal went round when East never won, the standings list every seat). Header only.
 */
namespace BECore::Mahjong::Tests {

    inline constexpr int kTableTotal = kSeatCount * kExpectedStartingScore;

    /// Match number `index` of a table: bit 0 flowers, bit 1 the short game, bit 2 a drawn hand counts (no replay).
    inline MahjongSettings SettingsOf(int index) {
        MahjongSettings settings;
        settings.flowers = (index & 1) != 0;
        settings.handsPerMatch = (index & 2) != 0 ? kShortMatchHands : kFullMatchHands;
        settings.replayDrawnHand = (index & 4) == 0;
        return settings;
    }

    /// What happened over the matches audited so far.
    struct MatchTotals {
        int wins = 0;
        int draws = 0;
        int replays = 0;
        /// Matches in which East never won, so the deal went round and the check of it ran.
        int dealtRoundChecks = 0;
    };

    /// The settlement of one hand is what the rules of scoring promise, whoever won.
    inline void ExpectWellFormed(const HandSettlement& settlement, const MahjongSettings& settings) {
        ExpectConsistent(settlement);
        const bool won = settlement.kind == ResultKind::Win;
        if (!won) {
            EXPECT_EQ(settlement.winner, kNoSeat);
            EXPECT_EQ(settlement.deltas, SeatScores{}) << "a drawn hand pays nothing";
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const ScoreBreakdown& breakdown = settlement.breakdowns[seat];
            EXPECT_GE(breakdown.score, 0) << "seat " << seat;
            EXPECT_LE(breakdown.score, LimitCap(settings.limit)) << "seat " << seat;
            if (won && seat == settlement.winner) {
                EXPECT_TRUE(breakdown.won) << "seat " << seat;
                EXPECT_TRUE(breakdown.limitHand.has_value() || CountLines(breakdown, ScoreKind::Win) == 1) << "the winner has neither the Win line nor a limit hand";
            } else {
                ExpectNoWinnerLine(breakdown, seat);
            }
        }
    }

    /// Audits one match: `BeforeHand`, play the hand, `EndHand`, `AfterHand`, ... then `AtEnd`. The functions use ASSERT, so a caller wraps
    /// each in `ASSERT_NO_FATAL_FAILURE`.
    class MatchAudit {
    public:
        MatchAudit(const MahjongSettings& settings, std::string where, MatchTotals& totals)
            : _settings(settings), _where(std::move(where)), _totals(totals), _handsPerRound(settings.handsPerMatch / kRoundsPerMatch) {}

        /// Before the hand is played: remembers where the match stands and checks the hand was dealt for it.
        void BeforeHand(const Match& match) {
            _before = MatchProgress{match.HandsPlayed(), match.East()};
            _roundWind = match.RoundWind();
            ASSERT_EQ(match.Hand().East(), _before.east) << HandWhere();
            ASSERT_EQ(match.Hand().RoundWind(), _roundWind) << HandWhere();
        }

        /// After `EndHand` returned `settlement` for hand number `index` of the match (replayed draws included).
        void AfterHand(const Match& match, const HandSettlement& settlement) {
            const std::string where = HandWhere();
            // The settlement is of this hand and well formed.
            EXPECT_EQ(settlement.east, _before.east) << where;
            EXPECT_EQ(settlement.roundWind, _roundWind) << where;
            ExpectWellFormed(settlement, _settings);

            // The points of the table add up and moved by the change of this hand only.
            EXPECT_EQ(Sum(match.Scores()), kTableTotal) << where;
            ASSERT_EQ(match.History().size(), static_cast<size_t>(_handIndex + 1)) << where;
            EXPECT_EQ(match.History().back().deltas, settlement.deltas) << where;

            // The count and the deal moved exactly as the rules say; a count grows by 0 (a replayed draw) or 1.
            const MatchProgress expected = NextProgress(_before, settlement.kind, settlement.winner, _settings);
            EXPECT_EQ(match.HandsPlayed(), expected.handsPlayed) << where;
            EXPECT_EQ(match.East(), expected.east) << where;
            const int grew = match.HandsPlayed() - _before.handsPlayed;
            EXPECT_TRUE(grew == 0 || grew == 1) << where << ": the count grew by " << grew;
            EXPECT_EQ(grew == 0, settlement.kind == ResultKind::Drawn && _settings.replayDrawnHand) << where;

            Count(settlement, grew);

            // The next hand is dealt for the new deal and round wind, or the match is over.
            if (match.GetPhase() == MatchPhase::InHand) {
                EXPECT_EQ(match.Hand().GetPhase(), Phase::Discard) << where;
                EXPECT_EQ(match.Hand().East(), match.East()) << where;
                EXPECT_EQ(match.Hand().RoundWind(), match.RoundWind()) << where;
            }
            ++_handIndex;
        }

        /// The match is over: finished after exactly `handsPerMatch` counted hands, and the rest of the end-of-match checks.
        void AtEnd(Match& match) {
            EXPECT_EQ(match.GetPhase(), MatchPhase::Finished) << _where;
            EXPECT_EQ(match.HandsPlayed(), _settings.handsPerMatch) << _where;
            EXPECT_EQ(_counted, _settings.handsPerMatch) << _where;
            const auto refused = match.EndHand();
            ASSERT_FALSE(refused.has_value()) << _where;
            EXPECT_EQ(refused.error(), MatchError::NotInHand) << _where;
            // The round winds ran East to North, a quarter of the hands each.
            for (int round = 0; round < kRoundsPerMatch; ++round) {
                EXPECT_EQ(_roundHands[static_cast<size_t>(round)], _handsPerRound) << _where << " round " << round;
            }
            // When East never won, the deal went round: every seat was East of the same number of hands.
            if (!_eastWon) {
                ++_totals.dealtRoundChecks;
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    EXPECT_EQ(_eastHands[static_cast<size_t>(seat)], _handsPerRound) << _where << " seat " << seat;
                }
            }
            // The standings list every seat once, best first.
            const eastl::array<Seat, kSeatCount> standings = match.Standings();
            int listed = 0;
            for (size_t place = 0; place < standings.size(); ++place) {
                ASSERT_TRUE(IsSeat(standings[place])) << _where;
                listed |= 1 << standings[place];
                if (place > 0) {
                    EXPECT_GE(match.Scores()[standings[place - 1]], match.Scores()[standings[place]]) << _where;
                }
            }
            EXPECT_EQ(listed, (1 << kSeatCount) - 1) << _where;
        }

        /// The number of the hand being played (replayed draws count), for a message and a bound on the length of a match.
        [[nodiscard]] int HandIndex() const noexcept {
            return _handIndex;
        }

    private:
        std::string HandWhere() const {
            return _where + " hand " + std::to_string(_handIndex);
        }

        void Count(const HandSettlement& settlement, int grew) {
            if (settlement.kind == ResultKind::Win) {
                ++_totals.wins;
            } else {
                ++_totals.draws;
            }
            if (grew == 0) {
                ++_totals.replays;
                return;
            }
            ++_counted;
            ++_eastHands[static_cast<size_t>(_before.east)];
            ++_roundHands[static_cast<size_t>(_roundWind)];
            _eastWon = _eastWon || (settlement.kind == ResultKind::Win && settlement.winner == _before.east);
        }

        MahjongSettings _settings;
        std::string _where;
        MatchTotals& _totals;
        int _handsPerRound = 0;
        int _handIndex = 0;
        int _counted = 0;
        bool _eastWon = false;
        MatchProgress _before;
        Wind _roundWind = Wind::East;
        eastl::array<int, kSeatCount> _eastHands{};
        eastl::array<int, kRoundsPerMatch> _roundHands{};
    };

}  // namespace BECore::Mahjong::Tests
