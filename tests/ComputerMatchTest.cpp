#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/Match.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>
#include <tests/HandTestSupport.h>
#include <tests/MatchTestSupport.h>

/// Normal plays all eight combinations of `SettingsOf`, Easy the three of `kLevelSubset`: the first test takes about 8 s in Debug.
/// A search seat is a profile knob, no level: one short match of 4 hands plays it among three Normal ones.
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
        /// The match with a search seat: 4 hands (a round is one hand), the seat that searches, a small budget (a search decision plays
        /// worlds x candidates whole hands, in Debug about 10 ms each).
        constexpr int kSearchMatchHands = 4;
        constexpr Seat kSearchSeat = 1;
        constexpr uint32_t kSearchMatchSeed = 710;
        constexpr int kSearchTestWorlds = 2;
        constexpr int kSearchTestCandidates = 3;
        /// The depth of the own-draws seat's match.
        constexpr int kOwnDrawsTestDepth = 2;
        /// The weighted seat's match: the seed of the weight sets and how many random ones are played besides the default, all zeros and all largest.
        constexpr uint32_t kWeightsSeed = 31;
        constexpr int kRandomWeightSets = 3;
        /// The value weights of the weighted seat's second match and its depths. Depth 2 is played once only: it is the dear one in Debug.
        constexpr int kValueWeights[] = {1, 4, kWeightMax};
        constexpr int kValueTestDepth = 1;
        constexpr int kValueDeeperTestDepth = 2;

        struct PlayedMatch {
            Vector<HandSettlement> history;
            SeatScores scores{};
            int steps = 0;
        };

        uint32_t SeedOf(int index) {
            return kFirstSeed + static_cast<uint32_t>(index);
        }

        String WhereOf(int index) {
            return Format("match {} (seed {})", index, SeedOf(index));
        }

        /// One hand, a step at a time: every action is listed for its actor and accepted.
        void StepHand(ComputerTable& table, HandModel& hand, const String& where, int& steps) {
            int handSteps = 0;
            while (hand.GetPhase() != Phase::Finished) {
                ASSERT_LT(handSteps, kStepLimitPerHand) << where << ": the hand does not end";
                const Seat actor = ComputerTable::NextActor(hand);
                ASSERT_TRUE(IsSeat(actor)) << where;
                const Vector<HandAction> legal = hand.LegalActions(actor);
                ASSERT_FALSE(legal.empty()) << where << " seat " << actor;
                const StepResult step = table.Step(hand);
                ASSERT_TRUE(step.has_value()) << where << " seat " << actor << ": " << Name(step.error());
                EXPECT_TRUE(Has(legal, *step)) << where << " seat " << actor << " chose " << Name(step->kind) << " " << ToStd(ToString(step->tile));
                ++handSteps;
                ++steps;
            }
        }

        /// Plays match number `index` with the checks of `MatchAudit` after every hand. A failed ASSERT ends the function.
        void PlayAudited(int index, ComputerLevel level, MatchTotals& totals, PlayedMatch& played) {
            const MahjongSettings settings = SettingsOf(index);
            const String where = WhereOf(index);
            Match match;
            ASSERT_TRUE(match.Start(settings, SeedOf(index)).has_value()) << where;
            ComputerTable table(SeedOf(index), level);
            MatchAudit audit(settings, where, totals);

            while (match.GetPhase() == MatchPhase::InHand) {
                ASSERT_LT(audit.HandIndex(), kHandLimit) << where << ": the match does not end";
                ASSERT_NO_FATAL_FAILURE(audit.BeforeHand(match));
                ASSERT_NO_FATAL_FAILURE(StepHand(table, match.Hand(), Format("{} hand {}", where, audit.HandIndex()), played.steps));
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

        /// One hand by four `ComputerPlayer`s that need not share a profile: the actor's own player decides, its action is listed and accepted.
        void StepMixedHand(Array<ComputerPlayer, kSeatCount>& players, HandModel& hand, const String& where, int& steps, int& searchSteps) {
            int handSteps = 0;
            while (hand.GetPhase() != Phase::Finished) {
                ASSERT_LT(handSteps, kStepLimitPerHand) << where << ": the hand does not end";
                const Seat actor = ComputerTable::NextActor(hand);
                ASSERT_TRUE(IsSeat(actor)) << where;
                const Vector<HandAction> legal = hand.LegalActions(actor);
                const Optional<HandAction> action = players[actor].Decide(hand, actor);
                ASSERT_TRUE(action.has_value()) << where << " seat " << actor;
                EXPECT_TRUE(Has(legal, *action)) << where << " seat " << actor << " chose " << Name(action->kind) << " " << ToStd(ToString(action->tile));
                const auto applied = hand.Apply(actor, *action);
                ASSERT_TRUE(applied.has_value()) << where << " seat " << actor << ": " << Name(applied.error());
                ++handSteps;
                ++steps;
                searchSteps += actor == kSearchSeat ? 1 : 0;
            }
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
                ASSERT_NO_FATAL_FAILURE(PlayAudited(index, level, totals, played)) << ToStd(LevelText(level));
                steps += played.steps;

                // The same seed played again, by `PlayMatch`, gives the same match: the winner, the kind and the change of every hand.
                PlayedMatch again;
                ASSERT_NO_FATAL_FAILURE(PlayWithTheTable(index, level, again)) << ToStd(LevelText(level));
                const String where = Format("{} at {}", WhereOf(index), LevelText(level));
                ASSERT_EQ(again.history.size(), played.history.size()) << where;
                for (size_t hand = 0; hand < played.history.size(); ++hand) {
                    EXPECT_EQ(again.history[hand].kind, played.history[hand].kind) << where << " hand " << hand;
                    EXPECT_EQ(again.history[hand].winner, played.history[hand].winner) << where << " hand " << hand;
                    EXPECT_EQ(again.history[hand].deltas, played.history[hand].deltas) << where << " hand " << hand;
                }
                EXPECT_EQ(again.scores, played.scores) << where;
            }
            EXPECT_GT(matches, 0);
            EXPECT_GT(steps, 0) << ToStd(LevelText(level));
            EXPECT_GT(totals.wins, 0) << "four computer players of level " << ToStd(LevelText(level)) << " never won a hand";
            const String name(LevelText(level));
            RecordProperty(Format("{}Wins", name).c_str(), totals.wins);
            RecordProperty(Format("{}Draws", name).c_str(), totals.draws);
            RecordProperty(Format("{}Replays", name).c_str(), totals.replays);
            RecordProperty(Format("{}Steps", name).c_str(), steps);
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

    TEST(ComputerMatchTest, OneSearchSeatAmongThreeNormalSeatsPlaysAShortMatchLegally) {
        MahjongSettings settings;
        settings.handsPerMatch = kSearchMatchHands;
        const String where = Format("search match (seed {})", kSearchMatchSeed);
        Match match;
        ASSERT_TRUE(match.Start(settings, kSearchMatchSeed).has_value()) << where;

        LevelProfile search = kNormalProfile;
        search.searchWorlds = kSearchTestWorlds;
        search.searchCandidates = kSearchTestCandidates;
        auto playerOf = [&](Seat seat) { return seat == kSearchSeat ? ComputerPlayer(SeatSeed(kSearchMatchSeed, seat), search) : ComputerPlayer(SeatSeed(kSearchMatchSeed, seat)); };
        Array<ComputerPlayer, kSeatCount> players{playerOf(0), playerOf(1), playerOf(2), playerOf(3)};

        MatchTotals totals;
        MatchAudit audit(settings, where, totals);
        int steps = 0;
        int searchSteps = 0;
        while (match.GetPhase() == MatchPhase::InHand) {
            ASSERT_LT(audit.HandIndex(), kHandLimit) << where << ": the match does not end";
            ASSERT_NO_FATAL_FAILURE(audit.BeforeHand(match));
            ASSERT_NO_FATAL_FAILURE(StepMixedHand(players, match.Hand(), Format("{} hand {}", where, audit.HandIndex()), steps, searchSteps));
            const auto ended = match.EndHand();
            ASSERT_TRUE(ended.has_value()) << where << ": " << Name(ended.error());
            ASSERT_NO_FATAL_FAILURE(audit.AfterHand(match, ended.value()));
        }
        ASSERT_NO_FATAL_FAILURE(audit.AtEnd(match));
        EXPECT_EQ(match.HandsPlayed(), kSearchMatchHands) << where;
        EXPECT_GT(searchSteps, 0) << "the search seat never acted";
        RecordProperty("SearchMatchSteps", steps);
        RecordProperty("SearchSeatSteps", searchSteps);
    }

    namespace {

        /// One seat plays `valued` among three Normal seats to the end of a short match, every action legal. `scores` gets the final scores
        /// when asked for, so a caller can play the match again and compare.
        void PlayOwnDrawsMatch(const LevelProfile& valued, const char* name, SeatScores* scores = nullptr) {
            MahjongSettings settings;
            settings.handsPerMatch = kSearchMatchHands;
            const String where = Format("{} (seed {})", name, kSearchMatchSeed);
            Match match;
            ASSERT_TRUE(match.Start(settings, kSearchMatchSeed).has_value()) << where;

            auto playerOf = [&](Seat seat) { return seat == kSearchSeat ? ComputerPlayer(SeatSeed(kSearchMatchSeed, seat), valued) : ComputerPlayer(SeatSeed(kSearchMatchSeed, seat)); };
            Array<ComputerPlayer, kSeatCount> players{playerOf(0), playerOf(1), playerOf(2), playerOf(3)};

            MatchTotals totals;
            MatchAudit audit(settings, where, totals);
            int steps = 0;
            int valuedSteps = 0;
            while (match.GetPhase() == MatchPhase::InHand) {
                ASSERT_LT(audit.HandIndex(), kHandLimit) << where << ": the match does not end";
                ASSERT_NO_FATAL_FAILURE(audit.BeforeHand(match));
                ASSERT_NO_FATAL_FAILURE(StepMixedHand(players, match.Hand(), Format("{} hand {}", where, audit.HandIndex()), steps, valuedSteps));
                const auto ended = match.EndHand();
                ASSERT_TRUE(ended.has_value()) << where << ": " << Name(ended.error());
                ASSERT_NO_FATAL_FAILURE(audit.AfterHand(match, ended.value()));
            }
            ASSERT_NO_FATAL_FAILURE(audit.AtEnd(match));
            EXPECT_EQ(match.HandsPlayed(), kSearchMatchHands) << where;
            EXPECT_GT(valuedSteps, 0) << "the own-draws seat never acted";
            if (scores != nullptr) {
                *scores = match.Scores();
            }
        }

    }  // namespace

    TEST(ComputerMatchTest, OneOwnDrawsSeatAmongThreeNormalSeatsPlaysAShortMatchLegally) {
        LevelProfile valued = kNormalProfile;
        valued.ownDrawsDepth = kOwnDrawsTestDepth;
        valued.ownDrawsPickWait = true;
        PlayOwnDrawsMatch(valued, "own-draws match");
    }

    TEST(ComputerMatchTest, OneOwnDrawsSeatThatCountsTheWinsOnADiscardPlaysAShortMatchLegally) {
        LevelProfile valued = kNormalProfile;
        valued.ownDrawsDepth = kOwnDrawsTestDepth;
        valued.ownDrawsPickWait = true;
        valued.ownDrawsDiscardWins = true;
        PlayOwnDrawsMatch(valued, "own-draws match with the wins on a discard");
    }

    TEST(ComputerMatchTest, OneSeatThatValuesItsClaimsPlaysAShortMatchLegally) {
        LevelProfile valued = kNormalProfile;
        valued.ownDrawsClaimDepth = kOwnDrawsTestDepth;
        PlayOwnDrawsMatch(valued, "own-draws claims match");
    }

    TEST(ComputerMatchTest, OneSeatThatDeclaresNowOrLaterPlaysAShortMatchLegally) {
        LevelProfile valued = kNormalProfile;
        valued.ownDrawsDeclareDepth = kOwnDrawsTestDepth;
        PlayOwnDrawsMatch(valued, "own-draws declaration match");
    }

    TEST(ComputerMatchTest, OneWeightedSeatAmongThreeNormalSeatsPlaysLegallyForAnyWeightsAndTheSameMatchTwice) {
        MahjongRng weights(kWeightsSeed);
        Vector<LevelProfile> profiles;
        profiles.push_back(Weighted(kNormalProfile));
        profiles.push_back(WeightedAt(0));
        profiles.push_back(WeightedAt(kWeightMax));
        for (int i = 0; i < kRandomWeightSets; ++i) {
            profiles.push_back(RandomWeights(weights));
        }
        for (size_t index = 0; index < profiles.size(); ++index) {
            const String name = Format("weighted match {}", index);
            SeatScores first{};
            SeatScores again{};
            ASSERT_NO_FATAL_FAILURE(PlayOwnDrawsMatch(profiles[index], name.c_str(), &first));
            ASSERT_NO_FATAL_FAILURE(PlayOwnDrawsMatch(profiles[index], name.c_str(), &again));
            EXPECT_EQ(first, again) << name;
        }
    }

    namespace {

        /// A weighted profile of the default eight weights with the two value weights set and the horizon of both.
        LevelProfile ValuedWeighted(int valueDiscard, int valueClaim, int depth) {
            LevelProfile profile = Weighted(kNormalProfile);
            profile.wValueDiscard = valueDiscard;
            profile.wValueClaim = valueClaim;
            profile.weightedValueDepth = depth;
            return profile;
        }

        void PlayValuedTwice(const LevelProfile& profile, const String& name) {
            SeatScores first{};
            SeatScores again{};
            ASSERT_NO_FATAL_FAILURE(PlayOwnDrawsMatch(profile, name.c_str(), &first));
            ASSERT_NO_FATAL_FAILURE(PlayOwnDrawsMatch(profile, name.c_str(), &again));
            EXPECT_EQ(first, again) << name;
        }

    }  // namespace

    // The value terms call `ValueDiscards` / `ValueClaims` on every decision of the seat: the matches here are about 15 s more of Debug time
    // than the weighted test above (depth 1: the weights of each term alone and together; depth 2: one case).
    TEST(ComputerMatchTest, OneWeightedSeatWithTheValueWeightsPlaysLegallyAndTheSameMatchTwice) {
        for (const int weight : kValueWeights) {
            PlayValuedTwice(ValuedWeighted(weight, 0, kValueTestDepth), Format("discard value {} at depth {}", weight, kValueTestDepth));
            PlayValuedTwice(ValuedWeighted(0, weight, kValueTestDepth), Format("claim value {} at depth {}", weight, kValueTestDepth));
        }
        PlayValuedTwice(ValuedWeighted(kValueWeights[1], kValueWeights[1], kValueDeeperTestDepth), Format("both values at depth {}", kValueDeeperTestDepth));
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
