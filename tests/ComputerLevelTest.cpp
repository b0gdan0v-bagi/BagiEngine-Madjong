#include "ComputerTestSupport.h"
#include "MatchTestSupport.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <gtest/gtest.h>

/**
 * `ComputerLevel` (the names, the parser, the default), the golden of the Normal player and the profiles of the levels. The golden is
 * the final scores of four whole matches, written down from the player as it was before levels existed, so that "Normal is the player
 * of `CLAUDE.md`" stays true for as long as the test stays green. The profile cases put one knob of `LevelProfile` at 100 percent on a
 * hand worked out by hand and look at the effect (the numbers of `kEasyProfile` are measured in `ComputerStrengthTest`); a knob at 0
 * draws nothing from the rng. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kFirstGoldenSeed = 700;
        constexpr int kGoldenMatchCount = 4;
        constexpr int kWholeHands = 6;

        /// The final scores of the 16-hand match (`SettingsOf(0)`, flowers off) of each seed 700..703 played by four computers, taken
        /// from the unmodified tree before the level was added.
        constexpr SeatScores kGoldenScores[kGoldenMatchCount] = {
            {2780, 2550, 1910, 760},
            {2090, 1420, 2600, 1890},
            {1480, 2560, 2060, 1900},
            {1780, 2130, 1670, 2420},
        };

        /// Every action of `kWholeHands` hands played by a table, in order.
        eastl::vector<HandAction> PlayActions(ComputerTable& table, uint32_t seed) {
            eastl::vector<HandAction> actions;
            Match match;
            EXPECT_TRUE(match.Start(SettingsOf(0), seed).has_value());
            for (int hand = 0; hand < kWholeHands && match.GetPhase() == MatchPhase::InHand; ++hand) {
                while (match.Hand().GetPhase() != Phase::Finished) {
                    const StepResult step = table.Step(match.Hand());
                    EXPECT_TRUE(step.has_value());
                    if (!step) {
                        return actions;
                    }
                    actions.push_back(*step);
                }
                EXPECT_TRUE(match.EndHand().has_value());
            }
            return actions;
        }

        /// The same actions as `PlayActions`, made by four players of `level` seeded `SeatSeed(seed, seat)` (what `ComputerTable` is).
        eastl::vector<HandAction> PlayActionsByPlayers(uint32_t seed, ComputerLevel level) {
            eastl::vector<ComputerPlayer> players;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                players.push_back(ComputerPlayer(SeatSeed(seed, seat), level));
            }
            eastl::vector<HandAction> actions;
            Match match;
            EXPECT_TRUE(match.Start(SettingsOf(0), seed).has_value());
            for (int hand = 0; hand < kWholeHands && match.GetPhase() == MatchPhase::InHand; ++hand) {
                while (match.Hand().GetPhase() != Phase::Finished) {
                    const Seat actor = ComputerTable::NextActor(match.Hand());
                    const std::optional<HandAction> action = players[actor].Decide(match.Hand(), actor);
                    EXPECT_TRUE(action.has_value());
                    if (!action || !match.Hand().Apply(actor, *action)) {
                        return actions;
                    }
                    actions.push_back(*action);
                }
                EXPECT_TRUE(match.EndHand().has_value());
            }
            return actions;
        }

        constexpr uint32_t kRngSeed = 1;
        constexpr uint32_t kRngSeeds = 8;
        constexpr int kRandomHands = 20;
        constexpr uint32_t kRandomFirstSeed = 700;

        /// A profile with one knob at 100 percent and the others as Normal's.
        LevelProfile Always(int LevelProfile::* knob) {
            LevelProfile profile = kNormalProfile;
            profile.*knob = kPercentBase;
            return profile;
        }

        HandAction TurnChoice(const HandModel& model, Seat seat, const LevelProfile& profile, uint32_t rngSeed = kRngSeed) {
            const TableView view = MakeTableView(model, seat);
            MahjongRng rng(rngSeed);
            return ChooseTurnAction(view, rng, profile);
        }

        HandAction ClaimChoice(const HandModel& model, Seat seat, const LevelProfile& profile, uint32_t rngSeed = kRngSeed) {
            const TableView view = MakeTableView(model, seat);
            MahjongRng rng(rngSeed);
            return ChooseClaimAction(view, rng, profile);
        }

        // The hands of the claim cases (those of `ComputerClaimTest`): East throws the only tile of `live`, seat 1 is under test.
        constexpr const char* kEast = "1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s";
        constexpr const char* kOther2 = "2p 5p 8p 2s 5s 8s 3m 6m 9m Gd Wd 9p 8m";
        constexpr const char* kOther3 = "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s";
        constexpr const char* kPartials = "3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw";

        HandModel WindowModel(const std::string& seat1, const char* discard) {
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

        // East waits on 4p and 7p with a pair of east winds once it has discarded: the 6p goes and the wait is declared.
        HandModel DeclarationModel() {
            return MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew"));
        }

        // East waits on the red dragon, declares with the 9p it draws and draws the red dragon two laps later.
        HandModel WinningModel() {
            HandModel model = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Rd"));
            Lap(model, "9p Wd Gd Nw", {kSeat0});
            return model;
        }

        // East draws the red dragon into 123s 456s 789m 5p 6p 9p Nw: shanten 1 whichever of 9p, Nw and Rd goes.
        HandModel DiscardModel() {
            return MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 9p Nw", "Rd"));
        }

    }  // namespace

    TEST(ComputerLevelTest, TheNamesRoundTripForEveryLevel) {
        EXPECT_EQ(kLevelCount, 3);  // a new level needs its name, its parse and a look at every table of levels
        for (const ComputerLevel level : EnumUtils<ComputerLevel>::Values()) {
            const std::string_view name = LevelName(level);
            EXPECT_FALSE(name.empty());
            EXPECT_EQ(TryParseLevel(name), level) << name;
        }
        EXPECT_EQ(LevelName(ComputerLevel::Easy), "easy");
        EXPECT_EQ(LevelName(ComputerLevel::Normal), "normal");
        EXPECT_EQ(LevelName(ComputerLevel::Hard), "hard");
    }

    TEST(ComputerLevelTest, ParsingTakesTheLowercaseNamesOnly) {
        for (const char* text : {"Easy", "EASY", "", "hard ", " hard", "medium", "Normal", "norma", "normall", "0", "easy\n"}) {
            EXPECT_FALSE(TryParseLevel(text).has_value()) << "'" << text << "'";
        }
    }

    TEST(ComputerLevelTest, TheDefaultIsNormal) {
        EXPECT_EQ(kDefaultLevel, ComputerLevel::Normal);
    }

    TEST(ComputerLevelTest, TheTableReportsItsLevelAndGivesEverySeatTheLevel) {
        for (const ComputerLevel level : EnumUtils<ComputerLevel>::Values()) {
            EXPECT_EQ(ComputerTable(5, level).Level(), level);
            EXPECT_EQ(ComputerPlayer(5, level).Level(), level);
        }
        // The seat seeds do not depend on the level, so a table of a level plays exactly what four players of that level seeded
        // `SeatSeed(seed, seat)` play: every seat has the level of the table (a seat at another level would play other actions). The
        // actions of the three levels differ.
        eastl::vector<eastl::vector<HandAction>> byLevel;
        for (const ComputerLevel level : EnumUtils<ComputerLevel>::Values()) {
            ComputerTable table(31, level);
            const eastl::vector<HandAction> played = PlayActions(table, 31);
            ASSERT_FALSE(played.empty());
            EXPECT_EQ(PlayActionsByPlayers(31, level), played) << LevelName(level);
            byLevel.push_back(played);
        }
        EXPECT_NE(byLevel[static_cast<size_t>(ComputerLevel::Easy)], byLevel[static_cast<size_t>(ComputerLevel::Normal)]);
    }

    TEST(ComputerLevelTest, TheProfilesAreNormalsAndEasys) {
        EXPECT_EQ(kNormalProfile.skipAcceptancePercent, 0);
        EXPECT_EQ(kNormalProfile.skipDeclarationPercent, 0);
        EXPECT_EQ(kNormalProfile.skipClaimPercent, 0);
        EXPECT_TRUE(kNormalProfile.takeValueClaimsAtEqualShanten);
        EXPECT_EQ(&ProfileOf(ComputerLevel::Normal), &kNormalProfile);
        EXPECT_EQ(&ProfileOf(ComputerLevel::Easy), &kEasyProfile);
        EXPECT_EQ(&ProfileOf(ComputerLevel::Hard), &kHardProfile);
        EXPECT_GT(kEasyProfile.skipAcceptancePercent, 0);
        EXPECT_GT(kEasyProfile.skipDeclarationPercent, 0);
        EXPECT_GT(kEasyProfile.skipClaimPercent, 0);
        EXPECT_FALSE(kEasyProfile.takeValueClaimsAtEqualShanten);
        for (const ComputerLevel level : EnumUtils<ComputerLevel>::Values()) {
            const LevelProfile& profile = ProfileOf(level);
            for (const int percent : {profile.skipAcceptancePercent, profile.skipDeclarationPercent, profile.skipClaimPercent}) {
                EXPECT_GE(percent, 0) << LevelName(level);
                EXPECT_LE(percent, kPercentBase) << LevelName(level);
            }
        }
    }

    TEST(ComputerLevelTest, ExplicitNormalPlaysLikeTheDefaultOnSixWholeHands) {
        for (uint32_t seed = 900; seed < 903; ++seed) {
            ComputerTable byDefault(seed);
            ComputerTable explicitNormal(seed, ComputerLevel::Normal);
            const eastl::vector<HandAction> expected = PlayActions(byDefault, seed);
            ASSERT_FALSE(expected.empty()) << "seed " << seed;
            EXPECT_EQ(PlayActions(explicitNormal, seed), expected) << "seed " << seed;
        }
    }

    // Hard is Normal's twin until the owner decides what Hard is (`ComputerLevel`, `kHardProfile`). These two cases are the ones that go
    // when Hard gets numbers of its own; the slow strength test does not play Hard again for the same reason.
    TEST(ComputerLevelTest, HardIsNormalsTwinUntilTheOwnerDecidesOtherwise) {
        const LevelProfile& normal = ProfileOf(ComputerLevel::Normal);
        const LevelProfile& hard = ProfileOf(ComputerLevel::Hard);
        EXPECT_EQ(hard.skipAcceptancePercent, normal.skipAcceptancePercent);
        EXPECT_EQ(hard.skipDeclarationPercent, normal.skipDeclarationPercent);
        EXPECT_EQ(hard.skipClaimPercent, normal.skipClaimPercent);
        EXPECT_EQ(hard.takeValueClaimsAtEqualShanten, normal.takeValueClaimsAtEqualShanten);
    }

    TEST(ComputerLevelTest, HardPlaysTheSameActionsAsNormalOnSixWholeHands) {
        for (uint32_t seed = 900; seed < 903; ++seed) {
            ComputerTable normal(seed, ComputerLevel::Normal);
            ComputerTable hard(seed, ComputerLevel::Hard);
            const eastl::vector<HandAction> expected = PlayActions(normal, seed);
            ASSERT_FALSE(expected.empty()) << "seed " << seed;
            EXPECT_EQ(PlayActions(hard, seed), expected) << "seed " << seed;
        }
    }

    TEST(ComputerLevelTest, NormalPlaysTheMatchesItPlayedBeforeLevelsExisted) {
        for (int index = 0; index < kGoldenMatchCount; ++index) {
            const uint32_t seed = kFirstGoldenSeed + static_cast<uint32_t>(index);
            Match match;
            ASSERT_TRUE(match.Start(SettingsOf(0), seed).has_value());
            ComputerTable table(seed, ComputerLevel::Normal);
            ASSERT_TRUE(table.PlayMatch(match).has_value());
            EXPECT_EQ(match.Scores(), kGoldenScores[index]) << "seed " << seed;
        }
    }

    // ----- the knobs, one at 100 percent -----

    TEST(ComputerLevelTest, ASkippedDeclarationKeepsThePlayerFromDeclaring) {
        const HandModel model = DeclarationModel();
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeDiscard(T("6p"), true)));
        const LevelProfile skipping = Always(&LevelProfile::skipDeclarationPercent);

        for (uint32_t seed = 1; seed <= kRngSeeds; ++seed) {
            EXPECT_EQ(TurnChoice(model, kSeat0, kNormalProfile, seed), HandAction::MakeDiscard(T("6p"), true)) << "rng seed " << seed;
            const HandAction skipped = TurnChoice(model, kSeat0, skipping, seed);
            EXPECT_EQ(skipped.kind, ActionKind::Discard) << "rng seed " << seed;
            EXPECT_FALSE(skipped.declareCalling) << "rng seed " << seed;
            EXPECT_TRUE(Has(view.legal, skipped)) << "rng seed " << seed;
        }
    }

    TEST(ComputerLevelTest, ASkippedClaimIsPassedAndTheClaimNormalTakesIsPassed) {
        // 5p 5p 123s 456s 789m Nw Wd: the pung of the 5p lowers the shanten from 1 to 0, Normal takes it.
        const HandModel model = WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        const LevelProfile skipping = Always(&LevelProfile::skipClaimPercent);

        for (uint32_t seed = 1; seed <= kRngSeeds; ++seed) {
            EXPECT_EQ(ClaimChoice(model, kSeat1, kNormalProfile, seed), HandAction::MakeClaim(ClaimKind::Pung)) << "rng seed " << seed;
            EXPECT_EQ(ClaimChoice(model, kSeat1, skipping, seed), HandAction::MakePass()) << "rng seed " << seed;
        }
    }

    TEST(ComputerLevelTest, AValuePungAtEqualShantenIsTakenByNormalAndPassedWhenTheKnobIsOff) {
        // Four partial sets, a pair of the red dragon and three single tiles: the pung leaves the shanten as it is, and a dragon scores
        // a double.
        const HandModel model = WindowModel(std::string(kPartials) + " Rd Rd", "Rd");
        LevelProfile picky = kNormalProfile;
        picky.takeValueClaimsAtEqualShanten = false;

        for (uint32_t seed = 1; seed <= kRngSeeds; ++seed) {
            EXPECT_EQ(ClaimChoice(model, kSeat1, kNormalProfile, seed), HandAction::MakeClaim(ClaimKind::Pung)) << "rng seed " << seed;
            EXPECT_EQ(ClaimChoice(model, kSeat1, picky, seed), HandAction::MakePass()) << "rng seed " << seed;
        }
        // The same knob does not stop a claim that lowers the shanten.
        const HandModel lowering = WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p");
        EXPECT_EQ(ClaimChoice(lowering, kSeat1, picky), HandAction::MakeClaim(ClaimKind::Pung));
    }

    TEST(ComputerLevelTest, WithoutTheAcceptanceTheDiscardGoesByTheKeepValue) {
        // 123s 456s 789m 5p 6p 9p Nw Rd: shanten 1 whichever of 9p, Nw and Rd is discarded. The red dragon is dead (all four copies
        // visible), so discarding it keeps the most improving copies (the 9p and the Nw can still be paired): Normal discards it. The
        // seat that does not look at the acceptance throws the worthless Nw (the lowest keep value) instead.
        const HandModel model = DiscardModel();
        TableView view = MakeTableView(model, kSeat0);
        view.visible[T("Rd").Id()] = kCopiesPerTile;
        const LevelProfile inattentive = Always(&LevelProfile::skipAcceptancePercent);

        for (uint32_t seed = 1; seed <= kRngSeeds; ++seed) {
            MahjongRng normal(seed);
            EXPECT_EQ(ChooseTurnAction(view, normal, kNormalProfile), HandAction::MakeDiscard(T("Rd"))) << "rng seed " << seed;
            MahjongRng skipping(seed);
            EXPECT_EQ(ChooseTurnAction(view, skipping, inattentive), HandAction::MakeDiscard(T("Nw"))) << "rng seed " << seed;
        }
    }

    TEST(ComputerLevelTest, ASkippedAcceptanceNeverGivesUpTheShanten) {
        // The hand of the case above: whatever is thrown, the shanten of the rest is the lowest (1); the 5p and the 6p would make it 2.
        const HandModel model = DiscardModel();
        const TableView view = MakeTableView(model, kSeat0);
        const LevelProfile inattentive = Always(&LevelProfile::skipAcceptancePercent);
        for (uint32_t seed = 1; seed <= kRngSeeds * 4; ++seed) {
            MahjongRng rng(seed);
            const HandAction action = ChooseTurnAction(view, rng, inattentive);
            ASSERT_TRUE(Has(view.legal, action));
            TileCounts counts = CountTiles(view.own.concealed);
            --counts[action.tile.Id()];
            EXPECT_EQ(Shanten(counts, 0), 1) << "rng seed " << seed << " discards " << ToString(action.tile);
        }
    }

    TEST(ComputerLevelTest, AWinAndAMahjongClaimAreNeverSkipped) {
        const LevelProfile everything = [] {
            LevelProfile profile;
            profile.skipAcceptancePercent = 100;
            profile.skipDeclarationPercent = 100;
            profile.skipClaimPercent = 100;
            profile.takeValueClaimsAtEqualShanten = false;
            return profile;
        }();

        const HandModel winning = WinningModel();
        ASSERT_TRUE(Has(MakeTableView(winning, kSeat0).legal, HandAction::MakeDeclareWin()));
        // Seat 1 waits on the red dragon and on 4p, has declared, and East throws the red dragon.
        FixedWallSpec spec;
        spec.hands[kSeat0] = kEast;
        spec.hands[kSeat1] = "Rd Rd 1s 2s 3s 4s 5s 6s 7m 8m 9m 4p 4p";
        spec.hands[kSeat2] = kOther2;
        spec.hands[kSeat3] = kOther3;
        spec.live = "9p Wd Gd Nw Rd";
        HandModel window = MakeModel(spec);
        Lap(window, "9p Wd Gd Nw", {kSeat1});
        ASSERT_TRUE(window.Discard(kSeat0, T("Rd")).has_value());
        ASSERT_TRUE(Has(MakeTableView(window, kSeat1).legal, HandAction::MakeClaim(ClaimKind::Mahjong)));

        for (uint32_t seed = 1; seed <= kRngSeeds; ++seed) {
            EXPECT_EQ(TurnChoice(winning, kSeat0, everything, seed), HandAction::MakeDeclareWin()) << "rng seed " << seed;
            EXPECT_EQ(ClaimChoice(window, kSeat1, everything, seed), HandAction::MakeClaim(ClaimKind::Mahjong)) << "rng seed " << seed;
        }
    }

    // ----- the rng -----

    TEST(ComputerLevelTest, ARollOfZeroPercentDrawsNothingAndOtherRollsDrawOneNumber) {
        MahjongRng drawn(7);
        MahjongRng fresh(7);
        EXPECT_FALSE(Roll(drawn, 0));
        EXPECT_FALSE(Roll(drawn, -5));
        EXPECT_EQ(drawn.Below(1000), fresh.Below(1000));
        EXPECT_TRUE(Roll(drawn, kPercentBase));
        const uint32_t rollOfTheFreshOne = fresh.Below(kPercentBase);  // the one number the roll of 100 percent drew
        EXPECT_LT(rollOfTheFreshOne, static_cast<uint32_t>(kPercentBase));
        EXPECT_EQ(drawn.Below(1000), fresh.Below(1000));
        for (uint32_t seed = 1; seed < 20; ++seed) {
            MahjongRng rng(seed);
            EXPECT_FALSE(Roll(rng, 0)) << seed;
            EXPECT_TRUE(Roll(rng, kPercentBase)) << seed;
        }
    }

    TEST(ComputerLevelTest, TheNormalProfileDrawsNoNumberWhereNothingIsTied) {
        // The declaration, the discard of the dead red dragon and the pung have one best answer: the rng is where it began.
        const auto untouched = [](MahjongRng& used) {
            MahjongRng fresh(kRngSeed);
            return used.Below(1000000) == fresh.Below(1000000);
        };
        {
            const TableView view = MakeTableView(DeclarationModel(), kSeat0);
            MahjongRng rng(kRngSeed);
            ASSERT_EQ(ChooseTurnAction(view, rng, kNormalProfile), HandAction::MakeDiscard(T("6p"), true));
            EXPECT_TRUE(untouched(rng));
        }
        {
            TableView view = MakeTableView(DiscardModel(), kSeat0);
            view.visible[T("Rd").Id()] = kCopiesPerTile;
            MahjongRng rng(kRngSeed);
            ASSERT_EQ(ChooseTurnAction(view, rng, kNormalProfile), HandAction::MakeDiscard(T("Rd")));
            EXPECT_TRUE(untouched(rng));
        }
        {
            const TableView view = MakeTableView(WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p"), kSeat1);
            MahjongRng rng(kRngSeed);
            ASSERT_EQ(ChooseClaimAction(view, rng, kNormalProfile), HandAction::MakeClaim(ClaimKind::Pung));
            EXPECT_TRUE(untouched(rng));
        }
        {
            // A profile that has a knob on does draw: the skipped claim moved the stream.
            const TableView view = MakeTableView(WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p"), kSeat1);
            MahjongRng rng(kRngSeed);
            ASSERT_EQ(ChooseClaimAction(view, rng, Always(&LevelProfile::skipClaimPercent)), HandAction::MakePass());
            EXPECT_FALSE(untouched(rng));
        }
    }

    // ----- every level: listed, repeatable, no peeking -----

    TEST(ComputerLevelTest, EveryChoiceOfEveryLevelIsListedAndRepeatableOnRandomHands) {
        for (const ComputerLevel level : EnumUtils<ComputerLevel>::Values()) {
            int turns = 0;
            int windows = 0;
            for (int index = 0; index < kRandomHands; ++index) {
                const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
                Match match;
                ASSERT_TRUE(match.Start(SettingsOf(index & 7), seed).has_value());
                HandModel& hand = match.Hand();
                int steps = 0;
                while (hand.GetPhase() != Phase::Finished && ++steps < 2000) {
                    const Seat actor = ComputerTable::NextActor(hand);
                    const TableView view = MakeTableView(hand, actor);
                    const uint32_t playerSeed = SeatSeed(seed, actor) + static_cast<uint32_t>(steps);
                    ComputerPlayer first(playerSeed, level);
                    ComputerPlayer second(playerSeed, level);
                    const HandAction chosen = first.Choose(view);
                    EXPECT_TRUE(Has(view.legal, chosen)) << LevelName(level) << " hand " << index << " step " << steps;
                    EXPECT_EQ(chosen, second.Choose(view)) << LevelName(level) << " hand " << index << " step " << steps;
                    (view.phase == Phase::Discard ? turns : windows) += 1;
                    ASSERT_TRUE(hand.Apply(actor, chosen).has_value()) << LevelName(level) << " hand " << index << " step " << steps;
                }
                EXPECT_EQ(hand.GetPhase(), Phase::Finished) << LevelName(level) << " hand " << index;
            }
            EXPECT_GT(turns, kRandomHands) << LevelName(level);
            EXPECT_GT(windows, 0) << LevelName(level);
        }
    }

    TEST(ComputerLevelTest, NoLevelSeesMoreThanItsViewShows) {
        // Two tables that differ only in the other seats' hands and in the live wall after the first draw give East the same view, and
        // so the same choice at every level and every rng seed.
        const auto build = [](const char* seat1, const char* seat2, const char* seat3, const char* live) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew";
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = seat2;
            spec.hands[kSeat3] = seat3;
            spec.live = live;
            return MakeModel(spec);
        };
        const HandModel a = build(kQuiet1, kQuiet2, kQuiet3, "Ew 1p 2p 3p");
        const HandModel b = build("4p 7p 4s 7s 4m 7m 1s 1m 9s 9m Rd Wd Gd", "2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd", "1p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd Wd", "Ew 9p 8p 7p");
        for (const ComputerLevel level : EnumUtils<ComputerLevel>::Values()) {
            for (uint32_t seed = 1; seed <= kRngSeeds; ++seed) {
                ComputerPlayer playerA(seed, level);
                ComputerPlayer playerB(seed, level);
                EXPECT_EQ(playerA.Choose(MakeTableView(a, kSeat0)), playerB.Choose(MakeTableView(b, kSeat0))) << LevelName(level) << " seed " << seed;
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
