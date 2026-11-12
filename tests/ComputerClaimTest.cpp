#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Match.h>
#include <gtest/gtest.h>

/**
 * `ChooseClaimAction`: the choices of a computer player when a discard (or an upgraded kong) is up for claiming, one case per rule, on
 * fixed walls with the expected answer worked out by hand first (the shanten of the hand as it is and of the hand after each claim).
 * Then the helpers (`ConcealedAfterClaim`, `RateClaim`, `ClaimRank`, `IsValueTile`) and a run over random hands: every pending seat
 * answers every window it is asked about with a listed action that `Apply` accepts. Pure logic: no engine services.
 *
 * `WindowModel` opens the window of most cases: East throws the only tile of `live`; seat 1 (South) is the seat under test, the others
 * hold hands without winds that claim nothing the tests look at. The cases of a robbed kong use `OpenPungModel` of
 * `ComputerTestSupport.h` instead (seat 1 upgrades its pung).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kRngSeed = 1;
        constexpr uint32_t kRngSeeds = 8;
        constexpr int kRandomHands = 20;
        constexpr uint32_t kRandomFirstSeed = 500;

        constexpr const char* kEast = "1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s";
        constexpr const char* kOther2 = "2p 5p 8p 2s 5s 8s 3m 6m 9m Gd Wd 9p 8m";
        constexpr const char* kOther3 = "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s";

        // Four partial sets (3p 4p, 6p 7p, 3s 4s, 6s 7s) and three single tiles: with a pair the hand is 3 from a win, a pung of that
        // pair leaves it 3 away (the partial sets beyond the sets still needed count for nothing).
        constexpr const char* kPartials = "3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw";

        std::string WithPair(const char* pair) {
            return std::string(kPartials) + " " + pair;
        }

        /// East throws `discard`, the only tile of the live wall. Seat 1 holds `seat1`.
        HandModel WindowModel(const std::string& seat1, const char* discard, Wind roundWind = Wind::East) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kEast;
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = kOther2;
            spec.hands[kSeat3] = kOther3;
            spec.live = discard;
            HandModel model = MakeModel(spec, kSeat0, roundWind);
            EXPECT_TRUE(model.Discard(kSeat0, T(discard)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        HandAction Answer(const HandModel& model, Seat seat, uint32_t rngSeed = kRngSeed) {
            const TableView view = MakeTableView(model, seat);
            MahjongRng rng(rngSeed);
            return ChooseClaimAction(view, rng);
        }

        /// Every rng seed from 1 to 8 gives the action `expected`, and the action is listed.
        void ExpectAnswerForEverySeed(const HandModel& model, Seat seat, const HandAction& expected) {
            const TableView view = MakeTableView(model, seat);
            for (uint32_t seed = 1; seed <= kRngSeeds; ++seed) {
                EXPECT_EQ(Answer(model, seat, seed), expected) << "rng seed " << seed;
            }
            EXPECT_TRUE(Has(view.legal, expected));
        }

        const HandAction kPass = HandAction::MakePass();
        const HandAction kPung = HandAction::MakeClaim(ClaimKind::Pung);

    }  // namespace

    // ----- 1. mahjong -----

    TEST(ComputerClaimTest, TakesMahjongEvenThoughAPungOfTheSameTileIsListed) {
        // Seat 1 waits on the red dragon and on 4p (shanpon) and has declared with its first discard (the Wd it drew); East throws the Rd.
        FixedWallSpec spec;
        spec.hands[kSeat0] = kEast;
        spec.hands[kSeat1] = "Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p";
        spec.hands[kSeat2] = kOther2;
        spec.hands[kSeat3] = kOther3;
        spec.live = "9p Wd Gd Nw Rd";
        HandModel model = MakeModel(spec);
        Lap(model, "9p Wd Gd Nw", {kSeat1});
        ASSERT_TRUE(model.GetHand(kSeat1).callingDeclared);
        ASSERT_TRUE(model.Discard(kSeat0, T("Rd")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Mahjong)));
        ASSERT_TRUE(Has(view.legal, kPung));

        ExpectAnswerForEverySeed(model, kSeat1, HandAction::MakeClaim(ClaimKind::Mahjong));
    }

    TEST(ComputerClaimTest, InARobbedKongWindowItTakesMahjong) {
        // Seat 2 waits on the 5p and has declared; seat 1 upgrades its pung of 5p and the window to rob it opens.
        HandModel model = OpenPungModel("5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd", "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Ew Ew", kQuiet3, {kSeat2});
        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);
        const TableView view = MakeTableView(model, kSeat2);
        ASSERT_EQ(view.phase, Phase::RobKong);

        ExpectAnswerForEverySeed(model, kSeat2, HandAction::MakeClaim(ClaimKind::Mahjong));
    }

    // ----- 2. pungs -----

    TEST(ComputerClaimTest, ClaimsAPungThatLowersTheShanten) {
        // 5p 5p 123s 456s 789m Nw Wd: three sets, a pair and two single tiles, shanten 1. After the pung and the discard of the Nw the
        // hand is four sets and the Wd, shanten 0.
        const HandModel model = WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_EQ(Shanten(CountTiles(view.own.concealed), 0), 1);
        ASSERT_TRUE(Has(view.legal, kPung));

        ExpectAnswerForEverySeed(model, kSeat1, kPung);
    }

    TEST(ComputerClaimTest, PassesAPungOfASimpleTileThatLeavesTheShantenAsItIs) {
        // Four partial sets, a pair of 5m and three single tiles: shanten 8 - 4 - 1 = 3. After the pung the pair is gone and only
        // three partial sets count (3 needed): 6 - 3 = 3 again. The pung of a simple tile does nothing.
        const HandModel model = WindowModel(WithPair("5m 5m"), "5m");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_EQ(Shanten(CountTiles(view.own.concealed), 0), 3);
        ASSERT_TRUE(Has(view.legal, kPung));
        EXPECT_EQ(RateClaim(view, kPung).shanten, 3);

        ExpectAnswerForEverySeed(model, kSeat1, kPass);
    }

    TEST(ComputerClaimTest, ClaimsAPungOfADragonAtTheSameShanten) {
        const HandModel model = WindowModel(WithPair("Rd Rd"), "Rd");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_EQ(RateClaim(view, kPung).shanten, Shanten(CountTiles(view.own.concealed), 0));

        ExpectAnswerForEverySeed(model, kSeat1, kPung);
    }

    TEST(ComputerClaimTest, ClaimsAPungOfTheOwnWind) {
        // Seat 1 is South.
        const HandModel model = WindowModel(WithPair("Sw Sw"), "Sw");
        ASSERT_EQ(MakeTableView(model, kSeat1).seatWind, Wind::South);

        ExpectAnswerForEverySeed(model, kSeat1, kPung);
    }

    TEST(ComputerClaimTest, ClaimsAPungOfTheRoundWind) {
        const HandModel model = WindowModel(WithPair("Ew Ew"), "Ew");
        ASSERT_EQ(MakeTableView(model, kSeat1).roundWind, Wind::East);

        ExpectAnswerForEverySeed(model, kSeat1, kPung);
    }

    TEST(ComputerClaimTest, PassesAPungOfAnotherSeatsWindAtTheSameShantenUnlessItIsTheRoundWind) {
        // The west wind is seat 2's wind and, in a hand of the round West, the round wind.
        const HandModel other = WindowModel(WithPair("Ww Ww"), "Ww");
        const HandModel round = WindowModel(WithPair("Ww Ww"), "Ww", Wind::West);
        ASSERT_TRUE(Has(MakeTableView(other, kSeat1).legal, kPung));

        ExpectAnswerForEverySeed(other, kSeat1, kPass);
        ExpectAnswerForEverySeed(round, kSeat1, kPung);
    }

    TEST(ComputerClaimTest, ClaimsTheKongRatherThanThePungOfTheSameTile) {
        // Rd Rd Rd 123s 456s and four single tiles: shanten 2. The pung (and the discard of the fourth red dragon, no use to it any
        // more: all four are in sight) and the kong leave the same ten tiles, so the kong wins at the rank.
        const HandModel model = WindowModel("Rd Rd Rd 1s 2s 3s 4s 5s 6s 1m 9m Nw Ww", "Rd");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(Has(view.legal, kPung));
        ASSERT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Kong)));
        ASSERT_EQ(RateClaim(view, kPung).shanten, RateClaim(view, HandAction::MakeClaim(ClaimKind::Kong)).shanten);
        ASSERT_EQ(RateClaim(view, kPung).acceptance, RateClaim(view, HandAction::MakeClaim(ClaimKind::Kong)).acceptance);

        ExpectAnswerForEverySeed(model, kSeat1, HandAction::MakeClaim(ClaimKind::Kong));
    }

    // ----- 3. chows -----

    TEST(ComputerClaimTest, ClaimsAChowThatLowersTheShanten) {
        // 4p 6p 123s 456s 789m Nw Wd: shanten 1 (three sets and a gap); with the 5p the chow is the fourth set, shanten 0.
        const HandModel model = WindowModel("4p 6p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");

        ExpectAnswerForEverySeed(model, kSeat1, HandAction::MakeClaim(ClaimKind::Chow, T("4p")));
    }

    TEST(ComputerClaimTest, PassesAChowThatTakesTilesOutOfASet) {
        // 456p 123s 456s 9m 9m Nw Ww: shanten 1. The only chow of the 3p (3p 4p 5p) breaks the set 456p and leaves it at 1.
        const HandModel model = WindowModel("4p 5p 6p 1s 2s 3s 4s 5s 6s 9m 9m Nw Ww", "3p");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Chow, T("3p"))));
        ASSERT_EQ(Shanten(CountTiles(view.own.concealed), 0), 1);
        ASSERT_EQ(RateClaim(view, HandAction::MakeClaim(ClaimKind::Chow, T("3p"))).shanten, 1);

        ExpectAnswerForEverySeed(model, kSeat1, kPass);
    }

    TEST(ComputerClaimTest, ChoosesTheSequenceWithTheBetterRating) {
        // 234p 6p 7p 123s 5m 5m 9s Nw Ww (shanten 2) and the 5p thrown: 3p 4p 5p and 4p 5p 6p break the set 234p and leave it at 2, only
        // 5p 6p 7p keeps 234p and makes the hand 1.
        const HandModel model = WindowModel("2p 3p 4p 6p 7p 1s 2s 3s 5m 5m 9s Nw Ww", "5p");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_EQ(Shanten(CountTiles(view.own.concealed), 0), 2);
        for (const char* first : {"3p", "4p", "5p"}) {
            ASSERT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Chow, T(first)))) << first;
        }
        EXPECT_EQ(RateClaim(view, HandAction::MakeClaim(ClaimKind::Chow, T("3p"))).shanten, 2);
        EXPECT_EQ(RateClaim(view, HandAction::MakeClaim(ClaimKind::Chow, T("4p"))).shanten, 2);
        EXPECT_EQ(RateClaim(view, HandAction::MakeClaim(ClaimKind::Chow, T("5p"))).shanten, 1);

        ExpectAnswerForEverySeed(model, kSeat1, HandAction::MakeClaim(ClaimKind::Chow, T("5p")));
    }

    // ----- 4. passing -----

    TEST(ComputerClaimTest, ABrokenHandWaitingPassesEveryClaimEvenAPungOfADragon) {
        // 1s-6s 7m-9m Rd Rd 4p 4p waits on the red dragon and on 4p but has not declared, so mahjong is not listed; the pung is.
        const HandModel model = WindowModel("Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p", "Rd");
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(Has(view.legal, kPung));
        ASSERT_FALSE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Mahjong)));
        ASSERT_EQ(Shanten(CountTiles(view.own.concealed), 0), 0);

        ExpectAnswerForEverySeed(model, kSeat1, kPass);
    }

    TEST(ComputerClaimTest, PassesWhenOnlyPassIsListed) {
        TableView view;
        view.seat = kSeat1;
        view.phase = Phase::Claim;
        view.currentSeat = kSeat0;
        view.pendingTile = T("5p");
        view.own.concealed = ParseTiles("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd").value();
        view.legal.push_back(kPass);
        MahjongRng rng(kRngSeed);

        EXPECT_EQ(ChooseClaimAction(view, rng), kPass);
    }

    // ----- the helpers -----

    TEST(ComputerClaimTest, ConcealedAfterAClaimLosesTheTilesOfTheNewSet) {
        const HandModel model = WindowModel("2p 3p 4p 6p 7p 1s 2s 3s 5m 5m 9s Nw Ww", "5p");
        const TableView view = MakeTableView(model, kSeat1);

        const TileCounts chow = ConcealedAfterClaim(view, HandAction::MakeClaim(ClaimKind::Chow, T("4p")));  // 4p 5p 6p: the 4p and 6p
        EXPECT_EQ(chow[T("4p").Id()], 0);
        EXPECT_EQ(chow[T("6p").Id()], 0);
        EXPECT_EQ(chow[T("3p").Id()], 1);
        EXPECT_EQ(chow[T("5p").Id()], 0);  // the pending tile was never in the hand

        const HandModel pungs = WindowModel(WithPair("5m 5m"), "5m");
        const TableView pungView = MakeTableView(pungs, kSeat1);
        EXPECT_EQ(ConcealedAfterClaim(pungView, kPung)[T("5m").Id()], 0);
        EXPECT_EQ(ConcealedAfterClaim(pungView, kPung)[T("3p").Id()], 1);

        const HandModel kong = WindowModel("Rd Rd Rd 1s 2s 3s 4s 5s 6s 1m 9m Nw Ww", "Rd");
        const TableView kongView = MakeTableView(kong, kSeat1);
        EXPECT_EQ(ConcealedAfterClaim(kongView, HandAction::MakeClaim(ClaimKind::Kong))[T("Rd").Id()], 0);
    }

    TEST(ComputerClaimTest, ARatedKongHasNoDiscardAndARatedPungHasOne) {
        const HandModel model = WindowModel("Rd Rd Rd 1s 2s 3s 4s 5s 6s 1m 9m Nw Ww", "Rd");
        const TableView view = MakeTableView(model, kSeat1);

        EXPECT_FALSE(RateClaim(view, HandAction::MakeClaim(ClaimKind::Kong)).tile.IsValid());
        EXPECT_TRUE(RateClaim(view, kPung).tile.IsValid());
    }

    TEST(ComputerClaimTest, ClaimRankPutsTheKongOverThePungOverTheChow) {
        EXPECT_GT(ClaimRank(ClaimKind::Kong), ClaimRank(ClaimKind::Pung));
        EXPECT_GT(ClaimRank(ClaimKind::Pung), ClaimRank(ClaimKind::Chow));
        EXPECT_EQ(ClaimRank(ClaimKind::Kong), kClaimRankKong);
        EXPECT_LT(ClaimRank(ClaimKind::Mahjong), ClaimRank(ClaimKind::Chow));
    }

    TEST(ComputerClaimTest, IsValueTileIsADragonTheOwnWindOrTheRoundWind) {
        EXPECT_TRUE(IsValueTile(T("Rd"), Wind::East, Wind::East));
        EXPECT_TRUE(IsValueTile(T("Sw"), Wind::South, Wind::East));
        EXPECT_TRUE(IsValueTile(T("Ew"), Wind::South, Wind::East));
        EXPECT_FALSE(IsValueTile(T("Ww"), Wind::South, Wind::East));
        EXPECT_FALSE(IsValueTile(T("5p"), Wind::South, Wind::East));
        EXPECT_FALSE(IsValueTile(Tile{}, Wind::South, Wind::East));
    }

    // ----- every answer is listed, repeatable, and accepted by the model -----

    TEST(ComputerClaimTest, EveryAnswerOfARandomHandIsListedRepeatableAndAcceptedAndWindowsAreClaimedAndPassed) {
        int windows = 0;
        int claims = 0;
        int passes = 0;
        for (int index = 0; index < kRandomHands; ++index) {
            const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
            MahjongSettings settings;
            settings.flowers = (index & 1) != 0;
            Match match;
            ASSERT_TRUE(match.Start(settings, seed).has_value());
            MahjongRng rng(seed);
            HandModel& hand = match.Hand();
            while (hand.GetPhase() != Phase::Finished) {
                if (hand.GetPhase() == Phase::Discard) {
                    const Seat actor = hand.CurrentSeat();
                    ASSERT_TRUE(hand.Apply(actor, Choose(hand.LegalActions(actor), Mode::SeekWin, rng)).has_value());
                    continue;
                }
                ++windows;
                for (const Seat seat : hand.PendingClaimSeats()) {
                    const TableView view = MakeTableView(hand, seat);
                    MahjongRng first(seed + static_cast<uint32_t>(windows));
                    MahjongRng second(seed + static_cast<uint32_t>(windows));
                    const HandAction answer = ChooseClaimAction(view, first);
                    EXPECT_TRUE(Has(view.legal, answer)) << "hand " << index << " window " << windows << " seat " << seat;
                    EXPECT_EQ(answer, ChooseClaimAction(view, second));
                    ASSERT_TRUE(hand.Apply(seat, answer).has_value()) << "hand " << index << " window " << windows << " seat " << seat;
                    if (answer.kind == ActionKind::Pass) {
                        ++passes;
                    } else {
                        ++claims;
                    }
                }
            }
        }
        EXPECT_GT(windows, kRandomHands);
        EXPECT_GT(claims, 0);
        EXPECT_GT(passes, 0);
    }

    // ----- the rule that decided, and the ratings -----

    namespace {

        ClaimDecision Decision(const HandModel& model, Seat seat, uint32_t rngSeed = kRngSeed) {
            const TableView view = MakeTableView(model, seat);
            MahjongRng rng(rngSeed);
            return DecideClaim(view, rng);
        }

    }  // namespace

    TEST(ComputerClaimTest, DecideClaimNamesTheRuleThatDecided) {
        HandModel robbed = OpenPungModel("5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd", "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Ew Ew", kQuiet3, {kSeat2});
        ASSERT_TRUE(robbed.UpgradeKong(kSeat1, T("5p")).has_value());
        EXPECT_EQ(Decision(robbed, kSeat2).step, ClaimStep::Mahjong);

        const HandModel waiting = WindowModel("Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p", "Rd");
        const ClaimDecision passed = Decision(waiting, kSeat1);
        EXPECT_EQ(passed.step, ClaimStep::WaitingHand);
        EXPECT_EQ(passed.action, kPass);

        const HandModel claim = WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        const ClaimDecision claimed = Decision(claim, kSeat1);
        EXPECT_EQ(claimed.step, ClaimStep::Claim);
        EXPECT_EQ(claimed.action, kPung);

        const HandModel noGain = WindowModel(WithPair("5m 5m"), "5m");
        const ClaimDecision declined = Decision(noGain, kSeat1);
        EXPECT_EQ(declined.step, ClaimStep::NoGain);
        EXPECT_EQ(declined.action, kPass);
    }

    TEST(ComputerClaimTest, AClaimPassedByTheRollOfSkipClaimPercentKeepsTheStepClaim) {
        const HandModel model = WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        const TableView view = MakeTableView(model, kSeat1);
        LevelProfile profile = kNormalProfile;
        profile.skipClaimPercent = 100;
        MahjongRng rng(kRngSeed);

        const ClaimDecision decision = DecideClaim(view, rng, profile);

        EXPECT_EQ(decision.action, kPass);
        EXPECT_EQ(decision.step, ClaimStep::Claim);
    }

    TEST(ComputerClaimTest, RateClaimsListsThePungKongAndChowsInTheOrderOfTheWindowWithTheirRatings) {
        // 4p 6p and 5p 5p: a chow (4p 5p 6p), a pung and no kong for the 5p thrown.
        const HandModel model = WindowModel("4p 6p 5p 5p 1s 2s 3s 7s 8s 9s Nw Nw Wd", "5p");
        const TableView view = MakeTableView(model, kSeat1);
        const int before = Shanten(CountTiles(view.own.concealed), 0);

        const eastl::vector<RatedClaim> options = RateClaims(view, before, kNormalProfile);

        size_t listed = 0;
        for (const HandAction& action : view.legal) {
            listed += action.kind == ActionKind::Claim && action.claim != ClaimKind::Mahjong ? 1u : 0u;
        }
        ASSERT_EQ(options.size(), listed);
        ASSERT_GE(options.size(), 2u);
        size_t at = 0;
        for (const HandAction& action : view.legal) {
            if (action.kind != ActionKind::Claim || action.claim == ClaimKind::Mahjong) {
                continue;
            }
            const RatedClaim& option = options[at++];
            EXPECT_EQ(option.action, action);
            EXPECT_EQ(option.rank, ClaimRank(action.claim));
            const DiscardRating rated = RateClaim(view, action);
            EXPECT_EQ(option.after.shanten, rated.shanten);
            EXPECT_EQ(option.after.acceptance, rated.acceptance);
            EXPECT_EQ(option.acceptable, rated.shanten < before);
        }
    }

    TEST(ComputerClaimTest, TheChoiceIsABestAcceptableOneOfRateClaimsOnRandomWindows) {
        int compared = 0;
        int acceptable = 0;
        for (int index = 0; index < kRandomHands; ++index) {
            const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
            Match match;
            ASSERT_TRUE(match.Start(MahjongSettings{}, seed).has_value());
            MahjongRng rng(seed);
            HandModel& hand = match.Hand();
            while (hand.GetPhase() != Phase::Finished) {
                if (hand.GetPhase() == Phase::Discard) {
                    const Seat actor = hand.CurrentSeat();
                    ASSERT_TRUE(hand.Apply(actor, Choose(hand.LegalActions(actor), Mode::SeekWin, rng)).has_value());
                    continue;
                }
                for (const Seat seat : hand.PendingClaimSeats()) {
                    const TableView view = MakeTableView(hand, seat);
                    const int before = Shanten(CountTiles(view.own.concealed), MeldCountOf(view));
                    MahjongRng chooser(seed + static_cast<uint32_t>(compared));
                    const HandAction answer = ChooseClaimAction(view, chooser);
                    if (FindMahjong(view) == nullptr && before > kWaitingShanten) {
                        const eastl::vector<RatedClaim> options = RateClaims(view, before, kNormalProfile);
                        const RatedClaim* best = nullptr;
                        for (const RatedClaim& option : options) {
                            if (option.acceptable && (best == nullptr || IsBetterClaim(option, *best))) {
                                best = &option;
                            }
                        }
                        ++compared;
                        if (best == nullptr) {
                            EXPECT_EQ(answer, kPass);
                        } else {
                            ++acceptable;
                            bool isBest = false;
                            for (const RatedClaim& option : options) {
                                if (option.action == answer) {
                                    isBest = option.acceptable && !IsBetterClaim(*best, option);
                                }
                            }
                            EXPECT_TRUE(isBest) << "hand " << index << " seat " << seat;
                        }
                    }
                    ASSERT_TRUE(hand.Apply(seat, answer).has_value());
                }
            }
        }
        EXPECT_GT(compared, kRandomHands);
        EXPECT_GT(acceptable, 0);
    }

    // ----- 2. a waiting hand offered a kong (`waitingKongByPoints`) -----

    namespace {

        /// A concealed pung of red dragons and a single 4p wait; East throws the fourth red dragon.
        constexpr const char* kWaitingOnPung = "Rd Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p";
        /// Live tiles the seat is told the wall still holds, so both sides have draws to win with (the fixed wall has only the discard).
        constexpr int kLiveForValues = 60;

        TableView WaitingKongView(const HandModel& model) {
            TableView view = MakeTableView(model, kSeat1);
            view.liveCount = kLiveForValues;
            return view;
        }

        /// The same seat with its 1s 2s 3s already an open chow: the hand has lost `FullyConcealed` before the window.
        TableView OpenWaitingKongView(const HandModel& model) {
            TableView view = WaitingKongView(model);
            eastl::vector<Tile> concealed;
            for (const Tile tile : view.own.concealed) {
                if (tile != T("1s") && tile != T("2s") && tile != T("3s")) {
                    concealed.push_back(tile);
                }
            }
            view.own.concealed = eastl::move(concealed);
            Meld chow;
            chow.kind = MeldKind::Chow;
            chow.first = T("1s");
            chow.from = kSeat0;
            chow.fromDiscard = true;
            chow.claimed = T("1s");
            view.own.melds.push_back(chow);
            return view;
        }

        LevelProfile ByPoints() {
            LevelProfile profile = kNormalProfile;
            profile.waitingKongByPoints = true;
            return profile;
        }

    }  // namespace

    TEST(ComputerClaimTest, AWaitingHandWithTheKnobOffPassesAKongThatKeepsItsWait) {
        const TableView view = WaitingKongView(WindowModel(kWaitingOnPung, "Rd"));
        ASSERT_TRUE(Has(view.legal, HandAction::MakeClaim(ClaimKind::Kong)));
        LevelProfile profile = kNormalProfile;
        profile.waitingKongByPoints = false;
        MahjongRng rng(kRngSeed);

        const ClaimDecision decision = DecideClaim(view, rng, profile);

        EXPECT_EQ(decision.action, kPass);
        EXPECT_EQ(decision.step, ClaimStep::WaitingHand);
    }

    TEST(ComputerClaimTest, RateWaitingKongRatesOnlyAKongThatKeepsTheWait) {
        const std::optional<WaitingKong> kong = RateWaitingKong(WaitingKongView(WindowModel(kWaitingOnPung, "Rd")));
        ASSERT_TRUE(kong.has_value());
        EXPECT_EQ(kong->kong, HandAction::MakeClaim(ClaimKind::Kong));

        // Two red dragons: a pung is listed, no kong, nothing to rate.
        EXPECT_FALSE(RateWaitingKong(WaitingKongView(WindowModel("Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p", "Rd"))).has_value());
    }

    TEST(ComputerClaimTest, AConcealedWaitingHandWeighsTheKongAgainstTheConcealedDouble) {
        const TableView view = WaitingKongView(WindowModel(kWaitingOnPung, "Rd"));
        const std::optional<WaitingKong> kong = RateWaitingKong(view);
        ASSERT_TRUE(kong.has_value());
        MahjongRng rng(kRngSeed);

        const ClaimDecision decision = DecideClaim(view, rng, ByPoints());

        // The concealed hand loses `FullyConcealed` with the kong; the decision follows the comparison either way.
        if (kong->KongIsBetter()) {
            EXPECT_EQ(decision.step, ClaimStep::WaitingKong);
            EXPECT_EQ(decision.action, kong->kong);
        } else {
            EXPECT_EQ(decision.step, ClaimStep::WaitingPass);
            EXPECT_EQ(decision.action, kPass);
        }
        EXPECT_LE(kong->kongValue, kong->passValue) << "the concealed double is worth more than the kong's points here";
    }

    TEST(ComputerClaimTest, AnOpenWaitingHandTakesTheKongThatKeepsItsWait) {
        const TableView view = OpenWaitingKongView(WindowModel(kWaitingOnPung, "Rd"));
        const std::optional<WaitingKong> kong = RateWaitingKong(view);
        ASSERT_TRUE(kong.has_value());
        EXPECT_GT(kong->kongValue, kong->passValue) << "nothing is lost, the kong adds points and a loose tile";
        for (uint32_t seed = 1; seed <= kRngSeeds; ++seed) {
            MahjongRng rng(seed);
            const ClaimDecision decision = DecideClaim(view, rng, ByPoints());
            EXPECT_EQ(decision.step, ClaimStep::WaitingKong) << "rng seed " << seed;
            EXPECT_EQ(decision.action, HandAction::MakeClaim(ClaimKind::Kong)) << "rng seed " << seed;
        }
    }

    TEST(ComputerClaimTest, TheKongOfAWaitingHandDrawsNothingFromTheRng) {
        const TableView view = OpenWaitingKongView(WindowModel(kWaitingOnPung, "Rd"));
        MahjongRng used(kRngSeed);
        MahjongRng fresh(kRngSeed);

        (void)DecideClaim(view, used, ByPoints());

        EXPECT_EQ(used.Below(1000), fresh.Below(1000));
    }

}  // namespace BECore::Mahjong::Tests
