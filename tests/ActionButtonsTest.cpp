#include "ComputerTestSupport.h"
#include "HandTestSupport.h"

#include <Mahjong/Table/ActionButtons.h>
#include <gtest/gtest.h>

/**
 * `Table/ActionButtons.h`: the legal actions of a seat as the action buttons of the table. Pure logic: hand-made lists and the lists of
 * a hand on a fixed wall.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        eastl::vector<HandAction> Claims(std::initializer_list<ClaimOption> options, bool pass = true) {
            eastl::vector<HandAction> list;
            for (const ClaimOption& option : options) {
                list.push_back(HandAction::MakeClaim(option.kind, option.chowFirst));
            }
            if (pass) {
                list.push_back(HandAction::MakePass());
            }
            return list;
        }

        std::string Label(const ActionButtons& buttons, ButtonKind kind) {
            return std::string(buttons[kind].label.c_str(), buttons[kind].label.size());
        }

        std::vector<ButtonKind> Shown(const ActionButtons& buttons) {
            std::vector<ButtonKind> shown;
            for (int i = 0; i < kButtonCount; ++i) {
                if (buttons.buttons[static_cast<size_t>(i)].shown) {
                    shown.push_back(static_cast<ButtonKind>(i));
                }
            }
            return shown;
        }

    }  // namespace

    TEST(ActionButtonsTest, EveryButtonHasItsOwnActionAndValueAndIsFoundByThem) {
        std::set<std::pair<std::string, std::string>> seen;
        std::set<std::string> elements;
        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonKind kind = static_cast<ButtonKind>(i);
            const ButtonSpec& spec = SpecOf(kind);
            EXPECT_TRUE(seen.insert({spec.action, spec.value}).second) << spec.element;
            EXPECT_TRUE(elements.insert(spec.element).second) << spec.element;
            const std::optional<ButtonKind> found = ButtonOf(spec.action, spec.value);
            ASSERT_TRUE(found.has_value()) << spec.element;
            EXPECT_EQ(*found, kind) << spec.element;
        }
        EXPECT_FALSE(ButtonOf("MahjongClaimChow", "3").has_value());
        EXPECT_FALSE(ButtonOf("MahjongClaimChow", "").has_value());
        EXPECT_FALSE(ButtonOf("MahjongWin", "1").has_value());
        EXPECT_FALSE(ButtonOf("Match3Reset", "").has_value());
        EXPECT_FALSE(ButtonOf("", "").has_value());
    }

    TEST(ActionButtonsTest, ALabelVariableBelongsToEveryButtonWhoseLabelChanges) {
        for (const ButtonKind kind :
             {ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2, ButtonKind::ConcealedKong0, ButtonKind::ConcealedKong1, ButtonKind::ConcealedKong2, ButtonKind::UpgradeKong}) {
            EXPECT_NE(SpecOf(kind).labelVar, nullptr) << SpecOf(kind).element;
        }
        for (const ButtonKind kind : {ButtonKind::Win, ButtonKind::ClaimMahjong, ButtonKind::ClaimPung, ButtonKind::ClaimKong, ButtonKind::Pass, ButtonKind::Declare}) {
            EXPECT_EQ(SpecOf(kind).labelVar, nullptr) << SpecOf(kind).element;
        }
    }

    TEST(ActionButtonsTest, NoActionsShowNoButtons) {
        const ActionButtons buttons = MakeActionButtons({});

        EXPECT_TRUE(Shown(buttons).empty());
        EXPECT_EQ(buttons, ActionButtons{});
    }

    TEST(ActionButtonsTest, DiscardsAreNoButtonsAndDeclareNeedsADeclaringDiscard) {
        eastl::vector<HandAction> plain{HandAction::MakeDiscard(T("1p")), HandAction::MakeDiscard(T("2p"))};
        EXPECT_TRUE(Shown(MakeActionButtons(plain)).empty());

        plain.push_back(HandAction::MakeDiscard(T("2p"), true));
        plain.push_back(HandAction::MakeDiscard(T("3p"), true));
        EXPECT_EQ(Shown(MakeActionButtons(plain)), std::vector<ButtonKind>{ButtonKind::Declare});
    }

    TEST(ActionButtonsTest, TheWinAndTheKongsOfAnOwnTurn) {
        const eastl::vector<HandAction> list{HandAction::MakeDiscard(T("1p")), HandAction::MakeDeclareWin(), HandAction::MakeConcealedKong(T("5p")), HandAction::MakeConcealedKong(T("9m")),
                                             HandAction::MakeUpgradeKong(T("Ew"))};

        const ActionButtons buttons = MakeActionButtons(list);

        EXPECT_EQ(Shown(buttons), (std::vector<ButtonKind>{ButtonKind::Win, ButtonKind::ConcealedKong0, ButtonKind::ConcealedKong1, ButtonKind::UpgradeKong}));
        EXPECT_EQ(buttons[ButtonKind::Win].action, HandAction::MakeDeclareWin());
        EXPECT_EQ(buttons[ButtonKind::ConcealedKong0].action, HandAction::MakeConcealedKong(T("5p")));
        EXPECT_EQ(buttons[ButtonKind::ConcealedKong1].action, HandAction::MakeConcealedKong(T("9m")));
        EXPECT_EQ(buttons[ButtonKind::UpgradeKong].action, HandAction::MakeUpgradeKong(T("Ew")));
        EXPECT_EQ(Label(buttons, ButtonKind::ConcealedKong0), "Kong 5p");
        EXPECT_EQ(Label(buttons, ButtonKind::ConcealedKong1), "Kong 9m");
        EXPECT_EQ(Label(buttons, ButtonKind::UpgradeKong), "Add Ew");
    }

    TEST(ActionButtonsTest, TheClaimsOfAWindowEachGetTheirButtonAndChowsAreLabelledBySequence) {
        const eastl::vector<HandAction> list =
            Claims({{ClaimKind::Mahjong, Tile{}}, {ClaimKind::Kong, Tile{}}, {ClaimKind::Pung, Tile{}}, {ClaimKind::Chow, T("4p")}, {ClaimKind::Chow, T("5p")}, {ClaimKind::Chow, T("7s")}});

        const ActionButtons buttons = MakeActionButtons(list);

        EXPECT_EQ(Shown(buttons), (std::vector<ButtonKind>{ButtonKind::ClaimMahjong, ButtonKind::ClaimPung, ButtonKind::ClaimKong, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1,
                                                           ButtonKind::ClaimChow2, ButtonKind::Pass}));
        EXPECT_EQ(buttons[ButtonKind::ClaimMahjong].action, HandAction::MakeClaim(ClaimKind::Mahjong));
        EXPECT_EQ(buttons[ButtonKind::ClaimPung].action, HandAction::MakeClaim(ClaimKind::Pung));
        EXPECT_EQ(buttons[ButtonKind::ClaimKong].action, HandAction::MakeClaim(ClaimKind::Kong));
        EXPECT_EQ(buttons[ButtonKind::ClaimChow0].action, HandAction::MakeClaim(ClaimKind::Chow, T("4p")));
        EXPECT_EQ(buttons[ButtonKind::ClaimChow2].action, HandAction::MakeClaim(ClaimKind::Chow, T("7s")));
        EXPECT_EQ(buttons[ButtonKind::Pass].action, HandAction::MakePass());
        EXPECT_EQ(Label(buttons, ButtonKind::ClaimChow0), "Chow 4-6p");
        EXPECT_EQ(Label(buttons, ButtonKind::ClaimChow1), "Chow 5-7p");
        EXPECT_EQ(Label(buttons, ButtonKind::ClaimChow2), "Chow 7-9s");
    }

    TEST(ActionButtonsTest, AFourthChowIsDroppedAndAFourthConcealedKongToo) {
        // The rules never list more (a tile has three sequences, fourteen tiles hold three fours); a longer list must not write outside
        // the table
        const ActionButtons chows = MakeActionButtons(Claims({{ClaimKind::Chow, T("1p")}, {ClaimKind::Chow, T("2p")}, {ClaimKind::Chow, T("3p")}, {ClaimKind::Chow, T("4p")}}));
        EXPECT_EQ(Label(chows, ButtonKind::ClaimChow2), "Chow 3-5p");
        EXPECT_EQ(Shown(chows), (std::vector<ButtonKind>{ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::ClaimChow2, ButtonKind::Pass}));

        const ActionButtons kongs =
            MakeActionButtons({HandAction::MakeConcealedKong(T("1p")), HandAction::MakeConcealedKong(T("2p")), HandAction::MakeConcealedKong(T("3p")), HandAction::MakeConcealedKong(T("4p"))});
        EXPECT_EQ(Label(kongs, ButtonKind::ConcealedKong2), "Kong 3p");
        EXPECT_EQ(Shown(kongs), (std::vector<ButtonKind>{ButtonKind::ConcealedKong0, ButtonKind::ConcealedKong1, ButtonKind::ConcealedKong2}));
    }

    TEST(ActionButtonsTest, TheButtonsOfARealClaimWindowAreItsLegalClaimsAndPass) {
        // Seat 1 holds 4p 5p 5p 6p 7p; East draws and discards a 5p: the pung and two chows (4-6p, 5-7p) are on offer
        FixedWallSpec spec = SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "5p");
        spec.hands[kSeat1] = "4p 5p 5p 6p 7p 2s 5s 8s 2m 5m 8m Ww Nw";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        const ActionButtons buttons = MakeActionButtons(model.LegalActions(kSeat1));

        EXPECT_EQ(Shown(buttons), (std::vector<ButtonKind>{ButtonKind::ClaimPung, ButtonKind::ClaimChow0, ButtonKind::ClaimChow1, ButtonKind::Pass}));
        EXPECT_EQ(Label(buttons, ButtonKind::ClaimChow0), "Chow 4-6p");
        EXPECT_EQ(Label(buttons, ButtonKind::ClaimChow1), "Chow 5-7p");
        for (int i = 0; i < kButtonCount; ++i) {
            const ButtonState& button = buttons.buttons[static_cast<size_t>(i)];
            if (button.shown && static_cast<ButtonKind>(i) != ButtonKind::Declare) {
                HandModel copy = model;
                EXPECT_TRUE(copy.Apply(kSeat1, button.action).has_value()) << SpecOf(static_cast<ButtonKind>(i)).element;
            }
        }
        EXPECT_TRUE(Shown(MakeActionButtons(model.LegalActions(kSeat2))).empty()) << "a seat out of the window has no button";
    }

}  // namespace BECore::Mahjong::Tests
