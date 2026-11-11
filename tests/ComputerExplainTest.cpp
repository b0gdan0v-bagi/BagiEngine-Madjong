#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/ComputerExplain.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Match.h>
#include <gtest/gtest.h>
#include <string>

/**
 * `ExplainAdvice`: the numbers the advice is chosen by, on fixed hands worked out by hand first (the waits of each hand, the unseen
 * copies of each wait), one case per rule of the chooser (`TurnStep`, `ClaimStep`). Every number is held against the rating function
 * of the chooser on the same view (`BestDiscardRating`, `RateDiscards`, `WaitingDiscards`, `RateKong`, `RateClaim`, `WinningTiles`),
 * and a run over random hands holds the explained action to `ComputerPlayer(kAdviceSeed).Choose`. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kRandomHands = 20;
        constexpr int kStepsPerHand = 120;
        constexpr uint32_t kRandomFirstSeed = 900;

        constexpr const char* kEast = "1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s";
        constexpr const char* kOther2 = "2p 5p 8p 2s 5s 8s 3m 6m 9m Gd Wd 9p 8m";
        constexpr const char* kOther3 = "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s";
        // 123s 456s 789m 5p 6p and a pair of east winds: waits on 4p and 7p.
        constexpr const char* kTwoSided = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p Ew Ew";

        const HandAction kPass = HandAction::MakePass();
        const HandAction kPung = HandAction::MakeClaim(ClaimKind::Pung);

        /// East throws `discard`, the only tile of the live wall. Seat 1 holds `seat1`.
        HandModel WindowModel(const char* seat1, const char* discard) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kEast;
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = kOther2;
            spec.hands[kSeat3] = kOther3;
            spec.live = discard;
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T(discard)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        /// East has declared the waiting `hand` and then draws `fourth`.
        HandModel DeclaredPungModel(const char* hand, const char* fourth) {
            HandModel model = MakeModel(SpecOf(hand, std::string("Gd Wd Gd Nw ") + fourth));
            Lap(model, "Gd Wd Gd Nw", {kSeat0});
            return model;
        }

        const TurnExplanation& TurnOf(const std::optional<Explanation>& explained) {
            EXPECT_TRUE(explained.has_value() && explained->turn.has_value());
            static const TurnExplanation kEmpty;
            return explained.has_value() && explained->turn.has_value() ? *explained->turn : kEmpty;
        }

        const ClaimExplanation& ClaimOf(const std::optional<Explanation>& explained) {
            EXPECT_TRUE(explained.has_value() && explained->claim.has_value());
            static const ClaimExplanation kEmpty;
            return explained.has_value() && explained->claim.has_value() ? *explained->claim : kEmpty;
        }

        HandAction PlayersChoice(const TableView& view) {
            ComputerPlayer player(kAdviceSeed);
            return player.Choose(view);
        }

        /// The rating of `tile` among `RateDiscards`.
        DiscardOption RatedDiscard(const TableView& view, Tile tile) {
            for (const DiscardOption& option : RateDiscards(CountTiles(view.own.concealed), MeldCountOf(view), view)) {
                if (option.tile == tile) {
                    return option;
                }
            }
            ADD_FAILURE() << "no rating for " << ToString(tile);
            return {};
        }

        void ExpectNumbersOf(const TurnOption& option, const DiscardOption& rated) {
            EXPECT_EQ(option.tile, rated.tile);
            EXPECT_EQ(option.shanten, rated.shanten);
            EXPECT_EQ(option.acceptance, rated.acceptance);
            EXPECT_EQ(option.keep, rated.keep);
        }

        /// The unseen copies of the winning tiles of the concealed tiles without `discard`, as `WinningTiles` lists them.
        std::vector<std::pair<std::string, int>> WaitsOfThirteen(const TableView& view, Tile discard) {
            TileCounts counts = CountTiles(view.own.concealed);
            --counts[discard.Id()];
            std::vector<std::pair<std::string, int>> waits;
            for (const Tile tile : WinningTiles(counts, MeldCountOf(view), CountMeldTiles(view.own.melds))) {
                waits.emplace_back(std::string(ToString(tile)), UnseenCopies(view, tile));
            }
            return waits;
        }

        std::vector<std::pair<std::string, int>> Listed(const eastl::vector<TileCopies>& waits) {
            std::vector<std::pair<std::string, int>> listed;
            for (const TileCopies& wait : waits) {
                listed.emplace_back(std::string(ToString(wait.tile)), wait.unseen);
            }
            return listed;
        }

        std::string Digest(const TurnOption& option) {
            return std::string(ToString(option.tile)) + ":" + std::to_string(option.shanten) + "," + std::to_string(option.acceptance) + "," + std::to_string(option.keep) + "," +
                   std::to_string(option.waitKinds) + "," + std::to_string(option.waitCopies);
        }

        std::string Digest(const RatedClaim& option) {
            return std::to_string(static_cast<int>(option.action.claim)) + ":" + std::to_string(option.after.shanten) + "," + std::to_string(option.after.acceptance) + "," +
                   std::to_string(option.rank) + "," + std::to_string(option.acceptable);
        }

        /// Everything an explanation says, as one string, so two explanations can be compared.
        std::string Digest(const std::optional<Explanation>& explained) {
            if (!explained.has_value()) {
                return "none";
            }
            std::string text;
            if (explained->turn.has_value()) {
                const TurnExplanation& turn = *explained->turn;
                text = std::to_string(static_cast<int>(turn.step)) + "|" + std::string(ToString(turn.action.tile)) + "|" + std::to_string(turn.shantenBefore) + "|" + Digest(turn.chosen) + "|" +
                       std::to_string(turn.equalChoices) + "|" + std::to_string(turn.kongShantenAfter) + "," + std::to_string(turn.kongAcceptanceBefore) + "," +
                       std::to_string(turn.kongAcceptanceAfter);
                for (const TurnOption& option : turn.alternatives) {
                    text += "|" + Digest(option);
                }
                for (const TileCopies& wait : turn.waits) {
                    text += "|" + std::string(ToString(wait.tile)) + "x" + std::to_string(wait.unseen);
                }
            } else {
                const ClaimExplanation& claim = *explained->claim;
                text = std::to_string(static_cast<int>(claim.step)) + "|" + std::to_string(claim.shantenBefore) + "|" + (claim.chosen.has_value() ? Digest(*claim.chosen) : "-");
                for (const RatedClaim& option : claim.alternatives) {
                    text += "|" + Digest(option);
                }
                for (const TileCopies& wait : claim.waits) {
                    text += "|" + std::string(ToString(wait.tile)) + "x" + std::to_string(wait.unseen);
                }
            }
            return text;
        }

    }  // namespace

    // ----- the turn -----

    TEST(ComputerExplainTest, ADiscardCarriesTheNumbersOfTheChooserAndTheTwoBestOthers) {
        // 123s 456s 789m 5p 5p and three single honours: Nw is worthless, Rd and Ew are value tiles; all three leave shanten 1 with the
        // same acceptance, so two others are equal to the advised one.
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew", "Nw"));
        const TableView view = MakeTableView(model, kSeat0);
        const TileCounts counts = CountTiles(view.own.concealed);

        const std::optional<Explanation> explained = ExplainAdvice(view);
        const TurnExplanation& turn = TurnOf(explained);

        EXPECT_EQ(turn.action, PlayersChoice(view));
        EXPECT_EQ(turn.action, HandAction::MakeDiscard(T("Nw")));
        EXPECT_EQ(turn.step, TurnStep::Discard);
        EXPECT_EQ(turn.shantenBefore, Shanten(counts, 0));
        const DiscardRating best = BestDiscardRating(counts, 0, view);
        EXPECT_EQ(turn.chosen.tile, best.tile);
        EXPECT_EQ(turn.chosen.shanten, best.shanten);
        EXPECT_EQ(turn.chosen.acceptance, best.acceptance);
        EXPECT_EQ(turn.chosen.keep, kKeepWorthless);
        ExpectNumbersOf(turn.chosen, RatedDiscard(view, T("Nw")));
        // The two others of the same rating (Rd, Ew) come next, then the rest; the alternatives are the entries after the advised one.
        EXPECT_EQ(turn.equalChoices, 2);
        ASSERT_EQ(turn.alternatives.size(), 2u);
        eastl::vector<DiscardOption> expected;
        for (const DiscardOption& option : RateDiscards(counts, 0, view)) {
            if (option.tile != T("Nw") && expected.size() < 2) {
                expected.push_back(option);
            }
        }
        for (size_t i = 0; i < 2; ++i) {
            ExpectNumbersOf(turn.alternatives[i], expected[i]);
        }
        EXPECT_TRUE(turn.waits.empty());
    }

    TEST(ComputerExplainTest, TheChosenOptionIsTheAdvisedKindWhateverTheRngPicksAmongEquals) {
        // Ww and Nw are equally worthless (round wind South): the rng picks one, `chosen` is the one picked.
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Ww Nw", "Ew"), kSeat0, Wind::South);
        const TableView view = MakeTableView(model, kSeat0);

        const TurnExplanation turn = TurnOf(ExplainAdvice(view));

        EXPECT_EQ(turn.chosen.tile, turn.action.tile);
        ExpectNumbersOf(turn.chosen, RatedDiscard(view, turn.action.tile));
    }

    TEST(ComputerExplainTest, ADeclarationCarriesTheWaitsOfTheThirteenAndTheOtherDeclaringDiscard) {
        // Discarding the 6p leaves the two-sided wait on 4p and 7p (4 + 4 copies); discarding the 5p leaves the pair wait on 6p and Ew
        // (2 + 2); no other discard declares.
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew"));
        const TableView view = MakeTableView(model, kSeat0);

        const TurnExplanation turn = TurnOf(ExplainAdvice(view));

        EXPECT_EQ(turn.action, PlayersChoice(view));
        EXPECT_EQ(turn.action, HandAction::MakeDiscard(T("6p"), true));
        EXPECT_EQ(turn.step, TurnStep::Declare);
        EXPECT_EQ(turn.chosen.tile, T("6p"));
        EXPECT_EQ(turn.chosen.waitKinds, 2);
        EXPECT_EQ(turn.chosen.waitCopies, 8);
        EXPECT_EQ(Listed(turn.waits), WaitsOfThirteen(view, T("6p")));
        EXPECT_EQ(Listed(turn.waits), (std::vector<std::pair<std::string, int>>{{"4p", 4}, {"7p", 4}}));
        ExpectNumbersOf(turn.chosen, RatedDiscard(view, T("6p")));
        ASSERT_EQ(turn.alternatives.size(), 1u);
        EXPECT_EQ(turn.alternatives[0].tile, T("5p"));
        EXPECT_EQ(turn.alternatives[0].waitKinds, 2);
        EXPECT_EQ(turn.alternatives[0].waitCopies, 4);
        ExpectNumbersOf(turn.alternatives[0], RatedDiscard(view, T("5p")));
        EXPECT_EQ(turn.equalChoices, 0);
        // The numbers are the ones `WaitingDiscards` ranks by.
        for (const WaitingDiscard& discard : WaitingDiscards(view, true)) {
            const TurnOption& option = discard.tile == T("6p") ? turn.chosen : turn.alternatives[0];
            EXPECT_EQ(option.waitCopies, discard.copies) << ToString(discard.tile);
            EXPECT_EQ(option.keep, discard.keep) << ToString(discard.tile);
        }
    }

    TEST(ComputerExplainTest, KeepingTheWaitOfADeclaredHandIsExplainedByTheCopies) {
        // 123s 456s 789m 5p 6p Ew Ew (declared) draws a second 6p: discarding it keeps the wait on 4p and 7p, discarding the 5p turns it
        // into the pair wait.
        HandModel model = MakeModel(SpecOf(kTwoSided, "Gd Wd Gd Nw 6p"));
        Lap(model, "Gd Wd Gd Nw", {kSeat0});
        ASSERT_EQ(model.CurrentSeat(), kSeat0);
        const TableView view = MakeTableView(model, kSeat0);

        const TurnExplanation turn = TurnOf(ExplainAdvice(view));

        EXPECT_EQ(turn.action, PlayersChoice(view));
        EXPECT_EQ(turn.action, HandAction::MakeDiscard(T("6p")));
        EXPECT_EQ(turn.step, TurnStep::KeepWaiting);
        EXPECT_EQ(turn.chosen.waitKinds, 2);
        EXPECT_EQ(turn.chosen.waitCopies, 8);
        EXPECT_EQ(Listed(turn.waits), WaitsOfThirteen(view, T("6p")));
        ASSERT_FALSE(turn.alternatives.empty());
        EXPECT_EQ(turn.alternatives[0].tile, T("5p"));
        EXPECT_EQ(turn.alternatives[0].waitCopies, 4);
        const eastl::vector<WaitingDiscard> listed = WaitingDiscards(view, false);
        for (const WaitingDiscard& discard : listed) {
            if (discard.tile == T("5p")) {
                EXPECT_EQ(turn.alternatives[0].waitCopies, discard.copies);
            }
        }
    }

    TEST(ComputerExplainTest, TwoDeclaringDiscardsOfTheSameCopiesAreCountedAsEqual) {
        // 5p 5p 6p 6p and the drawn Ew: any of the discards that declare is counted against the others by the unseen winning copies.
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p 6p 6p", "Ew"));
        const TableView view = MakeTableView(model, kSeat0);
        const TurnExplanation turn = TurnOf(ExplainAdvice(view));
        ASSERT_EQ(turn.step, TurnStep::Declare);
        int same = 0;
        for (const WaitingDiscard& discard : WaitingDiscards(view, true)) {
            same += discard.tile != turn.action.tile && discard.copies == turn.chosen.waitCopies ? 1 : 0;
        }
        EXPECT_EQ(turn.equalChoices, same);
    }

    TEST(ComputerExplainTest, AConcealedKongCarriesTheNumbersBeforeAndAfter) {
        const HandModel model = MakeModel(SpecOf("Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd", "Ew"));
        const TableView view = MakeTableView(model, kSeat0);
        const TileCounts counts = CountTiles(view.own.concealed);
        const HandAction kong = HandAction::MakeConcealedKong(T("Ew"));

        const TurnExplanation turn = TurnOf(ExplainAdvice(view));

        EXPECT_EQ(turn.action, kong);
        EXPECT_EQ(turn.action, PlayersChoice(view));
        EXPECT_EQ(turn.step, TurnStep::Kong);
        EXPECT_EQ(turn.chosen.tile, T("Ew"));
        const DiscardRating before = BestDiscardRating(counts, 0, view);
        const DiscardRating after = RateKong(view, counts, kong);
        EXPECT_EQ(turn.shantenBefore, before.shanten);
        EXPECT_EQ(turn.kongAcceptanceBefore, before.acceptance);
        EXPECT_EQ(turn.kongShantenAfter, after.shanten);
        EXPECT_EQ(turn.kongAcceptanceAfter, after.acceptance);
        EXPECT_LE(turn.kongShantenAfter, turn.shantenBefore);
    }

    TEST(ComputerExplainTest, AnUpgradedKongCarriesTheNumbersBeforeAndAfter) {
        const HandModel model = OpenPungModel("5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd");
        const TableView view = MakeTableView(model, kSeat1);
        const TileCounts counts = CountTiles(view.own.concealed);
        const HandAction kong = HandAction::MakeUpgradeKong(T("5p"));

        const TurnExplanation turn = TurnOf(ExplainAdvice(view));

        EXPECT_EQ(turn.action, kong);
        EXPECT_EQ(turn.step, TurnStep::Kong);
        const DiscardRating before = BestDiscardRating(counts, MeldCountOf(view), view);
        const DiscardRating after = RateKong(view, counts, kong);
        EXPECT_EQ(turn.shantenBefore, before.shanten);
        EXPECT_EQ(turn.kongShantenAfter, after.shanten);
        EXPECT_EQ(turn.kongAcceptanceAfter, after.acceptance);
    }

    TEST(ComputerExplainTest, AWinNeedsOnlyTheStep) {
        // East waits on the red dragon, declares with the 9p it draws and draws the red dragon two laps later.
        HandModel model = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Rd"));
        Lap(model, "9p Wd Gd Nw", {kSeat0});
        const TableView view = MakeTableView(model, kSeat0);

        const TurnExplanation turn = TurnOf(ExplainAdvice(view));

        EXPECT_EQ(turn.step, TurnStep::DeclareWin);
        EXPECT_EQ(turn.action, HandAction::MakeDeclareWin());
        EXPECT_TRUE(turn.alternatives.empty());
        EXPECT_TRUE(turn.waits.empty());
    }

    // ----- the claims -----

    TEST(ComputerExplainTest, APungThatLowersTheShantenCarriesItsRating) {
        const HandModel model = WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        const TableView view = MakeTableView(model, kSeat1);

        const ClaimExplanation claim = ClaimOf(ExplainAdvice(view));

        EXPECT_EQ(claim.action, kPung);
        EXPECT_EQ(claim.action, PlayersChoice(view));
        EXPECT_EQ(claim.step, ClaimStep::Claim);
        EXPECT_EQ(claim.shantenBefore, 1);
        ASSERT_TRUE(claim.chosen.has_value());
        const DiscardRating rated = RateClaim(view, kPung);
        EXPECT_EQ(claim.chosen->after.shanten, rated.shanten);
        EXPECT_EQ(claim.chosen->after.acceptance, rated.acceptance);
        EXPECT_EQ(claim.chosen->after.shanten, 0);
        EXPECT_TRUE(claim.chosen->acceptable);
        EXPECT_EQ(claim.chosen->rank, ClaimRank(ClaimKind::Pung));
        EXPECT_TRUE(claim.alternatives.empty());
    }

    TEST(ComputerExplainTest, AChowCarriesItsRatingAndTheOtherListedClaimsComeAfterIt) {
        // 4p 6p and 5p 5p beside 123s 789s: the 5p can be punged or taken as the chow 4p 5p 6p.
        const HandModel model = WindowModel("4p 6p 5p 5p 1s 2s 3s 7s 8s 9s Nw Nw Wd", "5p");
        const TableView view = MakeTableView(model, kSeat1);
        const int before = Shanten(CountTiles(view.own.concealed), 0);
        const eastl::vector<RatedClaim> rated = RateClaims(view, before, kNormalProfile);
        ASSERT_GE(rated.size(), 2u);

        const ClaimExplanation claim = ClaimOf(ExplainAdvice(view));

        EXPECT_EQ(claim.action, PlayersChoice(view));
        ASSERT_TRUE(claim.chosen.has_value());
        bool found = false;
        for (const RatedClaim& option : rated) {
            if (option.action == claim.action) {
                found = true;
                EXPECT_EQ(Digest(*claim.chosen), Digest(option));
            }
        }
        EXPECT_TRUE(found);
        // Alternatives: every other listed claim is a candidate; acceptable ones first, then the rest, no more than two.
        EXPECT_LE(claim.alternatives.size(), static_cast<size_t>(kExplainAlternatives));
        EXPECT_EQ(claim.alternatives.size(), std::min<size_t>(rated.size() - 1, kExplainAlternatives));
        bool sawUnacceptable = false;
        for (const RatedClaim& option : claim.alternatives) {
            EXPECT_NE(option.action, claim.action);
            EXPECT_TRUE(!sawUnacceptable || !option.acceptable) << "acceptable claims come first";
            sawUnacceptable = sawUnacceptable || !option.acceptable;
        }
        for (const RatedClaim& option : claim.alternatives) {
            EXPECT_FALSE(IsBetterClaim(option, *claim.chosen) && option.acceptable) << "the chosen claim is a best acceptable one";
        }
    }

    TEST(ComputerExplainTest, AChowIsExplainedWithItsRatingToo) {
        const HandModel model = WindowModel("4p 6p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        const TableView view = MakeTableView(model, kSeat1);

        const ClaimExplanation claim = ClaimOf(ExplainAdvice(view));

        EXPECT_EQ(claim.action, HandAction::MakeClaim(ClaimKind::Chow, T("4p")));
        EXPECT_EQ(claim.step, ClaimStep::Claim);
        ASSERT_TRUE(claim.chosen.has_value());
        EXPECT_EQ(claim.chosen->after.shanten, RateClaim(view, claim.action).shanten);
        EXPECT_EQ(claim.chosen->rank, ClaimRank(ClaimKind::Chow));
    }

    TEST(ComputerExplainTest, PassingAWaitingHandCarriesTheWaitsOfTheHeldTiles) {
        // 1s-6s 7m-9m Rd Rd 4p 4p waits on the red dragon and on 4p but has not declared; the pung is listed and passed. East throws the
        // red dragon: one copy of it is left unseen, the 4p has two (the other two are East's, who does not show them).
        const HandModel model = WindowModel("Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p", "Rd");
        const TableView view = MakeTableView(model, kSeat1);

        const ClaimExplanation claim = ClaimOf(ExplainAdvice(view));

        EXPECT_EQ(claim.action, kPass);
        EXPECT_EQ(claim.step, ClaimStep::WaitingHand);
        EXPECT_FALSE(claim.chosen.has_value());
        std::vector<std::pair<std::string, int>> expected;
        for (const Tile tile : WinningTiles(view.own)) {
            expected.emplace_back(std::string(ToString(tile)), UnseenCopies(view, tile));
        }
        EXPECT_EQ(Listed(claim.waits), expected);
        EXPECT_EQ(Listed(claim.waits), (std::vector<std::pair<std::string, int>>{{"4p", 2}, {"Rd", 1}}));
    }

    TEST(ComputerExplainTest, PassingWithNoGainListsTheBestClaimAsNotAcceptable) {
        // Four partial sets, a pair of 5m and three single tiles: the pung of the 5m leaves the shanten at 3.
        const HandModel model = WindowModel("3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw 5m 5m", "5m");
        const TableView view = MakeTableView(model, kSeat1);

        const ClaimExplanation claim = ClaimOf(ExplainAdvice(view));

        EXPECT_EQ(claim.action, kPass);
        EXPECT_EQ(claim.step, ClaimStep::NoGain);
        EXPECT_FALSE(claim.chosen.has_value());
        ASSERT_EQ(claim.alternatives.size(), 1u);
        EXPECT_EQ(claim.alternatives[0].action, kPung);
        EXPECT_FALSE(claim.alternatives[0].acceptable);
        EXPECT_EQ(claim.alternatives[0].after.shanten, RateClaim(view, kPung).shanten);
        EXPECT_EQ(claim.alternatives[0].after.shanten, claim.shantenBefore);
    }

    TEST(ComputerExplainTest, AMahjongClaimNeedsOnlyTheStep) {
        // Seat 1 waits on the red dragon and on 4p (shanpon) and has declared with its first discard; East throws the Rd.
        FixedWallSpec spec;
        spec.hands[kSeat0] = kEast;
        spec.hands[kSeat1] = "Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p";
        spec.hands[kSeat2] = kOther2;
        spec.hands[kSeat3] = kOther3;
        spec.live = "9p Wd Gd Nw Rd";
        HandModel model = MakeModel(spec);
        Lap(model, "9p Wd Gd Nw", {kSeat1});
        ASSERT_TRUE(model.Discard(kSeat0, T("Rd")).has_value());
        const TableView view = MakeTableView(model, kSeat1);

        const ClaimExplanation claim = ClaimOf(ExplainAdvice(view));

        EXPECT_EQ(claim.step, ClaimStep::Mahjong);
        EXPECT_EQ(claim.action, HandAction::MakeClaim(ClaimKind::Mahjong));
        EXPECT_TRUE(claim.alternatives.empty());
    }

    TEST(ComputerExplainTest, ARobbedKongWindowTakesMahjong) {
        HandModel model = UpgradeModel({kSeat2});
        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        const TableView view = MakeTableView(model, kSeat2);

        const ClaimExplanation claim = ClaimOf(ExplainAdvice(view));

        EXPECT_EQ(claim.step, ClaimStep::Mahjong);
        EXPECT_EQ(claim.action, HandAction::MakeClaim(ClaimKind::Mahjong));
    }

    // ----- nothing to explain, no peeking, every action -----

    TEST(ComputerExplainTest, NothingToExplainWhenNothingIsListed) {
        EXPECT_FALSE(ExplainAdvice(TableView{}).has_value());
        const HandModel model = MakeModel(SpecOf(kTwoSided, "Gd"));
        EXPECT_FALSE(ExplainAdvice(MakeTableView(model, kSeat1)).has_value());  // East acts
    }

    TEST(ComputerExplainTest, TwoModelsThatDifferOnlyInTheOtherHandsAndTheWallGiveEqualExplanations) {
        const auto turn = [](bool other) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew";
            spec.hands[kSeat1] = other ? kQuiet3 : kQuiet1;
            spec.hands[kSeat2] = other ? kQuiet1 : kQuiet2;
            spec.hands[kSeat3] = other ? kQuiet2 : kQuiet3;
            spec.live = std::string("Ew ") + (other ? "Gd Wd Nw" : "Nw Wd Gd");
            return MakeModel(spec);
        };
        const std::optional<Explanation> a = ExplainAdvice(MakeTableView(turn(false), kSeat0));
        const std::optional<Explanation> b = ExplainAdvice(MakeTableView(turn(true), kSeat0));
        ASSERT_TRUE(a.has_value());
        EXPECT_EQ(Digest(a), Digest(b));

        const auto window = [](const char* east, const char* seat3) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = east;
            spec.hands[kSeat1] = "4p 6p 5p 5p 1s 2s 3s 7s 8s 9s Nw Nw Wd";
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = seat3;
            spec.live = "5p";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            return model;
        };
        const HandModel c = window("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Rd Wd", kQuiet3);
        const HandModel d = window("2p 8p 2s 5s 8s 2m 5m 8m Ww Gd Rd Wd 9p", "4p 7p 4s 7s 4m 7m 1s 1m 9s 9m Rd Wd Gd");
        const std::optional<Explanation> windowA = ExplainAdvice(MakeTableView(c, kSeat1));
        ASSERT_TRUE(windowA.has_value());
        EXPECT_EQ(Digest(windowA), Digest(ExplainAdvice(MakeTableView(d, kSeat1))));
    }

    TEST(ComputerExplainTest, TheExplainedActionAndItsNumbersAreTheChoosersOnRandomHands) {
        int discards = 0;
        int claims = 0;
        for (int index = 0; index < kRandomHands; ++index) {
            const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
            MahjongSettings settings;
            settings.flowers = (index & 1) != 0;
            Match match;
            ASSERT_TRUE(match.Start(settings, seed).has_value());
            ComputerTable table(seed);
            for (int step = 0; step < kStepsPerHand && match.Hand().GetPhase() != Phase::Finished; ++step) {
                const Seat actor = ComputerTable::NextActor(match.Hand());
                ASSERT_NE(actor, kNoSeat);
                const TableView view = MakeTableView(match.Hand(), actor);
                const std::optional<Explanation> explained = ExplainAdvice(view);
                ASSERT_TRUE(explained.has_value()) << "hand " << index << " step " << step;
                EXPECT_EQ(explained->Action(), PlayersChoice(view)) << "hand " << index << " step " << step;
                if (explained->turn.has_value()) {
                    ++discards;
                    const TurnExplanation& turn = *explained->turn;
                    const TileCounts counts = CountTiles(view.own.concealed);
                    if (turn.step == TurnStep::Discard) {
                        const DiscardRating best = BestDiscardRating(counts, MeldCountOf(view), view);
                        EXPECT_EQ(turn.chosen.shanten, best.shanten) << "hand " << index << " step " << step;
                        EXPECT_EQ(turn.chosen.acceptance, best.acceptance) << "hand " << index << " step " << step;
                        ExpectNumbersOf(turn.chosen, RatedDiscard(view, turn.action.tile));
                        for (const TurnOption& option : turn.alternatives) {
                            ExpectNumbersOf(option, RatedDiscard(view, option.tile));
                            EXPECT_NE(option.tile, turn.action.tile);
                        }
                    }
                    if (turn.step == TurnStep::Declare || turn.step == TurnStep::KeepWaiting) {
                        EXPECT_EQ(Listed(turn.waits), WaitsOfThirteen(view, turn.action.tile));
                    }
                    if (turn.step == TurnStep::Kong) {
                        const DiscardRating after = RateKong(view, counts, turn.action);
                        EXPECT_EQ(turn.kongShantenAfter, after.shanten);
                        EXPECT_EQ(turn.kongAcceptanceAfter, after.acceptance);
                    }
                } else {
                    ++claims;
                    const ClaimExplanation& claim = *explained->claim;
                    if (claim.step == ClaimStep::Claim) {
                        ASSERT_TRUE(claim.chosen.has_value());
                        EXPECT_EQ(claim.chosen->after.shanten, RateClaim(view, claim.action).shanten);
                        EXPECT_EQ(claim.chosen->after.acceptance, RateClaim(view, claim.action).acceptance);
                    }
                    if (claim.step == ClaimStep::NoGain) {
                        EXPECT_EQ(claim.action, kPass);
                        for (const RatedClaim& option : claim.alternatives) {
                            EXPECT_FALSE(option.acceptable);
                        }
                    }
                }
                ASSERT_TRUE(table.Step(match.Hand()).has_value());
            }
        }
        EXPECT_GT(discards, kRandomHands);
        EXPECT_GT(claims, 0);
    }

}  // namespace BECore::Mahjong::Tests
