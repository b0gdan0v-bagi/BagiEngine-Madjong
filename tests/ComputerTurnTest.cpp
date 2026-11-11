#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Match.h>
#include <gtest/gtest.h>
#include <set>

/**
 * `ChooseTurnAction`: the choices of a computer player on its own turn, one case per rule, on fixed walls with the expected action
 * worked out by hand first (the waits of each hand, the unseen copies of each wait). Then the helpers (`UnseenCopies`, `KeepValue`,
 * `AcceptanceOf`) and a run over random hands: the answer is always listed, and the same view and rng seed give the same answer.
 * Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kRngSeed = 1;
        constexpr int kRandomHands = 20;
        constexpr int kTurnsPerHand = 30;
        constexpr uint32_t kRandomFirstSeed = 300;

        HandAction Choice(const HandModel& model, Seat seat, uint32_t rngSeed = kRngSeed) {
            const TableView view = MakeTableView(model, seat);
            MahjongRng rng(rngSeed);
            return ChooseTurnAction(view, rng);
        }

        /// Every rng seed from 1 to 8 gives the action `expected`.
        void ExpectChoiceForEverySeed(const HandModel& model, Seat seat, const HandAction& expected) {
            for (uint32_t seed = 1; seed <= 8; ++seed) {
                EXPECT_EQ(Choice(model, seat, seed), expected) << "rng seed " << seed;
            }
        }

        // A hand that waits on 4p and 7p (8 copies) with a pair of east winds; its fourteenth tile is drawn by the test.
        constexpr const char* kTwoSided = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p Ew Ew";

        // ----- the win -----

        // East waits on the red dragon, declares with the 9p it draws and draws the red dragon two laps later.
        HandModel WinningModel() {
            HandModel model = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Rd"));
            Lap(model, "9p Wd Gd Nw", {kSeat0});
            return model;
        }

        // ----- the kongs -----

        // East has declared the waiting `hand` (it draws a Gd on its first turn and discards it) and then draws `fourth`.
        HandModel DeclaredPungModel(const char* hand, const char* fourth) {
            FixedWallSpec spec = SpecOf(hand, std::string("Gd Wd Gd Nw ") + fourth);
            HandModel model = MakeModel(spec);
            Lap(model, "Gd Wd Gd Nw", {kSeat0});
            return model;
        }

        bool IsKong(const HandAction& action) {
            return action.kind == ActionKind::ConcealedKong || action.kind == ActionKind::UpgradeKong;
        }

    }  // namespace

    // ----- 1. the win -----

    TEST(ComputerTurnTest, TakesTheWinWhenItIsListedEvenThoughADiscardThatKeepsTheHandWaitingIsListedToo) {
        const HandModel model = WinningModel();
        ASSERT_EQ(model.CurrentSeat(), kSeat0);
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeDeclareWin()));
        // Discarding the red dragon it just drew keeps the wait: that discard is listed too.
        ASSERT_TRUE(Has(view.legal, HandAction::MakeDiscard(T("Rd"))));

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeDeclareWin());
    }

    // ----- 2. the calling hand -----

    TEST(ComputerTurnTest, DeclaresWithTheDiscardThatLeavesTheMostUnseenWinningCopies) {
        // 1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew Ew: discarding the 5p leaves a pair wait on 6p and Ew (2 + 2 unseen copies), discarding
        // a 6p leaves the two-sided wait on 4p and 7p (4 + 4): that one is declared.
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew"));
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeDiscard(T("5p"), true)));
        ASSERT_TRUE(Has(view.legal, HandAction::MakeDiscard(T("6p"), true)));

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeDiscard(T("6p"), true));
    }

    TEST(ComputerTurnTest, DeclaresTheOtherWaitWhenTheBetterOneHasItsCopiesInThePile) {
        // The same hand two laps on: the pile holds 4p 4p 4p 7p 7p (East's own discards and the seats' draws, none of them claimable), so
        // the two-sided wait has 1 + 2 unseen copies and the pair wait on 6p and Ew still has 2 + 2: the 5p goes.
        FixedWallSpec spec = SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "4p 4p 4p Gd 7p 7p Nw Wd Ew");
        HandModel model = MakeModel(spec);
        Lap(model, "4p 4p 4p Gd 7p 7p Nw Wd");
        ASSERT_EQ(model.CurrentSeat(), kSeat0);
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_EQ(view.visible[T("4p").Id()], 3);
        ASSERT_EQ(view.visible[T("7p").Id()], 2);

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeDiscard(T("5p"), true));
    }

    TEST(ComputerTurnTest, DeclaresASevenPairsWaitThatTheModelOffers) {
        // Six pairs of honours and a green dragon: the only way to complete it is a second green dragon; discarding the 1p it draws
        // declares.
        FixedWallSpec spec;
        spec.hands[kSeat0] = "Ew Ew Sw Sw Ww Ww Nw Nw Rd Rd Wd Wd Gd";
        spec.hands[kSeat1] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Ew Sw Ww Nw";
        spec.hands[kSeat2] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Rd Wd Gd Gd";
        spec.hands[kSeat3] = "4p 7p 4s 7s 4m 7m 1s 1m 9s 9m Rd Wd Gd";
        spec.live = "1p";
        const HandModel model = MakeModel(spec);
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeDiscard(T("1p"), true)));

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeDiscard(T("1p"), true));
    }

    TEST(ComputerTurnTest, AfterTheDeclarationItDiscardsTheDrawnTileAndKeepsTheWait) {
        // East waits on the red dragon (123p 456s 789m Ew Ew Ew Rd), has declared, and draws a white dragon: only that tile can go.
        const FixedWallSpec spec = SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Wd");
        HandModel model = MakeModel(spec);
        Lap(model, "9p Wd Gd Nw", {kSeat0});
        ASSERT_TRUE(model.GetHand(kSeat0).callingDeclared);
        ASSERT_EQ(model.CurrentSeat(), kSeat0);

        const HandAction action = Choice(model, kSeat0);

        EXPECT_EQ(action, HandAction::MakeDiscard(T("Wd")));
    }

    TEST(ComputerTurnTest, AfterTheDeclarationItKeepsTheWaitWithTheMostUnseenCopies) {
        // 123s 456s 789m 5p 6p Ew Ew waits on 4p and 7p; the declared hand draws a second 6p. Discarding the 6p keeps the two-sided
        // wait (8 copies), discarding the 5p turns it into a pair wait on 6p and Ew (2 + 2).
        FixedWallSpec spec = SpecOf(kTwoSided, "Gd Wd Gd Nw 6p");
        HandModel model = MakeModel(spec);
        Lap(model, "Gd Wd Gd Nw", {kSeat0});
        ASSERT_EQ(model.CurrentSeat(), kSeat0);
        ASSERT_TRUE(Has(MakeTableView(model, kSeat0).legal, HandAction::MakeDiscard(T("5p"))));

        const HandAction action = Choice(model, kSeat0);

        EXPECT_EQ(action, HandAction::MakeDiscard(T("6p")));
    }

    // ----- 3. the kongs -----

    TEST(ComputerTurnTest, DeclaresAConcealedKongWhenTheRestIsNoWorse) {
        const HandModel model = MakeModel(SpecOf("Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd", "Ew"));
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeConcealedKong(T("Ew"))));

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeConcealedKong(T("Ew")));
    }

    TEST(ComputerTurnTest, DoesNotDeclareAConcealedKongThatWouldSpoilAWaitingHand) {
        // 4p 4p 4p 5p 1s-6s 7m-9m (declared) waits on 3p, 5p and 6p (4 + 3 + 4 copies); the fourth 4p as a kong would leave the single
        // 5p wait (3 copies). It keeps the wait and discards the 4p.
        const HandModel model = DeclaredPungModel("4p 4p 4p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m", "4p");
        ASSERT_EQ(model.CurrentSeat(), kSeat0);
        ASSERT_TRUE(model.GetHand(kSeat0).callingDeclared);
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeConcealedKong(T("4p"))));
        ASSERT_FALSE(Has(view.legal, HandAction::MakeDeclareWin()));

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeDiscard(T("4p")));
    }

    TEST(ComputerTurnTest, DeclaresAConcealedKongThatKeepsTheWaitOfADeclaredHand) {
        // 7s 7s 7s 123p 456p 789m Ew waits on the east wind alone; the fourth 7s as a kong keeps that wait (4 copies either way).
        const HandModel model = DeclaredPungModel("7s 7s 7s 1p 2p 3p 4p 5p 6p 7m 8m 9m Ew", "7s");
        ASSERT_EQ(model.CurrentSeat(), kSeat0);
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeConcealedKong(T("7s"))));

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeConcealedKong(T("7s")));
    }

    TEST(ComputerTurnTest, UpgradesAnOpenPungWithTheDrawnFourthTile) {
        const HandModel model = OpenPungModel("5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeUpgradeKong(T("5p"))));

        ExpectChoiceForEverySeed(model, kSeat1, HandAction::MakeUpgradeKong(T("5p")));
    }

    TEST(ComputerTurnTest, DoesNotUpgradeAPungWhenTheFourthTileIsPartOfAChow) {
        // Seat 1 holds 4p and 6p: the drawn 5p makes the chow 4p 5p 6p, the kong would give it up.
        const HandModel model = OpenPungModel("5p 5p 4p 6p 2s 5s 8s 2m 5m 8m Ww Nw Wd");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeUpgradeKong(T("5p"))));

        for (uint32_t seed = 1; seed <= 8; ++seed) {
            const HandAction action = Choice(model, kSeat1, seed);
            EXPECT_EQ(action.kind, ActionKind::Discard) << "rng seed " << seed;
            EXPECT_FALSE(action.tile == T("4p") || action.tile == T("5p") || action.tile == T("6p")) << ToString(action.tile);
        }
    }

    TEST(ComputerTurnTest, DoesNotUpgradeAPungWhenTheKongWouldLeaveAWorseWait) {
        // Seat 1: open pung of 5p, 6p 7p 1s 2s 3s 4s 5s 6s 8m 8m, draws the fourth 5p (the 11 tiles are a complete hand, not declared).
        // With the kong it would wait on 5p (dead: all four copies are in the kong) and 8p, 4 unseen; the 1s leaves a better wait (1s, 4s
        // and 7s, 10 unseen) and keeps the pung, so the kong is not taken and the seat declares with the discard.
        const HandModel model = OpenPungModel("5p 5p 6p 7p 1s 2s 3s 4s 5s 6s 8m 8m Wd");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeUpgradeKong(T("5p"))));
        ASSERT_EQ(view.own.melds.size(), 1u);

        ExpectChoiceForEverySeed(model, kSeat1, HandAction::MakeDiscard(T("1s"), true));
    }

    TEST(ComputerTurnTest, DoesNotUpgradeAPungOfADeclaredSeatWhenTheKongWouldLowerTheWait) {
        // Seat 1 is declared and waits (123s 789s 3p 3p 4p 4p, the open pung of 5p, on 3p and 4p). It draws the fourth 5p, which is no win.
        // Discarding the 4p keeps a wait on 3p and 6p with the drawn 5p in the hand (6 unseen); the kong takes the 5p out and leaves a
        // wait of 4 unseen copies, so the acceptance of an upgrade is lower and it is refused.
        const HandModel model = OpenPungModel("5p 5p 1s 2s 3s 7s 8s 9s 3p 3p 4p 4p Wd", kQuiet2, kQuiet3, {kSeat1});
        ASSERT_TRUE(model.GetHand(kSeat1).callingDeclared);
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeUpgradeKong(T("5p"))));
        ASSERT_FALSE(Has(view.legal, HandAction::MakeDeclareWin()));

        ExpectChoiceForEverySeed(model, kSeat1, HandAction::MakeDiscard(T("4p")));
    }

    // ----- 4. the discard -----

    TEST(ComputerTurnTest, DiscardsTheWorthlessHonourBeforeAValueHonourWhenEfficiencyIsEqual) {
        // 123s 456s 789m 5p 5p and three single honours: north wind (worthless), red dragon and east wind (own wind) are equally useful
        // for the hand, so the value decides and the north wind goes.
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew", "Nw"));

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeDiscard(T("Nw")));
    }

    TEST(ComputerTurnTest, TheRoundWindIsAValueHonourToo) {
        // Round wind South: the south wind is kept; the west wind and the north wind are worthless, either of them goes (not the south).
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Sw Ww", "Nw"), kSeat0, Wind::South);
        std::set<std::string> discarded;

        for (uint32_t seed = 1; seed <= 16; ++seed) {
            const HandAction action = Choice(model, kSeat0, seed);
            ASSERT_EQ(action.kind, ActionKind::Discard) << "rng seed " << seed;
            discarded.insert(std::string(ToString(action.tile)));
        }

        EXPECT_EQ(discarded.count("Sw"), 0u);
        EXPECT_GT(discarded.count("Ww") + discarded.count("Nw"), 0u);
    }

    TEST(ComputerTurnTest, KeepsAPairAndTwoTileShapesAndDiscardsTheFloatingTile) {
        // 123s 456s 5p 5p 3m 4m 7p 8p and two floating honours: shanten 1 whichever honour goes; the north wind is worth less than the
        // green dragon.
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 5p 5p 3m 4m 7p 8p Nw", "Gd"));
        const TableView view = MakeTableView(model, kSeat0);
        const DiscardRating rating = BestDiscardRating(CountTiles(view.own.concealed), 0, view);
        ASSERT_EQ(rating.shanten, 1);

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeDiscard(T("Nw")));
    }

    TEST(ComputerTurnTest, ChoosesBetweenTwoTilesOfEqualShantenByTheCopiesStillUnseen) {
        // The single 5p, 5s and 5m beside three pungs of honours and a pair: every one of them leaves shanten 1. The pile holds 4m 6m 3m
        // 7m, the tiles that would join the 5m: the 5p and the 5s keep 19 improving copies each, the 5m 15, so keeping the 5p and the 5s
        // (38) beats keeping the 5m with one of them (34) and the 5m goes.
        FixedWallSpec spec;
        spec.hands[kSeat0] = "Ew Ew Ew Sw Sw Sw Rd Rd Rd Wd Wd 5p 5s";
        spec.hands[kSeat1] = kQuiet1;
        spec.hands[kSeat2] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ww Nw Gd Sw";
        spec.hands[kSeat3] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ww Nw Gd Ew";
        spec.live = "4m 6m 3m 7m 5m";
        HandModel model = MakeModel(spec);
        Lap(model, "4m 6m 3m 7m");
        ASSERT_EQ(model.CurrentSeat(), kSeat0);
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_EQ(view.visible[T("3m").Id()], 1);
        ASSERT_EQ(view.visible[T("5m").Id()], 1);

        ExpectChoiceForEverySeed(model, kSeat0, HandAction::MakeDiscard(T("5m")));
    }

    // ----- the helpers -----

    TEST(ComputerTurnTest, UnseenCopiesAreWhatIsNotVisibleAndNeverBelowZero) {
        TableView view;
        view.visible[T("5p").Id()] = 3;
        view.visible[T("Ew").Id()] = kCopiesPerTile;
        view.visible[T("9m").Id()] = kCopiesPerTile + 1;  // the caller's bug, never a negative answer

        EXPECT_EQ(UnseenCopies(view, T("5p")), 1);
        EXPECT_EQ(UnseenCopies(view, T("Ew")), 0);
        EXPECT_EQ(UnseenCopies(view, T("9m")), 0);
        EXPECT_EQ(UnseenCopies(view, T("1s")), kCopiesPerTile);
        EXPECT_EQ(UnseenCopies(view, T("F1")), 0);
        EXPECT_EQ(UnseenCopies(view, Tile{}), 0);
    }

    TEST(ComputerTurnTest, KeepValueRanksTilesByWhatTheSeatWantsFromThem) {
        // The seat wind is South, the round wind West.
        EXPECT_EQ(KeepValue(T("Nw"), Wind::South, Wind::West), kKeepWorthless);
        EXPECT_EQ(KeepValue(T("Ew"), Wind::South, Wind::West), kKeepWorthless);
        EXPECT_EQ(KeepValue(T("Sw"), Wind::South, Wind::West), kKeepValueTile);
        EXPECT_EQ(KeepValue(T("Ww"), Wind::South, Wind::West), kKeepValueTile);
        EXPECT_EQ(KeepValue(T("Rd"), Wind::South, Wind::West), kKeepValueTile);
        EXPECT_EQ(KeepValue(T("Gd"), Wind::South, Wind::West), kKeepValueTile);
        EXPECT_EQ(KeepValue(T("1p"), Wind::South, Wind::West), kKeepTerminal);
        EXPECT_EQ(KeepValue(T("9m"), Wind::South, Wind::West), kKeepTerminal);
        EXPECT_EQ(KeepValue(T("5s"), Wind::South, Wind::West), kKeepSimple);
        EXPECT_EQ(KeepValue(T("2p"), Wind::South, Wind::West), kKeepSimple);
        EXPECT_LT(kKeepWorthless, kKeepTerminal);
        EXPECT_LT(kKeepTerminal, kKeepSimple);
        EXPECT_LT(kKeepSimple, kKeepValueTile);
        // A wind that is both the own and the round wind is still one value.
        EXPECT_EQ(KeepValue(T("Ew"), Wind::East, Wind::East), kKeepValueTile);
    }

    TEST(ComputerTurnTest, AcceptanceCountsTheUnseenCopiesOfTheImprovingTiles) {
        TableView view;
        const TileCounts counts = CountTiles(ParseTiles(kTwoSided).value());

        EXPECT_EQ(AcceptanceOf(view, counts, 0), 8);  // 4p and 7p

        view.visible[T("4p").Id()] = 3;
        view.visible[T("7p").Id()] = 2;
        EXPECT_EQ(AcceptanceOf(view, counts, 0), 1 + 2);
    }

    // ----- every answer is listed and repeatable -----

    TEST(ComputerTurnTest, TheAnswerIsAlwaysListedAndTheSameViewAndSeedGiveTheSameAnswer) {
        int turns = 0;
        std::set<ActionKind> kinds;
        for (int index = 0; index < kRandomHands; ++index) {
            const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
            MahjongSettings settings;
            settings.flowers = (index & 1) != 0;
            Match match;
            ASSERT_TRUE(match.Start(settings, seed).has_value());
            MahjongRng rng(seed);
            HandModel& hand = match.Hand();
            for (int step = 0; step < kTurnsPerHand * kSeatCount && hand.GetPhase() != Phase::Finished; ++step) {
                const Seat actor = NextActor(hand, rng);
                ASSERT_NE(actor, kNoSeat);
                if (hand.GetPhase() == Phase::Discard) {
                    const TableView view = MakeTableView(hand, actor);
                    MahjongRng first(seed + static_cast<uint32_t>(step));
                    MahjongRng second(seed + static_cast<uint32_t>(step));
                    const HandAction answer = ChooseTurnAction(view, first);
                    EXPECT_TRUE(Has(view.legal, answer)) << "hand " << index << " step " << step << " seat " << actor;
                    EXPECT_EQ(answer, ChooseTurnAction(view, second)) << "hand " << index << " step " << step;
                    EXPECT_TRUE(hand.Apply(actor, answer).has_value()) << "hand " << index << " step " << step;
                    kinds.insert(answer.kind);
                    ++turns;
                    continue;
                }
                const eastl::vector<HandAction> actions = hand.LegalActions(actor);
                ASSERT_FALSE(actions.empty());
                ASSERT_TRUE(hand.Apply(actor, Choose(actions, Mode::SeekWin, rng)).has_value());
            }
        }
        EXPECT_GT(turns, kRandomHands * kTurnsPerHand / 2);
        EXPECT_EQ(kinds.count(ActionKind::Discard), 1u);
    }

    // ----- the rule that decided, and the ratings -----

    namespace {

        TurnDecision Decision(const HandModel& model, Seat seat, uint32_t rngSeed = kRngSeed) {
            const TableView view = MakeTableView(model, seat);
            MahjongRng rng(rngSeed);
            return DecideTurn(view, rng);
        }

        /// The tied group of `RateDiscards`: the entries equal to the first in shanten, acceptance and keep.
        eastl::vector<Tile> BestGroup(const eastl::vector<DiscardOption>& options) {
            eastl::vector<Tile> group;
            for (const DiscardOption& option : options) {
                if (option.shanten == options.front().shanten && option.acceptance == options.front().acceptance && option.keep == options.front().keep) {
                    group.push_back(option.tile);
                }
            }
            return group;
        }

    }  // namespace

    TEST(ComputerTurnTest, DecideTurnNamesTheRuleThatDecided) {
        const HandModel win = WinningModel();
        EXPECT_EQ(Decision(win, kSeat0).step, TurnStep::DeclareWin);

        const HandModel declare = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew"));
        const TurnDecision declared = Decision(declare, kSeat0);
        EXPECT_EQ(declared.step, TurnStep::Declare);
        EXPECT_EQ(declared.action, HandAction::MakeDiscard(T("6p"), true));

        const HandModel kong = MakeModel(SpecOf("Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd", "Ew"));
        const TurnDecision konged = Decision(kong, kSeat0);
        EXPECT_EQ(konged.step, TurnStep::Kong);
        EXPECT_EQ(konged.action, HandAction::MakeConcealedKong(T("Ew")));

        HandModel keeping = MakeModel(SpecOf(kTwoSided, "Gd Wd Gd Nw 6p"));
        Lap(keeping, "Gd Wd Gd Nw", {kSeat0});
        const TurnDecision kept = Decision(keeping, kSeat0);
        EXPECT_EQ(kept.step, TurnStep::KeepWaiting);
        EXPECT_EQ(kept.action, HandAction::MakeDiscard(T("6p")));

        const HandModel discard = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew", "Nw"));
        const TurnDecision discarded = Decision(discard, kSeat0);
        EXPECT_EQ(discarded.step, TurnStep::Discard);
        EXPECT_EQ(discarded.action, HandAction::MakeDiscard(T("Nw")));
    }

    TEST(ComputerTurnTest, DecideTurnGivesWhatChooseTurnActionGivesWithTheSameDraws) {
        for (int index = 0; index < kRandomHands; ++index) {
            const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
            Match match;
            ASSERT_TRUE(match.Start(MahjongSettings{}, seed).has_value());
            MahjongRng rng(seed);
            HandModel& hand = match.Hand();
            for (int step = 0; step < kTurnsPerHand * kSeatCount && hand.GetPhase() != Phase::Finished; ++step) {
                const Seat actor = NextActor(hand, rng);
                ASSERT_NE(actor, kNoSeat);
                if (hand.GetPhase() == Phase::Discard) {
                    const TableView view = MakeTableView(hand, actor);
                    MahjongRng chooser(seed + static_cast<uint32_t>(step));
                    MahjongRng decider(seed + static_cast<uint32_t>(step));
                    EXPECT_EQ(ChooseTurnAction(view, chooser), DecideTurn(view, decider).action);
                    EXPECT_EQ(chooser.Below(1000), decider.Below(1000)) << "the same number of draws";
                }
                ASSERT_TRUE(hand.Apply(actor, Choose(hand.LegalActions(actor), Mode::SeekWin, rng)).has_value());
            }
        }
    }

    TEST(ComputerTurnTest, RateDiscardsListsEveryKindHeldInTheOrderOfTheChooser) {
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 5p 5p 3m 4m 7p 8p Nw", "Gd"));
        const TableView view = MakeTableView(model, kSeat0);
        const TileCounts counts = CountTiles(view.own.concealed);

        const eastl::vector<DiscardOption> options = RateDiscards(counts, 0, view);

        size_t kinds = 0;
        for (const uint8_t count : counts) {
            kinds += count != 0 ? 1u : 0u;
        }
        ASSERT_EQ(options.size(), kinds);
        for (size_t i = 1; i < options.size(); ++i) {
            const DiscardOption& a = options[i - 1];
            const DiscardOption& b = options[i];
            const bool inOrder = a.shanten != b.shanten         ? a.shanten < b.shanten
                                 : a.acceptance != b.acceptance ? a.acceptance > b.acceptance
                                 : a.keep != b.keep             ? a.keep < b.keep
                                                                : a.tile.Id() < b.tile.Id();
            EXPECT_TRUE(inOrder) << "entry " << i;
        }
        EXPECT_EQ(options.front().tile, T("Nw"));
        EXPECT_EQ(options.front().keep, kKeepWorthless);
    }

    TEST(ComputerTurnTest, RateDiscardsAgreesWithBestDiscardOnRandomHands) {
        int hands = 0;
        int ties = 0;
        for (int index = 0; index < kRandomHands; ++index) {
            const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
            Match match;
            ASSERT_TRUE(match.Start(MahjongSettings{}, seed).has_value());
            MahjongRng rng(seed);
            HandModel& hand = match.Hand();
            for (int step = 0; step < kTurnsPerHand * kSeatCount && hand.GetPhase() != Phase::Finished; ++step) {
                const Seat actor = NextActor(hand, rng);
                ASSERT_NE(actor, kNoSeat);
                if (hand.GetPhase() == Phase::Discard) {
                    const TableView view = MakeTableView(hand, actor);
                    const TileCounts counts = CountTiles(view.own.concealed);
                    const eastl::vector<DiscardOption> options = RateDiscards(counts, MeldCountOf(view), view);
                    ASSERT_FALSE(options.empty());
                    const DiscardRating best = BestDiscardRating(counts, MeldCountOf(view), view);
                    EXPECT_EQ(options.front().tile, best.tile) << "hand " << index << " step " << step;
                    EXPECT_EQ(options.front().shanten, best.shanten);
                    EXPECT_EQ(options.front().acceptance, best.acceptance);

                    const eastl::vector<Tile> group = BestGroup(options);
                    ties += group.size() > 1 ? 1 : 0;
                    for (uint32_t pick = 1; pick <= 4; ++pick) {
                        MahjongRng picker(pick);
                        const DiscardRating picked = BestDiscard(counts, MeldCountOf(view), view, picker);
                        EXPECT_EQ(picked.shanten, best.shanten);
                        EXPECT_EQ(picked.acceptance, best.acceptance);
                        EXPECT_NE(eastl::find(group.begin(), group.end(), picked.tile), group.end()) << "hand " << index << " step " << step;
                    }
                    ++hands;
                }
                ASSERT_TRUE(hand.Apply(actor, Choose(hand.LegalActions(actor), Mode::SeekWin, rng)).has_value());
            }
        }
        EXPECT_GT(hands, kRandomHands * kTurnsPerHand / 2);
        EXPECT_GT(ties, 0);
    }

    TEST(ComputerTurnTest, RateKongGivesTheShantenAndAcceptanceOfTheHandWithoutTheKong) {
        // 4p 4p 4p 5p 1s-6s 7m-9m with the fourth 4p: without the kong the hand is 5p 1s-6s 7m-9m and a meld, waiting on 5p alone.
        const HandModel model = DeclaredPungModel("4p 4p 4p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m", "4p");
        const TableView view = MakeTableView(model, kSeat0);
        const TileCounts counts = CountTiles(view.own.concealed);

        const DiscardRating after = RateKong(view, counts, HandAction::MakeConcealedKong(T("4p")));

        EXPECT_FALSE(after.tile.IsPlayable());
        EXPECT_EQ(after.shanten, 0);
        EXPECT_EQ(after.acceptance, UnseenCopies(view, T("5p")));
    }

}  // namespace BECore::Mahjong::Tests
