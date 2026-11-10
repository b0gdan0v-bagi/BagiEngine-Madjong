#include "ComputerTestSupport.h"
#include "MatchTestSupport.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ComputerSearch.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerWorld.h>
#include <Mahjong/Game/Shanten.h>
#include <gtest/gtest.h>

/**
 * The search chooser (Game/ComputerSearch.h) and the profile knobs it hangs on: with `searchWorlds` 0 nothing is drawn and the answer is
 * Normal's; with it on, one number is drawn per searched decision and none for the decisions that are not searched (a win, a mahjong
 * claim, the pass of a waiting hand, a window with only `Pass`, a single candidate); the answer is listed and repeatable, Normal's
 * choice is the first candidate, a huge margin always gives Normal's choice, nothing is seen that the view does not show, and a
 * search seat plays whole hands. The budgets are a few worlds and a few candidates, so a Debug run stays short. Pure logic: no engine
 * services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kRngSeed = 17;
        constexpr uint32_t kRngDraws = 3;
        constexpr uint32_t kRngRange = 1000000000;
        constexpr int kWorlds = 2;
        constexpr int kCandidates = 3;
        /// Far above any margin the arena accepts: no paired difference is that many tenths of its error.
        constexpr int kHugeMargin = 1000000;
        constexpr int kStatesPerPhase = 12;
        constexpr uint32_t kFirstStateSeed = 5000;
        constexpr int kStepLimit = 2000;
        constexpr uint32_t kWholeHandSeeds = 2;

        LevelProfile SearchProfile(int worlds = kWorlds, int candidates = kCandidates, int margin = 0) {
            LevelProfile profile = kNormalProfile;
            profile.searchWorlds = worlds;
            profile.searchCandidates = candidates;
            profile.searchMargin = margin;
            return profile;
        }

        /// The next numbers of a copy of `rng`, to tell whether two generators stand in the same place.
        std::string RngState(MahjongRng rng) {
            std::string text;
            for (uint32_t i = 0; i < kRngDraws; ++i) {
                text += std::to_string(rng.Below(kRngRange)) + " ";
            }
            return text;
        }

        /// The state of `rng` after the one draw a search decision makes.
        MahjongRng AfterOneDraw(MahjongRng rng) {
            [[maybe_unused]] const uint32_t drawn = rng.Below(kSearchBaseSeedRange);
            return rng;
        }

        // East draws the east wind into 123s 456s 789m 5p 6p 6p Ew: it can declare with the 5p or the 6p.
        HandModel TurnModel() {
            return MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew 1p 2p 3p"));
        }

        // The same table as `TurnModel` as East sees it, with every other hand and the live wall after the draw changed.
        HandModel TurnModelElsewhere() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew";
            spec.hands[kSeat1] = "4p 7p 4s 7s 4m 7m 1s 1m 9s 9m Rd Wd Gd";
            spec.hands[kSeat2] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            spec.hands[kSeat3] = "1p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd Wd";
            spec.live = "Ew 9p 8p 7p";
            return MakeModel(spec);
        }

        constexpr const char* kEast = "1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s";
        constexpr const char* kOther2 = "2p 5p 8p 2s 5s 8s 3m 6m 9m Gd Wd 9p 8m";
        constexpr const char* kOther3 = "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s";

        /// East throws `discard` (the first live tile) at seat 1, which holds `seat1`: the claim window.
        HandModel WindowModel(const std::string& seat1, const char* discard, const char* other2 = kOther2) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kEast;
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = other2;
            spec.hands[kSeat3] = kOther3;
            spec.live = discard;
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T(discard)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        // Seat 1 may pung the 5p (and does not wait): a window with Pass and a claim.
        constexpr const char* kPungHand = "5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd";

        /// The first decision of the wanted kind from `seed` on, after `skip` earlier ones, that a search would look at: a turn with no
        /// win, or a window with no mahjong claim, a hand that does not wait and at least two actions. Normal plays up to it.
        std::optional<TableView> FindDecision(uint32_t seed, bool turn, int skip) {
            Match match;
            EXPECT_TRUE(match.Start(SettingsOf(static_cast<int>(seed % 2)), seed).has_value());
            HandModel& hand = match.Hand();
            ComputerTable table(seed);
            for (int step = 0; hand.GetPhase() != Phase::Finished && step < kStepLimit; ++step) {
                const Seat actor = ComputerTable::NextActor(hand);
                const TableView view = MakeTableView(hand, actor);
                const bool wanted =
                    turn ? view.phase == Phase::Discard && FindAction(view, ActionKind::DeclareWin) == nullptr
                         : view.phase == Phase::Claim && FindMahjong(view) == nullptr && view.legal.size() > 1 && Shanten(CountTiles(view.own.concealed), MeldCountOf(view)) > kWaitingShanten;
                if (wanted && skip-- <= 0) {
                    return view;
                }
                if (!table.Step(hand).has_value()) {
                    break;
                }
            }
            return std::nullopt;
        }

        eastl::vector<TableView> FindDecisions(bool turn) {
            eastl::vector<TableView> views;
            for (int i = 0; i < kStatesPerPhase; ++i) {
                if (const std::optional<TableView> view = FindDecision(kFirstStateSeed + static_cast<uint32_t>(i), turn, i % 4)) {
                    views.push_back(*view);
                }
            }
            return views;
        }

        HandAction Normal(const TableView& view, MahjongRng rng) {
            return view.phase == Phase::Discard ? ChooseTurnAction(view, rng, kNormalProfile) : ChooseClaimAction(view, rng, kNormalProfile);
        }

        std::optional<SearchRun> Begin(const TableView& view, MahjongRng& rng, const LevelProfile& profile) {
            return view.phase == Phase::Discard ? BeginTurnSearch(view, rng, profile) : BeginClaimSearch(view, rng, profile);
        }

        std::optional<HandAction> AtOnce(const TableView& view, MahjongRng& rng, const LevelProfile& profile) {
            return view.phase == Phase::Discard ? SearchTurnAction(view, rng, profile) : SearchClaimAction(view, rng, profile);
        }

        /// Steps `run` one `Step()` at a time to its end, checking the counters on the way; returns the number of steps taken.
        int StepToEnd(SearchRun& run, const std::string& where) {
            int steps = 0;
            int done = run.StepsDone();
            while (!run.Done()) {
                run.Step();
                ++steps;
                EXPECT_GE(run.StepsDone(), done) << where;
                done = run.StepsDone();
                EXPECT_LE(steps, run.StepsTotal()) << where;
                if (steps > run.StepsTotal()) {
                    break;
                }
            }
            EXPECT_EQ(run.StepsDone(), run.StepsTotal()) << where;
            return steps;
        }

        HandAction Searching(const TableView& view, MahjongRng& rng, const LevelProfile& profile) {
            return view.phase == Phase::Discard ? ChooseTurnAction(view, rng, profile) : ChooseClaimAction(view, rng, profile);
        }

    }  // namespace

    // ----- the knobs are off in the levels, and off draws nothing -----

    TEST(ComputerSearchTest, TheSearchIsOffInEveryLevelAndNeutralByDefault) {
        for (const ComputerLevel level : kAllLevels) {
            EXPECT_EQ(ProfileOf(level).searchWorlds, 0) << LevelText(level);
        }
        EXPECT_EQ(kNormalProfile.searchWorlds, 0);
        EXPECT_EQ(kNormalProfile.searchCandidates, kSearchDefaultCandidates);
        EXPECT_EQ(kNormalProfile.searchMargin, 0);
        EXPECT_GE(kSearchDefaultCandidates, kSearchMinCandidates);
        // A player made from a profile reports Normal as its level (it is no level), and so does a table.
        EXPECT_EQ(ComputerPlayer(kRngSeed, SearchProfile()).Level(), ComputerLevel::Normal);
        EXPECT_EQ(ComputerTable(kRngSeed, SearchProfile()).LevelOf(kSeat0), ComputerLevel::Normal);
    }

    TEST(ComputerSearchTest, WithTheKnobOffNothingIsDrawnAndTheAnswerIsNormals) {
        const eastl::vector<TableView> views = {MakeTableView(TurnModel(), kSeat0), MakeTableView(WindowModel(kPungHand, "5p"), kSeat1)};
        for (const TableView& view : views) {
            MahjongRng rng(kRngSeed);
            EXPECT_FALSE(view.phase == Phase::Discard ? SearchTurnAction(view, rng, kNormalProfile).has_value() : SearchClaimAction(view, rng, kNormalProfile).has_value());
            EXPECT_EQ(RngState(rng), RngState(MahjongRng(kRngSeed)));
            // The chooser of a profile that only names the other knobs (no worlds) is Normal's, in answer and in rng state.
            LevelProfile idle = kNormalProfile;
            idle.searchCandidates = kCandidates;
            idle.searchMargin = kHugeMargin;
            MahjongRng a(kRngSeed);
            MahjongRng b(kRngSeed);
            EXPECT_EQ(Searching(view, a, idle), Searching(view, b, kNormalProfile));
            EXPECT_EQ(RngState(a), RngState(b));
        }
        // Fewer than two candidates are nothing to compare: not searched, nothing drawn.
        const TableView view = MakeTableView(TurnModel(), kSeat0);
        MahjongRng rng(kRngSeed);
        EXPECT_FALSE(SearchTurnAction(view, rng, SearchProfile(kWorlds, 1)).has_value());
        EXPECT_EQ(RngState(rng), RngState(MahjongRng(kRngSeed)));
    }

    // ----- one draw per searched decision, none for the others -----

    TEST(ComputerSearchTest, ASearchedDecisionDrawsExactlyOneNumber) {
        {
            const TableView view = MakeTableView(TurnModel(), kSeat0);
            ASSERT_GE(TurnCandidates(view, Normal(view, MahjongRng(kRngSeed)), kCandidates).size(), static_cast<size_t>(kSearchMinCandidates));
            MahjongRng rng(kRngSeed);
            const std::optional<HandAction> answer = SearchTurnAction(view, rng, SearchProfile());
            ASSERT_TRUE(answer.has_value());
            EXPECT_TRUE(Has(view.legal, *answer));
            EXPECT_EQ(RngState(rng), RngState(AfterOneDraw(MahjongRng(kRngSeed))));
        }
        {
            const TableView view = MakeTableView(WindowModel(kPungHand, "5p"), kSeat1);
            MahjongRng rng(kRngSeed);
            const std::optional<HandAction> answer = SearchClaimAction(view, rng, SearchProfile());
            ASSERT_TRUE(answer.has_value());
            EXPECT_TRUE(Has(view.legal, *answer));
            EXPECT_EQ(RngState(rng), RngState(AfterOneDraw(MahjongRng(kRngSeed))));
        }
    }

    TEST(ComputerSearchTest, WhenNoWorldCanBeSampledTheAnswerIsNormalsAfterTheOneDraw) {
        // A wall that is five tiles too long for the view: `SampleWorld` refuses it, so every world is skipped.
        TableView turn = MakeTableView(TurnModel(), kSeat0);
        turn.liveCount += 5;
        MahjongRng probe(kRngSeed);
        ASSERT_FALSE(SampleWorld(turn, probe).has_value());
        TableView window = MakeTableView(WindowModel(kPungHand, "5p"), kSeat1);
        window.liveCount += 5;
        ASSERT_FALSE(SampleWorld(window, probe).has_value());

        for (const TableView& view : {turn, window}) {
            MahjongRng rng(kRngSeed);
            const std::optional<HandAction> answer = view.phase == Phase::Discard ? SearchTurnAction(view, rng, SearchProfile()) : SearchClaimAction(view, rng, SearchProfile());
            ASSERT_TRUE(answer.has_value());
            EXPECT_EQ(*answer, Normal(view, MahjongRng(kRngSeed)));
            EXPECT_EQ(RngState(rng), RngState(AfterOneDraw(MahjongRng(kRngSeed))));
            // The chooser takes the same way out.
            MahjongRng chooser(kRngSeed);
            EXPECT_EQ(Searching(view, chooser, SearchProfile()), *answer);
        }
    }

    TEST(ComputerSearchTest, ADecisionThatIsNotSearchedDrawsNothingAndIsNormalsOwn) {
        const LevelProfile profile = SearchProfile();
        const auto expectUnsearched = [&](const TableView& view, const char* what) {
            MahjongRng rng(kRngSeed);
            const HandAction chosen = Searching(view, rng, profile);
            // On the stream Normal has, Normal's answer and Normal's draws (ties only), as if the search were not there.
            MahjongRng reference(kRngSeed);
            EXPECT_EQ(chosen, Searching(view, reference, kNormalProfile)) << what;
            EXPECT_EQ(RngState(rng), RngState(reference)) << what;
            EXPECT_TRUE(Has(view.legal, chosen)) << what;
        };

        // The win: East has declared and draws its winning tile.
        HandModel winning = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Rd"));
        Lap(winning, "9p Wd Gd Nw", {kSeat0});
        const TableView win = MakeTableView(winning, kSeat0);
        ASSERT_NE(FindAction(win, ActionKind::DeclareWin), nullptr);
        expectUnsearched(win, "declare win");
        {
            MahjongRng rng(kRngSeed);
            EXPECT_FALSE(SearchTurnAction(win, rng, profile).has_value());
            EXPECT_EQ(RngState(rng), RngState(MahjongRng(kRngSeed)));
        }

        // The mahjong claim of a robbed kong.
        HandModel robbing = UpgradeModel({kSeat2});
        ASSERT_TRUE(robbing.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(robbing.GetPhase(), Phase::RobKong);
        const TableView mahjong = MakeTableView(robbing, kSeat2);
        ASSERT_NE(FindMahjong(mahjong), nullptr);
        expectUnsearched(mahjong, "mahjong claim");
        {
            MahjongRng rng(kRngSeed);
            EXPECT_FALSE(SearchClaimAction(mahjong, rng, profile).has_value());
        }
        // A seat outside the window has nothing listed (every seat in a window has a claim besides `Pass`): nothing to search.
        const TableView outside = MakeTableView(robbing, kSeat3);
        ASSERT_TRUE(outside.legal.empty());
        {
            MahjongRng rng(kRngSeed);
            EXPECT_FALSE(SearchClaimAction(outside, rng, profile).has_value());
            EXPECT_EQ(RngState(rng), RngState(MahjongRng(kRngSeed)));
        }

        // The pass of a waiting hand: seat 1 waits on 4p and 7p and could chow the 4p.
        const TableView waiting = MakeTableView(WindowModel("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p Nw Nw", "4p"), kSeat1);
        ASSERT_GT(waiting.legal.size(), 1U);
        ASSERT_NE(FindAction(waiting, ActionKind::Claim), nullptr);
        expectUnsearched(waiting, "waiting hand");
        {
            MahjongRng rng(kRngSeed);
            EXPECT_EQ(ChooseClaimAction(waiting, rng, profile), HandAction::MakePass());
            EXPECT_EQ(RngState(rng), RngState(MahjongRng(kRngSeed)));
        }

        // The declaration with no other declaring discard (the isolated 9s goes, anything else breaks the wait): a single candidate.
        const TableView declaring = MakeTableView(MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p Ew Ew", "9s")), kSeat0);
        const HandAction declaration = Normal(declaring, MahjongRng(kRngSeed));
        ASSERT_TRUE(declaration.kind == ActionKind::Discard && declaration.declareCalling);
        EXPECT_EQ(TurnCandidates(declaring, declaration, kCandidates).size(), 1U);
        expectUnsearched(declaring, "declaration");
    }

    // ----- the candidates -----

    TEST(ComputerSearchTest, NormalsChoiceIsTheFirstCandidateAndEveryCandidateIsListed) {
        const auto check = [](const TableView& view, int maxCount, const std::string& where) {
            const HandAction baseline = Normal(view, MahjongRng(kRngSeed));
            const eastl::vector<HandAction> candidates = view.phase == Phase::Discard ? TurnCandidates(view, baseline, maxCount) : ClaimCandidates(view, baseline, maxCount);
            ASSERT_FALSE(candidates.empty()) << where;
            EXPECT_EQ(candidates.front(), baseline) << where;
            EXPECT_LE(static_cast<int>(candidates.size()), maxCount) << where;
            for (size_t i = 0; i < candidates.size(); ++i) {
                EXPECT_TRUE(Has(view.legal, candidates[i])) << where << " candidate " << i;
                for (size_t j = i + 1; j < candidates.size(); ++j) {
                    EXPECT_NE(candidates[i], candidates[j]) << where;
                }
            }
        };
        const eastl::vector<TableView> turns = FindDecisions(true);
        const eastl::vector<TableView> windows = FindDecisions(false);
        ASSERT_GE(turns.size(), static_cast<size_t>(kStatesPerPhase / 2));
        ASSERT_GE(windows.size(), static_cast<size_t>(kStatesPerPhase / 2));
        for (const int maxCount : {kSearchMinCandidates, kCandidates, kSearchMaxCandidates}) {
            for (size_t i = 0; i < turns.size(); ++i) {
                check(turns[i], maxCount, "turn " + std::to_string(i));
            }
            for (size_t i = 0; i < windows.size(); ++i) {
                check(windows[i], maxCount, "window " + std::to_string(i));
            }
        }
    }

    TEST(ComputerSearchTest, ThePlainTurnAndTheWindowOfferTheirAlternatives) {
        // East draws the red dragon into 123s 456s 789m 5p 6p 9p Nw: shanten 1 whichever of 9p, Nw and Rd goes; the others are alternatives.
        const TableView turn = MakeTableView(MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 9p Nw", "Rd")), kSeat0);
        const HandAction baseline = Normal(turn, MahjongRng(kRngSeed));
        const eastl::vector<HandAction> turnCandidates = TurnCandidates(turn, baseline, kSearchMaxCandidates);
        EXPECT_GE(turnCandidates.size(), 2U);
        for (const HandAction& candidate : turnCandidates) {
            EXPECT_EQ(candidate.kind, ActionKind::Discard);
            EXPECT_FALSE(candidate.declareCalling);
        }
        // East draws the east wind into 123s 456s 789m 5p 6p 6p Ew: Normal declares, the alternatives are the other declaring discards.
        const TableView ready = MakeTableView(TurnModel(), kSeat0);
        const HandAction declaration = Normal(ready, MahjongRng(kRngSeed));
        ASSERT_TRUE(declaration.kind == ActionKind::Discard && declaration.declareCalling);
        const eastl::vector<HandAction> declaring = TurnCandidates(ready, declaration, kSearchMaxCandidates);
        EXPECT_GE(declaring.size(), 2U);
        for (const HandAction& candidate : declaring) {
            EXPECT_EQ(candidate.kind, ActionKind::Discard);
            EXPECT_TRUE(candidate.declareCalling);
        }
        // A window with a pung on offer: Normal's pung, Pass.
        const TableView window = MakeTableView(WindowModel(kPungHand, "5p"), kSeat1);
        const HandAction claim = Normal(window, MahjongRng(kRngSeed));
        ASSERT_EQ(claim, HandAction::MakeClaim(ClaimKind::Pung));
        const eastl::vector<HandAction> claimCandidates = ClaimCandidates(window, claim, kSearchMaxCandidates);
        ASSERT_GE(claimCandidates.size(), 2U);
        EXPECT_EQ(claimCandidates[0], HandAction::MakeClaim(ClaimKind::Pung));
        EXPECT_EQ(claimCandidates[1], HandAction::MakePass());
        // A kong on offer is a candidate of the turn (the hand is Normal's kong test: four equal tiles).
        const TableView kong = MakeTableView(MakeModel(SpecOf("1p 1p 1p 1p 2s 3s 9s 1m 4m 7m Ew Sw Rd", "Wd")), kSeat0);
        const eastl::vector<HandAction> kongCandidates = TurnCandidates(kong, HandAction::MakeDiscard(T("Wd")), kSearchMaxCandidates);
        EXPECT_TRUE(Has(kongCandidates, HandAction::MakeConcealedKong(T("1p"))));
    }

    // ----- the answer: listed, repeatable, the margin -----

    TEST(ComputerSearchTest, TheAnswerIsListedRepeatableAndAHugeMarginKeepsNormalsChoice) {
        for (const bool turn : {true, false}) {
            const eastl::vector<TableView> views = FindDecisions(turn);
            ASSERT_GE(views.size(), static_cast<size_t>(kStatesPerPhase / 2)) << (turn ? "turns" : "windows");
            int overridden = 0;
            for (size_t i = 0; i < views.size(); ++i) {
                const TableView& view = views[i];
                const std::string where = std::string(turn ? "turn " : "window ") + std::to_string(i);
                MahjongRng first(kRngSeed + static_cast<uint32_t>(i));
                MahjongRng second(kRngSeed + static_cast<uint32_t>(i));
                const HandAction answer = Searching(view, first, SearchProfile());
                EXPECT_TRUE(Has(view.legal, answer)) << where;
                EXPECT_EQ(answer, Searching(view, second, SearchProfile())) << where;
                EXPECT_EQ(RngState(first), RngState(second)) << where;
                overridden += answer != Normal(view, MahjongRng(kRngSeed + static_cast<uint32_t>(i))) ? 1 : 0;

                // A margin no difference can clear: Normal's choice, whatever the worlds said.
                MahjongRng huge(kRngSeed + static_cast<uint32_t>(i));
                EXPECT_EQ(Searching(view, huge, SearchProfile(kWorlds, kCandidates, kHugeMargin)), Normal(view, MahjongRng(kRngSeed + static_cast<uint32_t>(i)))) << where;
            }
            // The search can leave Normal's choice: with no margin it does somewhere (a fact of these seeds, not a promise of the rule).
            EXPECT_GT(overridden, 0) << (turn ? "turns" : "windows");
        }
    }

    // ----- the resumable run -----

    TEST(ComputerSearchTest, ASteppedRunGivesTheAtOnceAnswerAndLeavesTheRngWhereTheAtOnceCallDoes) {
        constexpr size_t kViewsPerPhase = 2;
        struct Knobs {
            int worlds;
            int margin;
        };
        for (const bool turn : {true, false}) {
            const eastl::vector<TableView> views = FindDecisions(turn);
            ASSERT_GE(views.size(), kViewsPerPhase);
            for (size_t i = 0; i < kViewsPerPhase; ++i) {
                for (const Knobs knobs : {Knobs{1, 0}, Knobs{2, 30}, Knobs{5, 0}, Knobs{5, 30}}) {
                    const LevelProfile profile = SearchProfile(knobs.worlds, kCandidates, knobs.margin);
                    const std::string where = std::string(turn ? "turn " : "window ") + std::to_string(i) + " worlds " + std::to_string(knobs.worlds) + " margin " + std::to_string(knobs.margin);
                    MahjongRng atOnce(kRngSeed + static_cast<uint32_t>(i));
                    MahjongRng begun(kRngSeed + static_cast<uint32_t>(i));
                    const std::optional<HandAction> expected = AtOnce(views[i], atOnce, profile);
                    std::optional<SearchRun> run = Begin(views[i], begun, profile);
                    ASSERT_EQ(run.has_value(), expected.has_value()) << where;
                    ASSERT_TRUE(run.has_value()) << where;
                    // The one draw is made by the factory, before any step.
                    EXPECT_EQ(RngState(begun), RngState(atOnce)) << where;
                    EXPECT_FALSE(run->Done()) << where;
                    EXPECT_EQ(run->StepsDone(), 0) << where;
                    EXPECT_GE(run->StepsTotal(), knobs.worlds * kSearchMinCandidates) << where;
                    EXPECT_LE(run->StepsTotal(), knobs.worlds * kCandidates) << where;
                    StepToEnd(*run, where);
                    EXPECT_EQ(run->Result(), *expected) << where;
                    EXPECT_EQ(RngState(begun), RngState(atOnce)) << where;
                }
            }
        }
    }

    TEST(ComputerSearchTest, AStepAfterTheEndChangesNothingAndACopyOfAHalfSteppedRunFinishesAlike) {
        for (const bool turn : {true, false}) {
            const eastl::vector<TableView> views = FindDecisions(turn);
            ASSERT_FALSE(views.empty());
            const TableView& view = views.front();
            const LevelProfile profile = SearchProfile(3, kCandidates);
            MahjongRng rng(kRngSeed);
            std::optional<SearchRun> run = Begin(view, rng, profile);
            ASSERT_TRUE(run.has_value());
            const int half = run->StepsTotal() / 2;
            for (int i = 0; i < half; ++i) {
                run->Step();
            }
            ASSERT_FALSE(run->Done());
            SearchRun copy = *run;
            EXPECT_EQ(copy.StepsDone(), run->StepsDone());
            StepToEnd(*run, "original");
            // The copy was not moved by the original's steps, and finishes with the same answer.
            EXPECT_EQ(copy.StepsDone(), half);
            StepToEnd(copy, "copy");
            EXPECT_EQ(copy.Result(), run->Result());

            const HandAction answer = run->Result();
            const int done = run->StepsDone();
            run->Step();
            run->Step();
            EXPECT_TRUE(run->Done());
            EXPECT_EQ(run->StepsDone(), done);
            EXPECT_EQ(run->Result(), answer);
        }
    }

    TEST(ComputerSearchTest, TheFactoriesAnswerNothingAndDrawNothingWhereTheAtOnceCallsDid) {
        const LevelProfile profile = SearchProfile();
        const auto expectNone = [&](const TableView& view, const LevelProfile& used, const char* what) {
            MahjongRng rng(kRngSeed);
            EXPECT_FALSE(Begin(view, rng, used).has_value()) << what;
            EXPECT_EQ(RngState(rng), RngState(MahjongRng(kRngSeed))) << what;
            MahjongRng reference(kRngSeed);
            EXPECT_FALSE(AtOnce(view, reference, used).has_value()) << what;
        };

        HandModel winning = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Rd"));
        Lap(winning, "9p Wd Gd Nw", {kSeat0});
        expectNone(MakeTableView(winning, kSeat0), profile, "declare win");

        HandModel robbing = UpgradeModel({kSeat2});
        ASSERT_TRUE(robbing.UpgradeKong(kSeat1, T("5p")).has_value());
        expectNone(MakeTableView(robbing, kSeat2), profile, "mahjong claim");
        expectNone(MakeTableView(robbing, kSeat3), profile, "no listed action");
        expectNone(MakeTableView(MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p Ew Ew", "9s")), kSeat0), profile, "single candidate");

        // The knob off, and fewer than two candidates asked for.
        const TableView turn = MakeTableView(TurnModel(), kSeat0);
        const TableView window = MakeTableView(WindowModel(kPungHand, "5p"), kSeat1);
        for (const TableView* view : {&turn, &window}) {
            expectNone(*view, kNormalProfile, "knob off");
            expectNone(*view, SearchProfile(kWorlds, 1), "one candidate asked for");
            expectNone(*view, SearchProfile(0, kCandidates), "no worlds");
        }
    }

    // ----- no peeking -----

    TEST(ComputerSearchTest, TheSearchSeesNoMoreThanTheViewShows) {
        // Two tables that differ only in the other seats' hands and the live wall after the draw give East the same view; with the search
        // on the answer and the rng state are the same too.
        const HandModel a = TurnModel();
        const HandModel b = TurnModelElsewhere();
        const TableView viewA = MakeTableView(a, kSeat0);
        const TableView viewB = MakeTableView(b, kSeat0);
        ASSERT_EQ(viewA.legal, viewB.legal);
        ASSERT_EQ(viewA.visible, viewB.visible);
        ASSERT_EQ(viewA.liveCount, viewB.liveCount);
        for (uint32_t seed = 1; seed <= 3; ++seed) {
            MahjongRng rngA(seed);
            MahjongRng rngB(seed);
            EXPECT_EQ(ChooseTurnAction(viewA, rngA, SearchProfile()), ChooseTurnAction(viewB, rngB, SearchProfile())) << "rng seed " << seed;
            EXPECT_EQ(RngState(rngA), RngState(rngB)) << "rng seed " << seed;
        }

        // The same for a window: seat 1 may pung, the others hold other tiles.
        const HandModel windowA = WindowModel(kPungHand, "5p");
        const HandModel windowB = WindowModel(kPungHand, "5p", "4p 7p 4s 7s 4m 7m Ew Ew Ew Gd Gd Rd Rd");
        const TableView claimA = MakeTableView(windowA, kSeat1);
        const TableView claimB = MakeTableView(windowB, kSeat1);
        ASSERT_EQ(claimA.legal, claimB.legal);
        ASSERT_EQ(claimA.visible, claimB.visible);
        for (uint32_t seed = 1; seed <= 3; ++seed) {
            MahjongRng rngA(seed);
            MahjongRng rngB(seed);
            EXPECT_EQ(ChooseClaimAction(claimA, rngA, SearchProfile()), ChooseClaimAction(claimB, rngB, SearchProfile())) << "rng seed " << seed;
            EXPECT_EQ(RngState(rngA), RngState(rngB)) << "rng seed " << seed;
        }
    }

    // ----- whole hands -----

    TEST(ComputerSearchTest, ASearchTableAndASearchPlayerPlayWholeHandsLegally) {
        for (uint32_t seed = 1; seed <= kWholeHandSeeds; ++seed) {
            for (const bool flowers : {false, true}) {
                MahjongSettings settings;
                settings.flowers = flowers;
                Match match;
                ASSERT_TRUE(match.Start(settings, seed).has_value());
                // One search seat (seat 0) against three Normal seats, each with its own player.
                ComputerPlayer searcher(SeatSeed(seed, kSeat0), SearchProfile(1, kCandidates));
                eastl::array<ComputerPlayer, kSeatCount> players = {searcher, ComputerPlayer(SeatSeed(seed, kSeat1)), ComputerPlayer(SeatSeed(seed, kSeat2)), ComputerPlayer(SeatSeed(seed, kSeat3))};
                HandModel& hand = match.Hand();
                int steps = 0;
                while (hand.GetPhase() != Phase::Finished && ++steps < kStepLimit) {
                    const Seat actor = ComputerTable::NextActor(hand);
                    const std::optional<HandAction> action = players[actor].Decide(hand, actor);
                    ASSERT_TRUE(action.has_value()) << "seed " << seed << " flowers " << flowers << " step " << steps;
                    ASSERT_TRUE(hand.Apply(actor, *action).has_value()) << "seed " << seed << " flowers " << flowers << " step " << steps;
                }
                EXPECT_EQ(hand.GetPhase(), Phase::Finished) << "seed " << seed << " flowers " << flowers;
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
