#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/ComputerWorld.h>
#include <Mahjong/Game/DiscardDanger.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongRng.h>
#include <gtest/gtest.h>

/**
 * `DiscardDanger.h`: the danger of a discard against a declared opponent, from public data only. The rules of the reading are tried
 * on views made by hand (every number of a kind is then written in the test), the history they read (`discardedKinds`, `passedKinds`)
 * on a played table, plus no peeking and a view of a sampled world. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr Seat kViewer = kSeat1;
        constexpr Seat kDeclared = kSeat0;
        constexpr uint32_t kWorldSeed = 7;

        /// Seat `kViewer` is to discard, `kDeclared` has declared; nothing is seen yet (every kind has its four copies unseen).
        TableView DeclaredView() {
            TableView view;
            view.seat = kViewer;
            view.phase = Phase::Discard;
            view.currentSeat = kViewer;
            view.seats[kDeclared].callingDeclared = true;
            return view;
        }

        /// Makes `count` copies of the kind visible.
        void See(TableView& view, const char* tile, int count = kCopiesPerTile) {
            view.visible[T(tile).Id()] = static_cast<uint8_t>(count);
        }

        Danger Grade(const TableView& view, const char* tile) {
            return DangerOf(view, T(tile));
        }

        // Seat 1 waits on the 5p (4p 6p, a pair of south winds) and has declared; seat 0 is to act at the end of the two laps below.
        constexpr const char* kViewerHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Ew Gd Rd Wd";
        constexpr const char* kWaitingHand = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Sw Sw";
        constexpr const char* kSeat2Hand = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd";
        constexpr const char* kSeat3Hand = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd";

        /**
         * Lap 1: Ew (seat 0), Rd (seat 1), Nw (seat 2), Ww (seat 3). Lap 2: Gd (seat 0), Wd (seat 1, declaring), 9m (seat 2), Ew (seat 3).
         * Seat 0 is to act. So, for seat 0: seat 1 declared with the Wd; the 9m and the Ew came after it (passed), the Ew, Rd, Nw, Ww
         * and Gd before it or by itself.
         */
        FixedWallSpec TwoLapsSpec(const char* seat2, const char* seat3, const char* tail) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kViewerHand;
            spec.hands[kSeat1] = kWaitingHand;
            spec.hands[kSeat2] = seat2;
            spec.hands[kSeat3] = seat3;
            spec.live = std::string("Ew Rd Nw Ww Gd Wd 9m Ew Rd ") + tail;
            return spec;
        }

        HandModel TwoLapsModel(const char* seat2 = kSeat2Hand, const char* seat3 = kSeat3Hand, const char* tail = "1p 2p") {
            HandModel model = MakeModel(TwoLapsSpec(seat2, seat3, tail));
            Lap(model, "Ew Rd Nw Ww");
            Lap(model, "Gd Wd 9m Ew", {kSeat1});
            EXPECT_EQ(model.GetPhase(), Phase::Discard);
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            return model;
        }

        uint8_t Copies(const TileCounts& counts, const char* tile) {
            return counts[T(tile).Id()];
        }

    }  // namespace

    TEST(DiscardDangerTest, WithNoDeclaredOpponentEveryKindIsSafe) {
        TableView view = DeclaredView();
        view.seats[kDeclared].callingDeclared = false;

        EXPECT_FALSE(HasDeclaredOpponent(view));
        for (const Danger grade : DiscardDangers(view)) {
            EXPECT_EQ(grade, Danger::Safe);
        }
        EXPECT_EQ(Grade(view, "5p"), Danger::Safe);
    }

    TEST(DiscardDangerTest, TheDeclaredSeatIsNeverTheViewer) {
        TableView view = DeclaredView();
        view.seats[kDeclared].callingDeclared = false;
        view.seats[kViewer].callingDeclared = true;  // the viewer's own declaration is no danger to it

        EXPECT_FALSE(HasDeclaredOpponent(view));
        EXPECT_EQ(Grade(view, "5p"), Danger::Safe);
        view.seats[kSeat2].callingDeclared = true;
        EXPECT_TRUE(HasDeclaredOpponent(view));
        EXPECT_EQ(Grade(view, "5p"), Danger::High);
    }

    TEST(DiscardDangerTest, AnHonourIsSafeWithAllFourCopiesSeenAndLowWithThreeSeen) {
        TableView view = DeclaredView();
        EXPECT_EQ(Grade(view, "Ew"), Danger::Low);  // an honour has the pair and the pung way only
        See(view, "Ew", kCopiesPerTile - 1);
        EXPECT_EQ(Grade(view, "Ew"), Danger::Low);
        See(view, "Ew");
        EXPECT_EQ(Grade(view, "Ew"), Danger::Safe);
        See(view, "Rd", kCopiesPerTile);
        EXPECT_EQ(Grade(view, "Rd"), Danger::Safe);
    }

    TEST(DiscardDangerTest, ASuitKindWithAllFourCopiesSeenIsNotSafeWhileASequenceWayLives) {
        TableView view = DeclaredView();
        See(view, "5p");
        // The opponent holds no 5p, and can still wait on the one thrown: 3p 4p, 4p 6p and 6p 7p are all open.
        EXPECT_EQ(Grade(view, "5p"), Danger::High);

        See(view, "1p");
        EXPECT_EQ(Grade(view, "1p"), Danger::Low);  // the 2p 3p way lives
        See(view, "2p");
        EXPECT_EQ(Grade(view, "1p"), Danger::Safe);  // now every way is dead too
    }

    TEST(DiscardDangerTest, TheGradeFollowsTheNumberOfLiveSequenceWays) {
        TableView view = DeclaredView();

        EXPECT_EQ(Grade(view, "5p"), Danger::High);
        EXPECT_EQ(Grade(view, "3s"), Danger::High);
        EXPECT_EQ(Grade(view, "7m"), Danger::High);
        EXPECT_EQ(Grade(view, "2p"), Danger::Medium);
        EXPECT_EQ(Grade(view, "8p"), Danger::Medium);
        EXPECT_EQ(Grade(view, "1p"), Danger::Low);
        EXPECT_EQ(Grade(view, "9m"), Danger::Low);
        EXPECT_EQ(Grade(view, "Gd"), Danger::Low);
    }

    TEST(DiscardDangerTest, AKindWhosePartnersAreOutLosesItsWays) {
        TableView view = DeclaredView();
        See(view, "4p");                            // the ways 3p 4p and 4p 6p of the 5p die
        EXPECT_EQ(Grade(view, "5p"), Danger::Low);  // the 6p 7p way is left
        See(view, "3p");
        See(view, "4p", 0);
        See(view, "6p");  // the ways 3p 4p (3p out) and 4p 6p (6p out) die, 6p 7p too
        EXPECT_EQ(Grade(view, "5p"), Danger::Low) << "all ways dead, but a copy of the 5p is unseen: the pair way lives";
        See(view, "5p");
        EXPECT_EQ(Grade(view, "5p"), Danger::Safe);
    }

    TEST(DiscardDangerTest, ACopyOfAPartnerKindIsEnoughToKeepAWayLive) {
        TableView view = DeclaredView();
        See(view, "4p", kCopiesPerTile - 1);
        See(view, "6p", kCopiesPerTile - 1);
        EXPECT_EQ(Grade(view, "5p"), Danger::High);
        See(view, "6p");                            // the way 4p 6p and 6p 7p die
        EXPECT_EQ(Grade(view, "5p"), Danger::Low);  // the 3p 4p way is left
    }

    TEST(DiscardDangerTest, ATerminalAndATwoCountOnlyTheWaysInsideTheSuit) {
        TableView view = DeclaredView();
        See(view, "3p");  // the only way of a 1p (2p 3p) and both ways of a 2p (1p 3p, 3p 4p) die
        EXPECT_EQ(Grade(view, "1p"), Danger::Low);
        EXPECT_EQ(Grade(view, "2p"), Danger::Low);

        TableView upper = DeclaredView();
        See(upper, "7p");  // the only way of a 9p (7p 8p) dies, and both of an 8p (6p 7p, 7p 9p)
        EXPECT_EQ(Grade(upper, "9p"), Danger::Low);
        EXPECT_EQ(Grade(upper, "8p"), Danger::Low);
        See(upper, "9p");
        EXPECT_EQ(Grade(upper, "9p"), Danger::Safe);
    }

    TEST(DiscardDangerTest, ASuitNeverBorrowsAPartnerFromTheNeighbouringSuit) {
        TableView view = DeclaredView();
        See(view, "9p");  // the id before the 1s
        EXPECT_EQ(Grade(view, "1s"), Danger::Low);
        EXPECT_EQ(Grade(view, "2s"), Danger::Medium);
        EXPECT_EQ(Grade(view, "8p"), Danger::Low) << "the 9p is out: 7p 9p is dead, 6p 7p is left";
        See(view, "1s");  // the id after the 9p
        EXPECT_EQ(Grade(view, "9p"), Danger::Low) << "no pair way, but the way 7p 8p lives; the 1s is no partner";
    }

    TEST(DiscardDangerTest, TheWorstOfTheDeclaredOpponentsWins) {
        TableView view = DeclaredView();
        view.seats[kSeat2].callingDeclared = true;
        view.seats[kDeclared].passedKinds[T("5p").Id()] = 1;

        EXPECT_EQ(Grade(view, "5p"), Danger::High) << "safe against seat 0, but seat 2 has not passed it";
        view.seats[kSeat2].passedKinds[T("5p").Id()] = 1;
        EXPECT_EQ(Grade(view, "5p"), Danger::Safe);
        view.seats[kSeat2].passedKinds[T("5p").Id()] = 0;
        view.seats[kSeat2].discardedKinds[T("5p").Id()] = 1;
        EXPECT_EQ(Grade(view, "5p"), Danger::Low) << "seat 2 discarded it itself (at most Low)";
    }

    TEST(DiscardDangerTest, AKindPassedAfterTheDeclarationIsSafeWhateverItsWays) {
        TableView view = DeclaredView();
        view.seats[kDeclared].passedKinds[T("5p").Id()] = 2;
        view.seats[kDeclared].discardedKinds[T("5p").Id()] = 1;

        EXPECT_EQ(Grade(view, "5p"), Danger::Safe);
        EXPECT_EQ(Grade(view, "4p"), Danger::High);
    }

    TEST(DiscardDangerTest, AnOwnDiscardLowersTheGradeToLowAtMostButNeverRaisesIt) {
        TableView view = DeclaredView();
        view.seats[kDeclared].discardedKinds[T("5p").Id()] = 1;  // High -> Low
        view.seats[kDeclared].discardedKinds[T("2p").Id()] = 1;  // Medium -> Low
        view.seats[kDeclared].discardedKinds[T("1p").Id()] = 1;  // Low stays
        view.seats[kDeclared].discardedKinds[T("Ew").Id()] = 2;  // Low stays
        view.seats[kDeclared].discardedKinds[T("Rd").Id()] = 1;
        See(view, "Rd");  // Safe stays Safe

        EXPECT_EQ(Grade(view, "5p"), Danger::Low);
        EXPECT_EQ(Grade(view, "2p"), Danger::Low);
        EXPECT_EQ(Grade(view, "1p"), Danger::Low);
        EXPECT_EQ(Grade(view, "Ew"), Danger::Low);
        EXPECT_EQ(Grade(view, "Rd"), Danger::Safe);
        EXPECT_EQ(Grade(view, "4p"), Danger::High) << "another kind is not touched";
    }

    TEST(DiscardDangerTest, ATileThatIsNotPlayableIsSafeAndTheTableAgreesWithDangerOf) {
        TableView view = DeclaredView();
        See(view, "5p", 2);
        view.seats[kDeclared].passedKinds[T("1m").Id()] = 1;
        const DangerTable table = DiscardDangers(view);

        EXPECT_EQ(DangerOf(view, Tile{}), Danger::Safe);
        EXPECT_EQ(DangerOf(view, T("F1")), Danger::Safe);
        for (uint8_t id = 0; id < kKindCount; ++id) {
            EXPECT_EQ(table[id], DangerOf(view, Tile{id})) << ToString(Tile{id});
        }
    }

    TEST(DiscardDangerTest, TheViewCountsTheDiscardsOfEverySeatAndWhatPassedTheDeclaration) {
        const HandModel model = TwoLapsModel();
        const TableView view = MakeTableView(model, kSeat0);

        ASSERT_TRUE(view.seats[kSeat1].callingDeclared);
        EXPECT_EQ(Copies(view.seats[kSeat0].discardedKinds, "Ew"), 1);
        EXPECT_EQ(Copies(view.seats[kSeat0].discardedKinds, "Gd"), 1);
        EXPECT_EQ(Copies(view.seats[kSeat1].discardedKinds, "Rd"), 1);
        EXPECT_EQ(Copies(view.seats[kSeat1].discardedKinds, "Wd"), 1) << "the declaring discard is a discard of its seat";
        EXPECT_EQ(Copies(view.seats[kSeat2].discardedKinds, "9m"), 1);
        EXPECT_EQ(Copies(view.seats[kSeat3].discardedKinds, "Ew"), 1);
        int discards = 0;
        for (const PublicSeat& seat : view.seats) {
            for (const uint8_t copies : seat.discardedKinds) {
                discards += copies;
            }
        }
        EXPECT_EQ(discards, 8);

        // Only what the others discarded after the declaring discard of seat 1: the 9m and the Ew.
        int passed = 0;
        for (const uint8_t copies : view.seats[kSeat1].passedKinds) {
            passed += copies;
        }
        EXPECT_EQ(passed, 2);
        EXPECT_EQ(Copies(view.seats[kSeat1].passedKinds, "9m"), 1);
        EXPECT_EQ(Copies(view.seats[kSeat1].passedKinds, "Ew"), 1);
        for (const Seat seat : {kSeat0, kSeat2, kSeat3}) {
            for (const uint8_t copies : view.seats[seat].passedKinds) {
                EXPECT_EQ(copies, 0) << "seat " << seat << " has not declared";
            }
        }
    }

    TEST(DiscardDangerTest, AKindDiscardedBeforeTheDeclarationIsNotSafeAndOneDiscardedAfterIsSafe) {
        const HandModel model = TwoLapsModel();
        const TableView view = MakeTableView(model, kSeat0);

        ASSERT_TRUE(HasDeclaredOpponent(view));
        EXPECT_EQ(Grade(view, "9m"), Danger::Safe) << "discarded by seat 2 after the declaration";
        EXPECT_EQ(Grade(view, "Ew"), Danger::Safe) << "discarded before and after: after counts";
        EXPECT_EQ(Grade(view, "Ww"), Danger::Low) << "discarded by seat 3 before the declaration only";
        EXPECT_EQ(Grade(view, "Nw"), Danger::Low);
        EXPECT_EQ(Grade(view, "Gd"), Danger::Low) << "discarded by the viewer itself just before the declaration";
        EXPECT_EQ(Grade(view, "Rd"), Danger::Low) << "the declared seat's own discard, before its declaration";
        EXPECT_EQ(Grade(view, "5p"), Danger::High);
        EXPECT_EQ(Grade(view, "Sw"), Danger::Low);
    }

    TEST(DiscardDangerTest, TwoModelsThatDifferOnlyInHiddenTilesGiveTheSameReading) {
        const HandModel first = TwoLapsModel();
        // Seats 2 and 3 hold other tiles and the wall behind the draw of seat 0 is another: nothing the viewer sees changes.
        const HandModel second = TwoLapsModel("2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd", "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Sw Ew Nw", "2s 3s");

        const TableView viewA = MakeTableView(first, kSeat0);
        const TableView viewB = MakeTableView(second, kSeat0);

        EXPECT_EQ(viewA.visible, viewB.visible);
        EXPECT_EQ(DiscardDangers(viewA), DiscardDangers(viewB));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(viewA.seats[seat].discardedKinds, viewB.seats[seat].discardedKinds) << "seat " << seat;
            EXPECT_EQ(viewA.seats[seat].passedKinds, viewB.seats[seat].passedKinds) << "seat " << seat;
        }
    }

    TEST(DiscardDangerTest, TheViewOfASampledWorldStillGivesAReadingFromTheCountsAlone) {
        const HandModel model = TwoLapsModel();
        const TableView view = MakeTableView(model, kSeat0);
        MahjongRng rng(kWorldSeed);
        const std::optional<HandModel> world = SampleWorld(view, rng);
        ASSERT_TRUE(world.has_value());

        const TableView sampled = MakeTableView(*world, kSeat0);

        EXPECT_TRUE(HasDeclaredOpponent(sampled));
        for (const PublicSeat& seat : sampled.seats) {
            for (uint8_t id = 0; id < kKindCount; ++id) {
                EXPECT_EQ(seat.discardedKinds[id], 0) << "a sampled world has no moves";
                EXPECT_EQ(seat.passedKinds[id], 0);
            }
        }
        // The same reading as the real view with its history taken away: it rests on `visible` and the declarations.
        TableView bare = view;
        for (PublicSeat& seat : bare.seats) {
            seat.discardedKinds = {};
            seat.passedKinds = {};
        }
        EXPECT_EQ(DiscardDangers(sampled), DiscardDangers(bare));
        EXPECT_EQ(Grade(sampled, "9m"), Danger::Low) << "no history: the passed 9m is only a Low kind";
        EXPECT_EQ(Grade(sampled, "5p"), Danger::High);
    }

}  // namespace BECore::Mahjong::Tests
