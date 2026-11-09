#include "HandTestSupport.h"
#include "MatchTestSupport.h"

#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/Match.h>
#include <gtest/gtest.h>

/**
 * Whole matches played by four computer players (`ComputerTable`), for fixed seeds, flowers off and on, 16 and 8 hands, a drawn hand
 * replayed and counted (the eight combinations of `SettingsOf`). At every step the chosen action is in `LegalActions(actor)` and the
 * model accepts it (`Step` applies it through `Apply` and never falls back), a hand is bounded so that a loop fails fast, and after
 * every hand the invariants of `MatchAudit` hold (the points add up, the count and the deal as `NextProgress` says, the settlement is
 * well formed); at the end the match is finished after exactly `handsPerMatch` counted hands. The same seed played again, this time by
 * `ComputerTable::PlayMatch`, gives the same history and the same scores; another table seed plays another match. Normal plays all
 * eight combinations, Easy the three of `kLevelSubset` (flowers off with 16 hands, flowers on with 8, flowers off with 8 and
 * counted draws), so the whole first test takes about 8 s in Debug. Easy plays another match than Normal on the same seed. Pure
 * logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kMatchCount = 8;
        /// The matches (indices of `SettingsOf`) the levels other than Normal play.
        constexpr int kLevelSubset[] = {0, 3, 5};
        constexpr uint32_t kFirstSeed = 700;
        /// A hand is at most about 4 x 70 actions; far more means a loop.
        constexpr int kStepLimitPerHand = 2000;
        /// Far more hands than a match of 16 counted ones needs, replayed draws included.
        constexpr int kHandLimit = 400;

        struct PlayedMatch {
            eastl::vector<HandSettlement> history;
            SeatScores scores{};
            int steps = 0;
        };

        uint32_t SeedOf(int index) {
            return kFirstSeed + static_cast<uint32_t>(index);
        }

        std::string WhereOf(int index) {
            return "match " + std::to_string(index) + " (seed " + std::to_string(SeedOf(index)) + ")";
        }

        /// One hand, a step at a time: every action is listed for its actor and accepted.
        void StepHand(ComputerTable& table, HandModel& hand, const std::string& where, int& steps) {
            int handSteps = 0;
            while (hand.GetPhase() != Phase::Finished) {
                ASSERT_LT(handSteps, kStepLimitPerHand) << where << ": the hand does not end";
                const Seat actor = ComputerTable::NextActor(hand);
                ASSERT_TRUE(IsSeat(actor)) << where;
                const eastl::vector<HandAction> legal = hand.LegalActions(actor);
                ASSERT_FALSE(legal.empty()) << where << " seat " << actor;
                const StepResult step = table.Step(hand);
                ASSERT_TRUE(step.has_value()) << where << " seat " << actor << ": " << Name(step.error());
                EXPECT_TRUE(Has(legal, *step)) << where << " seat " << actor << " chose " << Name(step->kind) << " " << ToString(step->tile);
                ++handSteps;
                ++steps;
            }
        }

        /// Plays match number `index` with the checks of `MatchAudit` after every hand. A failed ASSERT ends the function.
        void PlayAudited(int index, ComputerLevel level, MatchTotals& totals, PlayedMatch& played) {
            const MahjongSettings settings = SettingsOf(index);
            const std::string where = WhereOf(index);
            Match match;
            ASSERT_TRUE(match.Start(settings, SeedOf(index)).has_value()) << where;
            ComputerTable table(SeedOf(index), level);
            MatchAudit audit(settings, where, totals);

            while (match.GetPhase() == MatchPhase::InHand) {
                ASSERT_LT(audit.HandIndex(), kHandLimit) << where << ": the match does not end";
                ASSERT_NO_FATAL_FAILURE(audit.BeforeHand(match));
                ASSERT_NO_FATAL_FAILURE(StepHand(table, match.Hand(), where + " hand " + std::to_string(audit.HandIndex()), played.steps));
                const auto ended = match.EndHand();
                ASSERT_TRUE(ended.has_value()) << where << ": " << Name(ended.error());
                ASSERT_NO_FATAL_FAILURE(audit.AfterHand(match, ended.value()));
            }
            ASSERT_NO_FATAL_FAILURE(audit.AtEnd(match));
            played.history = match.History();
            played.scores = match.Scores();
        }

        /// The same match played again by `ComputerTable::PlayMatch`.
        void PlayWithTheTable(int index, ComputerLevel level, PlayedMatch& played) {
            Match match;
            ASSERT_TRUE(match.Start(SettingsOf(index), SeedOf(index)).has_value());
            ComputerTable table(SeedOf(index), level);
            ASSERT_TRUE(table.PlayMatch(match).has_value());
            played.history = match.History();
            played.scores = match.Scores();
        }

    }  // namespace

    TEST(ComputerMatchTest, WholeMatchesByFourComputerPlayersNeverMakeAnIllegalMoveAndKeepEveryInvariant) {
        for (const ComputerLevel level : {ComputerLevel::Normal, ComputerLevel::Easy}) {
            MatchTotals totals;
            int steps = 0;
            int matches = 0;
            for (int index = 0; index < kMatchCount; ++index) {
                if (level != ComputerLevel::Normal && std::find(std::begin(kLevelSubset), std::end(kLevelSubset), index) == std::end(kLevelSubset)) {
                    continue;
                }
                ++matches;
                PlayedMatch played;
                ASSERT_NO_FATAL_FAILURE(PlayAudited(index, level, totals, played)) << LevelName(level);
                steps += played.steps;

                // The same seed played again, by `PlayMatch`, gives the same match: the winner, the kind and the change of every hand.
                PlayedMatch again;
                ASSERT_NO_FATAL_FAILURE(PlayWithTheTable(index, level, again)) << LevelName(level);
                const std::string where = WhereOf(index) + " at " + std::string(LevelName(level));
                ASSERT_EQ(again.history.size(), played.history.size()) << where;
                for (size_t hand = 0; hand < played.history.size(); ++hand) {
                    EXPECT_EQ(again.history[hand].kind, played.history[hand].kind) << where << " hand " << hand;
                    EXPECT_EQ(again.history[hand].winner, played.history[hand].winner) << where << " hand " << hand;
                    EXPECT_EQ(again.history[hand].deltas, played.history[hand].deltas) << where << " hand " << hand;
                }
                EXPECT_EQ(again.scores, played.scores) << where;
            }
            EXPECT_GT(matches, 0);
            EXPECT_GT(steps, 0) << LevelName(level);
            EXPECT_GT(totals.wins, 0) << "four computer players of level " << LevelName(level) << " never won a hand";
            const std::string name(LevelName(level));
            RecordProperty(name + "Wins", totals.wins);
            RecordProperty(name + "Draws", totals.draws);
            RecordProperty(name + "Replays", totals.replays);
            RecordProperty(name + "Steps", steps);
        }
    }

    TEST(ComputerMatchTest, EasyPlaysAnotherMatchThanNormalOnTheSameSeed) {
        for (const int index : kLevelSubset) {
            PlayedMatch normal;
            PlayedMatch easy;
            ASSERT_NO_FATAL_FAILURE(PlayWithTheTable(index, ComputerLevel::Normal, normal));
            ASSERT_NO_FATAL_FAILURE(PlayWithTheTable(index, ComputerLevel::Easy, easy));
            EXPECT_NE(easy.scores, normal.scores) << WhereOf(index);
        }
    }

    TEST(ComputerMatchTest, ADifferentTableSeedPlaysADifferentMatchOnTheSameWalls) {
        const MahjongSettings settings = SettingsOf(2);
        Match first;
        Match second;
        ASSERT_TRUE(first.Start(settings, SeedOf(2)).has_value());
        ASSERT_TRUE(second.Start(settings, SeedOf(2)).has_value());
        ComputerTable tableA(1);
        ComputerTable tableB(2);

        ASSERT_TRUE(tableA.PlayMatch(first).has_value());
        ASSERT_TRUE(tableB.PlayMatch(second).has_value());

        bool differs = first.History().size() != second.History().size() || first.Scores() != second.Scores();
        for (size_t hand = 0; !differs && hand < first.History().size(); ++hand) {
            differs = first.History()[hand].deltas != second.History()[hand].deltas;
        }
        EXPECT_TRUE(differs) << "two table seeds played the same match";
    }

}  // namespace BECore::Mahjong::Tests
