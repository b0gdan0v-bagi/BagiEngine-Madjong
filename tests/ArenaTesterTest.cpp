#include "ArenaLogTestSupport.h"
#include "HandTestSupport.h"

#include <Mahjong/Analyzer/AnalyzerReport.h>
#include <Mahjong/Arena/ArenaTester.h>
#include <gtest/gtest.h>

/**
 * The person's seat against the computer levels (`Arena/ArenaTester.h`): every decision of a person in a replayed log is turned into the
 * view of the seat and judged by bots that see that view and nothing else. Pure logic: matches are played in memory, a person is a
 * `ComputerPlayer` of a known level.
 */
namespace BECore::Mahjong::Arena {

    namespace {

        using namespace BECore::Mahjong::Tests;

        constexpr uint32_t kSeedA = 41;
        constexpr uint32_t kSeedB = 77;
        constexpr int kRngSamples = 8;
        constexpr uint32_t kNormalLevelSeed = 5;
        constexpr const char* kLogName = "tester.jsonl";
        constexpr double kShareTolerance = 1e-9;
        // A person played by Normal at seat 0 of the match of seed 41 (a short match), judged by the Normal bot: the decisions with a choice
        // and the ones that agree. The rest are ties that Normal breaks by its rng (the person's rng and the tester's are not the same stream).
        constexpr int64_t kPinnedDecisions = 90;
        constexpr int64_t kPinnedEqual = 82;

        ArenaPlayer Bot(const char* spec) {
            const std::expected<ArenaPlayer, eastl::string> player = ParseArenaPlayer(spec);
            EXPECT_TRUE(player.has_value());
            return player.has_value() ? *player : ArenaPlayer{};
        }

        LogSpec PersonsAt(SeatSetup seats, ComputerLevel level, uint32_t seed = kSeedA) {
            LogSpec spec;
            spec.seed = seed;
            spec.seats = seats;
            spec.levels = UniformLevels(level);
            return spec;
        }

        TesterMatch Analysed(const eastl::vector<MatchLogEvent>& events, const eastl::vector<ArenaPlayer>& bots) {
            const std::expected<TesterMatch, eastl::string> match = AnalyzeTesterMatch(kLogName, events, bots);
            EXPECT_TRUE(match.has_value()) << (match.has_value() ? "" : match.error().c_str());
            return match.has_value() ? *match : TesterMatch{};
        }

        /// Every decision of a person in `events` with its position, in (hand, seat) order: the list the tests compare the analysis with.
        eastl::vector<Analyzer::LogPosition> PersonPositions(const eastl::vector<MatchLogEvent>& events) {
            eastl::vector<Analyzer::LogPosition> positions;
            const int hands = CountEvents(events, MatchLogKind::Settlement) + 1;
            for (int hand = 1; hand <= hands; ++hand) {
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    if (events.front().seats[static_cast<size_t>(seat)] != SeatKind::Person) {
                        continue;
                    }
                    const auto found = Analyzer::PositionsOfSeat(events, hand, seat);
                    if (found.has_value()) {
                        positions.insert(positions.end(), found->begin(), found->end());
                    }
                }
            }
            return positions;
        }

        int64_t Total(const TesterMatch& match, size_t bot) {
            int64_t sum = 0;
            for (const PhaseCount& count : match.bots[bot]) {
                sum += count.decisions;
            }
            return sum;
        }

