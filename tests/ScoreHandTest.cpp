#include "ScoreTestSupport.h"

#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/ScoreHand.h>

/**
 * `SettleHand` end to end: hands played on fixed walls to a self-draw, a win on a discard, a robbed kong and a drawn hand, then
 * scored and settled; the visible copies of the winning tile. Pure logic: no engine services. The expected numbers are worked out
 * by hand from the table of the rules (in `ScoreTestSupport.h`) and written beside each hand.
 *
 * The walls are built so that a discard opens a window only where a test means it to: the lap discards honours that no other seat
 * holds twice, and the other seats hold no tiles that make a chow with a discard.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // ----- a self-draw: South (seat 1) wins the East round -----

        // South waits on 1p and 4p with 2p 3p, three red dragons, three 6s, 7m 8m 9m and a pair of South winds; it declares on its first
        // discard (Nw) and draws the 4p.
        constexpr const char* kSelfDrawLap = "Ew Nw Ww Gd Wd";
        constexpr const char* kSelfDrawWinningTile = "4p";

        // Self-draw 36 points = 20 + wall tile 2 + pung of 6s concealed 4 + pung of red dragons concealed 8 + pair of own wind 2;
        // 3 doubles = dragons, fully concealed, calling hand in the first hand. 36 x 8 = 288, rounded up to 290.
        constexpr int kSelfDrawPoints = kWinning + kWallTile + kSimplePungConcealed + kHonourPungConcealed + kScoringPair;
        constexpr int kSelfDrawDoubles = 3;
        constexpr int kSelfDrawScore = 290;
        constexpr int kSelfDrawRaw = 288;

        // A seat with a concealed pung of 3s scores 4 points, rounded up to 10; nobody else here scores.
        constexpr int kConcealedPungScore = 10;
        // South's hand in a drawn hand: pung of 6s 4 + pung of red dragons 8 + pair of own wind 2 = 14, one double (dragons) = 28,
        // rounded up to 30.
        constexpr int kStandingHandScore = 30;

        // Seat 0 is East; the live wall: East 9m, South 2p, West 6p, North 7p, East 9s, South 4p.
        FixedWallSpec SelfDrawSpec(const char* live = "9m 2p 6p 7p 9s 4p") {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Ew Wd Gd 5p";
            spec.hands[kSeat1] = "3p 6s 6s 6s 7m 8m 9m Rd Rd Rd Sw Sw Nw";
            spec.hands[kSeat2] = "2p 5p 8p 2s 5s 8s 2m 5m 8m 3s 3s 3s Ww";
            spec.hands[kSeat3] = "3p 6p 9p 3m 6m 9m 1s 4s 7s Gd Nw Wd Ew";
            spec.live = live;
            return spec;
        }

        HandModel PlayToSelfDrawWin() {
            HandModel model = MakeModel(SelfDrawSpec());
            Lap(model, kSelfDrawLap, {kSeat1});
            EXPECT_EQ(model.CurrentSeat(), kSeat1);
            EXPECT_TRUE(model.DeclareWin(kSeat1).has_value());
            return model;
        }

        // ----- a win on a discard: West (seat 2) wins the South round on the fourth 5p -----

        // West waits on 5p only (4p 6p) with a pair of South winds. The 5p goes round: East, South (West may chow and passes), West
        // declares on its first discard (Nw), North (West may win and passes), then East discards the last 5p and West claims it.
        constexpr const char* kFifth = "5p";

        // Discard win 24 points = 20 + only winning tile 2 + pair of the round wind 2; 2 doubles = obviously last (three 5p lie in
        // the pile), calling hand in the first hand. 24 x 4 = 96, rounded up to 100.
        constexpr int kDiscardWinPoints = kWinning + kOnlyTile + kScoringPair;
        constexpr int kDiscardWinDoubles = 2;
        constexpr int kDiscardWinScore = 100;
        constexpr int kVisibleFifths = 3;
        // East holds a pung of West winds: 8 points, rounded up to 10.
        constexpr int kEastPungScore = 10;

        FixedWallSpec DiscardWinSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "5p 5p Ww Ww Ww 1s 4s 7s 1m 4m 7m 1p 8p";
            spec.hands[kSeat1] = "5p 9p 1s 9s 5s 8s 3m 6m 9m Ew Gd Wd Nw";
            spec.hands[kSeat2] = "4p 6p 1s 2s 3s 7s 8s 2m 3m 4m Sw Sw Nw";
            spec.hands[kSeat3] = "5p 3s 6s 9s 2m 5m 8m 1p 4s 7s Ew Gd Rd";
            spec.live = "8m 2p 9s 6p 2p";
            return spec;
        }

        HandModel PlayToDiscardWin() {
            HandModel model = MakeModel(DiscardWinSpec(), kSeat0, Wind::South);
            EXPECT_TRUE(model.Discard(kSeat0, T(kFifth)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Discard);
            EXPECT_TRUE(model.Discard(kSeat1, T(kFifth)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            EXPECT_TRUE(model.Pass(kSeat2).has_value());
            EXPECT_TRUE(model.Discard(kSeat2, T("Nw"), true).has_value());
            EXPECT_TRUE(model.Discard(kSeat3, T(kFifth)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            EXPECT_TRUE(model.Pass(kSeat2).has_value());
            EXPECT_TRUE(model.Discard(kSeat0, T(kFifth)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            EXPECT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());
            return model;
        }

        // ----- a robbed kong: West (seat 2) robs South's (seat 1) fourth 5p -----

        // South claims East's 5p as a pung and draws the fourth; West waits on 5p (4p 6p) and has declared in the lap.
        // Robbed kong 24 points = 20 + only winning tile 2 + pair of the round wind 2; 3 doubles = robbed kong, obviously last (South's
        // open pung of three 5p), calling hand in the first hand. 24 x 8 = 192, rounded up to 200.
        constexpr int kRobbedPoints = kWinning + kOnlyTile + kScoringPair;
        constexpr int kRobbedDoubles = 3;
        constexpr int kRobbedScore = 200;
        // South's open pung of 5p: 2 points, rounded up to 10.
        constexpr int kOpenPungScore = 10;

        FixedWallSpec RobbedKongSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = "5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            spec.hands[kSeat2] = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Ew Ew";
            spec.hands[kSeat3] = "4p 6p 1m 2m 3m 4s 5s 6s 7m 8m 9m Ww Ww";
            spec.live = "5p Rd Gd Wd 5p";
            spec.loose = "9m 9s";
            return spec;
        }

        HandModel PlayToRobbedKong() {
            HandModel model = MakeModel(RobbedKongSpec());
            EXPECT_TRUE(model.Discard(kSeat0, T(kFifth)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            EXPECT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
            Lap(model, "Wd Rd Gd Wd", {kSeat2});
            EXPECT_EQ(model.CurrentSeat(), kSeat1);
            EXPECT_TRUE(model.UpgradeKong(kSeat1, T(kFifth)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::RobKong);
            EXPECT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());
            return model;
        }

        // ----- helpers -----

        int Sum(const SeatScores& scores) {
            int total = 0;
            for (const int value : scores) {
                total += value;
            }
            return total;
        }

        /// A line that only a winner has.
        bool IsWinnerLine(const ScoreLine& line) {
            return line.kind == ScoreKind::Win || line.kind == ScoreKind::WinTileFromWall || line.kind == ScoreKind::OnlyWinningTile ||
                   static_cast<int>(line.kind) >= static_cast<int>(ScoreKind::OnlyPungs);
        }

        void ExpectNoWinnerLine(const ScoreBreakdown& breakdown, Seat seat) {
            for (const ScoreLine& line : breakdown.lines) {
                EXPECT_FALSE(IsWinnerLine(line)) << "seat " << seat << " has the winner's line " << Name(line.kind);
            }
            EXPECT_FALSE(breakdown.won) << "seat " << seat;
            EXPECT_FALSE(breakdown.limitHand.has_value()) << "seat " << seat;
        }

        /// What every settlement is: the scores are those of the breakdowns, the deltas are what `Settle` makes of the scores, the
        /// change sums to zero.
        void ExpectConsistent(const HandSettlement& settlement) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(settlement.scores[seat], settlement.breakdowns[seat].score) << "seat " << seat;
                EXPECT_EQ(settlement.scores[seat] % kScoreRounding, 0) << "seat " << seat;
            }
            const SeatScores expected = Settle(settlement.scores, settlement.winner, settlement.east);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(settlement.deltas[seat], expected[seat]) << "seat " << seat;
            }
            EXPECT_EQ(Sum(settlement.deltas), 0);
        }

        void ExpectScores(const HandSettlement& settlement, const SeatScores& scores, const SeatScores& deltas) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(settlement.scores[seat], scores[seat]) << "score of seat " << seat;
                EXPECT_EQ(settlement.deltas[seat], deltas[seat]) << "delta of seat " << seat;
            }
        }

    }  // namespace

    // ----- a self-draw -----

    TEST(ScoreHandTest, ASelfDrawWinIsScoredAndSettled) {
        const HandModel model = PlayToSelfDrawWin();
        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_EQ(model.Result()->source, WinSource::SelfDraw);

        const HandSettlement settlement = SettleHand(model);

        EXPECT_EQ(settlement.kind, ResultKind::Win);
        EXPECT_EQ(settlement.winner, kSeat1);
        EXPECT_EQ(settlement.east, kSeat0);
        EXPECT_EQ(settlement.roundWind, Wind::East);
        const ScoreBreakdown& winner = settlement.breakdowns[kSeat1];
        EXPECT_TRUE(winner.won);
        EXPECT_EQ(winner.points, kSelfDrawPoints);
        EXPECT_EQ(winner.doubles, kSelfDrawDoubles);
        EXPECT_EQ(winner.raw, kSelfDrawRaw);
        EXPECT_EQ(winner.score, kSelfDrawScore);
        EXPECT_EQ(LinePoints(winner, ScoreKind::Win), kWinning);
        EXPECT_EQ(LinePoints(winner, ScoreKind::WinTileFromWall), kWallTile);
        EXPECT_EQ(CountLines(winner, ScoreKind::OnlyWinningTile), 0);  // the hand waited on 1p and 4p
        EXPECT_EQ(LineDoubles(winner, ScoreKind::DragonSet), kOneDouble);
        EXPECT_EQ(LineDoubles(winner, ScoreKind::FullyConcealed), kOneDouble);
        EXPECT_EQ(LineDoubles(winner, ScoreKind::CallingFirstHand), kOneDouble);
        EXPECT_EQ(CountLines(winner, ScoreKind::ObviouslyLast), 0);
        // East pays 290 x 2, seats 2 and 3 pay 290 each; seat 2 (10) is above East and seat 3 (0): East pays it 10 x 2, seat 3 pays 10.
        ExpectScores(settlement, {0, kSelfDrawScore, kConcealedPungScore, 0}, {-580 - 20, 580 + 290 + 290, -290 + 20 + 10, -290 - 10});
        ExpectConsistent(settlement);
    }

    TEST(ScoreHandTest, EveryLoserIsScoredOnItsOwnHandWithNoWinnerLine) {
        const HandSettlement settlement = SettleHand(PlayToSelfDrawWin());

        for (const Seat seat : {kSeat0, kSeat2, kSeat3}) {
            ExpectNoWinnerLine(settlement.breakdowns[seat], seat);
        }
        EXPECT_EQ(LinePoints(settlement.breakdowns[kSeat2], ScoreKind::Pung), kSimplePungConcealed);  // the concealed 3s
        EXPECT_EQ(settlement.breakdowns[kSeat0].points, 0);
        EXPECT_EQ(settlement.breakdowns[kSeat3].points, 0);
    }

    // ----- a win on a discard -----

    TEST(ScoreHandTest, AWinOnADiscardOpensTheSetAndCountsTheTilesOnTheTable) {
        const HandModel model = PlayToDiscardWin();
        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_EQ(model.Result()->source, WinSource::Discard);
        ASSERT_EQ(model.Result()->from, kSeat0);

        const HandSettlement settlement = SettleHand(model);

        EXPECT_EQ(settlement.kind, ResultKind::Win);
        EXPECT_EQ(settlement.winner, kSeat2);
        EXPECT_EQ(settlement.east, kSeat0);
        EXPECT_EQ(settlement.roundWind, Wind::South);
        const ScoreBreakdown& winner = settlement.breakdowns[kSeat2];
        EXPECT_EQ(winner.points, kDiscardWinPoints);
        EXPECT_EQ(winner.doubles, kDiscardWinDoubles);
        EXPECT_EQ(winner.score, kDiscardWinScore);
        EXPECT_EQ(CountLines(winner, ScoreKind::WinTileFromWall), 0);
        EXPECT_EQ(LinePoints(winner, ScoreKind::OnlyWinningTile), kOnlyTile);
        EXPECT_EQ(LinePoints(winner, ScoreKind::Pair), kScoringPair);  // the South wind is the round wind
        EXPECT_EQ(LineDoubles(winner, ScoreKind::ObviouslyLast), kOneDouble);
        EXPECT_EQ(LineDoubles(winner, ScoreKind::CallingFirstHand), kOneDouble);
        EXPECT_EQ(CountLines(winner, ScoreKind::FullyConcealed), 0);  // the chow 4p 5p 6p was completed by a discard: open
        // East pays 100 x 2, seats 1 and 3 pay 100 each; seats 1 and 3 (0) pay East (10) 10 x 2 each.
        ExpectScores(settlement, {kEastPungScore, 0, kDiscardWinScore, 0}, {-200 + 40, -100 - 20, 400, -100 - 20});
        ExpectConsistent(settlement);
    }

    TEST(ScoreHandTest, TheVisibleCopiesAreThoseInThePileThatTheWinningDiscardLeftAndTheOpenMelds) {
        const HandModel model = PlayToDiscardWin();
        ASSERT_EQ(model.Discards().size(), static_cast<size_t>(kVisibleFifths) + 1);  // the three 5p and the Nw; the winning 5p left

        EXPECT_EQ(CountVisibleCopies(model, T(kFifth)), kVisibleFifths);
        EXPECT_EQ(CountVisibleCopies(model, T("Nw")), 1);
        EXPECT_EQ(CountVisibleCopies(model, T("Ww")), 0);  // East's pung of West winds is concealed
    }

    // ----- a robbed kong -----

    TEST(ScoreHandTest, ARobbedKongCountsTheOwnersPungAsThreeVisibleCopies) {
        const HandModel model = PlayToRobbedKong();
        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_EQ(model.Result()->source, WinSource::RobbedKong);
        ASSERT_EQ(model.GetHand(kSeat1).melds.size(), 1u);
        ASSERT_EQ(model.GetHand(kSeat1).melds[0].kind, MeldKind::Pung);  // the tile was taken from the kong

        const HandSettlement settlement = SettleHand(model);

        EXPECT_EQ(CountVisibleCopies(model, T(kFifth)), kVisibleFifths);
        EXPECT_EQ(settlement.winner, kSeat2);
        const ScoreBreakdown& winner = settlement.breakdowns[kSeat2];
        EXPECT_EQ(winner.points, kRobbedPoints);
        EXPECT_EQ(winner.doubles, kRobbedDoubles);
        EXPECT_EQ(winner.score, kRobbedScore);
        EXPECT_EQ(LineDoubles(winner, ScoreKind::RobbedKong), kOneDouble);
        EXPECT_EQ(LineDoubles(winner, ScoreKind::ObviouslyLast), kOneDouble);
        EXPECT_EQ(CountLines(winner, ScoreKind::WinTileFromWall), 0);
        EXPECT_EQ(CountLines(winner, ScoreKind::FullyConcealed), 0);
        // East pays 200 x 2, seats 1 and 3 pay 200 each; seat 1 (10) is above East and seat 3 (0): East pays it 10 x 2, seat 3 pays 10.
        ExpectScores(settlement, {0, kOpenPungScore, kRobbedScore, 0}, {-400 - 20, -200 + 20 + 10, 800, -200 - 10});
        ExpectConsistent(settlement);
    }

    TEST(ScoreHandTest, AnOpenChowCountsItsThreeTilesAndAConcealedKongDoesNot) {
        // East discards a 5p that South takes in a chow 4p 5p 6p.
        FixedWallSpec chowSpec;
        chowSpec.hands[kSeat0] = "5p 9p 1s 4s 7s 1m 4m 7m Ew Ww Nw Gd Wd";
        chowSpec.hands[kSeat1] = "4p 6p 2s 5s 8s 2m 5m 8m 3s 9s Rd Sw Nw";
        chowSpec.hands[kSeat2] = "2p 8p 3s 6s 3m 6m 9m 9s Ww Rd Gd Wd Nw";
        chowSpec.hands[kSeat3] = "3p 9p 3m 6m 1s 4s 7s Ew Ww Nw Gd Wd Rd";
        HandModel chow = MakeModel(chowSpec);
        ASSERT_TRUE(chow.Discard(kSeat0, T(kFifth)).has_value());
        ASSERT_EQ(chow.GetPhase(), Phase::Claim);
        ASSERT_TRUE(chow.Claim(kSeat1, ClaimKind::Chow, T("4p")).has_value());

        EXPECT_EQ(CountVisibleCopies(chow, T("4p")), 1);
        EXPECT_EQ(CountVisibleCopies(chow, T(kFifth)), 1);
        EXPECT_EQ(CountVisibleCopies(chow, T("6p")), 1);
        EXPECT_EQ(CountVisibleCopies(chow, T("3p")), 0);

        // East draws the fourth east wind and declares the kong: nothing of it is visible.
        FixedWallSpec kongSpec;
        kongSpec.hands[kSeat0] = "Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd";
        kongSpec.hands[kSeat1] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
        kongSpec.hands[kSeat2] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ww Nw Rd Wd";
        kongSpec.hands[kSeat3] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ww Nw Rd Gd";
        kongSpec.live = "Ew";
        kongSpec.loose = "8p 4s";
        HandModel kong = MakeModel(kongSpec);
        ASSERT_TRUE(kong.DeclareConcealedKong(kSeat0, T("Ew")).has_value());
        ASSERT_EQ(kong.GetHand(kSeat0).melds.size(), 1u);

        EXPECT_EQ(CountVisibleCopies(kong, T("Ew")), 0);
    }

    // ----- a drawn hand -----

    TEST(ScoreHandTest, ADrawnHandIsScoredForEveryoneAndPaysNothing) {
        FixedWallSpec spec = SelfDrawSpec("9m 2p 6p 7p");
        spec.liveAfterDeal = 4;
        HandModel model = MakeModel(spec);
        Lap(model, "Ew Nw Ww", {});
        ASSERT_TRUE(model.Discard(kSeat3, T("Gd")).has_value());  // the live wall is empty: East has nothing to draw
        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_EQ(model.Result()->kind, ResultKind::Drawn);

        const HandSettlement settlement = SettleHand(model);

        EXPECT_EQ(settlement.kind, ResultKind::Drawn);
        EXPECT_EQ(settlement.winner, kNoSeat);
        EXPECT_EQ(settlement.east, kSeat0);
        ExpectScores(settlement, {0, kStandingHandScore, kConcealedPungScore, 0}, {0, 0, 0, 0});
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            ExpectNoWinnerLine(settlement.breakdowns[seat], seat);
        }
        ExpectConsistent(settlement);
    }

}  // namespace BECore::Mahjong::Tests
