#include "ScoreTestSupport.h"

#include <Mahjong/Game/Match.h>

/**
 * Whole matches played with random legal actions to the end, for several seeds (flowers off and on, 16 and 8 hands, a drawn hand
 * replayed or counted). After every hand the invariants of a match hold: the points of the table still add up, the count and the deal
 * moved exactly as `NextProgress` says, the settlement is well formed; at the end the match is finished after exactly
 * `handsPerMatch` counted hands and the round winds ran East to North. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kMatchCount = 8;
        constexpr uint32_t kFirstSeed = 100;
        constexpr int kTableTotal = kSeatCount * kExpectedStartingScore;
        /// The match of this index is played by the player who never wins (so only drawn hands happen); it must not replay them, or it
        /// would never end.
        constexpr int kNeverWinMatch = 7;
        /// Far more hands than a match of 16 counted ones needs, replayed draws included.
        constexpr int kHandLimit = 400;

        struct Totals {
            int wins = 0;
            int draws = 0;
            int replays = 0;
            /// Matches in which East never won, so the deal went round and the check of it ran.
            int dealtRoundChecks = 0;
        };

        /// Match number `index` of the table: bit 0 flowers, bit 1 the short game, bit 2 a drawn hand counts (no replay).
        MahjongSettings SettingsOf(int index) {
            MahjongSettings settings;
            settings.flowers = (index & 1) != 0;
            settings.handsPerMatch = (index & 2) != 0 ? kShortMatchHands : kFullMatchHands;
            settings.replayDrawnHand = (index & 4) == 0;
            return settings;
        }

        /// The settlement of one hand is what the rules of scoring promise, whoever won.
        void ExpectWellFormed(const HandSettlement& settlement, const MahjongSettings& settings) {
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

        /// Plays one whole match; the checks run after every hand. A failed ASSERT ends the function.
        void PlayMatch(int index, Totals& totals) {
            const MahjongSettings settings = SettingsOf(index);
            const Mode mode = index == kNeverWinMatch ? Mode::NeverWin : Mode::SeekWin;
            const uint32_t seed = kFirstSeed + static_cast<uint32_t>(index);
            const std::string where = "match " + std::to_string(index) + " (seed " + std::to_string(seed) + ")";
            Match match;
            ASSERT_TRUE(match.Start(settings, seed).has_value()) << where;
            MahjongRng rng(seed);
            const int handsPerRound = settings.handsPerMatch / kRoundsPerMatch;

            int counted = 0;
            bool eastWon = false;
            eastl::array<int, kSeatCount> eastHands{};
            eastl::array<int, kRoundsPerMatch> roundHands{};
            for (int hand = 0; match.GetPhase() == MatchPhase::InHand; ++hand) {
                ASSERT_LT(hand, kHandLimit) << where << ": the match does not end";
                const std::string handWhere = where + " hand " + std::to_string(hand);
                const MatchProgress before{match.HandsPlayed(), match.East()};
                const Wind roundWind = match.RoundWind();
                ASSERT_EQ(match.Hand().East(), before.east) << handWhere;
                ASSERT_EQ(match.Hand().RoundWind(), roundWind) << handWhere;

                ASSERT_NO_FATAL_FAILURE(PlayHand(match, mode, rng));
                const auto ended = match.EndHand();
                ASSERT_TRUE(ended.has_value()) << handWhere << ": " << Name(ended.error());
                const HandSettlement& settlement = ended.value();

                // The settlement is of this hand and well formed.
                EXPECT_EQ(settlement.east, before.east) << handWhere;
                EXPECT_EQ(settlement.roundWind, roundWind) << handWhere;
                ExpectWellFormed(settlement, settings);

                // The points of the table add up and moved by the change of this hand only.
                EXPECT_EQ(Sum(match.Scores()), kTableTotal) << handWhere;
                ASSERT_EQ(match.History().size(), static_cast<size_t>(hand + 1)) << handWhere;
                EXPECT_EQ(match.History().back().deltas, settlement.deltas) << handWhere;

                // The count and the deal moved exactly as the rules say; a count grows by 0 (a replayed draw) or 1.
                const MatchProgress expected = NextProgress(before, settlement.kind, settlement.winner, settings);
                EXPECT_EQ(match.HandsPlayed(), expected.handsPlayed) << handWhere;
                EXPECT_EQ(match.East(), expected.east) << handWhere;
                const int grew = match.HandsPlayed() - before.handsPlayed;
                EXPECT_TRUE(grew == 0 || grew == 1) << handWhere << ": the count grew by " << grew;
                EXPECT_EQ(grew == 0, settlement.kind == ResultKind::Drawn && settings.replayDrawnHand) << handWhere;

                if (settlement.kind == ResultKind::Win) {
                    ++totals.wins;
                } else {
                    ++totals.draws;
                }
                if (grew == 0) {
                    ++totals.replays;
                } else {
                    ++counted;
                    ++eastHands[before.east];
                    ++roundHands[static_cast<size_t>(roundWind)];
                    eastWon = eastWon || (settlement.kind == ResultKind::Win && settlement.winner == before.east);
                }

                // The next hand is dealt for the new deal and round wind, or the match is over.
                if (match.GetPhase() == MatchPhase::InHand) {
                    EXPECT_EQ(match.Hand().GetPhase(), Phase::Discard) << handWhere;
                    EXPECT_EQ(match.Hand().East(), match.East()) << handWhere;
                    EXPECT_EQ(match.Hand().RoundWind(), match.RoundWind()) << handWhere;
                }
            }

            EXPECT_EQ(match.GetPhase(), MatchPhase::Finished) << where;
            EXPECT_EQ(match.HandsPlayed(), settings.handsPerMatch) << where;
            EXPECT_EQ(counted, settings.handsPerMatch) << where;
            const auto refused = match.EndHand();
            ASSERT_FALSE(refused.has_value()) << where;
            EXPECT_EQ(refused.error(), MatchError::NotInHand) << where;
            // The round winds ran East to North, a quarter of the hands each.
            for (int round = 0; round < kRoundsPerMatch; ++round) {
                EXPECT_EQ(roundHands[static_cast<size_t>(round)], handsPerRound) << where << " round " << round;
            }
            // When East never won, the deal went round: every seat was East of the same number of hands.
            if (!eastWon) {
                ++totals.dealtRoundChecks;
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    EXPECT_EQ(eastHands[static_cast<size_t>(seat)], handsPerRound) << where << " seat " << seat;
                }
            }
            // The standings list every seat once, best first.
            const eastl::array<Seat, kSeatCount> standings = match.Standings();
            int listed = 0;
            for (size_t place = 0; place < standings.size(); ++place) {
                ASSERT_TRUE(IsSeat(standings[place])) << where;
                listed |= 1 << standings[place];
                if (place > 0) {
                    EXPECT_GE(match.Scores()[standings[place - 1]], match.Scores()[standings[place]]) << where;
                }
            }
            EXPECT_EQ(listed, (1 << kSeatCount) - 1) << where;
        }

    }  // namespace

    TEST(MatchPlayoutTest, WholeMatchesPlayedWithRandomLegalActionsKeepEveryInvariant) {
        Totals totals;
        for (int index = 0; index < kMatchCount; ++index) {
            ASSERT_NO_FATAL_FAILURE(PlayMatch(index, totals));
        }
        EXPECT_GT(totals.wins, 0);
        EXPECT_GT(totals.draws, 0);
        EXPECT_GT(totals.dealtRoundChecks, 0);
    }

    TEST(MatchPlayoutTest, ThePlayerWhoNeverWinsDrawsEveryHandAndNobodyPays) {
        const MahjongSettings settings = SettingsOf(kNeverWinMatch);
        ASSERT_FALSE(settings.replayDrawnHand);
        Match match;
        ASSERT_TRUE(match.Start(settings, kFirstSeed).has_value());
        MahjongRng rng(kFirstSeed);

        while (match.GetPhase() == MatchPhase::InHand) {
            ASSERT_NO_FATAL_FAILURE(PlayHand(match, Mode::NeverWin, rng));
            ASSERT_TRUE(match.EndHand().has_value());
        }

        EXPECT_EQ(match.History().size(), static_cast<size_t>(settings.handsPerMatch));
        for (const HandSettlement& settlement : match.History()) {
            EXPECT_EQ(settlement.kind, ResultKind::Drawn);
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(match.Scores()[seat], kExpectedStartingScore) << "seat " << seat;
        }
    }

}  // namespace BECore::Mahjong::Tests