        int64_t Equal(const TesterMatch& match, size_t bot) {
            int64_t sum = 0;
            for (const PhaseCount& count : match.bots[bot]) {
                sum += count.equal;
            }
            return sum;
        }

    }  // namespace

    TEST(ArenaTesterTest, ThePhasesOfADecisionAreTheTurnAndTheClaimWindows) {
        EXPECT_EQ(TesterPhaseOf(Phase::Discard), TesterPhase::DiscardTurn);
        EXPECT_EQ(TesterPhaseOf(Phase::Claim), TesterPhase::ClaimWindow);
        EXPECT_EQ(TesterPhaseOf(Phase::RobKong), TesterPhase::ClaimWindow);
    }

    TEST(ArenaTesterTest, TheDecisionSeedIsAFunctionOfThePlaceOnly) {
        // Pinned: the same on every platform (FNV-1a over the name and the two numbers).
        EXPECT_EQ(DecisionSeed("", 0, 0), 2615243109U);
        EXPECT_EQ(DecisionSeed("a.jsonl", 3, 7), 278162928U);
        EXPECT_EQ(DecisionSeed(kLogName, 1, 0), DecisionSeed(kLogName, 1, 0));
        EXPECT_NE(DecisionSeed(kLogName, 1, 0), DecisionSeed(kLogName, 1, 1));
        EXPECT_NE(DecisionSeed(kLogName, 1, 0), DecisionSeed(kLogName, 2, 0));
        EXPECT_NE(DecisionSeed(kLogName, 1, 0), DecisionSeed("other.jsonl", 1, 0));
        EXPECT_NE(DecisionSeed(kLogName, 1, 2), DecisionSeed(kLogName, 2, 1));
    }

    TEST(ArenaTesterTest, TheBotsMustBeComputerPlayers) {
        EXPECT_TRUE(ValidateTesterBots({Bot("normal"), Bot("easy:skipClaimPercent=20"), Bot("normal:searchWorlds=2,searchCandidates=2,searchMargin=0")}).empty());
        EXPECT_FALSE(ValidateTesterBots({}).empty());
        const eastl::string random = ValidateTesterBots({Bot("normal"), Bot("random")});
        EXPECT_NE(random.find("random"), eastl::string::npos);
    }

    TEST(ArenaTesterTest, ADecisionWithOneLegalActionIsForcedAndNotAsked) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogPersonFirst, ComputerLevel::Normal));
        const eastl::vector<Analyzer::LogPosition> positions = PersonPositions(events);
        ASSERT_FALSE(positions.empty());
        TableView view = positions.front().view;
        ASSERT_GT(view.legal.size(), 1U);
        const HandAction only = view.legal.front();
        view.legal = {only};
        const TesterChoice forced = AnalyzeTesterDecision(view, only, Bot("normal"), 1);
        EXPECT_TRUE(forced.forced);
        EXPECT_FALSE(forced.equal);
        const TesterChoice asked = AnalyzeTesterDecision(positions.front().view, positions.front().logged, Bot("normal"), 1);
        EXPECT_FALSE(asked.forced);
        EXPECT_EQ(asked.equal, asked.chosen == positions.front().logged);
    }

    TEST(ArenaTesterTest, EveryDecisionWithAChoiceIsCountedOnceAndNothingElse) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogPersonFirst, ComputerLevel::Normal));
        int64_t choices = 0;
        int64_t discards = 0;
        for (const Analyzer::LogPosition& position : PersonPositions(events)) {
            if (position.view.legal.size() >= 2) {
                ++choices;
                discards += position.view.phase == Phase::Discard ? 1 : 0;
            }
        }
        const TesterMatch match = Analysed({events}, {Bot("normal")});
        EXPECT_EQ(Total(match, 0), choices);
        EXPECT_EQ(match.bots[0][static_cast<size_t>(TesterPhase::DiscardTurn)].decisions, discards);
        EXPECT_EQ(match.bots[0][static_cast<size_t>(TesterPhase::ClaimWindow)].decisions, choices - discards);
        EXPECT_GT(discards, 0);
        EXPECT_GT(choices - discards, 0) << "a person is asked in claim windows too";
    }

    TEST(ArenaTesterTest, APersonPlayedByNormalAgreesWithNormalWhereTheTieBreakDoesNotMatter) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogPersonFirst, ComputerLevel::Normal));
        const ArenaPlayer normal = Bot("normal");
        int64_t fixed = 0;
        for (const Analyzer::LogPosition& position : PersonPositions(events)) {
            if (position.view.legal.size() < 2) {
                continue;
            }
            // The decision does not depend on the rng when every tie-break seed gives the same choice.
            eastl::vector<HandAction> answers;
            for (uint32_t seed = 0; seed < kRngSamples; ++seed) {
                const HandAction chosen = AnalyzeTesterDecision(position.view, position.logged, normal, seed + kNormalLevelSeed).chosen;
                if (!Has(answers, chosen)) {
                    answers.push_back(chosen);
                }
            }
            if (answers.size() == 1) {
                ++fixed;
                EXPECT_EQ(answers.front(), position.logged) << "hand " << position.hand << " action " << position.actionIndex;
            }
        }
        EXPECT_GT(fixed, 0);
        const TesterMatch match = Analysed(events, {normal});
        EXPECT_GE(Equal(match, 0), fixed);
    }

    TEST(ArenaTesterTest, APersonPlayedByALowerLevelShowsDifferencesThatNameTheDecision) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogPersonFirst, ComputerLevel::Easy));
        const TesterMatch match = Analysed(events, {Bot("normal")});
        ASSERT_FALSE(match.differences.empty());
        EXPECT_EQ(static_cast<int64_t>(match.differences.size()), Total(match, 0) - Equal(match, 0));
        for (size_t index = 1; index < match.differences.size(); ++index) {
            const TesterDifference& before = match.differences[index - 1];
            const TesterDifference& after = match.differences[index];
            EXPECT_TRUE(before.hand < after.hand || (before.hand == after.hand && before.actionIndex < after.actionIndex)) << "log order, each decision once";
        }
        // Each difference is the decision the analyzer finds at that hand and action, and the bot's choice is what the view gives.
        for (const TesterDifference& difference : match.differences) {
            const auto position = Analyzer::PositionFromLog(events, difference.hand, difference.actionIndex);
            ASSERT_TRUE(position.has_value()) << position.error().c_str();
            EXPECT_EQ(position->seat, difference.seat);
            EXPECT_EQ(position->logged, difference.logged);
            EXPECT_EQ(difference.log, kLogName);
            EXPECT_NE(difference.chosen, difference.logged);
            const TesterChoice again = AnalyzeTesterDecision(position->view, position->logged, Bot("normal"), DecisionSeed(kLogName, difference.hand, difference.actionIndex));
            EXPECT_EQ(again.chosen, difference.chosen);
            EXPECT_TRUE(Has(position->view.legal, difference.chosen));
        }
    }

    TEST(ArenaTesterTest, TheFirstBotIsTheReferenceOfTheDifferences) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogPersonFirst, ComputerLevel::Easy));
        const TesterMatch normalFirst = Analysed(events, {Bot("normal"), Bot("easy")});
        const TesterMatch easyFirst = Analysed(events, {Bot("easy"), Bot("normal")});
        EXPECT_EQ(static_cast<int64_t>(normalFirst.differences.size()), Total(normalFirst, 0) - Equal(normalFirst, 0));
        EXPECT_EQ(static_cast<int64_t>(easyFirst.differences.size()), Total(easyFirst, 0) - Equal(easyFirst, 0));
        EXPECT_EQ(normalFirst.bots[0], easyFirst.bots[1]);
        EXPECT_EQ(normalFirst.bots[1], easyFirst.bots[0]);
        // The person is Easy: Easy agrees more than Normal does.
        EXPECT_GT(Equal(normalFirst, 1), Equal(normalFirst, 0));
    }

    TEST(ArenaTesterTest, TwoPersonsAtOneTableAreBothAnalysed) {
        const SeatSetup twoPersons{SeatKind::Person, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        const eastl::vector<MatchLogEvent> both = PlayLog(PersonsAt(twoPersons, ComputerLevel::Easy));
        int64_t choices = 0;
        int64_t second = 0;
        for (const Analyzer::LogPosition& position : PersonPositions(both)) {
            if (position.view.legal.size() >= 2) {
                ++choices;
                second += position.seat == 1 ? 1 : 0;
            }
        }
        EXPECT_GT(second, 0);
        const TesterMatch match = Analysed(both, {Bot("normal")});
        EXPECT_EQ(Total(match, 0), choices);
        bool seatOneDiffers = false;
        for (const TesterDifference& difference : match.differences) {
            seatOneDiffers = seatOneDiffers || difference.seat == 1;
        }
        EXPECT_TRUE(seatOneDiffers);
    }

    TEST(ArenaTesterTest, ALogWithNoPersonGivesNoDecisionsAndNoError) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogAllComputers, ComputerLevel::Normal));
        const std::expected<TesterMatch, eastl::string> match = AnalyzeTesterMatch(kLogName, events, {Bot("normal"), Bot("easy")});
        ASSERT_TRUE(match.has_value());
        ASSERT_EQ(match->bots.size(), 2U);
        EXPECT_EQ(Total(*match, 0), 0);
        EXPECT_EQ(Total(*match, 1), 0);
        EXPECT_TRUE(match->differences.empty());
    }

    TEST(ArenaTesterTest, ALogThatCannotBeTurnedIntoPositionsIsAnErrorThatNamesIt) {
        eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogPersonFirst, ComputerLevel::Normal));
        for (MatchLogEvent& event : events) {
            if (event.kind == MatchLogKind::Action) {
                event.action = HandAction::MakeDeclareWin();
                break;
            }
        }
        const std::expected<TesterMatch, eastl::string> match = AnalyzeTesterMatch(kLogName, events, {Bot("normal")});
        EXPECT_FALSE(match.has_value());
        EXPECT_EQ(std::string(match.error().c_str()).rfind(kLogName, 0), size_t{0});
    }

    TEST(ArenaTesterTest, TwoPositionsWithTheSameViewAndDifferentHiddenTilesGiveTheSameChoice) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogPersonFirst, ComputerLevel::Normal, kSeedB));
        // The model at the first decision of the person: the log up to its action, replayed.
        size_t first = 0;
        while (first < events.size() && !(events[first].kind == MatchLogKind::Action && events[first].by == SeatKind::Person)) {
            ++first;
        }
        ASSERT_LT(first, events.size());
        Match match;
        ASSERT_TRUE(ReplayMatchLog(eastl::vector<MatchLogEvent>(events.begin(), events.begin() + static_cast<ptrdiff_t>(first)), match).has_value());
        const Seat seat = events[first].seat;
        HandState state = match.Hand().Capture();
        // Swap a concealed tile of another seat with a live-wall tile of another kind: what the person sees stays.
        const Seat other = (seat + 1) % kSeatCount;
        bool swapped = false;
        for (size_t h = 0; h < state.hands[static_cast<size_t>(other)].concealed.size() && !swapped; ++h) {
            for (size_t l = 0; l < state.live.size() && !swapped; ++l) {
                const Tile hidden = state.hands[static_cast<size_t>(other)].concealed[h];
                if (hidden == state.live[l]) {
                    continue;
                }
                ASSERT_TRUE(state.hands[static_cast<size_t>(other)].RemoveConcealed(hidden));
                state.hands[static_cast<size_t>(other)].AddConcealed(state.live[l]);
                state.live[l] = hidden;
                swapped = true;
            }
        }
        ASSERT_TRUE(swapped);
        const std::expected<HandModel, eastl::string> changed = HandModel::Restore(state);
        ASSERT_TRUE(changed.has_value()) << changed.error().c_str();
        ASSERT_NE(Snapshot(match.Hand()), Snapshot(*changed));

        const TableView a = MakeTableView(match.Hand(), seat);
        const TableView b = MakeTableView(*changed, seat);
        ASSERT_EQ(a.legal, b.legal);
        for (const char* spec : {"normal", "easy", "10", "normal:searchWorlds=2,searchCandidates=2,searchMargin=0"}) {
            const TesterChoice one = AnalyzeTesterDecision(a, events[first].action, Bot(spec), DecisionSeed(kLogName, 1, 0));
            const TesterChoice two = AnalyzeTesterDecision(b, events[first].action, Bot(spec), DecisionSeed(kLogName, 1, 0));
            EXPECT_EQ(one.chosen, two.chosen) << spec;
            EXPECT_EQ(one.equal, two.equal) << spec;
            EXPECT_EQ(one.phase, two.phase) << spec;
        }
    }

    TEST(ArenaTesterTest, TheSameLogTwiceGivesTheSameNumbers) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogPersonFirst, ComputerLevel::Easy));
        const eastl::vector<ArenaPlayer> bots{Bot("normal"), Bot("easy:skipClaimPercent=20"), Bot("10")};
        const TesterMatch one = Analysed(events, bots);
        const TesterMatch two = Analysed(events, bots);
        ASSERT_EQ(one.bots.size(), two.bots.size());
        for (size_t bot = 0; bot < one.bots.size(); ++bot) {
            EXPECT_EQ(one.bots[bot], two.bots[bot]);
        }
        ASSERT_EQ(one.differences.size(), two.differences.size());
        for (size_t index = 0; index < one.differences.size(); ++index) {
            EXPECT_EQ(one.differences[index].hand, two.differences[index].hand);
            EXPECT_EQ(one.differences[index].actionIndex, two.differences[index].actionIndex);
            EXPECT_EQ(one.differences[index].chosen, two.differences[index].chosen);
        }
    }

    TEST(ArenaTesterTest, TheAgreementOfAMatchIsReadAsTheMatchNotTheDecision) {
        // Match one: 1 of 1 discards equal. Match two: 1 of 9. Match three: no discard decision at all (left out of that row).
        TesterMatch one;
        one.bots = {PhaseCounts{PhaseCount{1, 1}, PhaseCount{0, 0}}};
        TesterMatch two;
        two.bots = {PhaseCounts{PhaseCount{9, 1}, PhaseCount{1, 1}}};
        TesterMatch three;
        three.bots = {PhaseCounts{PhaseCount{0, 0}, PhaseCount{2, 1}}};
        two.differences.push_back({"b.jsonl", 2, 3, 1, HandAction::MakePass(), HandAction::MakeDeclareWin()});
        one.differences.push_back({"a.jsonl", 1, 0, 0, HandAction::MakePass(), HandAction::MakeDeclareWin()});

        const TesterResult result = MergeTesterMatches({Bot("normal")}, {one, two, three});
        EXPECT_EQ(result.matches, 3);
        ASSERT_EQ(result.bots.size(), 1U);
        const BotAgreement& agreement = result.bots[0];
        EXPECT_EQ(agreement.decisions[static_cast<size_t>(TesterPhase::DiscardTurn)], 10);
        EXPECT_EQ(agreement.equal[static_cast<size_t>(TesterPhase::DiscardTurn)], 2);
        EXPECT_EQ(agreement.decisions[kTesterBothRow], 13);
        EXPECT_EQ(agreement.equal[kTesterBothRow], 4);

        const MeanInterval& discards = agreement.share[static_cast<size_t>(TesterPhase::DiscardTurn)];
        EXPECT_EQ(discards.n, 2U) << "the match with no discard decision is not a sample";
        EXPECT_NEAR(discards.mean, (100.0 + 100.0 / 9.0) / 2.0, kShareTolerance);
        const MeanInterval expected = MeanCi({100.0, 100.0 / 9.0});
        EXPECT_NEAR(discards.half, expected.half, kShareTolerance);
        EXPECT_TRUE(discards.hasInterval);

        const MeanInterval& claims = agreement.share[static_cast<size_t>(TesterPhase::ClaimWindow)];
        EXPECT_EQ(claims.n, 2U);
        EXPECT_NEAR(claims.mean, (100.0 + 50.0) / 2.0, kShareTolerance);
        const MeanInterval& both = agreement.share[kTesterBothRow];
        EXPECT_EQ(both.n, 3U);
        EXPECT_NEAR(both.mean, (100.0 + 20.0 + 50.0) / 3.0, kShareTolerance);

        // The differences keep the order of the matches.
        ASSERT_EQ(result.differences.size(), 2U);
        EXPECT_EQ(result.differences[0].log, "a.jsonl");
        EXPECT_EQ(result.differences[1].log, "b.jsonl");
        EXPECT_EQ(MergeTesterMatches({Bot("normal")}, {}).bots[0].share[kTesterBothRow].n, 0U);
    }

    TEST(ArenaTesterTest, AMatchOfTheKnownPlayersHasPinnedNumbers) {
        const eastl::vector<MatchLogEvent> events = PlayLog(PersonsAt(kLogPersonFirst, ComputerLevel::Normal));
        const TesterMatch match = Analysed(events, {Bot("normal")});
        EXPECT_EQ(Total(match, 0), kPinnedDecisions);
        EXPECT_EQ(Equal(match, 0), kPinnedEqual);
        EXPECT_EQ(match.differences.size(), static_cast<size_t>(kPinnedDecisions - kPinnedEqual));
    }

}  // namespace BECore::Mahjong::Arena
