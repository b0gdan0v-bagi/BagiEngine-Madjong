#include "FixedWall.h"

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * `LegalActions` and `Apply`: the one list of what a seat can do, on fixed walls: a plain turn, the kongs, a calling hand that is
 * waiting but not declared, the claim window, the robbing window, a seat out of turn. Every action of a list is accepted by `Apply`
 * and a made-up one is refused. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr Seat kSeat0 = 0;
        constexpr Seat kSeat1 = 1;
        constexpr Seat kSeat2 = 2;
        constexpr Seat kSeat3 = 3;
        constexpr Seat kNoSuchSeat = kSeatCount;
        constexpr size_t kTilesInHand = static_cast<size_t>(kDealtTiles);

        // The four quiet hands, one per seat. They hold no pair and no two tiles near each other in a suit, so no hand is waiting.
        constexpr const char* kQuiet0 = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ww Nw Gd";
        constexpr const char* kQuiet1 = "2p 5p 8p 2s 5s 8s 2m 5m 8m Sw Ww Nw Ew";
        constexpr const char* kQuiet2 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd";
        constexpr const char* kQuiet3 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd";

        constexpr const char* kRed = "Rd";
        constexpr const char* kEast = "Ew";
        constexpr const char* kFifth = "5p";

        Tile T(const char* text) {
            return TryParse(text).value();
        }

        FixedWallSpec QuietSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kQuiet0;
            spec.hands[kSeat1] = kQuiet1;
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = kQuiet3;
            return spec;
        }

        std::optional<HandError> Error(const HandStatus& status) {
            return status.has_value() ? std::nullopt : std::optional<HandError>(status.error());
        }

        /// Everything an action could change, as text.
        std::string Snapshot(const HandModel& model) {
            std::string text = std::to_string(static_cast<int>(model.GetPhase())) + " seat " + std::to_string(model.CurrentSeat());
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PlayerHand& hand = model.GetHand(seat);
                text += " |";
                for (const Tile tile : hand.concealed) {
                    text += " ";
                    text += ToString(tile);
                }
                text += " melds " + std::to_string(hand.melds.size());
                text += " calling " + std::to_string(hand.callingDeclared) + " claim " + std::to_string(static_cast<int>(model.GetClaimStatus(seat)));
            }
            text += " | pile " + std::to_string(model.Discards().size());
            text += " | live " + std::to_string(model.GetWall().LiveCount());
            text += model.Result().has_value() ? " result" : " open";
            return text;
        }

        /// Each seat in turn discards the next tile of `tiles`; the seats in `declaring` declare the calling hand with their discard.
        /// No discard of a lap may open a claim window.
        void Lap(HandModel& model, const std::string& tiles, std::initializer_list<Seat> declaring = {}) {
            const eastl::vector<Tile> parsed = ParseTiles(tiles).value();
            for (const Tile tile : parsed) {
                const Seat seat = model.CurrentSeat();
                const bool declare = eastl::find(declaring.begin(), declaring.end(), seat) != declaring.end();
                ASSERT_TRUE(model.Discard(seat, tile, declare).has_value()) << "seat " << seat << " cannot discard " << ToString(tile);
                ASSERT_EQ(model.GetPhase(), Phase::Discard) << "the discard of " << ToString(tile) << " by seat " << seat << " opened a window";
            }
        }

        bool Has(const eastl::vector<HandAction>& actions, const HandAction& action) {
            return eastl::find(actions.begin(), actions.end(), action) != actions.end();
        }

        size_t Count(const eastl::vector<HandAction>& actions, ActionKind kind) {
            size_t count = 0;
            for (const HandAction& action : actions) {
                count += action.kind == kind ? 1 : 0;
            }
            return count;
        }

        /// Every action of every seat's list is accepted by `Apply` (on a copy, so that each is tried from the same state).
        void ExpectEveryLegalActionIsAccepted(const HandModel& model) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (const HandAction& action : model.LegalActions(seat)) {
                    HandModel copy = model;
                    EXPECT_TRUE(copy.Apply(seat, action).has_value()) << "seat " << seat << " action " << static_cast<int>(action.kind) << " " << ToString(action.tile);
                }
            }
        }

        /// The seats that have something to do.
        std::vector<Seat> SeatsWithActions(const HandModel& model) {
            std::vector<Seat> seats;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (!model.LegalActions(seat).empty()) {
                    seats.push_back(seat);
                }
            }
            return seats;
        }

        // ----- the upgraded kong -----

        // Seat 1 holds two 5p. East discards a third one and seat 1 claims the pung; after a lap seat 1 draws the fourth.
        // Seats 2 and 3 wait on the 5p.
        FixedWallSpec UpgradeSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = "5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            spec.hands[kSeat2] = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Ew Ew";
            spec.hands[kSeat3] = "4p 6p 1m 2m 3m 4s 5s 6s 7m 8m 9m Ww Ww";
            spec.live = std::string(kFifth) + " Rd Gd Wd " + kFifth;
            spec.loose = "9m 9s";
            return spec;
        }

        /// Seat 1 is to act with the fourth 5p drawn and an open pung of 5p laid; the seats in `declaring` have declared.
        HandModel UpgradeModel(std::initializer_list<Seat> declaring = {}) {
            HandModel model = MakeModel(UpgradeSpec());
            EXPECT_TRUE(model.Discard(kSeat0, T(kFifth)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            EXPECT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
            Lap(model, "Wd Rd Gd Wd", declaring);
            EXPECT_EQ(model.CurrentSeat(), kSeat1);
            return model;
        }

        // ----- a hand that is waiting -----

        // East waits on the red dragon (a pair wait) and draws it.
        constexpr const char* kWaitingEast = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";

        FixedWallSpec WaitingSpec(const std::string& live) {
            FixedWallSpec spec = QuietSpec();
            spec.hands[kSeat0] = kWaitingEast;
            spec.hands[kSeat1] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            spec.live = live;
            return spec;
        }

    }  // namespace

    TEST(HandModelActionsTest, APlainTurnOffersEveryDistinctDiscardAndNothingElse) {
        FixedWallSpec spec = QuietSpec();
        spec.live = kRed;
        HandModel model = MakeModel(spec);
        ASSERT_EQ(model.GetHand(kSeat0).concealed.size(), kTilesInHand + 1);

        const eastl::vector<HandAction> actions = model.LegalActions(kSeat0);

        EXPECT_EQ(actions.size(), kTilesInHand + 1);  // every tile of the hand is different
        EXPECT_EQ(Count(actions, ActionKind::Discard), actions.size());
        for (const Tile tile : model.GetHand(kSeat0).concealed) {
            EXPECT_TRUE(Has(actions, HandAction::MakeDiscard(tile)));
            EXPECT_FALSE(Has(actions, HandAction::MakeDiscard(tile, true)));  // nothing is waiting
        }
        ExpectEveryLegalActionIsAccepted(model);
    }

    TEST(HandModelActionsTest, EqualTilesGiveOneDiscardAction) {
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat0] = "1p 1p 7p 1s 4s 7s 1m 4m 7m Sw Ww Nw Gd";
        spec.live = kRed;
        HandModel model = MakeModel(spec);

        const eastl::vector<HandAction> actions = model.LegalActions(kSeat0);

        EXPECT_EQ(actions.size(), kTilesInHand);  // 14 tiles, one pair
        EXPECT_EQ(std::count(actions.begin(), actions.end(), HandAction::MakeDiscard(T("1p"))), 1);
    }

    TEST(HandModelActionsTest, ASeatThatIsNotToActHasNoActionsAndANumberThatIsNoSeatHasNone) {
        FixedWallSpec spec = QuietSpec();
        spec.live = kRed;
        HandModel model = MakeModel(spec);

        EXPECT_TRUE(model.LegalActions(kSeat1).empty());
        EXPECT_TRUE(model.LegalActions(kSeat2).empty());
        EXPECT_TRUE(model.LegalActions(kSeat3).empty());
        EXPECT_TRUE(model.LegalActions(-1).empty());
        EXPECT_TRUE(model.LegalActions(kNoSuchSeat).empty());
        const std::string before = Snapshot(model);
        EXPECT_EQ(Error(model.Apply(-1, HandAction::MakePass())), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Apply(kNoSuchSeat, HandAction::MakeDiscard(T("1p")))), HandError::NotYourTurn);
        EXPECT_EQ(Snapshot(model), before);
        EXPECT_EQ(model.GetClaimStatus(-1), ClaimStatus::NotInWindow);
        EXPECT_EQ(model.GetClaimStatus(kNoSuchSeat), ClaimStatus::NotInWindow);
    }

    TEST(HandModelActionsTest, AFinishedHandOffersNothing) {
        FixedWallSpec spec = QuietSpec();
        spec.liveAfterDeal = 0;
        HandModel model = MakeModel(spec);
        ASSERT_EQ(model.GetPhase(), Phase::Finished);

        EXPECT_TRUE(SeatsWithActions(model).empty());
        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakeDiscard(T("1p")))), HandError::WrongPhase);
    }

    TEST(HandModelActionsTest, ATurnWithFourEqualTilesOffersTheConcealedKong) {
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat0] = "Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd";
        spec.hands[kSeat1] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Sw Ww Nw Rd";
        spec.live = kEast;
        HandModel model = MakeModel(spec);

        const eastl::vector<HandAction> actions = model.LegalActions(kSeat0);

        EXPECT_TRUE(Has(actions, HandAction::MakeConcealedKong(T(kEast))));
        EXPECT_EQ(Count(actions, ActionKind::ConcealedKong), 1u);
        EXPECT_EQ(Count(actions, ActionKind::UpgradeKong), 0u);
        EXPECT_TRUE(Has(actions, HandAction::MakeDiscard(T(kEast))));  // the kong is not compulsory
        ExpectEveryLegalActionIsAccepted(model);
    }

    TEST(HandModelActionsTest, ATurnWithAnOpenPungAndTheFourthTileOffersTheUpgradedKong) {
        HandModel model = UpgradeModel();
        ASSERT_EQ(model.GetPhase(), Phase::Discard);

        const eastl::vector<HandAction> actions = model.LegalActions(kSeat1);

        EXPECT_TRUE(Has(actions, HandAction::MakeUpgradeKong(T(kFifth))));
        EXPECT_EQ(Count(actions, ActionKind::UpgradeKong), 1u);
        EXPECT_EQ(Count(actions, ActionKind::ConcealedKong), 0u);
        EXPECT_TRUE(Has(actions, HandAction::MakeDiscard(T(kFifth))));
        ExpectEveryLegalActionIsAccepted(model);
    }

    TEST(HandModelActionsTest, AWaitingHandThatIsNotDeclaredCannotWinAndDeclaresOnlyWhereTheRestIsWaiting) {
        HandModel model = MakeModel(WaitingSpec(kRed));
        const PlayerHand& hand = model.GetHand(kSeat0);
        ASSERT_EQ(hand.concealed.size(), kTilesInHand + 1);
        ASSERT_FALSE(hand.callingDeclared);

        const eastl::vector<HandAction> actions = model.LegalActions(kSeat0);

        EXPECT_EQ(Count(actions, ActionKind::DeclareWin), 0u);  // a winning shape, but not declared
        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakeDeclareWin())), HandError::NotCalling);
        size_t declaring = 0;
        for (const Tile tile : hand.concealed) {
            PlayerHand rest = hand;
            rest.RemoveConcealed(tile);
            const bool waiting = !WinningTiles(rest).empty();
            EXPECT_EQ(Has(actions, HandAction::MakeDiscard(tile, true)), waiting) << ToString(tile);
            EXPECT_TRUE(Has(actions, HandAction::MakeDiscard(tile))) << ToString(tile);
            declaring += waiting ? 1 : 0;
        }
        EXPECT_GT(declaring, 0u);
        EXPECT_TRUE(Has(actions, HandAction::MakeDiscard(T(kRed), true)));
        ExpectEveryLegalActionIsAccepted(model);
    }

    TEST(HandModelActionsTest, ADeclaredHandOffersTheWinAndNoMoreDeclarations) {
        // East discards 9p with the declaration (the rest waits on the red dragon), the others discard, East draws the red dragon.
        HandModel model = MakeModel(WaitingSpec("9p Wd Gd Nw Rd"));
        Lap(model, "9p Wd Gd Nw", {kSeat0});
        ASSERT_TRUE(model.GetHand(kSeat0).callingDeclared);
        ASSERT_EQ(model.CurrentSeat(), kSeat0);

        const eastl::vector<HandAction> actions = model.LegalActions(kSeat0);

        EXPECT_TRUE(Has(actions, HandAction::MakeDeclareWin()));
        for (const HandAction& action : actions) {
            EXPECT_FALSE(action.kind == ActionKind::Discard && action.declareCalling);
        }
        ExpectEveryLegalActionIsAccepted(model);
    }

    TEST(HandModelActionsTest, AClaimWindowOffersTheClaimsAndAPassToTheSeatsInIt) {
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat2] = "Rd Rd Rd 3p 6p 9p 3s 6s 9s 3m 6m 9m Sw";
        spec.live = kRed;
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Apply(kSeat0, HandAction::MakeDiscard(T(kRed))).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        const eastl::vector<HandAction> actions = model.LegalActions(kSeat2);

        ASSERT_EQ(actions.size(), 3u);
        EXPECT_TRUE(Has(actions, HandAction::MakeClaim(ClaimKind::Pung)));
        EXPECT_TRUE(Has(actions, HandAction::MakeClaim(ClaimKind::Kong)));
        EXPECT_TRUE(Has(actions, HandAction::MakePass()));
        EXPECT_EQ(SeatsWithActions(model), std::vector<Seat>{kSeat2});  // the discarder and the seats with no claim are out
        ExpectEveryLegalActionIsAccepted(model);

        ASSERT_TRUE(model.Apply(kSeat2, HandAction::MakePass()).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_EQ(SeatsWithActions(model), std::vector<Seat>{kSeat1});
    }

    TEST(HandModelActionsTest, AChowIsOfferedOncePerSequenceToTheNextSeatOnly) {
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat1] = "3p 4p 6p 7p 2s 5s 8s 2m 5m 8m Sw Ww Nw";
        spec.live = kFifth;
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Apply(kSeat0, HandAction::MakeDiscard(T(kFifth))).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        const eastl::vector<HandAction> actions = model.LegalActions(kSeat1);

        ASSERT_EQ(actions.size(), 4u);  // 3-4-5, 4-5-6, 5-6-7 and the pass
        EXPECT_EQ(actions[0], HandAction::MakeClaim(ClaimKind::Chow, T("3p")));
        EXPECT_EQ(actions[1], HandAction::MakeClaim(ClaimKind::Chow, T("4p")));
        EXPECT_EQ(actions[2], HandAction::MakeClaim(ClaimKind::Chow, T(kFifth)));
        EXPECT_EQ(actions[3], HandAction::MakePass());
        ExpectEveryLegalActionIsAccepted(model);
    }

    TEST(HandModelActionsTest, TheRobbingWindowOffersMahjongAndAPassToTheDeclaredSeatOnly) {
        HandModel model = UpgradeModel({kSeat2});
        ASSERT_TRUE(model.Apply(kSeat1, HandAction::MakeUpgradeKong(T(kFifth))).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);

        const eastl::vector<HandAction> actions = model.LegalActions(kSeat2);

        ASSERT_EQ(actions.size(), 2u);
        EXPECT_EQ(actions[0], HandAction::MakeClaim(ClaimKind::Mahjong));
        EXPECT_EQ(actions[1], HandAction::MakePass());
        EXPECT_EQ(SeatsWithActions(model), std::vector<Seat>{kSeat2});  // the owner of the kong and the undeclared seat are out
        ExpectEveryLegalActionIsAccepted(model);
    }

    TEST(HandModelActionsTest, AnActionThatIsNotInTheListIsRefusedAndChangesNothing) {
        FixedWallSpec spec = QuietSpec();
        spec.live = kRed;
        HandModel model = MakeModel(spec);
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakeDiscard(T("9m")))), HandError::TileNotInHand);  // not held
        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakeDiscard(Tile{}))), HandError::TileNotInHand);
        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakeDiscard(T("1p"), true))), HandError::NotWaiting);
        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakeDeclareWin())), HandError::NotCalling);
        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakeConcealedKong(T("1p")))), HandError::IllegalKong);
        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakeUpgradeKong(T("1p")))), HandError::IllegalKong);
        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakePass())), HandError::WrongPhase);
        EXPECT_EQ(Error(model.Apply(kSeat0, HandAction::MakeClaim(ClaimKind::Pung))), HandError::WrongPhase);
        EXPECT_EQ(Error(model.Apply(kSeat1, HandAction::MakeDiscard(T("2p")))), HandError::NotYourTurn);
        EXPECT_EQ(Snapshot(model), before);

        // In a window: the discarder, a seat with no claim and a claim the seat cannot make.
        FixedWallSpec claimSpec = QuietSpec();
        claimSpec.hands[kSeat2] = "Rd Rd Rd 3p 6p 9p 3s 6s 9s 3m 6m 9m Sw";
        claimSpec.live = kRed;
        HandModel claimWindow = MakeModel(claimSpec);
        ASSERT_TRUE(claimWindow.Apply(kSeat0, HandAction::MakeDiscard(T(kRed))).has_value());
        ASSERT_EQ(claimWindow.GetPhase(), Phase::Claim);
        const std::string windowBefore = Snapshot(claimWindow);
        EXPECT_EQ(Error(claimWindow.Apply(kSeat0, HandAction::MakeDiscard(T("1p")))), HandError::WrongPhase);
        EXPECT_EQ(Error(claimWindow.Apply(kSeat1, HandAction::MakePass())), HandError::NotYourTurn);
        EXPECT_EQ(Error(claimWindow.Apply(kSeat2, HandAction::MakeClaim(ClaimKind::Chow, T("3p")))), HandError::IllegalClaim);
        EXPECT_EQ(Error(claimWindow.Apply(kSeat2, HandAction::MakeClaim(ClaimKind::Mahjong))), HandError::NotCalling);
        EXPECT_EQ(Snapshot(claimWindow), windowBefore);
    }

}  // namespace BECore::Mahjong::Tests
