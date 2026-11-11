#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"
#include "StringsTestSupport.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Coach.h>
#include <gtest/gtest.h>
#include <vector>

/**
 * The coach (`Table/Coach.h`): the judge of a person's move against the computer's choice, the words of the note and the ledger of the
 * note and the tally. Fixed hands worked out by hand, one case per kind of action and per kind of difference; the judge is held to the
 * advice (`AdviceFor`) and to the chooser's rating pieces, and to no peeking. Pure logic: no engine services (the words are the shipped file).
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr const char* kEast = "1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s";
        constexpr const char* kOther2 = "2p 5p 8p 2s 5s 8s 3m 6m 9m Gd Wd 9p 8m";
        constexpr const char* kOther3 = "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s";
        // 123s 456s 789m 5p 6p and a pair of east winds: waits on 4p and 7p.
        constexpr const char* kTwoSided = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p Ew Ew";

        constexpr size_t kLinesOfAComplete = 2;

        HandAction Discard(const char* tile, bool declare = false) {
            return HandAction::MakeDiscard(T(tile), declare);
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

        /// East holds 5p 5p and three single honours beside 123s 456s 789m, and draws Nw: discarding Nw, Rd or Ew leaves shanten 1 with the same
        /// acceptance (`ComputerExplainTest`); discarding a 5p leaves shanten 2.
        TableView PairedView() {
            return MakeTableView(MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 5p Rd Ew", "Nw")), kSeat0);
        }

        /// East draws a third east wind: discarding it leaves the two-sided wait (4p, 7p), discarding the 5p or the 6p a single wait.
        TableView DeclaringView() {
            return MakeTableView(MakeModel(SpecOf(kTwoSided, "Ew")), kSeat0);
        }

        /// East has declared the hand of `kTwoSided` and draws a 6p: discarding it keeps the wait.
        TableView KeepingView() {
            HandModel model = MakeModel(SpecOf(kTwoSided, "Gd Wd Gd Nw 6p"));
            Lap(model, "Gd Wd Gd Nw", {kSeat0});
            return MakeTableView(model, kSeat0);
        }

        TableView KongView() {
            return MakeTableView(MakeModel(SpecOf("Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd", "Ew")), kSeat0);
        }

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

        TableView WindowView(const char* seat1, const char* discard) {
            return MakeTableView(WindowModel(seat1, discard), kSeat1);
        }

        /// East throws the red dragon that completes seat 1, which has declared the calling hand.
        TableView MahjongView() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kEast;
            spec.hands[kSeat1] = "Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p";
            spec.hands[kSeat2] = kOther2;
            spec.hands[kSeat3] = kOther3;
            spec.live = "9p Wd Gd Nw Rd";
            HandModel model = MakeModel(spec);
            Lap(model, "9p Wd Gd Nw", {kSeat1});
            EXPECT_TRUE(model.Discard(kSeat0, T("Rd")).has_value());
            return MakeTableView(model, kSeat1);
        }

        /// Seat 1 holds 4p 6p 5p 5p beside 123s 789s: the 5p is a pung or the chow 4p 5p 6p.
        TableView PungOrChowView() {
            return WindowView("4p 6p 5p 5p 1s 2s 3s 7s 8s 9s Nw Nw Wd", "5p");
        }

        const HandAction kPung = HandAction::MakeClaim(ClaimKind::Pung);
        const HandAction kPass = HandAction::MakePass();

        HandAction ChowOf(const char* first) {
            return HandAction::MakeClaim(ClaimKind::Chow, T(first));
        }

        CoachNote NoteOf(const TableView& view, const HandAction& taken) {
            const std::optional<CoachNote> note = JudgeMove(view, taken);
            EXPECT_TRUE(note.has_value());
            return note.value_or(CoachNote{});
        }

        MoveContext ContextOf(uint64_t starts, int hands, size_t settled, int persons = 1) {
            return {starts, hands, settled, persons};
        }

        size_t Letters(const std::string& text) {
            return static_cast<size_t>(std::count_if(text.begin(), text.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
        }

        std::vector<std::string> LinesOf(const UIStrings& strings, const CoachNote& note) {
            std::vector<std::string> lines;
            for (const eastl::string& line : CoachNoteLines(strings, note)) {
                lines.push_back(Std(line));
            }
            return lines;
        }

    }  // namespace

    // ----- the mode -----

    TEST(CoachTest, TheModeIdsRoundTripAndAnythingElseIsRefused) {
        for (const HintsMode mode : EnumUtils<HintsMode>::Values()) {
            EXPECT_EQ(TryParseHintsMode(HintsModeText(mode)), mode);
        }
        EXPECT_EQ(HintsModeText(HintsMode::Off), "off");
        EXPECT_EQ(HintsModeText(HintsMode::Hints), "hints");
        EXPECT_EQ(HintsModeText(HintsMode::Coach), "coach");
        for (const char* text : {"", "Coach", "on", "1", "hints "}) {
            EXPECT_FALSE(TryParseHintsMode(text).has_value()) << text;
        }
        EXPECT_EQ(kDefaultHintsMode, HintsMode::Hints);
    }

    // ----- a discard -----

    TEST(CoachTest, ADiscardThatIsTheAdviceSaysNothing) {
        const TableView view = PairedView();
        const std::optional<HandAction> advice = AdviceFor(view);
        ASSERT_TRUE(advice.has_value());

        EXPECT_FALSE(JudgeMove(view, *advice).has_value());
    }

    TEST(CoachTest, ADiscardOfTheSameNumbersSaysNothing) {
        const TableView view = PairedView();

        // Nw, Rd and Ew all leave shanten 1 with the same acceptance.
        for (const char* tile : {"Nw", "Rd", "Ew"}) {
            EXPECT_FALSE(JudgeMove(view, Discard(tile)).has_value()) << tile;
        }
    }

    TEST(CoachTest, ADifferingDiscardCarriesTheNumbersOfBothSides) {
        const TableView view = PairedView();
        const TileCounts counts = CountTiles(view.own.concealed);

        const CoachNote note = NoteOf(view, Discard("5p"));

        EXPECT_EQ(note.seat, kSeat0);
        EXPECT_EQ(note.taken, Discard("5p"));
        EXPECT_EQ(note.advised, *AdviceFor(view));
        EXPECT_EQ(note.stepsBefore, Shanten(counts, 0));
        const eastl::vector<DiscardOption> rated = RateDiscards(counts, 0, view);
        const auto advisedAt = std::find_if(rated.begin(), rated.end(), [&](const DiscardOption& o) { return o.tile == note.advised.tile; });
        const auto taken = std::find_if(rated.begin(), rated.end(), [](const DiscardOption& o) { return o.tile == T("5p"); });
        ASSERT_TRUE(advisedAt != rated.end() && taken != rated.end());
        const DiscardOption advised = *advisedAt;
        EXPECT_EQ(note.advisedSide, (CoachSide{CoachNumbers::Rated, advised.shanten, advised.acceptance}));
        EXPECT_EQ(note.takenSide, (CoachSide{CoachNumbers::Rated, taken->shanten, taken->acceptance}));
        EXPECT_GT(taken->shanten, advised.shanten);
        EXPECT_FALSE(note.pending.IsValid());
    }

    TEST(CoachTest, AnActionThatIsNotListedSaysNothing) {
        const TableView view = PairedView();

        EXPECT_FALSE(CanJudge(view, Discard("9p")));
        EXPECT_FALSE(JudgeMove(view, Discard("9p")).has_value());
        EXPECT_FALSE(JudgeMove(view, kPass).has_value());
        EXPECT_FALSE(JudgeMove(TableView{}, Discard("5p")).has_value());
    }

    // ----- a declaring and a keeping turn -----

    TEST(CoachTest, ADeclaringDiscardWithFewerWaitingCopiesDiffers) {
        const TableView view = DeclaringView();
        ASSERT_EQ(*AdviceFor(view), Discard("Ew", true));

        const CoachNote note = NoteOf(view, Discard("5p", true));

        EXPECT_EQ(note.advisedSide.numbers, CoachNumbers::Waiting);
        EXPECT_EQ(note.takenSide.numbers, CoachNumbers::Waiting);
        EXPECT_GT(note.advisedSide.copies, note.takenSide.copies);
    }

    TEST(CoachTest, TheAdvisedTileNotDeclaredDiffersWithTheSameCopies) {
        const TableView view = DeclaringView();

        const CoachNote note = NoteOf(view, Discard("Ew", false));

        EXPECT_EQ(note.takenSide, note.advisedSide);
        const std::vector<std::string> lines = LinesOf(Words(), note);
        ASSERT_EQ(lines.size(), 3u);
        EXPECT_NE(lines[1].find("not declared"), std::string::npos) << lines[1];
    }

    TEST(CoachTest, ADeclaringDiscardOfEqualWaitingCopiesSaysNothing) {
        // Four sets and a lone 5m and green dragon: either discard declares a tanki wait of the same unseen copies.
        TableView view;
        view.seat = kSeat0;
        view.phase = Phase::Discard;
        view.currentSeat = kSeat0;
        view.own.concealed = ParseTiles("1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 5m Gd").value();
        view.visible = CountTiles(view.own.concealed);
        for (const char* tile : {"5m", "Gd"}) {
            view.legal.push_back(HandAction::MakeDiscard(T(tile), true));
        }
        const std::optional<HandAction> advice = AdviceFor(view);
        ASSERT_TRUE(advice.has_value());

        for (const char* tile : {"5m", "Gd"}) {
            EXPECT_FALSE(JudgeMove(view, Discard(tile, true)).has_value()) << tile;
        }
    }

    TEST(CoachTest, KeepingTheWaitIsJudgedByTheWaitingCopiesAndBreakingItDiffers) {
        const TableView view = KeepingView();
        const std::optional<HandAction> advice = AdviceFor(view);
        ASSERT_TRUE(advice.has_value());
        ASSERT_EQ(advice->tile, T("6p"));

        EXPECT_FALSE(JudgeMove(view, *advice).has_value());
        // The 5p keeps a poorer wait (the pair wait on 6p and Ew), the east wind breaks it.
        const CoachNote poorer = NoteOf(view, Discard("5p"));
        EXPECT_EQ(poorer.takenSide.numbers, CoachNumbers::Waiting);
        EXPECT_LT(poorer.takenSide.copies, poorer.advisedSide.copies);
        const CoachNote broken = NoteOf(view, Discard("Ew"));
        EXPECT_EQ(broken.advisedSide.numbers, CoachNumbers::Waiting);
        EXPECT_EQ(broken.takenSide.numbers, CoachNumbers::Rated);
    }

    // ----- a kong -----

    TEST(CoachTest, ThePersonsKongThatIsTheAdviceSaysNothingAndAnotherActionDiffers) {
        const TableView view = KongView();
        const HandAction kong = HandAction::MakeConcealedKong(T("Ew"));
        ASSERT_EQ(*AdviceFor(view), kong);

        EXPECT_FALSE(JudgeMove(view, kong).has_value());
        const CoachNote note = NoteOf(view, Discard("Gd"));
        EXPECT_EQ(note.advised, kong);
        EXPECT_EQ(note.advisedSide.numbers, CoachNumbers::Rated);
        const TileCounts counts = CountTiles(view.own.concealed);
        const DiscardRating after = RateKong(view, counts, kong);
        EXPECT_EQ(note.advisedSide, (CoachSide{CoachNumbers::Rated, after.shanten, after.acceptance}));
    }

    TEST(CoachTest, AKongWhereTheComputerWouldDeclareDiffersWithTheNumbersOfTheHandWithoutIt) {
        // Four 4p beside four sets and a red dragon: the kong is no worse, but the computer declares the ready hand first (step 2).
        const TableView view = MakeTableView(MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p 4p Rd", "4p")), kSeat0);
        const HandAction kong = HandAction::MakeConcealedKong(T("4p"));
        ASSERT_TRUE(CanJudge(view, kong));
        const std::optional<HandAction> advice = AdviceFor(view);
        ASSERT_TRUE(advice.has_value());
        ASSERT_NE(*advice, kong);
        ASSERT_TRUE(advice->declareCalling);

        const CoachNote note = NoteOf(view, kong);

        const DiscardRating after = RateKong(view, CountTiles(view.own.concealed), kong);
        EXPECT_EQ(note.takenSide, (CoachSide{CoachNumbers::Rated, after.shanten, after.acceptance}));
        EXPECT_EQ(note.advisedSide.numbers, CoachNumbers::Waiting);
    }

    TEST(CoachTest, AKongThatBreaksTheHandDiffersFromTheDiscardTheComputerWouldMake) {
        // The four 1p take the 2p 3p's partner away: without them the hand is worse than with the best discard.
        const TableView view = MakeTableView(MakeModel(SpecOf("1p 1p 1p 2p 3p 5s 6s 8s 7m 9m Ew Rd Wd", "1p")), kSeat0);
        const HandAction kong = HandAction::MakeConcealedKong(T("1p"));
        const TileCounts counts = CountTiles(view.own.concealed);
        ASSERT_FALSE(KongIsNoWorse(view, counts, kong, BestDiscardRating(counts, 0, view)));

        const CoachNote note = NoteOf(view, kong);

        EXPECT_EQ(note.advised.kind, ActionKind::Discard);
        const DiscardRating after = RateKong(view, counts, kong);
        EXPECT_EQ(note.takenSide, (CoachSide{CoachNumbers::Rated, after.shanten, after.acceptance}));
        EXPECT_EQ(note.advisedSide.numbers, CoachNumbers::Rated);
        EXPECT_LT(note.advisedSide.steps, note.takenSide.steps);
    }

    // ----- a claim window -----

    TEST(CoachTest, PassingWhereTheComputerWouldClaimDiffers) {
        const TableView view = WindowView("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        ASSERT_EQ(*AdviceFor(view), kPung);

        const CoachNote note = NoteOf(view, kPass);

        EXPECT_EQ(note.pending, T("5p"));
        EXPECT_EQ(note.advisedSide.numbers, CoachNumbers::Rated);
        EXPECT_EQ(note.advisedSide.steps, 0);
        EXPECT_EQ(note.takenSide.numbers, CoachNumbers::None);
        EXPECT_EQ(note.stepsBefore, 1);
        EXPECT_FALSE(note.handWaits);
        const std::vector<std::string> lines = LinesOf(Words(), note);
        ASSERT_EQ(lines.size(), 3u);
        EXPECT_NE(lines[0].find("pung of 5 Dots"), std::string::npos) << lines[0];
        EXPECT_NE(lines[1].find("let the discard go"), std::string::npos) << lines[1];
        EXPECT_NE(lines[2].find("pung of 5 Dots"), std::string::npos) << lines[2];
    }

    TEST(CoachTest, TheClaimThatIsTheAdviceAndAPassWhereTheComputerPassesSayNothing) {
        const TableView claim = WindowView("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        const TableView noGain = WindowView("3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw 5m 5m", "5m");
        ASSERT_EQ(*AdviceFor(noGain), kPass);

        EXPECT_FALSE(JudgeMove(claim, kPung).has_value());
        EXPECT_FALSE(JudgeMove(noGain, kPass).has_value());
    }

    TEST(CoachTest, ClaimingWhereTheComputerWouldPassDiffers) {
        const TableView noGain = WindowView("3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw 5m 5m", "5m");
        const HandAction pung = kPung;
        ASSERT_TRUE(CanJudge(noGain, pung));

        const CoachNote note = NoteOf(noGain, pung);

        EXPECT_EQ(note.advised, kPass);
        EXPECT_EQ(note.advisedSide.numbers, CoachNumbers::None);
        EXPECT_EQ(note.takenSide.numbers, CoachNumbers::Rated);
        EXPECT_FALSE(note.handWaits);
        const std::vector<std::string> lines = LinesOf(Words(), note);
        ASSERT_EQ(lines.size(), 3u);
        EXPECT_NE(lines[1].find("Your pung of 5 Characters"), std::string::npos) << lines[1];
        EXPECT_NE(lines[2].find("passes"), std::string::npos) << lines[2];
    }

    TEST(CoachTest, ClaimingWithAHandThatWaitsDiffersAndSaysTheHandWaited) {
        const TableView view = WindowView("Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p", "Rd");
        ASSERT_EQ(*AdviceFor(view), kPass);

        const CoachNote note = NoteOf(view, kPung);

        EXPECT_TRUE(note.handWaits);
        const std::vector<std::string> lines = LinesOf(Words(), note);
        ASSERT_EQ(lines.size(), 3u);
        EXPECT_NE(lines[2].find("was waiting"), std::string::npos) << lines[2];
    }

    TEST(CoachTest, TheOtherClaimIsJudgedByTheNumbersAfterIt) {
        const TableView view = PungOrChowView();
        const std::optional<HandAction> advice = AdviceFor(view);
        ASSERT_TRUE(advice.has_value());
        const HandAction other = *advice == kPung ? ChowOf("4p") : kPung;
        ASSERT_TRUE(CanJudge(view, other));
        const int before = Shanten(CountTiles(view.own.concealed), 0);
        eastl::vector<RatedClaim> rated = RateClaims(view, before, ProfileOf(kDefaultLevel));
        const auto find = [&](const HandAction& action) {
            for (const RatedClaim& option : rated) {
                if (option.action == action) {
                    return option.after;
                }
            }
            ADD_FAILURE() << "claim not rated";
            return DiscardRating{};
        };
        const DiscardRating advised = find(*advice);
        const DiscardRating taken = find(other);

        const std::optional<CoachNote> note = JudgeMove(view, other);

        EXPECT_EQ(note.has_value(), advised.shanten != taken.shanten || advised.acceptance != taken.acceptance);
        if (note.has_value()) {
            EXPECT_EQ(note->advisedSide, (CoachSide{CoachNumbers::Rated, advised.shanten, advised.acceptance}));
            EXPECT_EQ(note->takenSide, (CoachSide{CoachNumbers::Rated, taken.shanten, taken.acceptance}));
        }
    }

    TEST(CoachTest, AMissedWinDiffersFromAnyOtherActionAndSaysTheHandWasComplete) {
        const TableView view = MahjongView();
        ASSERT_EQ(*AdviceFor(view), HandAction::MakeClaim(ClaimKind::Mahjong));

        const CoachNote note = NoteOf(view, kPass);

        EXPECT_EQ(note.advisedSide.numbers, CoachNumbers::None);
        EXPECT_EQ(note.takenSide.numbers, CoachNumbers::None);
        const std::vector<std::string> lines = LinesOf(Words(), note);
        ASSERT_EQ(lines.size(), kLinesOfAComplete);
        EXPECT_NE(lines[1].find("complete"), std::string::npos) << lines[1];
        EXPECT_FALSE(JudgeMove(view, HandAction::MakeClaim(ClaimKind::Mahjong)).has_value());
    }

    // ----- no peeking -----

    TEST(CoachTest, TwoModelsThatDifferOnlyInWhatTheViewerCannotSeeGiveTheSameNote) {
        const auto turn = [](bool other) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew";
            spec.hands[kSeat1] = other ? kQuiet3 : kQuiet1;
            spec.hands[kSeat2] = other ? kQuiet1 : kQuiet2;
            spec.hands[kSeat3] = other ? kQuiet2 : kQuiet3;
            spec.live = std::string("Ew ") + (other ? "Gd Wd Nw" : "Nw Wd Gd");
            return MakeTableView(MakeModel(spec), kSeat0);
        };
        const TableView a = turn(false);
        const TableView b = turn(true);
        for (const HandAction& action : a.legal) {
            const std::optional<CoachNote> noteA = JudgeMove(a, action);
            const std::optional<CoachNote> noteB = JudgeMove(b, action);
            ASSERT_EQ(noteA.has_value(), noteB.has_value());
            if (noteA.has_value()) {
                EXPECT_EQ(*noteA, *noteB);
                EXPECT_EQ(LinesOf(Words(), *noteA), LinesOf(Words(), *noteB));
            }
        }

        const auto window = [](const char* east, const char* seat3) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = east;
            spec.hands[kSeat1] = "4p 6p 5p 5p 1s 2s 3s 7s 8s 9s Nw Nw Wd";
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = seat3;
            spec.live = "5p";
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            return MakeTableView(model, kSeat1);
        };
        const TableView c = window("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Rd Wd", kQuiet3);
        const TableView d = window("2p 8p 2s 5s 8s 2m 5m 8m Ww Gd Rd Wd 9p", "4p 7p 4s 7s 4m 7m 1s 1m 9s 9m Rd Wd Gd");
        for (const HandAction& action : c.legal) {
            const std::optional<CoachNote> noteC = JudgeMove(c, action);
            const std::optional<CoachNote> noteD = JudgeMove(d, action);
            ASSERT_EQ(noteC.has_value(), noteD.has_value());
            if (noteC.has_value()) {
                EXPECT_EQ(*noteC, *noteD);
            }
        }
    }

    // ----- the words -----

    TEST(CoachTest, TheNoteSaysWhatTheComputerWouldHaveDoneAndTheNumbersOfBothInBothLanguages) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        const CoachNote note = NoteOf(PairedView(), Discard("5p"));

        ForEachLanguage(strings, [&](PoolString language) {
            const std::vector<std::string> lines = LinesOf(strings, note);
            ASSERT_EQ(lines.size(), kMaxCoachLines) << language.ToStringView().data();
            for (const std::string& line : lines) {
                EXPECT_EQ(line.find("coach."), std::string::npos) << line;
                EXPECT_EQ(line.find('{'), std::string::npos) << line;
            }
            EXPECT_NE(lines[0], lines[1]);
        });
        const std::vector<std::string> english = LinesOf(strings, note);
        EXPECT_EQ(english[0], "Computer: " + Std(AdviceText(strings, PairedView(), note.advised)));
        EXPECT_EQ(english[1], "Your discard 5 Dots: steps 1 -> 2, helps 27");
        EXPECT_EQ(english[2], "Computer's discard North Wind: steps 1 -> 1, helps 8");
    }

    TEST(CoachTest, EveryLineOfEveryKindOfNoteFitsTheBudgetInBothLanguages) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        std::vector<CoachNote> notes;
        notes.push_back(NoteOf(PairedView(), Discard("5p")));
        notes.push_back(NoteOf(DeclaringView(), Discard("5p", true)));
        notes.push_back(NoteOf(DeclaringView(), Discard("Ew", false)));
        notes.push_back(NoteOf(KeepingView(), Discard("5p")));
        notes.push_back(NoteOf(KongView(), Discard("Gd")));
        notes.push_back(NoteOf(WindowView("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p"), kPass));
        notes.push_back(NoteOf(WindowView("3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw 5m 5m", "5m"), kPung));
        notes.push_back(NoteOf(WindowView("Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p", "Rd"), kPung));
        notes.push_back(NoteOf(MahjongView(), kPass));
        // The longest names: a chow of characters the computer would take.
        notes.push_back(NoteOf(WindowView("2m 3m 8m 8m 1s 2s 3s 7s 8s 9s Nw Nw Wd", "4m"), kPass));

        ForEachLanguage(strings, [&](PoolString language) {
            for (const CoachNote& note : notes) {
                const eastl::vector<eastl::string> lines = CoachNoteLines(strings, note);
                EXPECT_GE(lines.size(), kLinesOfAComplete);
                EXPECT_LE(lines.size(), kMaxCoachLines);
                for (const eastl::string& line : lines) {
                    EXPECT_FALSE(line.empty());
                    EXPECT_LE(Letters(Std(line)), kMaxCoachLineLetters) << language.ToStringView().data() << ": " << Std(line);
                    EXPECT_EQ(Std(line).find("coach."), std::string::npos) << Std(line);
                }
            }
        });
    }

    TEST(CoachTest, TheWordsFollowTheLanguage) {
        UIStrings strings;
        ASSERT_TRUE(LoadShippedStrings(strings).empty());
        const CoachNote note = NoteOf(PairedView(), Discard("5p"));
        std::vector<std::vector<std::string>> byLanguage;

        ForEachLanguage(strings, [&](PoolString) { byLanguage.push_back(LinesOf(strings, note)); });

        ASSERT_GE(byLanguage.size(), 2u);
        EXPECT_NE(byLanguage[0], byLanguage[1]);
    }

    // ----- the ledger -----

    namespace {

        /// A ledger that is enabled, with `PairedView()` ready to be judged.
        struct Ledger {
            CoachLedger ledger;
            TableView view = PairedView();

            Ledger() {
                ledger.SetEnabled(true);
            }
            void Move(const char* tile, const MoveContext& context) {
                ledger.OnPersonMove(view, Discard(tile), context);
            }
        };

    }  // namespace

    TEST(CoachTest, TheLedgerCountsAgreedAndDifferingMovesAndKeepsTheNoteOfTheLastDifferingOne) {
        Ledger table;

        table.Move("Nw", ContextOf(1, 0, 0));
        EXPECT_EQ(table.ledger.HandTally(), (CoachTally{1, 1}));
        EXPECT_FALSE(table.ledger.Note().has_value());
        const uint64_t quiet = table.ledger.Version();

        table.Move("5p", ContextOf(1, 0, 0));
        EXPECT_EQ(table.ledger.HandTally(), (CoachTally{1, 2}));
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{1, 2}));
        ASSERT_TRUE(table.ledger.Note().has_value());
        EXPECT_EQ(table.ledger.Note()->taken, Discard("5p"));
        EXPECT_GT(table.ledger.Version(), quiet);

        // The next move drops the note before it is judged; an agreeing one leaves none.
        const uint64_t withNote = table.ledger.Version();
        table.Move("Rd", ContextOf(1, 0, 0));
        EXPECT_FALSE(table.ledger.Note().has_value());
        EXPECT_GT(table.ledger.Version(), withNote);
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{2, 3}));
    }

    TEST(CoachTest, ANewHandStartsTheHandTallyOverAndANewMatchBoth) {
        Ledger table;
        table.Move("5p", ContextOf(1, 0, 0));
        table.Move("Nw", ContextOf(1, 0, 0));

        table.Move("Nw", ContextOf(1, 1, 1));
        EXPECT_EQ(table.ledger.HandTally(), (CoachTally{1, 1}));
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{2, 3}));

        // A replayed drawn hand: the same hands played, one more settled hand.
        table.Move("5p", ContextOf(1, 1, 2));
        EXPECT_EQ(table.ledger.HandTally(), (CoachTally{0, 1}));
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{2, 4}));

        table.Move("Nw", ContextOf(2, 0, 0));
        EXPECT_EQ(table.ledger.HandTally(), (CoachTally{1, 1}));
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{1, 1}));
    }

    TEST(CoachTest, TheTallyOfAHandNobodyMovedInIsEmptyNotTheLastOnes) {
        Ledger table;
        table.Move("Nw", ContextOf(3, 2, 2));

        EXPECT_EQ(table.ledger.HandTallyOf(HandKeyOf(ContextOf(3, 2, 2))), (CoachTally{1, 1}));
        EXPECT_EQ(table.ledger.HandTallyOf(HandKeyOf(ContextOf(3, 3, 3))), (CoachTally{}));
        EXPECT_EQ(table.ledger.MatchTallyOf(3), (CoachTally{1, 1}));
        EXPECT_EQ(table.ledger.MatchTallyOf(4), (CoachTally{}));
    }

    TEST(CoachTest, ADisabledLedgerCountsNothingAndKeepsNoNote) {
        Ledger table;
        table.ledger.SetEnabled(false);

        table.Move("5p", ContextOf(1, 0, 0));
        EXPECT_FALSE(table.ledger.Note().has_value());
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{}));
        EXPECT_FALSE(table.ledger.Enabled());

        // Turning it on starts no tally of the past, turning it off drops the note and the counts.
        table.ledger.SetEnabled(true);
        table.Move("5p", ContextOf(1, 0, 0));
        ASSERT_TRUE(table.ledger.Note().has_value());
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{0, 1}));
        const uint64_t version = table.ledger.Version();
        table.ledger.SetEnabled(false);
        EXPECT_FALSE(table.ledger.Note().has_value());
        EXPECT_GT(table.ledger.Version(), version);
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{}));
        table.ledger.SetEnabled(true);
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{}));
    }

    TEST(CoachTest, TwoPeopleAtTheTableCountNothing) {
        Ledger table;

        table.ledger.OnPersonMove(table.view, Discard("5p"), ContextOf(1, 0, 0, 2));
        table.ledger.OnPersonMove(table.view, Discard("Nw"), ContextOf(1, 0, 0, 0));

        EXPECT_FALSE(table.ledger.Note().has_value());
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{}));
    }

    TEST(CoachTest, ClearingTheNoteMovesTheVersionOnlyWhenThereWasOne) {
        Ledger table;
        table.Move("5p", ContextOf(1, 0, 0));
        const uint64_t withNote = table.ledger.Version();

        table.ledger.ClearNote();
        EXPECT_GT(table.ledger.Version(), withNote);
        EXPECT_FALSE(table.ledger.Note().has_value());
        const uint64_t cleared = table.ledger.Version();
        table.ledger.ClearNote();
        EXPECT_EQ(table.ledger.Version(), cleared);
        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{0, 1}));
    }

    TEST(CoachTest, AMoveThatIsNotListedIsNotCounted) {
        Ledger table;

        table.Move("9p", ContextOf(1, 0, 0));

        EXPECT_EQ(table.ledger.MatchTally(), (CoachTally{}));
        EXPECT_FALSE(table.ledger.Note().has_value());
    }

    TEST(CoachTest, ThroughASessionAPersonWhoFollowsTheAdviceAgreesEveryTimeAndOtherwiseGetsNotes) {
        const auto play = [](bool follow, CoachLedger& ledger) {
            Match match;
            TableSession session(match);
            session.SetMoveObserver(&ledger);
            EXPECT_TRUE(session.StartMatch(MahjongSettings{}, 17, kDefaultSeatSetup).has_value());
            int moves = 0;
            for (int step = 0; step < 400 && session.GetPhase() == SessionPhase::Playing; ++step) {
                if (!session.ActorIsPerson()) {
                    EXPECT_TRUE(session.StepComputer().has_value());
                    continue;
                }
                const TableView view = MakeTableView(match.Hand(), session.Actor());
                const HandAction action = follow ? *AdviceFor(view) : view.legal.back();
                EXPECT_TRUE(session.PerformPerson(action).has_value());
                ++moves;
            }
            return moves;
        };

        CoachLedger following;
        following.SetEnabled(true);
        const int moves = play(true, following);
        EXPECT_GT(moves, 0);
        EXPECT_EQ(following.MatchTally(), (CoachTally{moves, moves}));
        EXPECT_FALSE(following.Note().has_value());

        CoachLedger careless;
        careless.SetEnabled(true);
        const int carelessMoves = play(false, careless);
        EXPECT_EQ(careless.MatchTally().moves, carelessMoves);
        EXPECT_LT(careless.MatchTally().agreed, carelessMoves);
    }

}  // namespace BECore::Mahjong::Tests
