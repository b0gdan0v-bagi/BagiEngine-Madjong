#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * The claim window: who may claim a discard and how, passing, the priority of the claims, the seat that plays after a claim, and
 * winning on a discard. Fixed walls say what every seat holds and draws. Pure logic: no engine services.
 *
 * The hands below are built so that a discard opens a window only where a test means it to: the quiet hands hold no pair and no two
 * tiles of one suit within reach of each other that the lap discards (honours and a few far tiles) could use.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kTilesInHand = kDealtTiles;
        constexpr size_t kMeldedByChowOrPung = 2;
        constexpr size_t kMeldedByKong = 3;

        // The four quiet hands, one per seat. They share the winds and a few single honours, never a pair.
        constexpr const char* kQuiet0 = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ww Nw Gd";
        constexpr const char* kQuiet1 = "2p 5p 8p 2s 5s 8s 2m 5m 8m Sw Ww Nw Ew";
        constexpr const char* kQuiet2 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd";
        constexpr const char* kQuiet3 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Gd";

        // A hand that waits on the red dragon alone (a pair wait), and one that waits on the same tile with other sets.
        constexpr const char* kWaitsOnRedA = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";
        constexpr const char* kWaitsOnRedB = "2p 3p 4p 5s 6s 7s 7m 8m 9m Sw Sw Sw Rd";
        constexpr const char* kRed = "Rd";
        constexpr const char* kUseless = "9p";
        /// The tiles `MahjongWindow` draws.
        constexpr int kLapTiles = 4;

        FixedWallSpec QuietSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kQuiet0;
            spec.hands[kSeat1] = kQuiet1;
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = kQuiet3;
            return spec;
        }

        bool HasMeld(const PlayerHand& hand, MeldKind kind, Tile first, Seat from, Tile claimed) {
            for (const Meld& meld : hand.melds) {
                if (meld.kind == kind && meld.first == first && meld.from == from && meld.claimed == claimed && meld.fromDiscard && !meld.concealed) {
                    return true;
                }
            }
            return false;
        }

        // ----- chow -----

        // Seat 1 can chow 5p with its 4p and 6p; seat 2 holds the same tiles but is not the next seat.
        FixedWallSpec ChowSpec() {
            FixedWallSpec spec = QuietSpec();
            spec.hands[kSeat1] = "4p 6p 2s 5s 8s 2m 5m 8m Sw Ww Nw Rd Wd";
            spec.hands[kSeat2] = "4p 6p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd Wd";
            spec.hands[kSeat3] = "3p 8p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd Wd";
            spec.live = "5p 9m";
            return spec;
        }

        // ----- the window with every kind of claim -----

        // Seat 3 discards 4p: seat 0 can chow it (3p 5p), seat 1 pung it (4p 4p), seat 2 has declared a calling hand that waits on it.
        constexpr const char* kPriorityTile = "4p";
        constexpr const char* kPriorityChow = "3p";

        HandModel PriorityWindow() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "3p 5p 1s 4s 7s 1m 4m 7m 2s 8s Sw Ww Gd";
            spec.hands[kSeat1] = "4p 4p 7p 1s 4s 7s 2m 5m 8m Sw Ww Rd 9p";
            spec.hands[kSeat2] = "3p 5p 4s 5s 6s 7m 8m 9m Ew Ew Ew 1m 1m";
            spec.hands[kSeat3] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Rd Wd Nw 9s";
            spec.live = std::string("Wd Gd Nw ") + kPriorityTile;
            HandModel model = MakeModel(spec);
            Lap(model, "Wd Gd Nw", {kSeat2});
            EXPECT_TRUE(model.GetHand(kSeat2).callingDeclared);
            EXPECT_EQ(model.CurrentSeat(), kSeat3);
            EXPECT_TRUE(model.Discard(kSeat3, T(kPriorityTile)).has_value());
            return model;
        }

        // ----- mahjong on a discard -----

        // Seat 0 declares on its first discard (a hand that waits on the red dragon); seat 3 draws the red dragon and discards it.
        FixedWallSpec MahjongSpec() {
            FixedWallSpec spec = QuietSpec();
            spec.hands[kSeat0] = kWaitsOnRedA;
            spec.live = std::string(kUseless) + " Wd Gd " + kRed;
            return spec;
        }

        HandModel MahjongWindow(std::optional<int> liveAfterDeal = std::nullopt) {
            FixedWallSpec spec = MahjongSpec();
            spec.liveAfterDeal = liveAfterDeal;
            HandModel model = MakeModel(spec);
            Lap(model, std::string(kUseless) + " Wd Gd", {kSeat0});
            EXPECT_EQ(model.CurrentSeat(), kSeat3);
            EXPECT_TRUE(model.Discard(kSeat3, T(kRed)).has_value());
            return model;
        }

    }  // namespace

    TEST(HandModelClaimTest, AChowCanOnlyBeClaimedByTheSeatAfterTheDiscarder) {
        HandModel model = MakeModel(ChowSpec());

        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());

        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        EXPECT_EQ(model.CurrentSeat(), kSeat0);  // the discarder, in the claim window
        const eastl::vector<Seat> pending = model.PendingClaimSeats();
        ASSERT_EQ(pending.size(), 1u);
        EXPECT_EQ(pending[0], kSeat1);
        const eastl::vector<ClaimOption> options = model.LegalClaims(kSeat1);
        ASSERT_EQ(options.size(), 1u);
        EXPECT_EQ(options[0].kind, ClaimKind::Chow);
        EXPECT_EQ(options[0].chowFirst, T("4p"));
        EXPECT_TRUE(model.LegalClaims(kSeat2).empty());  // it holds 4p 6p too, but is not the next seat
        EXPECT_TRUE(model.LegalClaims(kSeat3).empty());

        const std::string before = Snapshot(model);
        EXPECT_EQ(Error(model.Claim(kSeat2, ClaimKind::Chow, T("4p"))), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Claim(kSeat3, ClaimKind::Chow, T("3p"))), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Claim(kSeat0, ClaimKind::Chow, T("4p"))), HandError::NotYourTurn);  // the discarder
        EXPECT_EQ(Error(model.Pass(kSeat2)), HandError::NotYourTurn);
        EXPECT_EQ(Snapshot(model), before);
    }

    TEST(HandModelClaimTest, AClaimedChowIsLaidOpenTheClaimerDiscardsAndTheTurnGoesOnFromTheSeatAfterIt) {
        HandModel model = MakeModel(ChowSpec());
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());

        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Chow, T("4p")).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        const PlayerHand& hand = model.GetHand(kSeat1);
        ASSERT_EQ(hand.melds.size(), 1u);
        EXPECT_TRUE(HasMeld(hand, MeldKind::Chow, T("4p"), kSeat0, T("5p")));
        EXPECT_EQ(hand.concealed.size(), static_cast<size_t>(kTilesInHand) - kMeldedByChowOrPung);
        EXPECT_EQ(hand.CountConcealed(T("4p")), 0);
        EXPECT_EQ(hand.CountConcealed(T("6p")), 0);
        EXPECT_TRUE(model.Discards().empty());  // the claimed tile left the pile
        EXPECT_TRUE(model.PendingClaimSeats().empty());
        EXPECT_EQ(model.GetClaimStatus(kSeat1), ClaimStatus::NotInWindow);

        // The claimer did not draw: it may not declare a win, and the turn after its discard goes to the seat after it.
        EXPECT_EQ(Error(model.DeclareWin(kSeat1)), HandError::NotCalling);
        EXPECT_EQ(Error(model.Discard(kSeat2, T("Rd"))), HandError::NotYourTurn);
        ASSERT_TRUE(model.Discard(kSeat1, T("Rd")).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat2);
        EXPECT_EQ(model.GetHand(kSeat2).concealed.size(), static_cast<size_t>(kTilesInHand) + 1);
        EXPECT_EQ(model.GetHand(kSeat2).CountConcealed(T("9m")), 2);  // the draw that follows is the next live tile
    }

    TEST(HandModelClaimTest, AChowWithSeveralSequencesTakesTheChosenOne) {
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat1] = "3p 4p 6p 7p 1m 4m 7m 1s 4s 7s 2m 5m 8m";
        spec.hands[kSeat2] = "9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Rd Wd Gd";
        spec.hands[kSeat3] = "8p 3s 6s 9s 3m 6m 9m Ew Sw Ww Nw Rd Wd";
        spec.live = "5p";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        const eastl::vector<ClaimOption> options = model.LegalClaims(kSeat1);
        ASSERT_EQ(options.size(), 3u);
        EXPECT_EQ(options[0], (ClaimOption{ClaimKind::Chow, T("3p")}));
        EXPECT_EQ(options[1], (ClaimOption{ClaimKind::Chow, T("4p")}));
        EXPECT_EQ(options[2], (ClaimOption{ClaimKind::Chow, T("5p")}));

        // A sequence the seat cannot complete, and a chow without a sequence, are refused and change nothing.
        const std::string before = Snapshot(model);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Chow, T("2p"))), HandError::IllegalClaim);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Chow, T("6p"))), HandError::IllegalClaim);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Chow)), HandError::IllegalClaim);
        EXPECT_EQ(Snapshot(model), before);

        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Chow, T("5p")).has_value());
        const PlayerHand& hand = model.GetHand(kSeat1);
        EXPECT_TRUE(HasMeld(hand, MeldKind::Chow, T("5p"), kSeat0, T("5p")));
        EXPECT_EQ(hand.CountConcealed(T("3p")), 1);
        EXPECT_EQ(hand.CountConcealed(T("4p")), 1);
        EXPECT_EQ(hand.CountConcealed(T("6p")), 0);
        EXPECT_EQ(hand.CountConcealed(T("7p")), 0);
    }

    TEST(HandModelClaimTest, AHonourNeverMakesAChowAndAWindowWithNoClaimIsNeverOpened) {
        FixedWallSpec spec = QuietSpec();
        spec.live = "Ew";
        HandModel model = MakeModel(spec);

        ASSERT_TRUE(model.Discard(kSeat0, T("Ew")).has_value());  // seat 1 holds Sw Ww Nw Ew: no pung, no chow

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_TRUE(model.PendingClaimSeats().empty());
        EXPECT_EQ(model.Discards().size(), 1u);
    }

    TEST(HandModelClaimTest, ASequenceNeverWrapsFromTheEndOfOneSuitToTheStartOfTheNext) {
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat1] = "8p 9p 3s 6s 9s 2m 5m 8m 4m 7m Sw Ww Nw";
        spec.live = "1s";
        HandModel model = MakeModel(spec);

        ASSERT_TRUE(model.Discard(kSeat0, T("1s")).has_value());  // 8p 9p 1s are consecutive ids, not a sequence

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
    }

    TEST(HandModelClaimTest, AllPassingLetsTheNextSeatDrawAndTheTileStaysInThePile) {
        HandModel model = MakeModel(ChowSpec());
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        ASSERT_TRUE(model.Pass(kSeat1).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_EQ(model.GetHand(kSeat1).concealed.size(), static_cast<size_t>(kTilesInHand) + 1);
        EXPECT_EQ(model.GetHand(kSeat1).CountConcealed(T("9m")), 1);
        ASSERT_EQ(model.Discards().size(), 1u);
        EXPECT_EQ(model.Discards().back().tile, T("5p"));
        EXPECT_TRUE(model.GetHand(kSeat1).melds.empty());
    }

    TEST(HandModelClaimTest, APungCanBeClaimedFromAnySeatAndTheTurnWrapsAroundTheTable) {
        // East is seat 3: it draws the red dragon and discards it, seat 1 (opposite) holds two.
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat1] = "Rd Rd 2p 5p 8p 2s 5s 8s 2m 5m 8m 9p 1p";
        spec.live = "Rd 9m";
        HandModel model = MakeModel(spec, kSeat3);
        ASSERT_EQ(model.CurrentSeat(), kSeat3);

        ASSERT_TRUE(model.Discard(kSeat3, T(kRed)).has_value());

        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        const eastl::vector<ClaimOption> options = model.LegalClaims(kSeat1);
        ASSERT_EQ(options.size(), 1u);
        EXPECT_EQ(options[0].kind, ClaimKind::Pung);
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        const PlayerHand& hand = model.GetHand(kSeat1);
        EXPECT_TRUE(HasMeld(hand, MeldKind::Pung, T(kRed), kSeat3, T(kRed)));
        EXPECT_EQ(hand.concealed.size(), static_cast<size_t>(kTilesInHand) - kMeldedByChowOrPung);
        EXPECT_TRUE(model.Discards().empty());

        ASSERT_TRUE(model.Discard(kSeat1, T("1p")).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat2);  // the seat after the claimer (seat 1), although seat 3 discarded
    }

    TEST(HandModelClaimTest, AKongFromADiscardIsLaidOpenThenTheClaimerDrawsALooseTileAndDiscards) {
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat2] = "Rd Rd Rd 3p 6p 9p 3s 6s 9s 3m 6m 9m Sw";
        spec.live = kRed;
        spec.loose = "8p 4s";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T(kRed)).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        const size_t liveBefore = model.GetWall().LiveCount();

        const eastl::vector<ClaimOption> options = model.LegalClaims(kSeat2);
        ASSERT_EQ(options.size(), 2u);
        EXPECT_EQ(options[0].kind, ClaimKind::Pung);
        EXPECT_EQ(options[1].kind, ClaimKind::Kong);
        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Kong).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat2);
        const PlayerHand& hand = model.GetHand(kSeat2);
        EXPECT_TRUE(HasMeld(hand, MeldKind::Kong, T(kRed), kSeat0, T(kRed)));
        EXPECT_EQ(hand.concealed.size(), static_cast<size_t>(kTilesInHand) - kMeldedByKong + 1);  // the loose tile joined
        EXPECT_EQ(hand.CountConcealed(T("8p")), 1);
        EXPECT_EQ(hand.CountConcealed(T(kRed)), 0);
        EXPECT_EQ(model.GetWall().LiveCount(), liveBefore - 1);  // the reserve was refilled from the live tail
        EXPECT_EQ(model.GetWall().LooseCount(), static_cast<size_t>(kLooseTiles));
        EXPECT_TRUE(model.Discards().empty());
        EXPECT_EQ(Error(model.DeclareWin(kSeat2)), HandError::NotCalling);

        ASSERT_TRUE(model.Discard(kSeat2, T("8p")).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat3);  // discarder 0, claimer 2: the turn goes on from the seat after the claimer
    }

    TEST(HandModelClaimTest, PassingOnALegalKongClaimIsAllowedAndTheNextSeatDraws) {
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat2] = "Rd Rd Rd 3p 6p 9p 3s 6s 9s 3m 6m 9m Sw";
        spec.live = kRed;
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T(kRed)).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_EQ(model.LegalClaims(kSeat2).size(), 2u);  // pung and kong
        const std::string before = Snapshot(model);

        ASSERT_TRUE(model.Pass(kSeat2).has_value());

        EXPECT_NE(Snapshot(model), before);
        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_TRUE(model.GetHand(kSeat2).melds.empty());
        EXPECT_EQ(model.GetHand(kSeat2).CountConcealed(T(kRed)), kPungTilesFromHand + 1);
        EXPECT_EQ(model.Discards().back().tile, T(kRed));  // nobody took it
        EXPECT_EQ(model.GetHand(kSeat1).concealed.size(), static_cast<size_t>(kTilesInHand) + 1);
    }

    TEST(HandModelClaimTest, AMahjongClaimNeedsTheDeclarationAndTheWindowStillResolves) {
        // Seat 2 waits on the red dragon but has not declared; seat 3 holds a pair of them.
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat2] = kWaitsOnRedA;
        spec.hands[kSeat3] = "Rd Rd 2p 5p 8p 2s 5s 8s 2m 5m 8m 9p Wd";
        spec.live = kRed;
        HandModel model = MakeModel(spec);
        ASSERT_EQ(WinningTiles(model.GetHand(kSeat2)).size(), 1u);
        ASSERT_TRUE(model.Discard(kSeat0, T(kRed)).has_value());

        // Seat 2 has no legal claim, so it is not in the window; the window is open for seat 3.
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_EQ(model.PendingClaimSeats().size(), 1u);
        EXPECT_EQ(model.PendingClaimSeats()[0], kSeat3);
        EXPECT_TRUE(model.LegalClaims(kSeat2).empty());
        const std::string before = Snapshot(model);
        // Whether a seat is in the window is asked first, for every kind of claim; the declaration only for a seat in it.
        EXPECT_EQ(Error(model.Claim(kSeat2, ClaimKind::Mahjong)), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Claim(kSeat2, ClaimKind::Pung)), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Pass(kSeat2)), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Claim(kSeat3, ClaimKind::Mahjong)), HandError::NotCalling);  // in the window, but did not declare
        EXPECT_EQ(Snapshot(model), before);
        EXPECT_FALSE(model.Result().has_value());

        ASSERT_TRUE(model.Claim(kSeat3, ClaimKind::Pung).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat3);
        EXPECT_FALSE(model.Result().has_value());
    }

    TEST(HandModelClaimTest, AMahjongClaimByADeclaredSeatEndsTheHandWithTheDiscardAsTheWinningTile) {
        HandModel model = MahjongWindow();
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_EQ(model.PendingClaimSeats().size(), 1u);
        EXPECT_EQ(model.PendingClaimSeats()[0], kSeat0);
        const eastl::vector<ClaimOption> options = model.LegalClaims(kSeat0);
        ASSERT_EQ(options.size(), 1u);
        EXPECT_EQ(options[0].kind, ClaimKind::Mahjong);

        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Mahjong).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        const HandResult& result = *model.Result();
        EXPECT_EQ(result.kind, ResultKind::Win);
        EXPECT_EQ(result.winner, kSeat0);
        EXPECT_EQ(result.winningTile, T(kRed));
        EXPECT_EQ(result.source, WinSource::Discard);
        EXPECT_EQ(result.from, kSeat3);
        EXPECT_FALSE(result.loose);
        EXPECT_FALSE(result.lastLiveTile);
        EXPECT_EQ(model.Discards().size(), 3u);  // the winning tile left the pile
        EXPECT_EQ(model.GetHand(kSeat0).concealed.size(), static_cast<size_t>(kTilesInHand) + 1);
        EXPECT_TRUE(IsWinningShape(CountTiles(model.GetHand(kSeat0).concealed), 0));
        EXPECT_EQ(Error(model.Pass(kSeat0)), HandError::WrongPhase);
        EXPECT_EQ(Error(model.Claim(kSeat0, ClaimKind::Mahjong)), HandError::WrongPhase);
    }

    TEST(HandModelClaimTest, AWinOnTheDiscardAfterTheLastLiveTileIsNotALastLiveTile) {
        HandModel model = MahjongWindow(kLapTiles);  // the four tiles of the lap are all there is
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_EQ(model.GetWall().LiveCount(), 0u);

        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Mahjong).has_value());

        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->kind, ResultKind::Win);
        EXPECT_EQ(model.Result()->source, WinSource::Discard);
        EXPECT_FALSE(model.Result()->lastLiveTile);  // only a self-draw of the last live tile counts
    }

    TEST(HandModelClaimTest, PassingTheLastDiscardOfAnEmptyLiveWallIsADrawnHand) {
        HandModel model = MahjongWindow(kLapTiles);
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        ASSERT_TRUE(model.Pass(kSeat0).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->kind, ResultKind::Drawn);
    }

    TEST(HandModelClaimTest, ADeclaredSeatGetsNoWindowForATileThatDoesNotCompleteItsHand) {
        // Seat 0 has declared (it waits on the red dragon). A discard that is not the red dragon opens no window for it.
        FixedWallSpec spec = MahjongSpec();
        spec.live = std::string(kUseless) + " Wd Gd Wd";
        HandModel model = MakeModel(spec);
        Lap(model, std::string(kUseless) + " Wd Gd", {kSeat0});

        ASSERT_TRUE(model.Discard(kSeat3, T("Wd")).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat0);
        EXPECT_EQ(Error(model.Claim(kSeat0, ClaimKind::Mahjong)), HandError::WrongPhase);
    }

    TEST(HandModelClaimTest, MahjongBeatsAPungAndAChowWhenAllThreeClaim) {
        HandModel model = PriorityWindow();
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        const eastl::vector<Seat> pending = model.PendingClaimSeats();
        ASSERT_EQ(pending.size(), 3u);

        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow)).has_value());
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Claim);  // seat 2 has not decided
        ASSERT_EQ(model.PendingClaimSeats().size(), 1u);
        EXPECT_EQ(model.PendingClaimSeats()[0], kSeat2);
        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->kind, ResultKind::Win);
        EXPECT_EQ(model.Result()->winner, kSeat2);
        EXPECT_EQ(model.Result()->winningTile, T(kPriorityTile));
        EXPECT_EQ(model.Result()->from, kSeat3);
        EXPECT_TRUE(model.GetHand(kSeat0).melds.empty());
        EXPECT_TRUE(model.GetHand(kSeat1).melds.empty());
    }

    TEST(HandModelClaimTest, APungBeatsAChowWhateverTheOrderOfTheDecisions) {
        HandModel model = PriorityWindow();
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow)).has_value());
        ASSERT_TRUE(model.Pass(kSeat2).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Claim);  // seat 1 has not decided
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_TRUE(HasMeld(model.GetHand(kSeat1), MeldKind::Pung, T(kPriorityTile), kSeat3, T(kPriorityTile)));
        EXPECT_TRUE(model.GetHand(kSeat0).melds.empty());
        EXPECT_EQ(model.GetHand(kSeat0).concealed.size(), static_cast<size_t>(kTilesInHand));
        EXPECT_EQ(model.Discards().size(), 3u);  // the three lap discards; the claimed tile left
    }

    TEST(HandModelClaimTest, AChowIsCarriedOutWhenTheOthersPass) {
        HandModel model = PriorityWindow();
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        ASSERT_TRUE(model.Pass(kSeat2).has_value());
        ASSERT_TRUE(model.Pass(kSeat1).has_value());
        EXPECT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow)).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat0);
        EXPECT_TRUE(HasMeld(model.GetHand(kSeat0), MeldKind::Chow, T(kPriorityChow), kSeat3, T(kPriorityTile)));
        EXPECT_TRUE(model.GetHand(kSeat1).melds.empty());
    }

    TEST(HandModelClaimTest, EveryoneMayPassAndThenTheSeatAfterTheDiscarderDraws) {
        HandModel model = PriorityWindow();
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        for (const Seat seat : model.PendingClaimSeats()) {
            ASSERT_TRUE(model.Pass(seat).has_value()) << "seat " << seat;
        }

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat0);
        EXPECT_EQ(model.GetHand(kSeat0).concealed.size(), static_cast<size_t>(kTilesInHand) + 1);
        EXPECT_EQ(model.Discards().back().tile, T(kPriorityTile));  // nobody took it
        EXPECT_TRUE(model.PendingClaimSeats().empty());
    }

    TEST(HandModelClaimTest, ADecisionIsMadeOnceAndARefusedClaimLeavesTheSeatFree) {
        HandModel model = PriorityWindow();
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        // Claims the seat cannot make: a pung or a kong without the tiles, a chow by a seat that is not next, a mahjong that is not one.
        const std::string before = Snapshot(model);
        EXPECT_EQ(Error(model.Claim(kSeat0, ClaimKind::Pung)), HandError::IllegalClaim);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Kong)), HandError::IllegalClaim);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Chow, T(kPriorityChow))), HandError::IllegalClaim);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Mahjong)), HandError::NotCalling);
        EXPECT_EQ(Error(model.Claim(kSeat3, ClaimKind::Pung)), HandError::NotYourTurn);  // the discarder
        EXPECT_EQ(Error(model.Pass(kSeat3)), HandError::NotYourTurn);
        EXPECT_EQ(Snapshot(model), before);

        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow)).has_value());
        ASSERT_TRUE(model.Pass(kSeat1).has_value());
        EXPECT_EQ(model.GetClaimStatus(kSeat0), ClaimStatus::Claimed);
        EXPECT_EQ(model.GetClaimStatus(kSeat1), ClaimStatus::Passed);
        EXPECT_EQ(model.GetClaimStatus(kSeat2), ClaimStatus::Waiting);
        const std::string decided = Snapshot(model);
        EXPECT_EQ(Error(model.Pass(kSeat0)), HandError::AlreadyDecided);
        EXPECT_EQ(Error(model.Claim(kSeat0, ClaimKind::Chow, T(kPriorityChow))), HandError::AlreadyDecided);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Pung)), HandError::AlreadyDecided);
        EXPECT_EQ(Error(model.Pass(kSeat1)), HandError::AlreadyDecided);
        EXPECT_EQ(Snapshot(model), decided);
    }

    TEST(HandModelClaimTest, AnActionOutsideItsPhaseIsRefusedAndNothingChanges) {
        // In the window only the claims are possible ...
        HandModel window = PriorityWindow();
        ASSERT_EQ(window.GetPhase(), Phase::Claim);
        const std::string inWindow = Snapshot(window);
        EXPECT_EQ(Error(window.Discard(kSeat3, T("Rd"))), HandError::WrongPhase);
        EXPECT_EQ(Error(window.Discard(kSeat0, T("5p"))), HandError::WrongPhase);
        EXPECT_EQ(Error(window.DeclareWin(kSeat2)), HandError::WrongPhase);
        EXPECT_EQ(Snapshot(window), inWindow);

        // ... and outside it there is nothing to claim or pass.
        FixedWallSpec spec = QuietSpec();
        spec.live = kRed;
        HandModel turn = MakeModel(spec);
        ASSERT_EQ(turn.GetPhase(), Phase::Discard);
        const std::string inTurn = Snapshot(turn);
        EXPECT_EQ(Error(turn.Claim(kSeat1, ClaimKind::Pung)), HandError::WrongPhase);
        EXPECT_EQ(Error(turn.Pass(kSeat1)), HandError::WrongPhase);
        EXPECT_TRUE(turn.PendingClaimSeats().empty());
        EXPECT_TRUE(turn.LegalClaims(kSeat1).empty());
        EXPECT_EQ(Snapshot(turn), inTurn);
    }

    TEST(HandModelClaimTest, TwoMahjongClaimsGoToTheSeatFartherFromTheDiscarderCounterClockwise) {
        // East is seat 0; seats 0 and 1 have declared; seat 3 discards: the distances are 1 and 2, so seat 1 wins whoever decides first.
        for (const bool nearFirst : {true, false}) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kWaitsOnRedA;
            spec.hands[kSeat1] = kWaitsOnRedB;
            spec.hands[kSeat2] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ww Nw Wd Gd";
            spec.hands[kSeat3] = "2s 5s 8s 2m 5m 8m 4p 7p 1p Ww Nw Wd Gd";
            spec.live = "9p 9s Gd Rd";
            HandModel model = MakeModel(spec);
            Lap(model, "9p 9s Gd", {kSeat0, kSeat1});
            ASSERT_TRUE(model.Discard(kSeat3, T(kRed)).has_value());
            ASSERT_EQ(model.GetPhase(), Phase::Claim);
            ASSERT_EQ(model.PendingClaimSeats().size(), 2u);

            ASSERT_TRUE(model.Claim(nearFirst ? kSeat0 : kSeat1, ClaimKind::Mahjong).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            ASSERT_TRUE(model.Claim(nearFirst ? kSeat1 : kSeat0, ClaimKind::Mahjong).has_value());

            ASSERT_TRUE(model.Result().has_value());
            EXPECT_EQ(model.Result()->kind, ResultKind::Win);
            EXPECT_EQ(model.Result()->winner, kSeat1) << "near first: " << nearFirst;
            EXPECT_EQ(model.Result()->from, kSeat3);
            EXPECT_EQ(model.Result()->source, WinSource::Discard);
        }
    }

    TEST(HandModelClaimTest, TheFartherSeatWinsOnTheOtherDiscarderPositionToo) {
        // East is seat 2; seats 2 and 0 have declared; seat 1 discards: the distances are 1 (seat 2) and 3 (seat 0), so seat 0 wins.
        FixedWallSpec spec;
        spec.hands[kSeat2] = kWaitsOnRedA;
        spec.hands[kSeat0] = kWaitsOnRedB;
        spec.hands[kSeat1] = "2s 5s 8s 2m 5m 8m 4p 7p 1p Ww Nw Wd Gd";
        spec.hands[kSeat3] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ww Nw Wd Gd";
        spec.live = "9p Gd 9s Rd";
        HandModel model = MakeModel(spec, kSeat2);
        Lap(model, "9p Gd 9s", {kSeat2, kSeat0});
        ASSERT_EQ(model.CurrentSeat(), kSeat1);
        ASSERT_TRUE(model.Discard(kSeat1, T(kRed)).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);

        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());
        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Mahjong).has_value());

        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->winner, kSeat0);
        EXPECT_EQ(model.Result()->from, kSeat1);
    }

    TEST(HandModelClaimTest, ASeatThatClaimedAPungHasNotDrawnAndCannotDeclareAWin) {
        // Seat 0 has declared a hand that waits on 5p or the red dragon; after claiming a pung of 5p its hand is a winning shape but it
        // has drawn nothing.
        FixedWallSpec spec;
        spec.hands[kSeat0] = "1p 2p 3p 4s 5s 6s 7m 8m 9m 5p 5p Rd Rd";
        spec.hands[kSeat1] = "2p 8p 2s 5s 8s 2m 5m 8m Sw Ww Nw Ew 1s";
        spec.hands[kSeat2] = "3p 9p 3s 6s 9s 3m 6m 9m Sw Ww Nw Wd 1m";
        spec.hands[kSeat3] = "1p 7p 1s 4s 7s 1m 4m 7m Sw Ww Nw Gd Rd";
        spec.live = "9p Wd Gd 5p";
        HandModel model = MakeModel(spec);
        Lap(model, "9p Wd Gd", {kSeat0});
        ASSERT_TRUE(model.Discard(kSeat3, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        const eastl::vector<ClaimOption> options = model.LegalClaims(kSeat0);
        ASSERT_EQ(options.size(), 2u);
        EXPECT_EQ(options[0].kind, ClaimKind::Mahjong);
        EXPECT_EQ(options[1].kind, ClaimKind::Pung);

        ASSERT_TRUE(model.Claim(kSeat0, ClaimKind::Pung).has_value());

        const PlayerHand& hand = model.GetHand(kSeat0);
        EXPECT_TRUE(IsWinningShape(CountTiles(hand.concealed), static_cast<int>(hand.melds.size())));
        const std::string before = Snapshot(model);
        EXPECT_EQ(Error(model.DeclareWin(kSeat0)), HandError::CannotWin);
        EXPECT_EQ(Snapshot(model), before);
        ASSERT_TRUE(model.Discard(kSeat0, T(kRed)).has_value());
        EXPECT_EQ(model.CurrentSeat(), kSeat1);  // the claimer was seat 0: the turn wraps to seat 1
    }

    TEST(HandModelClaimTest, TheCallingHandOfASeatWithAClaimedMeldCountsTheMeld) {
        FixedWallSpec spec = QuietSpec();
        spec.hands[kSeat2] = "Rd Rd 1p 2p 3p 4s 5s 6s 7m 8m 9m 9p Gd";
        spec.live = kRed;
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T(kRed)).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Pung).has_value());
        ASSERT_EQ(model.CurrentSeat(), kSeat2);

        // 7m is part of a set: without it the hand is not waiting. 9p leaves three sets, the meld and a lone Gd.
        const std::string before = Snapshot(model);
        EXPECT_EQ(Error(model.Discard(kSeat2, T("7m"), true)), HandError::NotWaiting);
        EXPECT_EQ(Snapshot(model), before);
        ASSERT_TRUE(model.Discard(kSeat2, T("9p"), true).has_value());

        EXPECT_TRUE(model.GetHand(kSeat2).callingDeclared);
        EXPECT_EQ(model.GetHand(kSeat2).callingAtDiscardCount, 0);
        EXPECT_EQ(model.CurrentSeat(), kSeat3);
    }

}  // namespace BECore::Mahjong::Tests
