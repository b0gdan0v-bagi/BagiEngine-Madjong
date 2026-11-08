#include "ComputerTestSupport.h"
#include "MahjongTableTestSupport.h"

#include <BECore/UI/UIButton.h>
#include <Events/InputEvents.h>

/**
 * The action buttons of the table (`Table/ActionButtons.h`, the canvas `Actions` of the shipped scene): which buttons are on screen for a
 * claim window, a win, the kongs and a robbing window, and what each one performs. Hands come from `FixedWall.h`; the scene file itself
 * is checked by `MahjongTableScreenTest`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        void EmitAction(const char* action, const char* value = "") {
            InputEvents::ActionEvent::Emit(PoolString::Intern(action), PoolString::Intern(value));
        }

        std::string Var(const char* name) {
            const eastl::string_view value = CoreManager::GetUIVars().Get(PoolString::Intern(name));
            return std::string(value.data(), value.size());
        }

        /// East discards the 5p it drew; seat 1 holds 4p 5p 5p 6p 7p, so it may pung or take the chow 4-6p or 5-7p.
        HandModel ClaimWindowModel() {
            FixedWallSpec spec = SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "5p");
            spec.hands[kSeat1] = "4p 5p 5p 6p 7p 2s 5s 8s 2m 5m 8m Ww Nw";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        /// Seat 1 declared with its first discard and waits on 5p; East discards a 5p: seat 1 may win, take the chow 4-6p or pass.
        HandModel MahjongWindowModel() {
            FixedWallSpec spec = SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "9p Wd Gd Sw 5p");
            spec.hands[kSeat1] = kSeat2Waits;
            HandModel model = MakeModel(spec);
            Lap(model, "9p Wd Gd Sw", {kSeat1});
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        /// East waits on Rd and declared with its first discard; a lap later it draws the Rd.
        HandModel SelfDrawModel() {
            HandModel model = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Sw Rd"));
            EXPECT_TRUE(model.Discard(kSeat0, T("9p"), true).has_value());
            Lap(model, "Wd Gd Sw");
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            return model;
        }

    }  // namespace

    class MahjongTableActionsTest : public MahjongTableTestBase {
    protected:
        void StartWith(const SeatSetup& seats, const HandModel& model) {
            ASSERT_NO_FATAL_FAILURE(Start(seats));
            Lay(model);
            Draw();
        }

        static constexpr SeatSetup kPersonAtSeat1{SeatKind::Computer, SeatKind::Person, SeatKind::Computer, SeatKind::Computer};
        static constexpr SeatSetup kPersonAtSeat2{SeatKind::Computer, SeatKind::Computer, SeatKind::Person, SeatKind::Computer};

        /// The buttons on screen, by kind: shown in the last publication and a rect in the last draw (the two agree).
        std::vector<ButtonKind> OnScreenButtons() const {
            std::vector<ButtonKind> shown;
            for (int i = 0; i < kButtonCount; ++i) {
                const ButtonKind kind = static_cast<ButtonKind>(i);
                const char* element = kind == ButtonKind::Declare ? "DeclareButton" : SpecOf(kind).element;
                EXPECT_EQ(_table->GetButtons()[kind].shown, OnScreen("Actions", element)) << SpecOf(kind).element;
                if (OnScreen("Actions", element)) {
                    shown.push_back(kind);
                }
            }
            return shown;
        }

        std::string LabelOf(const char* element) const {
            const auto* button = Element("Actions", element) != nullptr ? Element("Actions", element)->Cast<UIButton>() : nullptr;
            EXPECT_NE(button, nullptr) << element;
            return button != nullptr ? std::string(button->GetDisplayText().data(), button->GetDisplayText().size()) : std::string();
        }
    };

    TEST_F(MahjongTableActionsTest, NoButtonIsOnScreenWhenNobodyCanAct) {
        ASSERT_NO_FATAL_FAILURE(Start(kAllComputers));
        Draw();
        EXPECT_TRUE(OnScreenButtons().empty());

        _game->GetSession().RunComputers();
        Draw();
        EXPECT_TRUE(OnScreenButtons().empty()) << "the hand is over";
    }

    // ----- claims -----

    TEST_F(MahjongTableActionsTest, AClaimWindowShowsExactlyTheLegalClaimsAndPass) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, ClaimWindowModel()));
        ASSERT_EQ(_game->GetSession().Actor(), kSeat1);

        EXPECT_EQ(OnScreenButtons(), (std::vector<ButtonKind>{ButtonKind::ClaimPung, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::Pass}));
        EXPECT_EQ(LabelOf("Chow0"), "Chow 4-6p");
        EXPECT_EQ(LabelOf("Chow1"), "Chow 5-7p");
        EXPECT_EQ(LabelOf("Pung"), "Pung");
        EXPECT_EQ(Var("chow0"), "Chow 4-6p");
    }

    TEST_F(MahjongTableActionsTest, PungLaysAPungAndTheSeatDiscardsNext) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, ClaimWindowModel()));

        EmitAction("MahjongClaimPung");
        Draw();

        const PlayerHand& held = Hand().GetHand(kSeat1);
        ASSERT_EQ(held.melds.size(), 1u);
        EXPECT_EQ(held.melds[0].kind, MeldKind::Pung);
        EXPECT_EQ(held.melds[0].first, T("5p"));
        EXPECT_EQ(Hand().GetPhase(), Phase::Discard);
        EXPECT_EQ(Hand().CurrentSeat(), kSeat1);
        EXPECT_TRUE(OnScreenButtons().empty());
    }

    TEST_F(MahjongTableActionsTest, EachChowButtonClaimsItsOwnSequence) {
        for (const auto& [value, first] : {std::pair{"0", "4p"}, std::pair{"1", "5p"}}) {
            ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, ClaimWindowModel()));

            EmitAction("MahjongClaimChow", value);

            const PlayerHand& held = Hand().GetHand(kSeat1);
            ASSERT_EQ(held.melds.size(), 1u) << value;
            EXPECT_EQ(held.melds[0].kind, MeldKind::Chow) << value;
            EXPECT_EQ(held.melds[0].first, T(first)) << value;
            EXPECT_EQ(Hand().CurrentSeat(), kSeat1) << value;
        }
    }

    TEST_F(MahjongTableActionsTest, PassLeavesTheDiscardAndTheSeatAfterTheDiscarderDraws) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, ClaimWindowModel()));

        EmitAction("MahjongPass");
        Draw();

        EXPECT_TRUE(Hand().GetHand(kSeat1).melds.empty());
        EXPECT_EQ(Hand().Discards().back().tile, T("5p"));
        EXPECT_EQ(Hand().GetPhase(), Phase::Discard);
        EXPECT_EQ(Hand().CurrentSeat(), kSeat1) << "all pass: the seat after the discarder draws, here the one who passed";
        EXPECT_EQ(Hand().GetHand(kSeat1).concealed.size(), static_cast<size_t>(kDealtTiles + 1));
        EXPECT_FALSE(_table->GetButtons()[ButtonKind::Pass].shown);
    }

    TEST_F(MahjongTableActionsTest, AButtonThatIsNotOnScreenDoesNothing) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, ClaimWindowModel()));
        const std::string before = Snapshot(Hand());
        const uint64_t version = _game->GetSession().Version();

        EmitAction("MahjongClaimKong");
        EmitAction("MahjongClaimMahjong");
        EmitAction("MahjongClaimChow", "2");
        EmitAction("MahjongWin");
        EmitAction("MahjongConcealedKong", "0");
        EmitAction("MahjongUpgradeKong");
        EmitAction("MahjongClaimChow", "7");
        EmitAction("SomeoneElsesAction");

        EXPECT_EQ(Snapshot(Hand()), before);
        EXPECT_EQ(_game->GetSession().Version(), version);
    }

    TEST_F(MahjongTableActionsTest, ClickingAButtonOnScreenPerformsItsAction) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, ClaimWindowModel()));

        EXPECT_TRUE(ClickElement("Actions", "Pung")) << "the canvas takes the click";

        ASSERT_EQ(Hand().GetHand(kSeat1).melds.size(), 1u);
        EXPECT_EQ(Hand().GetHand(kSeat1).melds[0].kind, MeldKind::Pung);
    }

    TEST_F(MahjongTableActionsTest, MahjongIsOfferedToTheDeclaredSeatAndWinsTheHand) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, MahjongWindowModel()));
        ASSERT_EQ(_game->GetSession().Actor(), kSeat1);

        EXPECT_EQ(OnScreenButtons(), (std::vector<ButtonKind>{ButtonKind::ClaimMahjong, ButtonKind::ClaimChow0, ButtonKind::Pass}));

        EmitAction("MahjongClaimMahjong");
        Draw();

        ASSERT_TRUE(Hand().Result().has_value());
        EXPECT_EQ(Hand().Result()->kind, ResultKind::Win);
        EXPECT_EQ(Hand().Result()->winner, kSeat1);
        EXPECT_EQ(Hand().Result()->source, WinSource::Discard);
        EXPECT_EQ(_game->GetSession().GetPhase(), SessionPhase::HandOver);
        EXPECT_TRUE(OnScreenButtons().empty());
    }

    // ----- the turn: win, kongs -----

    TEST_F(MahjongTableActionsTest, WinIsOfferedOnADeclaredWinningHandAndEndsTheHand) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, SelfDrawModel()));

        const std::vector<ButtonKind> shown = OnScreenButtons();
        EXPECT_NE(std::find(shown.begin(), shown.end(), ButtonKind::Win), shown.end());

        EmitAction("MahjongWin");

        ASSERT_TRUE(Hand().Result().has_value());
        EXPECT_EQ(Hand().Result()->winner, kSeat0);
        EXPECT_EQ(Hand().Result()->source, WinSource::SelfDraw);
        EXPECT_EQ(Hand().Result()->winningTile, T("Rd"));
    }

    TEST_F(MahjongTableActionsTest, AConcealedKongButtonLaysTheKongAndItsTilesStayFaceUpForTheViewer) {
        FixedWallSpec spec = SpecOf("1p 1p 1p 1p 4s 5s 6s 7m 8m 9m Ew Ew Ew", "9p");
        spec.loose = "1m";
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, MakeModel(spec)));
        const std::vector<ButtonKind> shown = OnScreenButtons();
        EXPECT_NE(std::find(shown.begin(), shown.end(), ButtonKind::ConcealedKong0), shown.end());
        EXPECT_EQ(LabelOf("ConcealedKong0"), "Kong 1p");

        EmitAction("MahjongConcealedKong", "0");
        Draw();

        const PlayerHand& held = Hand().GetHand(kSeat0);
        ASSERT_EQ(held.melds.size(), 1u);
        EXPECT_TRUE(held.melds[0].concealed);
        EXPECT_EQ(held.melds[0].kind, MeldKind::Kong);
        EXPECT_EQ(held.concealed.size(), static_cast<size_t>(kDealtTiles + 1 - 4 + 1)) << "four tiles laid, a loose tile drawn";
        // The viewer sees its own concealed kong: four faces of 1p among the draw calls
        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kSeat0));
        int faces = 0;
        const std::vector<DrawCall> calls = TileCalls();
        for (size_t i = 0; i < calls.size(); ++i) {
            const TileSlot& slot = _table->GetLayout().slots[i];
            faces += slot.role == SlotRole::MeldTile && slot.seat == kSeat0 && calls[i].source->x == SourceRect(T("1p")).x && calls[i].source->y == SourceRect(T("1p")).y ? 1 : 0;
        }
        EXPECT_EQ(faces, 4);
    }

    TEST_F(MahjongTableActionsTest, AnotherSeatsConcealedKongIsDrawnFaceDown) {
        FixedWallSpec spec = SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "9p");
        spec.hands[kSeat1] = "5p 5p 5p 5p 2s 5s 8s 2m 5m 8m Ww Nw Gd";
        spec.loose = "1m";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T("9p")).has_value());
        ASSERT_EQ(model.CurrentSeat(), kSeat1);
        ASSERT_TRUE(model.DeclareConcealedKong(kSeat1, T("5p")).has_value());
        ASSERT_NO_FATAL_FAILURE(StartWith(kDefaultSeatSetup, model));

        ASSERT_NO_FATAL_FAILURE(ExpectTableDrawn(false, kSeat0));
        int backs = 0;
        const std::vector<DrawCall> calls = TileCalls();
        for (size_t i = 0; i < calls.size(); ++i) {
            const TileSlot& slot = _table->GetLayout().slots[i];
            backs += slot.role == SlotRole::MeldTile && slot.seat == kSeat1 && calls[i].source->x == BackRect().x && calls[i].source->y == BackRect().y ? 1 : 0;
        }
        EXPECT_EQ(backs, 4);
    }

    TEST_F(MahjongTableActionsTest, TheUpgradeButtonAddsTheFourthTileToThePung) {
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat1, UpgradeModel()));
        ASSERT_EQ(_game->GetSession().Actor(), kSeat1);
        const std::vector<ButtonKind> shown = OnScreenButtons();
        ASSERT_NE(std::find(shown.begin(), shown.end(), ButtonKind::UpgradeKong), shown.end());
        EXPECT_EQ(LabelOf("UpgradeKong"), "Add 5p");

        EmitAction("MahjongUpgradeKong");

        ASSERT_EQ(Hand().GetHand(kSeat1).melds.size(), 1u);
        EXPECT_EQ(Hand().GetHand(kSeat1).melds[0].kind, MeldKind::Kong);
        EXPECT_EQ(Hand().GetPhase(), Phase::Discard);
    }

    TEST_F(MahjongTableActionsTest, ARobbingWindowOffersMahjongAndPassToTheDeclaredSeat) {
        HandModel model = UpgradeModel({kSeat2});
        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);
        ASSERT_NO_FATAL_FAILURE(StartWith(kPersonAtSeat2, model));
        ASSERT_EQ(_game->GetSession().Actor(), kSeat2);

        EXPECT_EQ(OnScreenButtons(), (std::vector<ButtonKind>{ButtonKind::ClaimMahjong, ButtonKind::Pass}));

        EmitAction("MahjongClaimMahjong");

        ASSERT_TRUE(Hand().Result().has_value());
        EXPECT_EQ(Hand().Result()->source, WinSource::RobbedKong);
        EXPECT_EQ(Hand().Result()->winner, kSeat2);
    }

}  // namespace BECore::Mahjong::Tests
