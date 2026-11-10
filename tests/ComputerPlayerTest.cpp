#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/Shanten.h>
#include <gtest/gtest.h>
#include <set>

/**
 * `ComputerPlayer` and `ComputerTable`: `Decide` (empty for a seat with nothing to do, an element of the list otherwise), `Choose` on
 * both kinds of phase, `SeatSeed`, `NextActor` (the discard, the first of a window, a finished hand), `Step` (the action it applied,
 * `WrongPhase` on a finished hand), the same seed the same actions on the same model, and a hand played by four players on a fixed wall
 * to a win that needs a declaration two turns earlier. `Begin` / `BeginStep` / `FinishStep`: a search decision comes back as a run that
 * finishes to what `Choose` gives (a whole hand, so the rng order is held), every other decision comes back ready. Pure logic: no
 * engine services.
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

        constexpr int kSearchWorlds = 1;
        constexpr int kSearchCandidates = 2;
        constexpr uint32_t kHandSeeds = 1;
        constexpr uint32_t kScanSeeds = 300;
        constexpr int kStepLimit = 2000;

        LevelProfile SearchProfile(int worlds = kSearchWorlds, int candidates = kSearchCandidates) {
            LevelProfile profile = kNormalProfile;
            profile.searchWorlds = worlds;
            profile.searchCandidates = candidates;
            return profile;
        }

        HandModel PlayedHandModel(uint32_t seed) {
            Match match;
            EXPECT_TRUE(match.Start(MahjongSettings{}, seed).has_value());
            return match.Hand();
        }

        /// The first view of each kind a search must leave alone, found by playing Normal hands: a win, a mahjong claim, a window of a
        /// waiting hand with something to claim (a window opens only for a seat with a claim, so one with only `Pass` does not occur).
        struct UnsearchedViews {
            std::optional<TableView> win;
            std::optional<TableView> mahjong;
            std::optional<TableView> waitingPass;
        };

        UnsearchedViews ScanForUnsearchedViews() {
            UnsearchedViews found;
            for (uint32_t seed = 1; seed <= kScanSeeds && !(found.win && found.mahjong && found.waitingPass); ++seed) {
                HandModel hand = PlayedHandModel(seed);
                ComputerTable table(seed);
                for (int step = 0; hand.GetPhase() != Phase::Finished && step < kStepLimit; ++step) {
                    const TableView view = MakeTableView(hand, ComputerTable::NextActor(hand));
                    if (view.phase == Phase::Discard && FindAction(view, ActionKind::DeclareWin) != nullptr && !found.win) {
                        found.win = view;
                    }
                    if (view.phase != Phase::Discard) {
                        if (FindMahjong(view) != nullptr) {
                            if (!found.mahjong) {
                                found.mahjong = view;
                            }
                        } else if (Shanten(CountTiles(view.own.concealed), MeldCountOf(view)) <= kWaitingShanten && !found.waitingPass) {
                            found.waitingPass = view;
                        }
                    }
                    if (!table.Step(hand).has_value()) {
                        break;
                    }
                }
            }
            return found;
        }

        /// Plays `model` to its end by `Decide` of four players seeded like a table's: what `ComputerTable::Step` must equal.
        void PlayByDecide(HandModel& model, uint32_t seed, const LevelProfile& profile) {
            eastl::array<ComputerPlayer, kSeatCount> players = {ComputerPlayer(SeatSeed(seed, 0), profile), ComputerPlayer(SeatSeed(seed, 1), profile), ComputerPlayer(SeatSeed(seed, 2), profile),
                                                                ComputerPlayer(SeatSeed(seed, 3), profile)};
            for (int step = 0; model.GetPhase() != Phase::Finished && step < kStepLimit; ++step) {
                const Seat actor = ComputerTable::NextActor(model);
                const std::optional<HandAction> action = players[actor].Decide(model, actor);
                ASSERT_TRUE(action.has_value());
                ASSERT_TRUE(model.Apply(actor, *action).has_value());
            }
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

    // ----- Begin: a search is handed over, the rest comes back ready -----

    TEST(ComputerPlayerTest, BeginHandsASearchOverAndTheRunFinishesToWhatChooseGivesWithTheSameRngAtEveryStep) {
        const LevelProfile profile = SearchProfile();
        int searched = 0;
        int ready = 0;
        for (uint32_t seed = 1; seed <= kHandSeeds; ++seed) {
            HandModel model = PlayedHandModel(seed);
            eastl::array<ComputerPlayer, kSeatCount> begun = {ComputerPlayer(SeatSeed(seed, 0), profile), ComputerPlayer(SeatSeed(seed, 1), profile), ComputerPlayer(SeatSeed(seed, 2), profile),
                                                              ComputerPlayer(SeatSeed(seed, 3), profile)};
            eastl::array<ComputerPlayer, kSeatCount> chosen = begun;
            for (int step = 0; model.GetPhase() != Phase::Finished && step < kStepLimit; ++step) {
                const Seat actor = ComputerTable::NextActor(model);
                const TableView view = MakeTableView(model, actor);
                PlayerDecision decision = begun[actor].Begin(view);
                const HandAction expected = chosen[actor].Choose(view);
                ASSERT_NE(decision.action.has_value(), decision.run.has_value()) << "exactly one of the two is set";
                HandAction answer = HandAction::MakePass();
                if (decision.run) {
                    ++searched;
                    while (!decision.run->Done()) {
                        decision.run->Step();
                    }
                    answer = decision.run->Result();
                } else {
                    ++ready;
                    answer = *decision.action;
                }
                // A rng that stood elsewhere after the draw would show in the tie-breaks of the later decisions of this seat, which are
                // compared in the same way on the next steps.
                ASSERT_EQ(answer, expected) << "seed " << seed << " step " << step;
                ASSERT_TRUE(model.Apply(actor, answer).has_value());
            }
        }
        EXPECT_GT(searched, 0) << "the profile searches some decision of these hands";
        EXPECT_GT(ready, 0);
    }

    TEST(ComputerPlayerTest, BeginAnswersReadyWithNoSearchProfileAndForTheDecisionsASearchLeavesAlone) {
        const UnsearchedViews views = ScanForUnsearchedViews();
        const LevelProfile profile = SearchProfile();
        const eastl::vector<std::pair<const char*, const std::optional<TableView>*>> cases = {
            {"a win", &views.win}, {"a mahjong claim", &views.mahjong}, {"a waiting hand that passes", &views.waitingPass}};
        for (const auto& [name, view] : cases) {
            ASSERT_TRUE(view->has_value()) << name << " was not found";
            ComputerPlayer begun(kSeed, profile);
            ComputerPlayer chosen(kSeed, profile);
            const PlayerDecision decision = begun.Begin(**view);
            ASSERT_TRUE(decision.action.has_value()) << name;
            EXPECT_FALSE(decision.run.has_value()) << name;
            EXPECT_EQ(*decision.action, chosen.Choose(**view)) << name;
            // Nothing was drawn by the one and the other: the next decision of both is the same.
            const TableView next = MakeTableView(WinningWallModel(), kSeat0);
            EXPECT_EQ(begun.Choose(next), chosen.Choose(next)) << name;
        }

        // A single candidate is not searched either.
        const TableView turn = MakeTableView(WinningWallModel(), kSeat0);
        ComputerPlayer one(kSeed, SearchProfile(kSearchWorlds, 1));
        ComputerPlayer oneChosen(kSeed, SearchProfile(kSearchWorlds, 1));
        const PlayerDecision single = one.Begin(turn);
        ASSERT_TRUE(single.action.has_value());
        EXPECT_FALSE(single.run.has_value());
        EXPECT_EQ(*single.action, oneChosen.Choose(turn));

        // And a profile with no search gives a ready action on every step of a hand.
        HandModel model = PlayedHandModel(kSeed);
        ComputerPlayer normalBegun(kSeed);
        ComputerPlayer normalChosen(kSeed);
        for (int step = 0; model.GetPhase() != Phase::Finished && step < kStepLimit; ++step) {
            const Seat actor = ComputerTable::NextActor(model);
            const TableView view = MakeTableView(model, actor);
            const PlayerDecision decision = normalBegun.Begin(view);
            ASSERT_TRUE(decision.action.has_value());
            EXPECT_FALSE(decision.run.has_value());
            ASSERT_EQ(*decision.action, normalChosen.Choose(view)) << "step " << step;
            ASSERT_TRUE(model.Apply(actor, *decision.action).has_value());
        }
    }

    TEST(ComputerPlayerTest, TheTwoHalvesOfAStepPlayAHandAsDecideDoes) {
        const LevelProfile profile = SearchProfile();
        for (uint32_t seed = 1; seed <= kHandSeeds; ++seed) {
            HandModel byHalves = PlayedHandModel(seed);
            HandModel byDecide = PlayedHandModel(seed);
            ComputerTable table(seed, profile);
            int searched = 0;
            for (int step = 0; byHalves.GetPhase() != Phase::Finished && step < kStepLimit; ++step) {
                std::expected<ComputerTable::BegunStep, HandError> begun = table.BeginStep(byHalves);
                ASSERT_TRUE(begun.has_value());
                const HandModel before = byHalves;
                HandAction action = HandAction::MakePass();
                if (begun->decision.run) {
                    ++searched;
                    while (!begun->decision.run->Done()) {
                        begun->decision.run->Step();
                    }
                    action = begun->decision.run->Result();
                } else {
                    action = *begun->decision.action;
                }
                EXPECT_EQ(Snapshot(before), Snapshot(byHalves)) << "BeginStep changes no model";
                const StepResult applied = ComputerTable::FinishStep(byHalves, begun->actor, action);
                ASSERT_TRUE(applied.has_value());
                EXPECT_EQ(*applied, action);
            }
            EXPECT_GT(searched, 0);
            PlayByDecide(byDecide, seed, profile);
            EXPECT_EQ(Snapshot(byHalves), Snapshot(byDecide)) << "seed " << seed;
        }
    }

    TEST(ComputerPlayerTest, StepWithASearchProfileEqualsAHandPlayedByDecide) {
        const LevelProfile profile = SearchProfile();
        for (uint32_t seed = 1; seed <= kHandSeeds; ++seed) {
            HandModel byStep = PlayedHandModel(seed);
            HandModel byDecide = PlayedHandModel(seed);
            ComputerTable table(seed, profile);
            ASSERT_TRUE(table.PlayHand(byStep).has_value());
            PlayByDecide(byDecide, seed, profile);
            EXPECT_EQ(Snapshot(byStep), Snapshot(byDecide)) << "seed " << seed;
        }
    }

    TEST(ComputerPlayerTest, TheHalvesRefuseLikeStep) {
        ComputerTable table(kSeed);
        HandModel finished;
        const std::expected<ComputerTable::BegunStep, HandError> none = table.BeginStep(finished);
        ASSERT_FALSE(none.has_value());
        EXPECT_EQ(none.error(), HandError::WrongPhase);

        HandModel model = WinningWallModel();
        const Seat actor = ComputerTable::NextActor(model);
        const StepResult refused = ComputerTable::FinishStep(model, actor, HandAction::MakePass());
        ASSERT_FALSE(refused.has_value()) << "a pass on the own turn is not listed";
        EXPECT_EQ(std::optional<HandError>(refused.error()), Error(model.Apply(actor, HandAction::MakePass())));
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
