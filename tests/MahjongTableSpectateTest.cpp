#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"
#include "UiStyleTestSupport.h"

#include <Mahjong/Table/TileAtlas.h>
#include <Mahjong/Table/UiPalette.h>

/**
 * A watched match, no seat a person (`Components/MahjongTableComponent.h`, "No peeking"): while the hand is played the concealed tiles of
 * all four seats are drawn face up, the concealed kongs too, and none of the marks that follow the viewer is drawn (a spectator has none).
 * A match with a person keeps its hands as it was: one person sees only its own, two people see the cover. Nothing in the model, the
 * session or the variables of the UI changes with the draw.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr SeatSetup kPersonAtSeat0{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        /// Seat 0 waits on the 5p with a pair of East winds (its seat wind and the round wind), seat 1 is quiet, seats 2 and 3 wait too.
        /// Seat 0 draws a 9s and, with its declaration made on the first discard, is to discard again: every mark of the viewer is there.
        constexpr const char* kSeat0Declares = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Ew Ew";
        /// The hands of a claim window on a 4p that seat 0 may pung (`MahjongTableLostClaimTest`): seat 3 discards it after a lap.
        constexpr const char* kPungHand = "4p 4p 1s 4s 7s 1m 4m 7m 2s 8s Sw Ww Gd";
        constexpr const char* kQuietHand = "1p 7p 2s 5s 8s 2m 5m 8m Ww Rd 9p Nw 9m";
        constexpr const char* kDiscarder = "2p 5p 8p 2s 5s 8s 2m 5m 8m Rd Wd Nw 9s";
        /// Seat 1 holds four 5p (a concealed kong) and discards after the 9p of seat 0.
        constexpr const char* kKongHand = "5p 5p 5p 5p 2s 5s 8s 2m 5m 8m Ww Nw Gd";
        /// The marks of the viewer in the palette.
        constexpr Color kViewerMarks[] = {kDrawnTileMark, kKeepsWaitingMark, kSeatWindMark, kRoundWindMark};

        bool SameSource(const DrawCall& call, const Rect& source) {
            return call.source.has_value() && SameRect(*call.source, source);
        }

        FixedWallSpec DeclaringSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kSeat0Declares;
            spec.hands[kSeat1] = kQuiet1;
            spec.hands[kSeat2] = kSeat2Waits;
            spec.hands[kSeat3] = kSeat3Waits;
            spec.live = "Gd Rd Wd Gd 9s Sw";
            return spec;
        }

        /// Seat 0 declared with its first discard and is to act with the 9s drawn.
        HandModel DeclaredModel() {
            HandModel model = MakeModel(DeclaringSpec());
            Lap(model, "Gd Rd Wd Gd", {kSeat0});
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            EXPECT_TRUE(model.GetHand(kSeat0).callingDeclared);
            return model;
        }

        /// A claim window: seats 0, 1, 2 discard what they draw ("Wd Gd Nw"), then seat 3 discards the 4p; seat 0 may pung it.
        HandModel WindowModel() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kPungHand;
            spec.hands[kSeat1] = kQuietHand;
            spec.hands[kSeat2] = kSeat3Waits;
            spec.hands[kSeat3] = kDiscarder;
            spec.live = "Wd Gd Nw 4p";
            HandModel model = MakeModel(spec);
            Lap(model, "Wd Gd Nw");
            EXPECT_EQ(model.CurrentSeat(), kSeat3);
            EXPECT_TRUE(model.Discard(kSeat3, T("4p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        /// Seat 1 has made a concealed kong of 5p after the 9p of seat 0.
        HandModel KongModel() {
            FixedWallSpec spec = SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "9p");
            spec.hands[kSeat1] = kKongHand;
            spec.loose = "1m";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("9p")).has_value());
            EXPECT_EQ(model.CurrentSeat(), kSeat1);
            EXPECT_TRUE(model.DeclareConcealedKong(kSeat1, T("5p")).has_value());
            return model;
        }

    }  // namespace

    class MahjongTableSpectateTest : public MahjongTableTestBase {
    protected:
        HandModel& Model() const {
            return _game->GetMatch().Hand();
        }

        TableSession& Session() const {
            return _game->GetSession();
        }

        /// A match with `seats` and `model` laid in; the first frame is drawn.
        void StartWith(const SeatSetup& seats, const HandModel& model) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(model);
            Draw();
        }

        /// Every concealed tile of every seat, and every tile of a concealed kong, is drawn as its own face: the source of the draw is
        /// the atlas cell of the tile the model holds there.
        void ExpectEveryHiddenTileFaceUp() const {
            const std::vector<DrawCall> calls = TileCalls();
            const auto& slots = _table->GetLayout().slots;
            ASSERT_EQ(calls.size(), slots.size());
            int concealed = 0;
            for (size_t i = 0; i < slots.size(); ++i) {
                const TileSlot& slot = slots[i];
                const PlayerHand& held = Hand().GetHand(slot.seat);
                if (slot.role == SlotRole::HandTile) {
                    EXPECT_TRUE(SameSource(calls[i], SourceRect(held.concealed[static_cast<size_t>(slot.index)]))) << "seat " << slot.seat << " hand tile " << slot.index;
                    ++concealed;
                } else if (slot.role == SlotRole::MeldTile && held.melds[static_cast<size_t>(slot.index)].concealed) {
                    EXPECT_TRUE(SameSource(calls[i], SourceRect(LaidTile(held.melds[static_cast<size_t>(slot.index)], slot.part)))) << "seat " << slot.seat << " kong tile " << slot.part;
                    ++concealed;
                }
            }
            EXPECT_GT(concealed, 0);
            ExpectNoBackInAHand();
        }

        /// No hand tile, no concealed kong tile is a back.
        void ExpectNoBackInAHand() const {
            const std::vector<DrawCall> calls = TileCalls();
            const auto& slots = _table->GetLayout().slots;
            ASSERT_EQ(calls.size(), slots.size());
            for (size_t i = 0; i < slots.size(); ++i) {
                if (slots[i].role == SlotRole::HandTile || slots[i].role == SlotRole::MeldTile) {
                    EXPECT_FALSE(SameSource(calls[i], BackRect())) << "seat " << slots[i].seat << " role " << static_cast<int>(slots[i].role) << " index " << slots[i].index;
                }
            }
        }

        /// The hand tiles of `seat` drawn as backs in the last frame.
        int BacksOf(Seat seat) const {
            const std::vector<DrawCall> calls = TileCalls();
            const auto& slots = _table->GetLayout().slots;
            int backs = 0;
            for (size_t i = 0; i < slots.size() && i < calls.size(); ++i) {
                backs += slots[i].role == SlotRole::HandTile && slots[i].seat == seat && SameSource(calls[i], BackRect()) ? 1 : 0;
            }
            return backs;
        }

        int HandTilesOf(Seat seat) const {
            int count = 0;
            for (const TileSlot& slot : _table->GetLayout().slots) {
                count += slot.role == SlotRole::HandTile && slot.seat == seat ? 1 : 0;
            }
            return count;
        }

        int RectCount(const Color& color) const {
            int count = 0;
            for (const DrawCall& call : _renderer->calls) {
                count += call.kind == DrawKind::Rect && SameColor(call.color, color) ? 1 : 0;
            }
            return count;
        }

        /// The marks of the viewer in the last frame: the tints, bars and frames that follow it.
        int ViewerMarkCount() const {
            int count = RectCount(kPendingFrame);
            for (const Color& color : kViewerMarks) {
                count += RectCount(color);
            }
            return count;
        }
    };

    TEST_F(MahjongTableSpectateTest, FourComputersDrawAllFourHandsFaceUp) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        ASSERT_EQ(Session().GetPhase(), SessionPhase::Playing);
        ASSERT_EQ(Session().PersonCount(), 0);
        ExpectEveryHiddenTileFaceUp();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_GE(HandTilesOf(seat), 13) << "seat " << seat;
        }
    }

    TEST_F(MahjongTableSpectateTest, AComputersConcealedKongShowsFourFaces) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kAllComputers, KongModel()));
        ASSERT_TRUE(Hand().GetHand(kSeat1).melds[0].concealed);
        ExpectEveryHiddenTileFaceUp();
        int kongFaces = 0;
        const std::vector<DrawCall> calls = TileCalls();
        for (size_t i = 0; i < calls.size(); ++i) {
            const TileSlot& slot = _table->GetLayout().slots[i];
            kongFaces += slot.role == SlotRole::MeldTile && slot.seat == kSeat1 && SameSource(calls[i], SourceRect(T("5p"))) ? 1 : 0;
        }
        EXPECT_EQ(kongFaces, 4);
    }

    TEST_F(MahjongTableSpectateTest, TheHandsStayFaceUpAfterADiscardInAClaimWindowAfterAPungAndAfterAKong) {
        HandModel model = WindowModel();
        ASSERT_NO_FATAL_FAILURE(StartWith(kAllComputers, MakeModel(DeclaringSpec())));
        ExpectEveryHiddenTileFaceUp();

        // A discard
        HandModel afterDiscard = MakeModel(DeclaringSpec());
        ASSERT_TRUE(afterDiscard.Discard(kSeat0, T("Gd")).has_value());
        Lay(afterDiscard);
        Draw();
        ExpectEveryHiddenTileFaceUp();

        // A claim window
        Lay(model);
        Draw();
        ASSERT_EQ(Hand().GetPhase(), Phase::Claim);
        ExpectEveryHiddenTileFaceUp();

        // A pung of seat 0: its hand is shorter by two tiles and one of them is to be discarded
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Pung).has_value());
        Lay(model);
        Draw();
        ASSERT_FALSE(Hand().GetHand(kSeat0).melds.empty());
        ExpectEveryHiddenTileFaceUp();

        // A kong
        Lay(KongModel());
        Draw();
        ASSERT_TRUE(Hand().GetHand(kSeat1).melds[0].concealed);
        ExpectEveryHiddenTileFaceUp();
    }

    TEST_F(MahjongTableSpectateTest, NoCoverIsUpAndNobodyIsTheViewer) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        EXPECT_FALSE(Session().HandoffPending());
        EXPECT_FALSE(CoverShown());
        EXPECT_EQ(Session().Viewer(), kNoSeat);
    }

    TEST_F(MahjongTableSpectateTest, NoMarkOfTheViewerIsDrawnInAWatchedMatch) {
        // The same table with a person at seat 0 draws every one of them: the drawn tile, the bars of the declared hand, the winds
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat0, DeclaredModel()));
        Session().Reveal();
        Draw();
        for (const Color& color : kViewerMarks) {
            EXPECT_GT(RectCount(color), 0) << "the person's table has the mark";
        }
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat0, WindowModel()));
        Session().Reveal();
        Draw();
        EXPECT_GT(RectCount(kPendingFrame), 0) << "the person is asked to claim: the discard is framed";

        ASSERT_NO_FATAL_FAILURE(StartWith(kAllComputers, DeclaredModel()));
        EXPECT_EQ(ViewerMarkCount(), 0);
        ASSERT_NO_FATAL_FAILURE(StartWith(kAllComputers, WindowModel()));
        const eastl::vector<Seat> asked = Hand().PendingClaimSeats();
        ASSERT_NE(eastl::find(asked.begin(), asked.end(), kSeat0), asked.end()) << "seat 0 has a claim, and nobody is asked";
        EXPECT_EQ(ViewerMarkCount(), 0);
    }

    TEST_F(MahjongTableSpectateTest, APersonSeesOnlyItsOwnHandAndTwoPeopleSeeNoneUnderTheCover) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat0, DeclaredModel()));
        Session().Reveal();
        Draw();
        ASSERT_EQ(Session().Viewer(), kSeat0);
        EXPECT_EQ(BacksOf(kSeat0), 0);
        for (Seat seat = kSeat1; seat < kSeatCount; ++seat) {
            EXPECT_EQ(BacksOf(seat), HandTilesOf(seat)) << "seat " << seat;
        }

        ASSERT_NO_FATAL_FAILURE(StartWith(kTwoPeople, DeclaredModel()));
        ASSERT_TRUE(Session().HandoffPending());
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(BacksOf(seat), HandTilesOf(seat)) << "seat " << seat;
        }
        EXPECT_EQ(ViewerMarkCount(), 0);
    }

    TEST_F(MahjongTableSpectateTest, AtTheEndOfTheHandAllFourHandsAreFaceUpAndThereIsNoDim) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        ASSERT_GT(Session().RunComputers(), 0);
        ASSERT_EQ(Session().GetPhase(), SessionPhase::HandOver);
        Draw();
        ExpectEveryHiddenTileFaceUp();
        EXPECT_EQ(Element("HandOver", "Dim"), nullptr);
        EXPECT_EQ(ViewerMarkCount(), 0);
    }

    TEST_F(MahjongTableSpectateTest, DrawingAWatchedTableAgainChangesNeitherTheSessionNorTheVariablesOfTheUi) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kAllComputers, WindowModel()));
        Draw();
        const uint64_t sessionVersion = Session().Version();
        const uint64_t uiVersion = CoreManager::GetUIVars().GetVersion();
        Draw();
        Draw();
        EXPECT_EQ(Session().Version(), sessionVersion);
        EXPECT_EQ(CoreManager::GetUIVars().GetVersion(), uiVersion);
    }

}  // namespace BECore::Mahjong::Tests
