#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/Match.h>
#include <gtest/gtest.h>
#include <set>

/**
 * `ComputerPlayer` and `ComputerTable`: `Decide` (empty for a seat with nothing to do, an element of the list otherwise), `Choose` on
 * both kinds of phase, `SeatSeed`, `NextActor` (the discard, the first of a window, a finished hand), `Step` (the action it applied,
 * `WrongPhase` on a finished hand), the same seed the same actions on the same model, and a hand played by four players on a fixed wall
 * to a win that needs a declaration two turns earlier. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeed = 7;

        // East waits on the red dragon, declares with the 9p it draws and draws the red dragon a lap later.
        constexpr const char* kWaitingEast = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";
        constexpr const char* kWinningLive = "9p Wd Gd Nw Rd";

        HandModel WinningWallModel() {
            return MakeModel(SpecOf(kWaitingEast, kWinningLive));
        }

        // East throws a 5p; seat 1 may claim a chow of it (4p 6p) and seat 3 a pung (two 5p): a window with two pending seats, seat 2 is
        // not in it.
        HandModel WindowOfTwoModel() {
            FixedWallSpec spec = SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "5p");
            spec.hands[kSeat1] = "4p 6p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            spec.hands[kSeat3] = "5p 5p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

    }  // namespace

    // ----- The level -----

    TEST(ComputerPlayerTest, ThePlayerAndTheTableReportTheirLevelAndTheDefaultIsNormal) {
        EXPECT_EQ(ComputerPlayer(kSeed).Level(), ComputerLevel::Normal);
        EXPECT_EQ(ComputerTable(kSeed).LevelOf(kSeat0), ComputerLevel::Normal);
        EXPECT_EQ(ComputerPlayer(kSeed, ComputerLevel::Easy).Level(), ComputerLevel::Easy);
        EXPECT_EQ(ComputerPlayer(kSeed, ComputerLevel::Normal).Level(), ComputerLevel::Normal);
        EXPECT_EQ(ComputerTable(kSeed, ComputerLevel::Easy).LevelOf(kSeat0), ComputerLevel::Easy);
        EXPECT_EQ(ComputerTable(kSeed, ComputerLevel::Normal).LevelOf(kSeat0), ComputerLevel::Normal);
    }

    // ----- Decide and Choose -----

    TEST(ComputerPlayerTest, DecideIsEmptyForASeatWithNothingToDoAndForANumberThatIsNoSeat) {
        const HandModel model = WinningWallModel();
        ComputerPlayer player(kSeed);

        EXPECT_TRUE(player.Decide(model, kSeat0).has_value());
        EXPECT_FALSE(player.Decide(model, kSeat1).has_value());
        EXPECT_FALSE(player.Decide(model, kSeat3).has_value());
        EXPECT_FALSE(player.Decide(model, kNoSeat).has_value());
        EXPECT_FALSE(player.Decide(model, kSeatCount).has_value());
        EXPECT_FALSE(player.Decide(HandModel{}, kSeat0).has_value());
    }

    TEST(ComputerPlayerTest, DecideAnswersWithAnElementOfTheListOnTheTurnAndInAWindow) {
        const HandModel turn = WinningWallModel();
        ComputerPlayer player(kSeed);
        const std::optional<HandAction> onTurn = player.Decide(turn, kSeat0);
        ASSERT_TRUE(onTurn.has_value());
        EXPECT_TRUE(Has(turn.LegalActions(kSeat0), *onTurn));

        const HandModel window = WindowOfTwoModel();
        for (const Seat seat : window.PendingClaimSeats()) {
            const std::optional<HandAction> answer = player.Decide(window, seat);
            ASSERT_TRUE(answer.has_value()) << "seat " << seat;
            EXPECT_TRUE(Has(window.LegalActions(seat), *answer)) << "seat " << seat;
        }
        EXPECT_FALSE(player.Decide(window, kSeat0).has_value()) << "the discarder has nothing to do while the window is open";
    }

    TEST(ComputerPlayerTest, ChooseSendsTheViewToTheRuleOfItsPhase) {
        const HandModel turn = WinningWallModel();
        const HandModel window = WindowOfTwoModel();
        const TableView turnView = MakeTableView(turn, kSeat0);
        const TableView windowView = MakeTableView(window, kSeat1);
        ComputerPlayer player(kSeed);
        MahjongRng rng(kSeed);

        EXPECT_EQ(player.Choose(turnView), ChooseTurnAction(turnView, rng));
        const HandAction claim = player.Choose(windowView);
        EXPECT_TRUE(claim.kind == ActionKind::Claim || claim.kind == ActionKind::Pass);
    }

    // ----- the seeds -----

    TEST(ComputerPlayerTest, SeatSeedsDifferBetweenSeatsAndBetweenTablesAndRepeat) {
        std::set<uint32_t> seen;
        for (uint32_t table = 0; table < 50; ++table) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(SeatSeed(table, seat), SeatSeed(table, seat));
                seen.insert(SeatSeed(table, seat));
            }
        }
        EXPECT_EQ(seen.size(), static_cast<size_t>(50 * kSeatCount)) << "two seats of the tables share a seed";
        // The mixing is fixed: this value would change if the rule (or the rng) did.
        EXPECT_EQ(SeatSeed(1, kSeat2), MahjongRng(1 * kSeatCount + kSeat2).Below(std::numeric_limits<uint32_t>::max()));
    }

    // ----- NextActor and Step -----

    TEST(ComputerPlayerTest, NextActorIsTheSeatToDiscardTheFirstOfAWindowAndNoOneWhenFinished) {
        const HandModel turn = WinningWallModel();
        EXPECT_EQ(ComputerTable::NextActor(turn), kSeat0);

        const HandModel window = WindowOfTwoModel();
        ASSERT_EQ(window.PendingClaimSeats(), (eastl::vector<Seat>{kSeat1, kSeat3}));
        EXPECT_EQ(ComputerTable::NextActor(window), kSeat1);

        EXPECT_EQ(ComputerTable::NextActor(HandModel{}), kNoSeat) << "a model never begun is a finished hand";
    }

    TEST(ComputerPlayerTest, NextActorMovesOnWhenTheFirstSeatOfAWindowHasDecided) {
        HandModel window = WindowOfTwoModel();
        ASSERT_TRUE(window.Pass(kSeat1).has_value());
        EXPECT_EQ(ComputerTable::NextActor(window), kSeat3);
    }

    TEST(ComputerPlayerTest, StepOnAFinishedHandIsWrongPhaseAndChangesNothing) {
        HandModel finished;
        ComputerTable table(kSeed);

        const StepResult step = table.Step(finished);

        ASSERT_FALSE(step.has_value());
        EXPECT_EQ(step.error(), HandError::WrongPhase);
        EXPECT_EQ(finished.GetPhase(), Phase::Finished);
    }

    TEST(ComputerPlayerTest, StepAppliesTheActionItReturns) {
        HandModel model = WinningWallModel();
        HandModel replay = model;
        ComputerTable table(kSeed);

        const StepResult step = table.Step(model);

        ASSERT_TRUE(step.has_value());
        ASSERT_TRUE(replay.Apply(kSeat0, *step).has_value());
        EXPECT_EQ(Snapshot(model), Snapshot(replay));
    }

    TEST(ComputerPlayerTest, TheSameTableSeedGivesTheSameActionsOnTheSameModel) {
        // A claim window decided by the rng at times: play whole hands from the same model with two tables of one seed.
        for (uint32_t seed = 1; seed <= 6; ++seed) {
            Match match;
            ASSERT_TRUE(match.Start(MahjongSettings{}, seed).has_value());
            HandModel first = match.Hand();
            HandModel second = match.Hand();
            ComputerTable tableA(seed);
            ComputerTable tableB(seed);
            while (first.GetPhase() != Phase::Finished) {
                const StepResult a = tableA.Step(first);
                const StepResult b = tableB.Step(second);
                ASSERT_TRUE(a.has_value());
                ASSERT_TRUE(b.has_value());
                ASSERT_EQ(*a, *b) << "seed " << seed;
            }
            EXPECT_EQ(second.GetPhase(), Phase::Finished);
            EXPECT_EQ(Snapshot(first), Snapshot(second));
        }
    }

    // ----- a hand to a win -----

    TEST(ComputerPlayerTest, FourPlayersPlayAHandToTheWinTheFirstOneDeclaredTwoTurnsEarlier) {
        HandModel model = WinningWallModel();
        ComputerTable table(kSeed);
        eastl::vector<HandAction> eastActions;

        for (int steps = 0; model.GetPhase() != Phase::Finished; ++steps) {
            ASSERT_LT(steps, 100);
            const Seat actor = ComputerTable::NextActor(model);
            const StepResult step = table.Step(model);
            ASSERT_TRUE(step.has_value()) << Name(step.error());
            if (actor == kSeat0) {
                eastActions.push_back(*step);
            }
        }

        ASSERT_TRUE(model.Result().has_value());
        const HandResult& result = *model.Result();
        EXPECT_EQ(result.kind, ResultKind::Win);
        EXPECT_EQ(result.winner, kSeat0);
        EXPECT_EQ(result.source, WinSource::SelfDraw);
        EXPECT_EQ(result.winningTile, T("Rd"));
        ASSERT_EQ(eastActions.size(), 2u) << "East discards once (with the declaration) and wins";
        EXPECT_EQ(eastActions[0], HandAction::MakeDiscard(T("9p"), true));
        EXPECT_EQ(eastActions[1], HandAction::MakeDeclareWin());
    }

    TEST(ComputerPlayerTest, PlayHandPlaysAHandToTheEndAndIsANoOpOnAFinishedHand) {
        ComputerTable table(kSeed);
        HandModel model = WinningWallModel();
        ASSERT_TRUE(table.PlayHand(model).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Finished);

        const HandStatus again = table.PlayHand(model);
        EXPECT_TRUE(again.has_value()) << "a finished hand is already played";
    }

    TEST(ComputerPlayerTest, PlayMatchPlaysEveryHandOfAMatchAndLeavesItFinished) {
        MahjongSettings settings;
        settings.handsPerMatch = kShortMatchHands;
        Match match;
        ASSERT_TRUE(match.Start(settings, kSeed).has_value());
        ComputerTable table(kSeed);

        ASSERT_TRUE(table.PlayMatch(match).has_value());

        EXPECT_EQ(match.GetPhase(), MatchPhase::Finished);
        EXPECT_EQ(match.HandsPlayed(), kShortMatchHands);
        EXPECT_EQ(Sum(match.Scores()), kSeatCount * kExpectedStartingScore);
    }

}  // namespace BECore::Mahjong::Tests
