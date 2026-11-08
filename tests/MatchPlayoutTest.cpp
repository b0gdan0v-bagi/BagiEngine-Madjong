#include "MatchTestSupport.h"

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
        /// The match of this index is played by the player who never wins (so only drawn hands happen); it must not replay them, or it
        /// would never end.
        constexpr int kNeverWinMatch = 7;
        /// Far more hands than a match of 16 counted ones needs, replayed draws included.
        constexpr int kHandLimit = 400;

        /// Plays one whole match; the checks run after every hand. A failed ASSERT ends the function.
        void PlayMatch(int index, MatchTotals& totals) {
            const MahjongSettings settings = SettingsOf(index);
            const Mode mode = index == kNeverWinMatch ? Mode::NeverWin : Mode::SeekWin;
            const uint32_t seed = kFirstSeed + static_cast<uint32_t>(index);
            const std::string where = "match " + std::to_string(index) + " (seed " + std::to_string(seed) + ")";
            Match match;
            ASSERT_TRUE(match.Start(settings, seed).has_value()) << where;
            MahjongRng rng(seed);
            MatchAudit audit(settings, where, totals);

            while (match.GetPhase() == MatchPhase::InHand) {
                ASSERT_LT(audit.HandIndex(), kHandLimit) << where << ": the match does not end";
                ASSERT_NO_FATAL_FAILURE(audit.BeforeHand(match));
                ASSERT_NO_FATAL_FAILURE(PlayHand(match, mode, rng));
                const auto ended = match.EndHand();
                ASSERT_TRUE(ended.has_value()) << where << ": " << Name(ended.error());
                ASSERT_NO_FATAL_FAILURE(audit.AfterHand(match, ended.value()));
            }
            ASSERT_NO_FATAL_FAILURE(audit.AtEnd(match));
        }

    }  // namespace

    TEST(MatchPlayoutTest, WholeMatchesPlayedWithRandomLegalActionsKeepEveryInvariant) {
        MatchTotals totals;
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
