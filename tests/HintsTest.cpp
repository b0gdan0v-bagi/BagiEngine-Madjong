#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"
#include "StringsTestSupport.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Hints.h>
#include <Mahjong/Table/ScoreText.h>
#include <gtest/gtest.h>
#include <set>

/**
 * The pure helper of the hints (`Table/Hints.h`): the waiting row, the discards that declare the calling hand, the advice and its words,
 * and the rule that says when hints are on screen. Fixed walls with the answers worked out by hand; the waiting hands are seat 1 (13
 * tiles) seen while East acts, so no draw is needed. Pure logic: no engine services (the words are the shipped file).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // East's hand and the quiet hands of the others (no claim window opens on a draw of a test).
        constexpr const char* kEastHand = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        // A hand that waits on the east wind alone (123s 456s 234p 789m and a single Ew).
        constexpr const char* kSingleWait = "1s 2s 3s 4s 5s 6s 7m 8m 9m 2p 3p 4p Ew";
        // 123s 456s 789m 5p 6p and a pair of east winds: waits on 4p and 7p.
        constexpr const char* kTwoSidedWait = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p Ew Ew";
        // 1p 2p wait on the 3p (an edge wait).
        constexpr const char* kEdgeWait = "1p 2p 4s 5s 6s 7s 8s 9s 7m 8m 9m Rd Rd";
        // 4p 6p wait on the 5p (a middle wait).
        constexpr const char* kMiddleWait = "4p 6p 1s 2s 3s 4s 5s 6s 7m 8m 9m Ew Ew";
        // Six pairs of honours and a south wind: seven pairs, waits on the second south wind.
        constexpr const char* kSevenPairsWait = "Ew Ew Ww Ww Nw Nw Rd Rd Wd Wd Gd Gd Sw";
        // One of each orphan: waits on all thirteen.
        constexpr const char* kOrphansWait = "1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd";

        /// Seat 1 holds `seat1` (13 tiles); East acts first. `live` is the start of the live wall (East draws its first tile).
        FixedWallSpec ViewerSpec(const char* seat1, const std::string& live = "") {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kEastHand;
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = kQuiet3;
            spec.live = live;
            return spec;
        }

        HandModel ViewerModel(const char* seat1, const std::string& live = "") {
            return MakeModel(ViewerSpec(seat1, live));
        }

        TableView ViewOfSeat1(const char* seat1, const std::string& live = "") {
            return MakeTableView(ViewerModel(seat1, live), kSeat1);
        }

        using Waits = std::vector<std::pair<std::string, int>>;

        Waits WaitsOf(const TableView& view) {
            Waits waits;
            for (const WaitEntry& entry : WaitingTiles(view)) {
                waits.emplace_back(std::string(ToString(entry.tile)), entry.unseen);
            }
            return waits;
        }

        /// The ids of the tiles marked as declaring.
        std::set<int> MarkedTiles(const TableView& view) {
            std::set<int> marked;
            const auto declaring = DeclaringDiscards(view);
            for (size_t id = 0; id < declaring.size(); ++id) {
                if (declaring[id]) {
                    marked.insert(static_cast<int>(id));
                }
            }
            return marked;
        }

        std::set<int> Ids(std::initializer_list<const char*> tiles) {
            std::set<int> ids;
            for (const char* tile : tiles) {
                ids.insert(T(tile).Id());
            }
            return ids;
        }

        std::string Std(const eastl::string& text) {
            return std::string(text.data(), text.size());
        }

        const UIStrings& Words() {
            static UIStrings strings;
            [[maybe_unused]] static const bool loaded = [] {
                EXPECT_TRUE(LoadShippedStrings(strings).empty());
                return true;
            }();
            return strings;
        }

        /// East throws the only tile of `live`; seat 1 (South) holds `seat1`, the others hold hands that claim nothing the tests look at.
        HandModel WindowModel(const char* seat1, const char* discard) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s";
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = "2p 5p 8p 2s 5s 8s 3m 6m 9m Gd Wd 9p 8m";
            spec.hands[kSeat3] = "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s";
            spec.live = discard;
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T(discard)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        /// The advice the computer player gives with the rng of the rules, worked out from the phase of the view.
        HandAction ComputerChoice(const TableView& view) {
            MahjongRng rng(kAdviceSeed);
            return view.phase == Phase::Discard ? ChooseTurnAction(view, rng) : ChooseClaimAction(view, rng);
        }

        /// The person who has to act follows the advice.
        void PersonFollowsTheAdvice(TableSession& session, const Match& match) {
            const std::optional<HandAction> action = AdviceFor(MakeTableView(match.Hand(), session.Actor()));
            ASSERT_TRUE(action.has_value());
            ASSERT_TRUE(session.PerformPerson(*action).has_value());
        }

    }  // namespace

    // ----- the waiting row -----

    TEST(HintsTest, ASingleWaitListsTheTileWithItsUnseenCopies) {
        // The viewer holds one east wind: 3 are unseen (the others' hands do not count as seen).
        EXPECT_EQ(WaitsOf(ViewOfSeat1(kSingleWait)), (Waits{{"Ew", 3}}));
    }

    TEST(HintsTest, ATwoSidedWaitListsBothTilesInTheOrderOfTheIds) {
        // Seat 0 holds a 4p and a 7p, but that is not seen: all four copies of each are unseen.
        EXPECT_EQ(WaitsOf(ViewOfSeat1(kTwoSidedWait)), (Waits{{"4p", 4}, {"7p", 4}}));
    }

    TEST(HintsTest, AnEdgeWaitAndAMiddleWait) {
        EXPECT_EQ(WaitsOf(ViewOfSeat1(kEdgeWait)), (Waits{{"3p", 4}}));
        EXPECT_EQ(WaitsOf(ViewOfSeat1(kMiddleWait)), (Waits{{"5p", 4}}));
    }

    TEST(HintsTest, SevenPairsWaitsOnTheLastSingleTile) {
        EXPECT_EQ(WaitsOf(ViewOfSeat1(kSevenPairsWait)), (Waits{{"Sw", 3}}));
    }

    TEST(HintsTest, ThirteenOrphansListsAllThirteenKinds) {
        const eastl::vector<WaitEntry> waits = WaitingTiles(ViewOfSeat1(kOrphansWait));

        ASSERT_EQ(static_cast<int>(waits.size()), kMaxWaitingTiles);
        for (size_t i = 0; i < waits.size(); ++i) {
            EXPECT_EQ(waits[i].unseen, 3) << ToString(waits[i].tile);
            if (i > 0) {
                EXPECT_LT(waits[i - 1].tile.Id(), waits[i].tile.Id());
            }
        }
    }

    TEST(HintsTest, TheCopiesInTheMeldsOfTheViewerAreCounted) {
        // Seat 1 pungs the 5p of East and discards the Gd: 4p 6p wait on the 5p, three copies of which lie in its own meld.
        FixedWallSpec spec = ViewerSpec("5p 5p 4p 6p 1s 2s 3s 7m 8m 9m Ww Ww Gd", "5p");
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
        ASSERT_TRUE(model.Discard(kSeat1, T("Gd")).has_value());
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_EQ(view.own.melds.size(), 1U);
        ASSERT_EQ(view.own.concealed.size(), 10U);

        EXPECT_EQ(WaitsOf(view), (Waits{{"5p", 1}}));
    }

    TEST(HintsTest, TheUnseenCopiesFallWithThePile) {
        // East throws the 4p it draws: the window of seat 1 (a chow 4p 5p 6p) is open and the 4p lies in the pile.
        HandModel model = ViewerModel(kTwoSidedWait, "4p");
        ASSERT_TRUE(model.Discard(kSeat0, T("4p")).has_value());
        const TableView view = MakeTableView(model, kSeat1);

        EXPECT_EQ(WaitsOf(view), (Waits{{"4p", 3}, {"7p", 4}}));
    }

    TEST(HintsTest, TheOpenMeldOfAnotherSeatIsSeen) {
        // Seat 1 has punged the 5p; seat 2 waits on the 5p and sees three of its four copies.
        const HandModel model = UpgradeModel();
        const TableView view = MakeTableView(model, kSeat2);
        ASSERT_EQ(view.own.concealed.size(), 13U);

        EXPECT_EQ(WaitsOf(view), (Waits{{"5p", 1}}));
    }

    TEST(HintsTest, ADeadWaitIsListedWithNoCopies) {
        TableView view = ViewOfSeat1(kSingleWait);
        view.visible[T("Ew").Id()] = 4;

        EXPECT_EQ(WaitsOf(view), (Waits{{"Ew", 0}}));
    }

    TEST(HintsTest, AHandThatDoesNotWaitHasNoRow) {
        EXPECT_TRUE(WaitingTiles(ViewOfSeat1("1p 3p 5p 8p 2s 4s 6s 9s 1m 3m 5m 7m Ew")).empty());
        EXPECT_TRUE(WaitingTiles(MakeTableView(ViewerModel(kSingleWait), kSeat2)).empty());
        EXPECT_TRUE(WaitingTiles(TableView{}).empty());
    }

    TEST(HintsTest, NoRowAtFourteenTiles) {
        // East holds a waiting hand plus the tile it draws: 14 tiles, no row until it has discarded.
        FixedWallSpec spec = ViewerSpec(kSingleWait, "Ww");
        spec.hands[kSeat0] = kTwoSidedWait;
        const HandModel model = MakeModel(spec);
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_EQ(view.own.concealed.size(), 14U);

        EXPECT_TRUE(WaitingTiles(view).empty());
    }

    TEST(HintsTest, TheRowDoesNotDependOnTheDeclaration) {
        // Seat 1 declares with its first discard (the Gd it draws); the row is the same before and after.
        HandModel model = ViewerModel(kTwoSidedWait, "Rd Gd Wd Nw");
        const Waits before = WaitsOf(MakeTableView(model, kSeat1));
        Lap(model, "Rd Gd", {kSeat1});
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(view.own.callingDeclared);

        EXPECT_EQ(before, (Waits{{"4p", 4}, {"7p", 4}}));
        EXPECT_EQ(WaitsOf(view), before);
    }

    // ----- the discards that declare -----

    TEST(HintsTest, TheMarkedDiscardsAreTheDeclaringDiscardsOfTheLegalActions) {
        // 123s 456s 789m 5p 6p 6p Ew Ew: the 5p leaves a pair wait on 6p and Ew, a 6p the two-sided wait; nothing else leaves a waiting hand.
        FixedWallSpec spec = ViewerSpec(kSingleWait, "Ew");
        spec.hands[kSeat0] = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew";
        const HandModel model = MakeModel(spec);
        const TableView view = MakeTableView(model, kSeat0);

        EXPECT_EQ(MarkedTiles(view), Ids({"5p", "6p"}));
        std::set<int> fromLegal;
        for (const HandAction& action : view.legal) {
            if (action.kind == ActionKind::Discard && action.declareCalling) {
                fromLegal.insert(action.tile.Id());
            }
        }
        EXPECT_EQ(MarkedTiles(view), fromLegal);
    }

    TEST(HintsTest, NothingIsMarkedOnAHandThatCannotDeclare) {
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew", "Nw"));

        EXPECT_TRUE(MarkedTiles(MakeTableView(model, kSeat0)).empty());
    }

    TEST(HintsTest, TheMarksAfterADeclarationAreStillTheLegalOnes) {
        // East has declared on 123s 456s 789m 5p 6p Ew Ew and draws a 6p: whatever the model lists with the declaration is marked.
        HandModel model = MakeModel(SpecOf(kTwoSidedWait, "Gd Wd Gd Nw 6p"));
        Lap(model, "Gd Wd Gd Nw", {kSeat0});
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(view.own.callingDeclared);
        std::set<int> fromLegal;
        for (const HandAction& action : view.legal) {
            if (action.kind == ActionKind::Discard && action.declareCalling) {
                fromLegal.insert(action.tile.Id());
            }
        }

        EXPECT_EQ(MarkedTiles(view), fromLegal);
    }

    TEST(HintsTest, NothingIsMarkedOutOfTurnOrInAClaimWindow) {
        const HandModel turn = ViewerModel(kTwoSidedWait);
        EXPECT_TRUE(MarkedTiles(MakeTableView(turn, kSeat1)).empty());  // East acts, not seat 1

        const HandModel window = WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        EXPECT_TRUE(MarkedTiles(MakeTableView(window, kSeat1)).empty());
        EXPECT_TRUE(MarkedTiles(TableView{}).empty());
    }

    // ----- the advice -----

    TEST(HintsTest, TheAdviceIsWhatTheComputerPlayerChoosesWithTheFixedSeed) {
        const HandModel turn = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew", "Nw"));
        const TableView view = MakeTableView(turn, kSeat0);

        const std::optional<HandAction> advice = AdviceFor(view);

        ASSERT_TRUE(advice.has_value());
        EXPECT_EQ(*advice, ComputerChoice(view));
        EXPECT_TRUE(Has(view.legal, *advice));
        EXPECT_EQ(AdviceFor(view), advice);  // repeatable
        // Worked out by hand: 123s 456s 789m 5p 5p and three single honours; the north wind is worthless, so it goes.
        EXPECT_EQ(*advice, HandAction::MakeDiscard(T("Nw")));
    }

    TEST(HintsTest, TheAdviceDeclaresTheReadyHandWithTheBetterWait) {
        const HandModel model = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew"));

        // 4p 7p (4 + 4 unseen copies) beat the pair wait on 6p and Ew (2 + 2).
        EXPECT_EQ(AdviceFor(MakeTableView(model, kSeat0)), std::optional<HandAction>(HandAction::MakeDiscard(T("6p"), true)));
    }

    TEST(HintsTest, TheAdviceInAClaimWindow) {
        // A pung that lowers the shanten is claimed, a pung of a simple tile that does nothing is passed, a chow that completes a set is claimed.
        const HandModel pung = WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        const TableView pungView = MakeTableView(pung, kSeat1);
        EXPECT_EQ(AdviceFor(pungView), std::optional<HandAction>(HandAction::MakeClaim(ClaimKind::Pung)));
        EXPECT_EQ(*AdviceFor(pungView), ComputerChoice(pungView));

        const HandModel pass = WindowModel("3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw 5m 5m", "5m");
        const TableView passView = MakeTableView(pass, kSeat1);
        ASSERT_TRUE(Has(passView.legal, HandAction::MakeClaim(ClaimKind::Pung)));
        EXPECT_EQ(AdviceFor(passView), std::optional<HandAction>(HandAction::MakePass()));

        const HandModel chow = WindowModel("4p 6p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        EXPECT_EQ(AdviceFor(MakeTableView(chow, kSeat1)), std::optional<HandAction>(HandAction::MakeClaim(ClaimKind::Chow, T("4p"))));
    }

    TEST(HintsTest, TheAdviceInARobbedKongWindowTakesMahjong) {
        HandModel model = UpgradeModel({kSeat2});
        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);
        const TableView view = MakeTableView(model, kSeat2);

        EXPECT_EQ(AdviceFor(view), std::optional<HandAction>(HandAction::MakeClaim(ClaimKind::Mahjong)));
    }

    TEST(HintsTest, NoAdviceWhenThereIsNothingToDo) {
        const HandModel model = ViewerModel(kTwoSidedWait);

        EXPECT_FALSE(AdviceFor(MakeTableView(model, kSeat1)).has_value());  // East acts
        EXPECT_FALSE(AdviceFor(TableView{}).has_value());
    }

    TEST(HintsTest, TheAdviceIsAlwaysListedAndRepeatableOnRandomHands) {
        for (uint32_t seed = 1; seed <= 20; ++seed) {
            MahjongSettings settings;
            Match match;
            ASSERT_TRUE(match.Start(settings, seed).has_value());
            ComputerTable table(seed);
            for (int step = 0; step < 60 && !match.Hand().Result().has_value(); ++step) {
                const Seat actor = ComputerTable::NextActor(match.Hand());
                const TableView view = MakeTableView(match.Hand(), actor);
                const std::optional<HandAction> advice = AdviceFor(view);
                ASSERT_TRUE(advice.has_value()) << "seed " << seed << " step " << step;
                EXPECT_TRUE(Has(view.legal, *advice));
                EXPECT_EQ(AdviceFor(view), advice);
                ASSERT_TRUE(table.Step(match.Hand()).has_value());
            }
        }
    }

    // ----- no peeking -----

    TEST(HintsTest, TwoModelsThatDifferOnlyInTheOtherHandsGiveTheSameHints) {
        const auto make = [](bool other) {
            FixedWallSpec spec;
            // Seat 0 is the viewer (it draws the first tile of `live`); the others and the rest of the wall are swapped around.
            spec.hands[kSeat0] = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew";
            spec.hands[kSeat1] = other ? kQuiet3 : kQuiet1;
            spec.hands[kSeat2] = other ? kQuiet1 : kQuiet2;
            spec.hands[kSeat3] = other ? kQuiet2 : kQuiet3;
            spec.live = std::string("Ew ") + (other ? "Gd Wd Nw" : "Nw Wd Gd");
            return MakeModel(spec);
        };
        const HandModel a = make(false);
        const HandModel b = make(true);
        const TableView viewA = MakeTableView(a, kSeat0);
        const TableView viewB = MakeTableView(b, kSeat0);

        EXPECT_EQ(WaitsOf(viewA), WaitsOf(viewB));
        EXPECT_EQ(MarkedTiles(viewA), MarkedTiles(viewB));
        EXPECT_FALSE(MarkedTiles(viewA).empty());
        EXPECT_EQ(AdviceFor(viewA), AdviceFor(viewB));
        EXPECT_EQ(Std(AdviceText(Words(), viewA, *AdviceFor(viewA))), Std(AdviceText(Words(), viewB, *AdviceFor(viewB))));
    }

    TEST(HintsTest, TheWaitingRowOfAClaimWindowDoesNotSeeTheOtherHands) {
        const auto make = [](const char* east, const char* seat3) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = east;
            spec.hands[kSeat1] = kTwoSidedWait;
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = seat3;
            spec.live = "Gd";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("Gd")).has_value());
            return model;
        };
        const HandModel a = make("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Rd Wd", kQuiet3);
        const HandModel b = make("2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Rd Wd", kQuiet1);
        const TableView viewA = MakeTableView(a, kSeat1);
        const TableView viewB = MakeTableView(b, kSeat1);

        EXPECT_EQ(WaitsOf(viewA), WaitsOf(viewB));
        EXPECT_EQ(AdviceFor(viewA), AdviceFor(viewB));
    }

    // ----- the words of the advice -----

    TEST(HintsTest, EveryKindOfAdviceHasASentenceInEveryLanguage) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        TableView view;
        view.pendingTile = T("5p");
        struct Case {
            HandAction advice;
            std::vector<const char*> tiles;
        };
        const std::vector<Case> cases{
            {HandAction::MakeDiscard(T("7s")), {"7s"}},
            {HandAction::MakeDiscard(T("Nw"), true), {"Nw"}},
            {HandAction::MakeDeclareWin(), {}},
            {HandAction::MakeConcealedKong(T("Rd")), {"Rd"}},
            {HandAction::MakeUpgradeKong(T("3m")), {"3m"}},
            {HandAction::MakeClaim(ClaimKind::Mahjong), {}},
            {HandAction::MakeClaim(ClaimKind::Pung), {"5p"}},
            {HandAction::MakeClaim(ClaimKind::Kong), {"5p"}},
            {HandAction::MakeClaim(ClaimKind::Chow, T("4p")), {"4p", "5p", "6p"}},
            {HandAction::MakePass(), {}},
        };

        ForEachLanguage(strings, [&](PoolString language) {
            std::set<std::string> seen;
            for (const Case& one : cases) {
                const std::string text = Std(AdviceText(strings, view, one.advice));
                EXPECT_FALSE(text.empty()) << language.ToStringView().data();
                EXPECT_EQ(text.find('{'), std::string::npos) << text;
                EXPECT_EQ(text.find('}'), std::string::npos) << text;
                EXPECT_EQ(text.find("hints."), std::string::npos) << "a key is missing: " << text;
                for (const char* tile : one.tiles) {
                    const std::string name = Std(TileName(strings, T(tile)));
                    EXPECT_NE(text.find(name), std::string::npos) << text << " lacks " << name;
                }
                EXPECT_TRUE(seen.insert(text).second) << "two kinds say the same: " << text;
            }
        });
    }

    TEST(HintsTest, TheSentencesFollowTheLanguage) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        TableView view;
        const HandAction advice = HandAction::MakeDiscard(T("5p"));

        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        EXPECT_EQ(Std(AdviceText(strings, view, advice)), "Discard 5 Dots.");
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_NE(Std(AdviceText(strings, view, advice)), "Discard 5 Dots.");
        EXPECT_NE(Std(AdviceText(strings, view, advice)).find("Круги 5"), std::string::npos);
    }

    TEST(HintsTest, TheKeysOfThePanelAreInBothLanguages) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());

        ForEachLanguage(strings, [&](PoolString language) {
            for (const char* key : {kWaitCaptionKey, kAdviseButtonKey, kSetupHintsKey, kSetupOnKey, kSetupOffKey}) {
                EXPECT_NE(Std(Tr(strings, key)), key) << language.ToStringView().data() << " " << key;
            }
        });
    }

    // ----- when hints apply -----

    TEST(HintsTest, HintsApplyForAPersonAtAPlayingTable) {
        Match match;
        TableSession session(match);
        EXPECT_FALSE(HintsApply(true, false, session));  // no match started
        ASSERT_TRUE(session.StartMatch(MahjongSettings{}, 3, kDefaultSeatSetup).has_value());
        ASSERT_EQ(session.GetPhase(), SessionPhase::Playing);

        EXPECT_TRUE(HintsApply(true, false, session));
        EXPECT_FALSE(HintsApply(false, false, session));  // switched off
        EXPECT_FALSE(HintsApply(true, true, session));    // the seat choice is open
    }

    TEST(HintsTest, NoHintsForASpectator) {
        Match match;
        TableSession session(match);
        constexpr SeatSetup kNoPeople{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
        ASSERT_TRUE(session.StartMatch(MahjongSettings{}, 3, kNoPeople).has_value());
        ASSERT_EQ(session.GetPhase(), SessionPhase::Playing);
        ASSERT_EQ(session.Viewer(), kNoSeat);

        EXPECT_FALSE(HintsApply(true, false, session));
    }

    TEST(HintsTest, NoHintsUnderTheCoverAndTheViewersOwnHintsAfterReveal) {
        Match match;
        TableSession session(match);
        constexpr SeatSetup kAllPeople{SeatKind::Person, SeatKind::Person, SeatKind::Person, SeatKind::Person};
        ASSERT_TRUE(session.StartMatch(MahjongSettings{}, 3, kAllPeople).has_value());
        ASSERT_TRUE(session.HandoffPending());

        EXPECT_FALSE(HintsApply(true, false, session));
        session.Reveal();
        ASSERT_FALSE(session.HandoffPending());
        EXPECT_TRUE(HintsApply(true, false, session));
        // Another person acts next: the cover is up again.
        PersonFollowsTheAdvice(session, match);
        ASSERT_TRUE(session.HandoffPending());
        EXPECT_FALSE(HintsApply(true, false, session));
    }

    TEST(HintsTest, NoHintsWhenTheHandOrTheMatchIsOver) {
        Match match;
        TableSession session(match);
        MahjongSettings settings;
        settings.handsPerMatch = kShortMatchHands;
        ASSERT_TRUE(session.StartMatch(settings, 3, kDefaultSeatSetup).has_value());
        bool sawHandOver = false;

        for (int guard = 0; guard < 20000 && session.GetPhase() != SessionPhase::MatchOver; ++guard) {
            session.RunComputers();
            if (session.GetPhase() == SessionPhase::HandOver) {
                sawHandOver = true;
                EXPECT_FALSE(HintsApply(true, false, session));
                ASSERT_TRUE(session.NextHand().has_value());
            } else if (session.GetPhase() == SessionPhase::Playing) {
                EXPECT_TRUE(HintsApply(true, false, session));
                PersonFollowsTheAdvice(session, match);
            }
        }

        EXPECT_TRUE(sawHandOver);
        ASSERT_EQ(session.GetPhase(), SessionPhase::MatchOver);
        EXPECT_FALSE(HintsApply(true, false, session));
    }

}  // namespace BECore::Mahjong::Tests
