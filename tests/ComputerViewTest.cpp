#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
#include <gtest/gtest.h>

/**
 * `MakeTableView`: what a seat is allowed to see of a hand, on fixed walls. The visible copies (own tiles, piles, open melds, not a
 * concealed kong), the public facts about the others, the live count, the legal actions, and the main property: a view holds nothing
 * of the other hands or of the live wall, so two models that differ only there give equal views and equal choices. Pure logic: no
 * engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kPungTiles = 3;
        constexpr int kKongTiles = 4;
        constexpr uint32_t kFirstRngSeed = 1;
        constexpr uint32_t kRngSeeds = 5;

        constexpr const char* kQuiet1 = "2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
        constexpr const char* kQuiet2 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd";
        constexpr const char* kQuiet3 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd";

        // Seat 1 holds two 5p, East discards a third and seat 1 claims the pung; after a lap seat 1 draws the fourth. Seats 2 and 3 wait
        // on the 5p (so seat 2 can rob the kong once it has declared).
        FixedWallSpec UpgradeSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = "5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            spec.hands[kSeat2] = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Ew Ew";
            spec.hands[kSeat3] = "4p 6p 1m 2m 3m 4s 5s 6s 7m 8m 9m Ww Ww";
            spec.live = "5p Rd Gd Wd 5p";
            spec.loose = "9m 9s";
            return spec;
        }

        /// Seat 1 is to act with the fourth 5p drawn and an open pung of 5p laid; the seats in `declaring` have declared.
        HandModel UpgradeModel(std::initializer_list<Seat> declaring = {}) {
            HandModel model = MakeModel(UpgradeSpec());
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            EXPECT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
            Lap(model, "Wd Rd Gd Wd", declaring);
            EXPECT_EQ(model.CurrentSeat(), kSeat1);
            return model;
        }

        int VisibleTotal(const TableView& view) {
            int total = 0;
            for (const uint8_t copies : view.visible) {
                total += copies;
            }
            return total;
        }

        int Visible(const TableView& view, const char* tile) {
            return view.visible[T(tile).Id()];
        }

        /// Everything a view holds, as text, to compare two views.
        std::string ViewText(const TableView& view) {
            std::string text = "seat " + std::to_string(view.seat) + " phase " + Name(view.phase) + " current " + std::to_string(view.currentSeat) + " pending " +
                               std::string(ToString(view.pendingTile)) + " winds " + Name(view.seatWind) + "/" + Name(view.roundWind);
            text += " own";
            for (const Tile tile : view.own.concealed) {
                text += " " + std::string(ToString(tile));
            }
            for (const Meld& meld : view.own.melds) {
                text += " | meld " + Name(meld.kind) + " " + std::string(ToString(meld.first)) + " " + std::to_string(meld.concealed);
            }
            text += " bonus " + std::to_string(view.own.bonus.size()) + " calling " + std::to_string(view.own.callingDeclared) + "/" + std::to_string(view.own.callingAtDiscardCount) + "/" +
                    std::to_string(view.own.discardCount);
            for (const PublicSeat& seat : view.seats) {
                text += " || open";
                for (const Meld& meld : seat.openMelds) {
                    text += " " + Name(meld.kind) + std::string(ToString(meld.first));
                }
                text += " ck " + std::to_string(seat.concealedKongs) + " bonus " + std::to_string(seat.bonusCount) + " disc " + std::to_string(seat.discardCount) + " calling " +
                        std::to_string(seat.callingDeclared);
            }
            text += " visible";
            for (const uint8_t copies : view.visible) {
                text += " " + std::to_string(copies);
            }
            text += " live " + std::to_string(view.liveCount) + " legal";
            for (const HandAction& action : view.legal) {
                text += " " + Name(action.kind) + ":" + std::string(ToString(action.tile)) + std::to_string(action.declareCalling);
            }
            return text;
        }

        HandAction Choice(const TableView& view, uint32_t rngSeed) {
            MahjongRng rng(rngSeed);
            return ChooseTurnAction(view, rng);
        }

    }  // namespace

    TEST(ComputerViewTest, VisibleCountsTheOwnTilesThePileAndAnOpenPungOfAnotherSeat) {
        const HandModel model = UpgradeModel();

        // Seat 0: its 13 tiles; the pile Wd Rd Gd Wd (the 5p of the pung left the pile); the open pung of seat 1.
        const TableView view = MakeTableView(model, kSeat0);

        EXPECT_EQ(VisibleTotal(view), 13 + 4 + kPungTiles);
        EXPECT_EQ(Visible(view, "5p"), kPungTiles);
        EXPECT_EQ(Visible(view, "Wd"), 2);  // both in the pile
        EXPECT_EQ(Visible(view, "Rd"), 2);  // one held, one in the pile
        EXPECT_EQ(Visible(view, "Gd"), 2);
        EXPECT_EQ(Visible(view, "1p"), 1);
        EXPECT_EQ(Visible(view, "9s"), 0);
        EXPECT_EQ(UnseenCopies(view, T("5p")), 1);
        EXPECT_EQ(UnseenCopies(view, T("9s")), kCopiesPerTile);

        // The public facts of seat 1: the pung is named, nothing is hidden in a concealed kong.
        ASSERT_EQ(view.seats[kSeat1].openMelds.size(), 1u);
        EXPECT_EQ(view.seats[kSeat1].openMelds[0].kind, MeldKind::Pung);
        EXPECT_EQ(view.seats[kSeat1].openMelds[0].first, T("5p"));
        EXPECT_EQ(view.seats[kSeat1].concealedKongs, 0);
        EXPECT_TRUE(view.seats[kSeat0].openMelds.empty());
        EXPECT_EQ(view.seats[kSeat0].discardCount, 2);  // the 5p and the white dragon
        EXPECT_EQ(view.seats[kSeat0].discardCount, model.GetHand(kSeat0).discardCount);
    }

    TEST(ComputerViewTest, TheOwnerSeesItsOwnPungOnceAndItsDrawnTile) {
        const HandModel model = UpgradeModel();

        const TableView view = MakeTableView(model, kSeat1);

        EXPECT_EQ(Visible(view, "5p"), kPungTiles + 1);  // the pung and the fourth tile in hand, not the pung twice
        EXPECT_EQ(UnseenCopies(view, T("5p")), 0);
        EXPECT_EQ(view.seat, kSeat1);
        EXPECT_EQ(view.phase, Phase::Discard);
        EXPECT_EQ(view.currentSeat, kSeat1);
        EXPECT_EQ(view.own.melds.size(), 1u);
        EXPECT_EQ(view.seatWind, Wind::South);
        EXPECT_EQ(view.roundWind, Wind::East);
    }

    TEST(ComputerViewTest, ARobbedKongShowsItsTileToTheOthersWithoutCountingItTwiceForTheOwner) {
        HandModel model = UpgradeModel({kSeat2});
        ASSERT_TRUE(model.Apply(kSeat1, HandAction::MakeUpgradeKong(T("5p"))).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);

        const TableView robber = MakeTableView(model, kSeat2);
        const TableView owner = MakeTableView(model, kSeat1);

        EXPECT_EQ(robber.phase, Phase::RobKong);
        EXPECT_EQ(robber.pendingTile, T("5p"));
        EXPECT_EQ(Visible(robber, "5p"), kKongTiles);  // the open pung and the tile that is being added to it
        EXPECT_EQ(Visible(owner, "5p"), kKongTiles);   // the same four tiles: three in the pung, one in its hand
        EXPECT_TRUE(Has(robber.legal, HandAction::MakeClaim(ClaimKind::Mahjong)));
        EXPECT_TRUE(owner.legal.empty());
    }

    TEST(ComputerViewTest, AConcealedKongOfAnotherSeatIsOnlyCountedNeverNamed) {
        FixedWallSpec spec;
        spec.hands[kSeat0] = "Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd";
        spec.hands[kSeat1] = kQuiet1;
        spec.hands[kSeat2] = kQuiet2;
        spec.hands[kSeat3] = kQuiet3;
        spec.live = "Ew 2p";
        spec.loose = "9p";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Apply(kSeat0, HandAction::MakeConcealedKong(T("Ew"))).has_value());
        ASSERT_TRUE(model.Apply(kSeat0, HandAction::MakeDiscard(T("Gd"))).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Discard);
        ASSERT_EQ(model.CurrentSeat(), kSeat1);

        const TableView other = MakeTableView(model, kSeat1);
        const TableView owner = MakeTableView(model, kSeat0);

        EXPECT_EQ(other.seats[kSeat0].concealedKongs, 1);
        EXPECT_TRUE(other.seats[kSeat0].openMelds.empty());
        EXPECT_EQ(Visible(other, "Ew"), 0) << "a concealed kong is face down";
        EXPECT_EQ(UnseenCopies(other, T("Ew")), kCopiesPerTile);
        EXPECT_EQ(Visible(other, "Gd"), 2);  // the discard and the seat's own
        EXPECT_EQ(Visible(owner, "Ew"), kKongTiles) << "the owner sees its own kong";
        EXPECT_EQ(owner.own.melds.size(), 1u);
        EXPECT_TRUE(owner.own.melds[0].concealed);
    }

    TEST(ComputerViewTest, TheOthersDeclarationBonusTilesAndDiscardsArePublic) {
        FixedWallSpec spec;
        spec.hands[kSeat0] = "F1 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";  // the 1p from the loose tiles replaces the flower
        spec.hands[kSeat1] = kQuiet1;
        spec.hands[kSeat2] = kQuiet2;
        spec.hands[kSeat3] = kQuiet3;
        spec.live = "9p Wd Gd Nw Rd";
        spec.loose = "1p";
        HandModel model = MakeModel(spec, kSeat0, Wind::East, true);
        // East then waits on the red dragon and declares with its first discard, the 9p it draws.
        ASSERT_EQ(model.GetHand(kSeat0).bonus.size(), 1u);
        Lap(model, "9p Wd Gd Nw", {kSeat0});

        const TableView view = MakeTableView(model, kSeat1);

        EXPECT_TRUE(view.seats[kSeat0].callingDeclared);
        EXPECT_FALSE(view.seats[kSeat1].callingDeclared);
        EXPECT_EQ(view.seats[kSeat0].bonusCount, 1);
        EXPECT_EQ(view.seats[kSeat1].bonusCount, 0);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(view.seats[seat].discardCount, 1) << "seat " << seat;
        }
        EXPECT_EQ(VisibleTotal(view), 13 + 4) << "the 13 own tiles and the four discards; the bonus tile is not counted";
        EXPECT_EQ(view.phase, Phase::Discard);
        EXPECT_EQ(view.currentSeat, kSeat0);
        EXPECT_TRUE(view.legal.empty());  // seat 1 has nothing to do
    }

    TEST(ComputerViewTest, TheLiveCountAndTheLegalActionsAreTheModels) {
        const HandModel model = UpgradeModel();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const TableView view = MakeTableView(model, seat);
            EXPECT_EQ(view.liveCount, static_cast<int>(model.GetWall().LiveCount())) << "seat " << seat;
            EXPECT_EQ(view.legal, model.LegalActions(seat)) << "seat " << seat;
        }
        EXPECT_FALSE(MakeTableView(model, kSeat1).legal.empty());
        EXPECT_TRUE(MakeTableView(model, kSeat2).legal.empty());
    }

    TEST(ComputerViewTest, AViewDoesNotChangeWhenOnlyTheOtherHandsAndTheLiveWallDiffer) {
        // The two models give seat 0 the same hand and the same first draw; every other tile of the other seats and of the wall differs.
        FixedWallSpec first;
        first.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        first.hands[kSeat1] = kQuiet1;
        first.hands[kSeat2] = kQuiet2;
        first.hands[kSeat3] = kQuiet3;
        first.live = "Wd Nw Ww Sw";
        FixedWallSpec second = first;
        second.hands[kSeat1] = "4p 7p 4s 7s 4m 7m Ew Ew Ew Gd Gd Rd Rd";
        second.hands[kSeat2] = "2p 2p 5p 5p 8p 8p 2s 2s 5s 5s 8s 8s Wd";
        second.hands[kSeat3] = "Ww Ww Nw Nw Sw Sw 1p 1p 1s 1s 1m 1m Nw";
        second.live = "Wd Gd Rd Wd";

        HandModel modelA = MakeModel(first);
        HandModel modelB = MakeModel(second);

        const TableView viewA = MakeTableView(modelA, kSeat0);
        const TableView viewB = MakeTableView(modelB, kSeat0);
        EXPECT_EQ(ViewText(viewA), ViewText(viewB));
        ASSERT_FALSE(viewA.legal.empty());
        for (uint32_t seed = kFirstRngSeed; seed < kFirstRngSeed + kRngSeeds; ++seed) {
            EXPECT_EQ(Choice(viewA, seed), Choice(viewB, seed)) << "rng seed " << seed;
        }

        // After the same discard the views of seat 0 (the pile, the live count) are still equal, and so are the others' public facts.
        ASSERT_TRUE(modelA.Apply(kSeat0, HandAction::MakeDiscard(T("Wd"))).has_value());
        ASSERT_TRUE(modelB.Apply(kSeat0, HandAction::MakeDiscard(T("Wd"))).has_value());
        ASSERT_EQ(modelA.GetPhase(), Phase::Discard);
        ASSERT_EQ(modelB.GetPhase(), Phase::Discard);
        EXPECT_EQ(ViewText(MakeTableView(modelA, kSeat0)), ViewText(MakeTableView(modelB, kSeat0)));
        EXPECT_EQ(MakeTableView(modelA, kSeat0).liveCount, MakeTableView(modelB, kSeat0).liveCount);
        // The hands of the other seats do differ: seat 1 sees something else of its own.
        EXPECT_NE(ViewText(MakeTableView(modelA, kSeat1)), ViewText(MakeTableView(modelB, kSeat1)));
    }

    TEST(ComputerViewTest, ANumberThatIsNoSeatGivesAnEmptyView) {
        const HandModel model = UpgradeModel();
        for (const Seat seat : {-1, kSeatCount, kSeatCount + 3}) {
            const TableView view = MakeTableView(model, seat);
            EXPECT_EQ(view.seat, kNoSeat) << "seat " << seat;
            EXPECT_TRUE(view.legal.empty());
            EXPECT_TRUE(view.own.concealed.empty());
            EXPECT_TRUE(view.own.melds.empty());
            EXPECT_EQ(VisibleTotal(view), 0);
            EXPECT_EQ(view.liveCount, 0);
            EXPECT_EQ(view.phase, Phase::Finished);
            for (const PublicSeat& other : view.seats) {
                EXPECT_TRUE(other.openMelds.empty());
                EXPECT_EQ(other.discardCount, 0);
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
