#include <Common/Printers.h>
#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/HintCard.h>
#include <Mahjong/Table/ScoreText.h>
#include <gtest/gtest.h>
#include <set>
#include <tests/ComputerTestSupport.h>
#include <tests/FixedWall.h>
#include <tests/HandTestSupport.h>
#include <tests/StringsTestSupport.h>
#include <vector>

/**
 * The pure helper of the hints (`Table/Hints.h`, `Table/HintCard.h`). Fixed walls with the answers worked out by hand; the waiting
 * hands are seat 1 (13 tiles) seen while East acts, so no draw is needed. Pure logic: no engine services (the words are the
 * shipped file).
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
        FixedWallSpec ViewerSpec(const char* seat1, const String& live = "") {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kEastHand;
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = kQuiet3;
            spec.live = live;
            return spec;
        }

        HandModel ViewerModel(const char* seat1, const String& live = "") {
            return MakeModel(ViewerSpec(seat1, live));
        }

        TableView ViewOfSeat1(const char* seat1, const String& live = "") {
            return MakeTableView(ViewerModel(seat1, live), kSeat1);
        }

        using Waits = Vector<Pair<String, int>>;

        Waits WaitsOf(const TableView& view) {
            Waits waits;
            for (const WaitEntry& entry : WaitingTiles(view)) {
                waits.emplace_back(ToString(entry.tile), entry.unseen);
            }
            return waits;
        }

        /// The ids of the tiles marked as declaring.
        Set<int> MarkedTiles(const TableView& view) {
            Set<int> marked;
            const auto declaring = DeclaringDiscards(view);
            for (size_t id = 0; id < declaring.size(); ++id) {
                if (declaring[id]) {
                    marked.insert(static_cast<int>(id));
                }
            }
            return marked;
        }

        Set<int> Ids(std::initializer_list<const char*> tiles) {
            Set<int> ids;
            for (const char* tile : tiles) {
                ids.insert(T(tile).Id());
            }
            return ids;
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

        /// One advised table for the reasons: the view of the viewer and the rule of the chooser that is expected to decide.
        struct ReasonCase {
            const char* name;
            TableView view;
            Optional<TurnStep> turnStep;
            Optional<ClaimStep> claimStep;
        };

        ReasonCase TurnCase(const char* name, const HandModel& model, Seat seat, TurnStep step) {
            return {name, MakeTableView(model, seat), step, std::nullopt};
        }

        ReasonCase ClaimCase(const char* name, const HandModel& model, Seat seat, ClaimStep step) {
            return {name, MakeTableView(model, seat), std::nullopt, step};
        }

        /// East throws the red dragon that completes seat 1, which has declared the calling hand.
        HandModel MahjongWindowModel() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s";
            spec.hands[kSeat1] = "Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p";
            spec.hands[kSeat2] = "2p 5p 8p 2s 5s 8s 3m 6m 9m Gd Wd 9p 8m";
            spec.hands[kSeat3] = "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s";
            spec.live = "9p Wd Gd Nw Rd";
            HandModel model = MakeModel(spec);
            Lap(model, "9p Wd Gd Nw", {kSeat1});
            EXPECT_TRUE(model.Discard(kSeat0, T("Rd")).has_value());
            return model;
        }

        /// Every kind of advice, each on a hand worked out by hand (the numbers are those of `ComputerExplainTest`).
        Vector<ReasonCase> ReasonCases() {
            Vector<ReasonCase> cases;
            cases.push_back(TurnCase("discard", MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew", "Nw")), kSeat0, TurnStep::Discard));
            cases.push_back(TurnCase("declare", MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew")), kSeat0, TurnStep::Declare));
            HandModel keeping = MakeModel(SpecOf(kTwoSidedWait, "Gd Wd Gd Nw 6p"));
            Lap(keeping, "Gd Wd Gd Nw", {kSeat0});
            cases.push_back(TurnCase("keepWaiting", keeping, kSeat0, TurnStep::KeepWaiting));
            cases.push_back(TurnCase("concealedKong", MakeModel(SpecOf("Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd", "Ew")), kSeat0, TurnStep::Kong));
            cases.push_back(TurnCase("upgradeKong", OpenPungModel("5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd"), kSeat1, TurnStep::Kong));
            HandModel winning = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Rd"));
            Lap(winning, "9p Wd Gd Nw", {kSeat0});
            cases.push_back(TurnCase("win", winning, kSeat0, TurnStep::DeclareWin));
            cases.push_back(ClaimCase("claimPung", WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p"), kSeat1, ClaimStep::Claim));
            cases.push_back(ClaimCase("claimChow", WindowModel("4p 6p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p"), kSeat1, ClaimStep::Claim));
            cases.push_back(ClaimCase("claimWithAlternatives", WindowModel("4p 6p 5p 5p 1s 2s 3s 7s 8s 9s Nw Nw Wd", "5p"), kSeat1, ClaimStep::Claim));
            cases.push_back(ClaimCase("passWaiting", WindowModel("Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p", "Rd"), kSeat1, ClaimStep::WaitingHand));
            cases.push_back(ClaimCase("passNoGain", WindowModel("3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw 5m 5m", "5m"), kSeat1, ClaimStep::NoGain));
            cases.push_back(ClaimCase("claimMahjong", MahjongWindowModel(), kSeat1, ClaimStep::Mahjong));
            HandModel robbed = UpgradeModel({kSeat2});
            EXPECT_TRUE(robbed.UpgradeKong(kSeat1, T("5p")).has_value());
            cases.push_back(ClaimCase("robbedKong", robbed, kSeat2, ClaimStep::Mahjong));
            return cases;
        }

        /// The same table as seen with `opponent` declared (a hand-made change of the view: the reading is of the declarations of `view.seats`).
        TableView WithDeclaredOpponent(TableView view) {
            view.seats[(view.seat + 1) % kSeatCount].callingDeclared = true;
            return view;
        }

        /// The sentences of the danger line in English, one per grade.
        const char* const kDangerSentences[] = {"Safe against the ready hand.", "Low danger from the ready hand.", "Risky against the ready hand.", "Dangerous against the ready hand."};
        constexpr const char* kSafestTie = "this one is the safest.";

        bool HasDangerLine(const Vector<String>& lines) {
            return std::any_of(lines.begin(), lines.end(),
                               [](const String& line) { return std::any_of(std::begin(kDangerSentences), std::end(kDangerSentences), [&](const char* sentence) { return line == sentence; }); });
        }

        /// Four sets and the lone 5m and green dragon, seat 1 declared: both discards declare, a tanki wait on the other (`ComputerExplainTest`).
        TableView TwoDeclaringDiscardsView() {
            TableView view;
            view.seat = kSeat0;
            view.phase = Phase::Discard;
            view.currentSeat = kSeat0;
            view.own.concealed = ParseTiles("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 5m Gd").value();
            view.visible = CountTiles(view.own.concealed);
            view.seats[kSeat1].callingDeclared = true;
            for (const char* tile : {"5m", "Gd"}) {
                view.legal.push_back(HandAction::MakeDiscard(T(tile), true));
            }
            return view;
        }

        /// The length of a text in letters (the Cyrillic ones are two bytes of UTF-8).
        size_t Letters(const String& text) {
            return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
        }

        /// The card of the advice of `view` (`ExplainAdviceFor`) with `tally` under it.
        HintCard CardOf(const UIStrings& strings, const TableView& view, StringView tally = {}) {
            const Optional<Explanation> explained = ExplainAdviceFor(view);
            EXPECT_TRUE(explained.has_value());
            return explained.has_value() ? MakeAdviceCard(strings, view, *explained, tally) : HintCard{};
        }

        /// A row as one string: the cells joined by `|`, the tiles in notation.
        String RowText(const CardRow& row) {
            String tiles;
            for (const Tile tile : row.tiles) {
                if (tile.IsValid()) {
                    AppendFormat(tiles, "{} ", ToString(tile));
                }
            }
            return Format("{}|{}|{}|{}|{}", row.who, tiles, row.move, row.steps, row.useful);
        }

        Vector<String> FooterOf(const HintCard& card) {
            Vector<String> lines;
            for (const String& line : card.footer) {
                lines.push_back(line);
            }
            return lines;
        }

        /// Everything the card says: the title, the head, a string per row, the lines under them.
        Vector<String> TextOf(const HintCard& card) {
            Vector<String> text{card.title, card.now};
            for (const CardRow& row : card.rows) {
                text.push_back(RowText(row));
            }
            for (const String& line : FooterOf(card)) {
                text.push_back(line);
            }
            return text;
        }

        /// The lines under the rows of the card of `view` (no tally).
        Vector<String> Lines(const UIStrings& strings, const TableView& view) {
            return FooterOf(CardOf(strings, view));
        }

        /// The steps as the card says them.
        String StepsWord(int steps) {
            return steps <= 0 ? String("ready") : BECore::ToString(steps);
        }

        /// The person who has to act follows the advice.
        void PersonFollowsTheAdvice(TableSession& session, const Match& match) {
            const Optional<HandAction> action = AdviceFor(MakeTableView(match.Hand(), session.Actor()));
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
        const Vector<WaitEntry> waits = WaitingTiles(ViewOfSeat1(kOrphansWait));

        ASSERT_EQ(static_cast<int>(waits.size()), kMaxWaitingTiles);
        for (size_t i = 0; i < waits.size(); ++i) {
            EXPECT_EQ(waits[i].unseen, 3) << ToStd(ToString(waits[i].tile));
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
        Set<int> fromLegal;
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
        Set<int> fromLegal;
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

        const Optional<HandAction> advice = AdviceFor(view);

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
        EXPECT_EQ(AdviceFor(MakeTableView(model, kSeat0)), Optional<HandAction>(HandAction::MakeDiscard(T("6p"), true)));
    }

    TEST(HintsTest, TheAdviceInAClaimWindow) {
        // A pung that lowers the shanten is claimed, a pung of a simple tile that does nothing is passed, a chow that completes a set is claimed.
        const HandModel pung = WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        const TableView pungView = MakeTableView(pung, kSeat1);
        EXPECT_EQ(AdviceFor(pungView), Optional<HandAction>(HandAction::MakeClaim(ClaimKind::Pung)));
        EXPECT_EQ(*AdviceFor(pungView), ComputerChoice(pungView));

        const HandModel pass = WindowModel("3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw 5m 5m", "5m");
        const TableView passView = MakeTableView(pass, kSeat1);
        ASSERT_TRUE(Has(passView.legal, HandAction::MakeClaim(ClaimKind::Pung)));
        EXPECT_EQ(AdviceFor(passView), Optional<HandAction>(HandAction::MakePass()));

        const HandModel chow = WindowModel("4p 6p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        EXPECT_EQ(AdviceFor(MakeTableView(chow, kSeat1)), Optional<HandAction>(HandAction::MakeClaim(ClaimKind::Chow, T("4p"))));
    }

    TEST(HintsTest, TheAdviceInARobbedKongWindowTakesMahjong) {
        HandModel model = UpgradeModel({kSeat2});
        ASSERT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);
        const TableView view = MakeTableView(model, kSeat2);

        EXPECT_EQ(AdviceFor(view), Optional<HandAction>(HandAction::MakeClaim(ClaimKind::Mahjong)));
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
                const Optional<HandAction> advice = AdviceFor(view);
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
            spec.live = Format("Ew {}", (other ? "Gd Wd Nw" : "Nw Wd Gd"));
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
        EXPECT_EQ(AdviceText(Words(), viewA, *AdviceFor(viewA)), AdviceText(Words(), viewB, *AdviceFor(viewB)));
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
            Vector<const char*> tiles;
        };
        const Vector<Case> cases{
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
            Set<String> seen;
            for (const Case& one : cases) {
                const String text = AdviceText(strings, view, one.advice);
                EXPECT_FALSE(text.empty()) << language.ToStringView().data();
                EXPECT_EQ(text.find('{'), String::npos) << text;
                EXPECT_EQ(text.find('}'), String::npos) << text;
                EXPECT_EQ(text.find("hints."), String::npos) << "a key is missing: " << text;
                for (const char* tile : one.tiles) {
                    const String name = TileName(strings, T(tile));
                    EXPECT_NE(text.find(name), String::npos) << text << " lacks " << name;
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
        EXPECT_EQ(AdviceText(strings, view, advice), "Discard 5 Dots.");
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        EXPECT_NE(AdviceText(strings, view, advice), "Discard 5 Dots.");
        EXPECT_NE(AdviceText(strings, view, advice).find("Круги 5"), String::npos);
    }

    TEST(HintsTest, TheKeysOfThePanelAreInBothLanguages) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        const Vector<const char*> keys{"hints.waitCaption",      "hints.adviseButton",       "setup.hints",
                                       "options.hintsHints",     "options.hintsCoach",       "setup.off",
                                       "hints.card.now",         "hints.card.steps",         "hints.card.useful",
                                       "hints.card.ready",       "hints.card.title.chow",    "hints.why.tie.none",
                                       "hints.why.tie.keep",     "hints.why.tie.chance",     "hints.why.tie.danger",
                                       "hints.why.danger.safe",  "hints.why.danger.low",     "hints.why.danger.medium",
                                       "hints.why.danger.high",  "hints.why.keep.worthless", "hints.why.keep.terminal",
                                       "hints.why.keep.simple",  "hints.why.keep.value",     "hints.card.best",
                                       "hints.card.yours",       "hints.card.declare",       "hints.card.kong",
                                       "hints.card.pung",        "hints.card.chow",          "hints.card.pass",
                                       "hints.card.mahjong",     "hints.card.noKong",        "hints.why.kong.noWorse",
                                       "hints.why.win",          "hints.why.claim.mahjong",  "hints.why.claim.value",
                                       "hints.why.pass.waiting", "hints.why.pass.noGain",    "hints.why.pass.points",
                                       "hints.why.kong.points"};

        ForEachLanguage(strings, [&](PoolString language) {
            for (const char* key : keys) {
                EXPECT_NE(Tr(strings, key), key) << language.ToStringView().data() << " " << key;
            }
        });
    }

    // ----- the reasons -----

    TEST(HintsTest, TheExplainedAdviceIsTheAdviceOfEveryKind) {
        for (const ReasonCase& one : ReasonCases()) {
            const Optional<Explanation> explained = ExplainAdviceFor(one.view);
            ASSERT_TRUE(explained.has_value()) << one.name;
            EXPECT_EQ(explained->Action(), *AdviceFor(one.view)) << one.name;
            ASSERT_EQ(explained->turn.has_value(), one.turnStep.has_value()) << one.name;
            if (one.turnStep.has_value()) {
                EXPECT_EQ(explained->turn->step, *one.turnStep) << one.name;
            } else {
                ASSERT_TRUE(explained->claim.has_value()) << one.name;
                EXPECT_EQ(explained->claim->step, *one.claimStep) << one.name;
            }
        }
        EXPECT_FALSE(ExplainAdviceFor(TableView{}).has_value());
        EXPECT_FALSE(ExplainAdviceFor(ViewOfSeat1(kTwoSidedWait)).has_value());  // East acts
    }

    TEST(HintsTest, EveryKindOfAdviceHasACardInEveryLanguage) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        const Vector<ReasonCase> cases = ReasonCases();

        ForEachLanguage(strings, [&](PoolString language) {
            for (const ReasonCase& one : cases) {
                const HintCard card = CardOf(strings, one.view);
                const String where = Format("{} {}", one.name, language.ToStringView().data());
                const HandAction advice = *AdviceFor(one.view);
                const bool chow = advice.kind == ActionKind::Claim && advice.claim == ClaimKind::Chow;
                EXPECT_EQ(card.title, chow ? Tr(strings, "hints.card.title.chow") : AdviceText(strings, one.view, advice)) << where << ": a chow by its kind, its tiles are pictures";
                EXPECT_LE(card.rows.size(), kMaxCardRows) << where;
                const bool claims = one.claimStep.has_value() && *one.claimStep == ClaimStep::Claim;
                EXPECT_TRUE(claims || !card.footer.empty()) << where << ": every kind but a claim that gains a step says why in words";
                EXPECT_LE(card.footer.size(), kMaxCardFooter) << where;
                EXPECT_EQ(card.now.empty(), card.rows.empty()) << where;
                const bool wins = String(one.name) == "win" || String(one.name) == "claimMahjong" || String(one.name) == "robbedKong";
                EXPECT_EQ(card.rows.empty(), wins) << where << ": nothing is compared with a win";
                if (!card.rows.empty()) {
                    EXPECT_EQ(card.rows[0].kind, CardRowKind::Best) << where;
                    EXPECT_FALSE(card.rows[0].who.empty()) << where;
                }
                const Vector<String> text = TextOf(card);
                for (const String& line : text) {
                    EXPECT_EQ(line.find('{'), String::npos) << where << ": " << line;
                    EXPECT_EQ(line.find('}'), String::npos) << where << ": " << line;
                    EXPECT_EQ(line.find("hints."), String::npos) << where << ": a key is missing: " << line;
                }
                const Vector<String> footer = FooterOf(card);
                EXPECT_EQ(Set<String>(footer.begin(), footer.end()).size(), footer.size()) << where << ": a line is said twice";
            }
        });
    }

    TEST(HintsTest, TheNumbersOfTheCardAreTheNumbersOfTheExplanation) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        const Vector<ReasonCase> cases = ReasonCases();
        const auto of = [&](const char* name) -> const TableView& {
            for (const ReasonCase& one : cases) {
                if (String(one.name) == name) {
                    return one.view;
                }
            }
            ADD_FAILURE() << name;
            return cases.front().view;
        };

        // A discard: the steps now over the rows, the advised tile with its steps and the unseen tiles that help, then the two best others.
        const TableView& discard = of("discard");
        const TurnExplanation turn = *ExplainAdviceFor(discard)->turn;
        const HintCard card = CardOf(strings, discard);
        EXPECT_EQ(card.now, Format("Steps to ready now: {}", StepsWord(turn.shantenBefore)));
        ASSERT_EQ(card.rows.size(), 3u);
        EXPECT_EQ(RowText(card.rows[0]), Format("Best|{} ||{}|{}", ToString(turn.chosen.tile), StepsWord(turn.chosen.shanten), turn.chosen.acceptance));
        ASSERT_EQ(turn.alternatives.size(), 2u);
        for (size_t at = 0; at < turn.alternatives.size(); ++at) {
            const TurnOption& option = turn.alternatives[at];
            EXPECT_EQ(card.rows[1 + at].kind, CardRowKind::Other);
            EXPECT_EQ(RowText(card.rows[1 + at]), Format("|{} ||{}|{}", ToString(option.tile), StepsWord(option.shanten), option.acceptance));
        }
        for (const CardRow& row : card.rows) {
            int steps = 0;
            if (row.steps != "ready") {
                const auto parsed = std::from_chars(row.steps.data(), row.steps.data() + row.steps.size(), steps);
                ASSERT_EQ(parsed.ec, std::errc()) << RowText(row);
            }
            const CardTrend expected = steps < turn.shantenBefore ? CardTrend::Closer : (steps == turn.shantenBefore ? CardTrend::Same : CardTrend::Farther);
            EXPECT_EQ(row.trend, expected) << RowText(row);
        }

        // A declaration: the hand is ready after it, with the unseen copies of 4p and 7p, 4 + 4; the other declaring discard is worse.
        const HintCard declare = CardOf(strings, of("declare"));
        ASSERT_GE(declare.rows.size(), 2u);
        const TurnExplanation declaring = *ExplainAdviceFor(of("declare"))->turn;
        EXPECT_EQ(RowText(declare.rows[0]), Format("Best|{} |declare|ready|8", ToString(declaring.chosen.tile)));
        EXPECT_EQ(RowText(declare.rows[1]), "|5p ||ready|4");
        EXPECT_EQ(FooterOf(declare), (Vector<String>{"No other tile rates as well."}));

        // A kong: the hand with it against the hand without it.
        const TurnExplanation kong = *ExplainAdviceFor(of("concealedKong"))->turn;
        const HintCard kongCard = CardOf(strings, of("concealedKong"));
        ASSERT_EQ(kongCard.rows.size(), 2u);
        EXPECT_EQ(RowText(kongCard.rows[0]), Format("Best|Ew |Kong|{}|{}", StepsWord(kong.kongShantenAfter), kong.kongAcceptanceAfter));
        EXPECT_EQ(RowText(kongCard.rows[1]), Format("||No kong|{}|{}", StepsWord(kong.shantenBefore), kong.kongAcceptanceBefore));
        EXPECT_EQ(FooterOf(kongCard), (Vector<String>{"The kong does not make the hand worse."}));

        // A claim: the pung of the pending tile with the numbers after it.
        const ClaimExplanation pung = *ExplainAdviceFor(of("claimPung"))->claim;
        const HintCard pungCard = CardOf(strings, of("claimPung"));
        EXPECT_EQ(pungCard.now, Format("Steps to ready now: {}", StepsWord(pung.shantenBefore)));
        EXPECT_EQ(RowText(pungCard.rows[0]), Format("Best|5p |Pung|{}|{}", StepsWord(pung.chosen->after.shanten), pung.chosen->after.acceptance));
        const HintCard chow = CardOf(strings, of("claimChow"));
        EXPECT_EQ(RowText(chow.rows[0]).substr(0, 19), "Best|4p 5p 6p |Chow");

        // A pass: a hand that waits stays ready with its 3 unseen winning copies; a pass that gains nothing keeps its steps.
        const HintCard waiting = CardOf(strings, of("passWaiting"));
        EXPECT_EQ(RowText(waiting.rows[0]), "Best||Pass|ready|3");
        EXPECT_EQ(FooterOf(waiting), (Vector<String>{"Your hand waits: a claim would break it."}));
        const ClaimExplanation noGain = *ExplainAdviceFor(of("passNoGain"))->claim;
        const HintCard noGainCard = CardOf(strings, of("passNoGain"));
        ASSERT_EQ(noGain.alternatives.size(), 1u);
        ASSERT_EQ(noGainCard.rows.size(), 2u);
        EXPECT_EQ(RowText(noGainCard.rows[0]), Format("Best||Pass|{}|", StepsWord(noGain.shantenBefore)));
        EXPECT_EQ(noGainCard.rows[0].trend, CardTrend::Same);
        EXPECT_EQ(RowText(noGainCard.rows[1]), Format("|5m |Pung|{}|{}", StepsWord(noGain.alternatives[0].after.shanten), noGain.alternatives[0].after.acceptance));
        EXPECT_EQ(FooterOf(noGainCard), (Vector<String>{"No claim brings the hand closer to ready."}));

        // The listed claims after the advised one are rows of their own.
        const ClaimExplanation withAlternatives = *ExplainAdviceFor(of("claimWithAlternatives"))->claim;
        const HintCard alternativeCard = CardOf(strings, of("claimWithAlternatives"));
        ASSERT_FALSE(withAlternatives.alternatives.empty());
        ASSERT_EQ(alternativeCard.rows.size(), 1 + withAlternatives.alternatives.size());
        for (size_t at = 0; at < withAlternatives.alternatives.size(); ++at) {
            EXPECT_EQ(alternativeCard.rows[1 + at].steps, StepsWord(withAlternatives.alternatives[at].after.shanten).c_str());
            EXPECT_EQ(alternativeCard.rows[1 + at].useful, BECore::ToString(withAlternatives.alternatives[at].after.acceptance));
        }

        EXPECT_EQ(Lines(strings, of("win")), (Vector<String>{"A complete hand: nothing beats a win."}));
        EXPECT_EQ(Lines(strings, of("claimMahjong")), (Vector<String>{"Take it: a win ends the hand."}));
        EXPECT_EQ(Lines(strings, of("robbedKong")), (Vector<String>{"Take it: a win ends the hand."}));
    }

    TEST(HintsTest, AnEqualTieIsSettledByTheKeepClassOrLeftToChance) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ASSERT_TRUE(strings.SetLanguage("en"_intern));

        // Ww and Nw are equally worthless with the round wind South: the others are equal and the choice is chance.
        const HandModel alike = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Ww Nw", "Ew"), kSeat0, Wind::South);
        const TableView alikeView = MakeTableView(alike, kSeat0);
        const TurnExplanation alikeTurn = *ExplainAdviceFor(alikeView)->turn;
        ASSERT_GT(alikeTurn.equalChoices, 0);
        const Vector<String> alikeLines = Lines(strings, alikeView);
        EXPECT_NE(std::find(alikeLines.begin(), alikeLines.end(), Format("Rated the same: {} more; any of them will do.", alikeTurn.equalChoices)), alikeLines.end());

        // The worthless north wind goes before the value tiles that rate the same: the keep class settles the tie.
        const TableView discard = ReasonCases().front().view;
        const TurnExplanation turn = *ExplainAdviceFor(discard)->turn;
        ASSERT_GT(turn.equalChoices, 0);
        ASSERT_EQ(turn.alternatives.front().keep > turn.chosen.keep, true);
        const Vector<String> lines = Lines(strings, discard);
        EXPECT_NE(std::find(lines.begin(), lines.end(), Format("Rated the same: {} more; drop a useless honour first.", turn.equalChoices)), lines.end());
    }

    TEST(HintsTest, TheTallyIsTheLastLineAndLeavesRoomForOneReason) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        const TableView view = WithDeclaredOpponent(ReasonCases().front().view);
        ASSERT_EQ(Lines(strings, view).size(), kMaxCardFooter) << "the danger and the tie";

        const HintCard card = CardOf(strings, view, "tally");

        ASSERT_EQ(card.footer.size(), kMaxCardFooter);
        EXPECT_EQ(card.footer[0], Lines(strings, view)[0]) << "the first reason stays";
        EXPECT_EQ(card.footer[1], "tally");
    }

    TEST(HintsTest, TheCardFollowsTheLanguage) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        const TableView view = ReasonCases().front().view;

        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        const HintCard english = CardOf(strings, view);
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        const HintCard russian = CardOf(strings, view);

        ASSERT_FALSE(english.rows.empty());
        EXPECT_EQ(english.rows.size(), russian.rows.size());
        EXPECT_EQ(english.now.find("Steps to ready"), 0u);
        EXPECT_EQ(russian.now.find("Шагов до готовности"), 0u);
        EXPECT_EQ(russian.rows[0].who, "Лучше");
    }

    TEST(HintsTest, TheCardDoesNotSeeTheOtherHands) {
        const auto make = [](bool other) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew";
            spec.hands[kSeat1] = other ? kQuiet3 : kQuiet1;
            spec.hands[kSeat2] = other ? kQuiet1 : kQuiet2;
            spec.hands[kSeat3] = other ? kQuiet2 : kQuiet3;
            spec.live = Format("Ew {}", (other ? "Gd Wd Nw" : "Nw Wd Gd"));
            return MakeModel(spec);
        };
        const HandModel a = make(false);
        const HandModel b = make(true);
        const TableView viewA = MakeTableView(a, kSeat0);
        const TableView viewB = MakeTableView(b, kSeat0);
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());

        ForEachLanguage(strings, [&](PoolString) {
            EXPECT_EQ(TextOf(CardOf(strings, viewA)), TextOf(CardOf(strings, viewB)));
            EXPECT_FALSE(CardOf(strings, viewA).rows.empty());
        });
    }

    // ----- the danger on the card -----

    TEST(HintsTest, ADiscardAdviceAgainstADeclaredOpponentSaysTheDangerOfTheAdvisedTileBeforeTheTie) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        const TableView view = WithDeclaredOpponent(ReasonCases().front().view);
        const TurnExplanation turn = *ExplainAdviceFor(view)->turn;
        ASSERT_EQ(turn.step, TurnStep::Discard);
        ASSERT_EQ(turn.declaredOpponents, 1);

        const Vector<String> lines = Lines(strings, view);

        ASSERT_EQ(lines.size(), kMaxCardFooter);
        EXPECT_EQ(lines[0], kDangerSentences[static_cast<size_t>(turn.chosen.danger)]);
        EXPECT_EQ(lines[1].find("Rated the same: "), 0u) << "the tie line follows the danger";
    }

    TEST(HintsTest, TheDangerLineIsThereExactlyForADiscardStepWithADeclaredOpponent) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        size_t withLine = 0;
        for (const ReasonCase& one : ReasonCases()) {
            EXPECT_FALSE(HasDangerLine(Lines(strings, one.view))) << one.name << ": nobody has declared";

            const Vector<String> lines = Lines(strings, WithDeclaredOpponent(one.view));
            const bool discards = one.turnStep.has_value() && (*one.turnStep == TurnStep::Discard || *one.turnStep == TurnStep::Declare || *one.turnStep == TurnStep::KeepWaiting);
            EXPECT_EQ(HasDangerLine(lines), discards) << one.name;
            EXPECT_LE(lines.size(), kMaxCardFooter) << one.name;
            withLine += HasDangerLine(lines) ? 1 : 0;
        }
        EXPECT_EQ(withLine, 3u) << "a discard, a declaration and the keeping of a wait";
    }

    TEST(HintsTest, TheDangerLineFollowsTheGradeOfTheAdvisedTile) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        TableView view = WithDeclaredOpponent(ReasonCases().front().view);
        const Tile advised = ExplainAdviceFor(view)->Action().tile;
        Set<String> said;

        for (const uint8_t copies : {static_cast<uint8_t>(0), kCopiesPerTile}) {
            view.visible[advised.Id()] = std::max(view.visible[advised.Id()], copies);
            const TurnExplanation turn = *ExplainAdviceFor(view)->turn;
            ASSERT_EQ(turn.chosen.tile, advised);
            const Vector<String> lines = Lines(strings, view);
            ASSERT_FALSE(lines.empty());
            EXPECT_EQ(lines[0], kDangerSentences[static_cast<size_t>(turn.chosen.danger)]);
            said.insert(lines[0]);
        }
        EXPECT_EQ(said.size(), 2u) << "an honour with every copy seen is safe, one with copies unseen is not";
        EXPECT_TRUE(said.contains(kDangerSentences[static_cast<size_t>(Danger::Safe)]));
    }

    TEST(HintsTest, TheTieLineNamesTheDangerOnlyWhenTheDangerDecided) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        const TableView view = TwoDeclaringDiscardsView();
        const auto tieLine = [&](const LevelProfile& profile) {
            const Explanation explained = *ExplainAdvice(view, profile);
            for (const String& line : FooterOf(MakeAdviceCard(strings, view, explained, {}))) {
                if (line.find("Rated the same: ") == 0) {
                    return line;
                }
            }
            return String();
        };
        LevelProfile careful = kNormalProfile;
        careful.readDanger = true;

        // The 5m is chosen by the keep value when the danger is not read; the green dragon, the safer, when it is
        const TurnExplanation plain = *ExplainAdvice(view)->turn;
        const TurnExplanation reading = *ExplainAdvice(view, careful)->turn;
        ASSERT_EQ(plain.equalChoices, 1);
        ASSERT_NE(plain.alternatives.front().danger, plain.chosen.danger) << "the two discards differ in danger";
        EXPECT_EQ(tieLine(careful), Format("Rated the same: 1 more; {}", kSafestTie));
        EXPECT_EQ(reading.chosen.tile, T("Gd"));
        EXPECT_EQ(tieLine(kNormalProfile).find(kSafestTie), String::npos) << "the advice player does not read the danger: it did not decide";
        EXPECT_NE(tieLine(kNormalProfile), "");

        // With nobody declared the knob has nothing to read
        TableView quiet = view;
        quiet.seats[kSeat1].callingDeclared = false;
        const Explanation explained = *ExplainAdvice(quiet, careful);
        for (const String& line : FooterOf(MakeAdviceCard(strings, quiet, explained, {}))) {
            EXPECT_EQ(line.find(kSafestTie), String::npos) << line;
            EXPECT_FALSE(HasDangerLine({line}));
        }
    }

    TEST(HintsTest, TheDangerSentencesAreInBothLanguagesAndNoLongerThanTheLongestLineUnderTheRows) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        const Vector<String> mine{"hints.why.danger.safe", "hints.why.danger.low", "hints.why.danger.medium", "hints.why.danger.high", "hints.why.tie.danger"};
        // The lines under the rows of the card share one label: a danger sentence is no longer than the longest of them (the tally of the hand)
        const Vector<String> others{"coach.card.tally.hand",   "hints.why.tie.none",    "hints.why.tie.keep",     "hints.why.tie.chance",  "hints.why.kong.noWorse", "hints.why.win",
                                    "hints.why.claim.mahjong", "hints.why.claim.value", "hints.why.pass.waiting", "hints.why.pass.noGain", "hints.why.pass.points",  "hints.why.kong.points"};
        ForEachLanguage(strings, [&](PoolString language) {
            size_t longest = 0;
            for (const String& key : others) {
                longest = std::max(longest, Letters(Tr(strings, key.c_str())));
            }
            for (const String& key : mine) {
                const String text = Tr(strings, key.c_str());
                EXPECT_NE(text, key) << language.ToStringView().data();
                EXPECT_LE(Letters(text), longest) << language.ToStringView().data() << " " << key;
            }
        });
    }

    TEST(HintsTest, TheDangerLinesFollowTheLanguage) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        const TableView view = WithDeclaredOpponent(ReasonCases().front().view);

        ASSERT_TRUE(strings.SetLanguage("en"_intern));
        const Vector<String> english = Lines(strings, view);
        ASSERT_TRUE(strings.SetLanguage("ru"_intern));
        const Vector<String> russian = Lines(strings, view);

        ASSERT_EQ(english.size(), kMaxCardFooter);
        EXPECT_EQ(russian.size(), kMaxCardFooter);
        EXPECT_NE(english[0], russian[0]);
        EXPECT_NE(russian[0].find("готов"), String::npos);
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
