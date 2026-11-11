#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/DiscardDanger.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Table/DangerMarks.h>
#include <gtest/gtest.h>

/**
 * The danger marks of the hand tiles (`Table/DangerMarks.h`, pure logic): the mapping from the grades of `DiscardDanger.h`, nothing off
 * the viewer's turn, in a window, with no declared opponent or for a kind not held, and the same for models that differ only in what the
 * viewer cannot see.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr Seat kViewer = kSeat1;
        constexpr Seat kDeclared = kSeat0;

        /// Seat 1 holds the tiles of `hand` and is to discard; seat 0 has declared; nothing else is seen (every kind has its copies unseen).
        TableView MarkedView(const char* hand) {
            TableView view;
            view.seat = kViewer;
            view.phase = Phase::Discard;
            view.currentSeat = kViewer;
            view.seats[kDeclared].callingDeclared = true;
            for (const Tile tile : ParseTiles(hand).value()) {
                view.own.concealed.push_back(tile);
            }
            view.legal.push_back(HandAction::MakeDiscard(view.own.concealed.front(), false));
            return view;
        }

        DangerMark MarkOf(const TableView& view, const char* tile) {
            return DangerMarks(view)[T(tile).Id()];
        }

        bool AllNone(const TableView& view) {
            for (const DangerMark mark : DangerMarks(view)) {
                if (mark != DangerMark::None) {
                    return false;
                }
            }
            return true;
        }

        // Seat 1 waits on the 5p and declares with its discard of the Wd in the second lap; seat 0 is to act again (the table of `DiscardDangerTest`).
        constexpr const char* kViewerHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Ew Gd Rd Wd";
        constexpr const char* kWaitingHand = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Sw Sw";
        constexpr const char* kSeat2Hand = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd";
        constexpr const char* kSeat3Hand = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd";

        HandModel PlayedModel(const char* seat2 = kSeat2Hand, const char* seat3 = kSeat3Hand, const char* tail = "1p 2p") {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kViewerHand;
            spec.hands[kSeat1] = kWaitingHand;
            spec.hands[kSeat2] = seat2;
            spec.hands[kSeat3] = seat3;
            spec.live = std::string("Ew Rd Nw Ww Gd Wd 9m Ew Rd ") + tail;
            HandModel model = MakeModel(spec);
            Lap(model, "Ew Rd Nw Ww");
            Lap(model, "Gd Wd 9m Ew", {kSeat1});
            EXPECT_EQ(model.GetPhase(), Phase::Discard);
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            return model;
        }

    }  // namespace

    TEST(DangerMarksTest, TheGradesMapToSafeNothingRiskyAndDangerous) {
        TableView view = MarkedView("5p 2p 1p Ew Rd 9m");
        view.visible[T("Rd").Id()] = kCopiesPerTile;  // an honour all of whose copies are seen: Safe

        EXPECT_EQ(MarkOf(view, "Rd"), DangerMark::Safe);
        EXPECT_EQ(MarkOf(view, "1p"), DangerMark::None) << "Low";
        EXPECT_EQ(MarkOf(view, "Ew"), DangerMark::None) << "Low";
        EXPECT_EQ(MarkOf(view, "2p"), DangerMark::Risky) << "Medium";
        EXPECT_EQ(MarkOf(view, "5p"), DangerMark::Dangerous) << "High";
    }

    TEST(DangerMarksTest, TheMarkFollowsTheGradeOfDiscardDangers) {
        const TableView view = MarkedView("5p 2p 1p Ew Rd 9m 3s 8s");
        const DangerTable dangers = DiscardDangers(view);
        const auto marks = DangerMarks(view);

        for (const Tile tile : view.own.concealed) {
            const Danger grade = dangers[tile.Id()];
            const DangerMark expected = grade == Danger::Safe ? DangerMark::Safe : grade == Danger::Medium ? DangerMark::Risky : grade == Danger::High ? DangerMark::Dangerous : DangerMark::None;
            EXPECT_EQ(marks[tile.Id()], expected) << ToString(tile);
        }
    }

    TEST(DangerMarksTest, OnlyKindsTheViewerHoldsAreMarked) {
        TableView view = MarkedView("5p 2p");
        view.visible[T("Rd").Id()] = kCopiesPerTile;  // Safe, but not in the hand

        const auto marks = DangerMarks(view);
        for (size_t id = 0; id < marks.size(); ++id) {
            const bool held = id == T("5p").Id() || id == T("2p").Id();
            EXPECT_EQ(marks[id] != DangerMark::None, held) << ToString(Tile{static_cast<uint8_t>(id)});
        }
    }

    TEST(DangerMarksTest, WithNoDeclaredOpponentNothingIsMarked) {
        TableView view = MarkedView("5p 2p Rd");
        view.visible[T("Rd").Id()] = kCopiesPerTile;
        ASSERT_FALSE(AllNone(view));
        view.seats[kDeclared].callingDeclared = false;
        EXPECT_TRUE(AllNone(view));

        view.seats[kViewer].callingDeclared = true;  // the viewer's own declaration is no danger to it
        EXPECT_TRUE(AllNone(view));
    }

    TEST(DangerMarksTest, NothingIsMarkedOffTheViewersTurnInAWindowOrWithNothingToChooseFrom) {
        const TableView base = MarkedView("5p 2p Rd");
        ASSERT_FALSE(AllNone(base));

        TableView otherTurn = base;
        otherTurn.currentSeat = kSeat2;
        EXPECT_TRUE(AllNone(otherTurn));

        TableView claim = base;
        claim.phase = Phase::Claim;
        EXPECT_TRUE(AllNone(claim));

        TableView robbing = base;
        robbing.phase = Phase::RobKong;
        EXPECT_TRUE(AllNone(robbing));

        TableView noChoice = base;
        noChoice.legal.clear();
        EXPECT_TRUE(AllNone(noChoice));

        TableView noSeat = base;
        noSeat.seat = kNoSeat;
        EXPECT_TRUE(AllNone(noSeat));
    }

    TEST(DangerMarksTest, TheHandOfAPlayedTableIsMarkedFromThePublicTable) {
        const HandModel model = PlayedModel();
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(view.seats[kSeat1].callingDeclared);

        const auto marks = DangerMarks(view);
        EXPECT_EQ(marks[T("4p").Id()], DangerMark::Dangerous) << "every way of a middle tile lives";
        EXPECT_EQ(marks[T("Ew").Id()], DangerMark::Safe) << "discarded by seat 3 after the declaration";
        EXPECT_EQ(marks[T("1p").Id()], DangerMark::None);
        EXPECT_EQ(marks[T("5p").Id()], DangerMark::None) << "not held";
    }

    TEST(DangerMarksTest, TwoModelsThatDifferOnlyInHiddenTilesGiveTheSameMarks) {
        const HandModel first = PlayedModel();
        const HandModel second = PlayedModel("2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd", "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Sw Ew Nw", "2s 3s");

        const TableView viewA = MakeTableView(first, kSeat0);
        const TableView viewB = MakeTableView(second, kSeat0);

        ASSERT_EQ(viewA.visible, viewB.visible);
        EXPECT_EQ(DangerMarks(viewA), DangerMarks(viewB));
    }

}  // namespace BECore::Mahjong::Tests
