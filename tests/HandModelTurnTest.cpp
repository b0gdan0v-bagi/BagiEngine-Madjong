#include "FixedWall.h"

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * The turn of a hand: the start, discards, the calling-hand declaration, the self-draw win, bonus tiles and the drawn hand (no claims
 * yet). Fixed walls say what every seat holds and what comes next. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr Seat kSeat0 = 0;
        constexpr Seat kSeat1 = 1;
        constexpr Seat kSeat2 = 2;
        constexpr Seat kSeat3 = 3;

        constexpr size_t kFullWall = 136;
        constexpr size_t kDealtTotal = static_cast<size_t>(kDealtTiles) * kSeatCount;

        /// Seat 0 waits on `kWaitingTile` (a pair wait); the other seats hold scattered tiles that wait on nothing.
        constexpr const char* kWaitingHand = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";
        constexpr const char* kWaitingTile = "Rd";
        /// A tile that does not help seat 0, and a tile of its hand whose discard leaves it not waiting.
        constexpr const char* kUselessTile = "9p";
        constexpr const char* kNeededTile = "7m";

        Tile T(const char* text) {
            return TryParse(text).value();
        }

        FixedWallSpec BaseSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kWaitingHand;
            spec.hands[kSeat1] = "1m 3m 5m 7m 9m 2s 4s 6s 8s 1p 3p 5p 7p";
            spec.hands[kSeat2] = "2m 4m 6m 8m 1s 3s 5s 7s 9s 2p 4p 6p 8p";
            spec.hands[kSeat3] = "9p 1m 3m 5m 7m 9m 2s 4s 6s 8s Sw Ww Nw";
            return spec;
        }

        /// The live wall of a lap: East draws the useless tile, the three others a tile each, then East draws `last`.
        FixedWallSpec LapSpec(const char* last) {
            FixedWallSpec spec = BaseSpec();
            spec.live = std::string(kUselessTile) + " 4m 6m 8p " + last;
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
                text += " calling " + std::to_string(hand.callingDeclared) + "/" + std::to_string(hand.callingAtDiscardCount) + "/" + std::to_string(hand.discardCount);
            }
            text += " | pile";
            for (const DiscardEntry& entry : model.Discards()) {
                text += " ";
                text += ToString(entry.tile);
            }
            text += " | live " + std::to_string(model.GetWall().LiveCount()) + " loose " + std::to_string(model.GetWall().LooseCount());
            text += model.Result().has_value() ? " result" : " open";
            return text;
        }

        /// Each seat in turn discards the next tile of `tiles` (a lap's `live`: every seat discards what it has just drawn).
        void DiscardEach(HandModel& model, const std::string& tiles) {
            const eastl::vector<Tile> parsed = ParseTiles(tiles).value();
            for (const Tile tile : parsed) {
                const Seat seat = model.CurrentSeat();
                const HandStatus status = model.Discard(seat, tile);
                ASSERT_TRUE(status.has_value()) << "seat " << seat << " cannot discard " << ToString(tile);
            }
        }

        /// The model after East declared on its first discard and the three others discarded what they drew: East is to act again,
        /// holding the last tile of `LapSpec(last)`.
        HandModel AfterTheDeclaration(const char* last, bool flowers = false, const std::string& loose = "") {
            FixedWallSpec spec = LapSpec(last);
            spec.loose = loose;
            HandModel model = MakeModel(spec, kSeat0, Wind::East, flowers);
            EXPECT_TRUE(model.Discard(kSeat0, T(kUselessTile), true).has_value());
            DiscardEach(model, "4m 6m 8p");
            return model;
        }

    }  // namespace

    TEST(HandModelTurnTest, BeginLeavesEastWithFourteenAndTheOthersWithThirteen) {
        FixedWallSpec spec = BaseSpec();
        spec.live = kUselessTile;
        const HandModel model = MakeModel(spec);

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat0);
        EXPECT_EQ(model.East(), kSeat0);
        EXPECT_EQ(model.GetHand(kSeat0).concealed.size(), static_cast<size_t>(kDealtTiles) + 1);
        for (const Seat seat : {kSeat1, kSeat2, kSeat3}) {
            EXPECT_EQ(model.GetHand(seat).concealed.size(), static_cast<size_t>(kDealtTiles));
        }
        EXPECT_EQ(model.GetHand(kSeat0).CountConcealed(T(kUselessTile)), 1);
        EXPECT_TRUE(model.Discards().empty());
        EXPECT_FALSE(model.Result().has_value());
        EXPECT_EQ(model.GetWall().LiveCount(), kFullWall - kLooseTiles - kDeadWallTiles - kDealtTotal - 1);
        EXPECT_EQ(model.RoundWind(), Wind::East);
    }

    TEST(HandModelTurnTest, EastAtAnotherSeatDealsAndDrawsFirst) {
        FixedWallSpec spec = BaseSpec();
        spec.live = kUselessTile;
        const HandModel model = MakeModel(spec, kSeat2, Wind::South);

        EXPECT_EQ(model.East(), kSeat2);
        EXPECT_EQ(model.CurrentSeat(), kSeat2);
        EXPECT_EQ(model.GetHand(kSeat2).concealed.size(), static_cast<size_t>(kDealtTiles) + 1);
        EXPECT_EQ(model.GetHand(kSeat2).CountConcealed(T(kUselessTile)), 1);
        EXPECT_EQ(model.SeatWind(kSeat2), Wind::East);
        EXPECT_EQ(model.SeatWind(kSeat3), Wind::South);
        EXPECT_EQ(model.SeatWind(kSeat0), Wind::West);
        EXPECT_EQ(model.SeatWind(kSeat1), Wind::North);
        EXPECT_EQ(model.RoundWind(), Wind::South);
    }

    TEST(HandModelTurnTest, ADiscardMovesTheTurnToTheNextSeatWhoNowHasFourteen) {
        FixedWallSpec spec = BaseSpec();
        spec.live = std::string(kUselessTile) + " 4m";
        HandModel model = MakeModel(spec);

        ASSERT_TRUE(model.Discard(kSeat0, T(kUselessTile)).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_EQ(model.GetHand(kSeat0).concealed.size(), static_cast<size_t>(kDealtTiles));
        EXPECT_EQ(model.GetHand(kSeat0).discardCount, 1);
        EXPECT_EQ(model.GetHand(kSeat1).concealed.size(), static_cast<size_t>(kDealtTiles) + 1);
        EXPECT_EQ(model.GetHand(kSeat1).CountConcealed(T("4m")), 1);  // the next tile of the live wall
    }

    TEST(HandModelTurnTest, TheDiscardPileKeepsTheOrder) {
        FixedWallSpec spec = BaseSpec();
        spec.live = "9p 4m 6m 8p 5m";
        HandModel model = MakeModel(spec);
        DiscardEach(model, "9p 4m 6m 8p 5m");

        const eastl::vector<DiscardEntry>& pile = model.Discards();
        ASSERT_EQ(pile.size(), 5u);
        const char* const tiles[] = {"9p", "4m", "6m", "8p", "5m"};
        const Seat seats[] = {kSeat0, kSeat1, kSeat2, kSeat3, kSeat0};
        for (size_t i = 0; i < pile.size(); ++i) {
            EXPECT_EQ(pile[i].tile, T(tiles[i])) << "entry " << i;
            EXPECT_EQ(pile[i].seat, seats[i]) << "entry " << i;
        }
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_EQ(model.GetHand(kSeat0).discardCount, 2);
    }

    TEST(HandModelTurnTest, AWrongSeatIsRefusedAndNothingChanges) {
        FixedWallSpec spec = BaseSpec();
        spec.live = kUselessTile;
        HandModel model = MakeModel(spec);
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.Discard(kSeat1, T("1m"))), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Discard(-1, T(kUselessTile))), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Discard(kSeatCount, T(kUselessTile))), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.DeclareWin(kSeat2)), HandError::NotYourTurn);
        EXPECT_EQ(Snapshot(model), before);
    }

    TEST(HandModelTurnTest, ATileThatIsNotInTheHandIsRefusedAndNothingChanges) {
        FixedWallSpec spec = BaseSpec();
        spec.live = kUselessTile;
        HandModel model = MakeModel(spec);
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.Discard(kSeat0, T("Gd"))), HandError::TileNotInHand);
        EXPECT_EQ(Error(model.Discard(kSeat0, T("Gd"), true)), HandError::TileNotInHand);
        EXPECT_EQ(Error(model.Discard(kSeat0, Tile{})), HandError::TileNotInHand);
        EXPECT_EQ(Snapshot(model), before);
    }

    TEST(HandModelTurnTest, AnActionOutsideItsPhaseIsRefusedAndNothingChanges) {
        FixedWallSpec spec = BaseSpec();
        spec.liveAfterDeal = 0;
        HandModel model = MakeModel(spec);
        ASSERT_EQ(model.GetPhase(), Phase::Finished);
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.Discard(kSeat0, T(kWaitingTile))), HandError::WrongPhase);
        EXPECT_EQ(Error(model.DeclareWin(kSeat0)), HandError::WrongPhase);
        EXPECT_EQ(Snapshot(model), before);
    }

    TEST(HandModelTurnTest, AWaitingSeatThatDidNotDeclareCannotWinAndHasToDiscard) {
        FixedWallSpec spec = BaseSpec();
        spec.live = kWaitingTile;
        HandModel model = MakeModel(spec);
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.DeclareWin(kSeat0)), HandError::NotCalling);
        EXPECT_EQ(Snapshot(model), before);
        EXPECT_FALSE(model.Result().has_value());
        EXPECT_TRUE(model.Discard(kSeat0, T(kWaitingTile)).has_value());
    }

    TEST(HandModelTurnTest, ACallingHandCannotBeDeclaredWithADiscardThatLeavesTheHandNotWaiting) {
        FixedWallSpec spec = BaseSpec();
        spec.live = kUselessTile;
        HandModel model = MakeModel(spec);
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.Discard(kSeat0, T(kNeededTile), true)), HandError::NotWaiting);
        EXPECT_EQ(Snapshot(model), before);
        EXPECT_FALSE(model.GetHand(kSeat0).callingDeclared);

        // The same discard without the declaration is fine, and so is the declaring one that keeps the wait.
        EXPECT_TRUE(model.Discard(kSeat0, T(kUselessTile), true).has_value());
        EXPECT_TRUE(model.GetHand(kSeat0).callingDeclared);
    }

    TEST(HandModelTurnTest, ADeclarationOnTheFirstDiscardStoresZero) {
        FixedWallSpec spec = BaseSpec();
        spec.live = kUselessTile;
        HandModel model = MakeModel(spec);

        ASSERT_TRUE(model.Discard(kSeat0, T(kUselessTile), true).has_value());

        EXPECT_TRUE(model.GetHand(kSeat0).callingDeclared);
        EXPECT_EQ(model.GetHand(kSeat0).callingAtDiscardCount, 0);
        EXPECT_EQ(model.GetHand(kSeat0).discardCount, 1);
        EXPECT_FALSE(model.GetHand(kSeat1).callingDeclared);
    }

    TEST(HandModelTurnTest, ADeclarationOnALaterDiscardStoresTheNumberOfEarlierDiscards) {
        FixedWallSpec spec = LapSpec(kUselessTile);
        HandModel model = MakeModel(spec);
        DiscardEach(model, std::string(kUselessTile) + " 4m 6m 8p");
        ASSERT_EQ(model.CurrentSeat(), kSeat0);

        ASSERT_TRUE(model.Discard(kSeat0, T(kUselessTile), true).has_value());

        EXPECT_TRUE(model.GetHand(kSeat0).callingDeclared);
        EXPECT_EQ(model.GetHand(kSeat0).callingAtDiscardCount, 1);
        EXPECT_EQ(model.GetHand(kSeat0).discardCount, 2);
    }

    TEST(HandModelTurnTest, ASeatThatHasDeclaredCannotDeclareAgain) {
        HandModel model = AfterTheDeclaration("5m");
        ASSERT_EQ(model.CurrentSeat(), kSeat0);
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.Discard(kSeat0, T("5m"), true)), HandError::AlreadyCalling);
        EXPECT_EQ(Snapshot(model), before);
        EXPECT_TRUE(model.Discard(kSeat0, T("5m")).has_value());
        EXPECT_EQ(model.GetHand(kSeat0).callingAtDiscardCount, 0);
    }

    TEST(HandModelTurnTest, ASelfDrawWinAfterTheDeclarationEndsTheHand) {
        HandModel model = AfterTheDeclaration(kWaitingTile);
        ASSERT_EQ(model.CurrentSeat(), kSeat0);

        ASSERT_TRUE(model.DeclareWin(kSeat0).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        const HandResult& result = *model.Result();
        EXPECT_EQ(result.kind, ResultKind::Win);
        EXPECT_EQ(result.winner, kSeat0);
        EXPECT_EQ(result.winningTile, T(kWaitingTile));
        EXPECT_EQ(result.source, WinSource::SelfDraw);
        EXPECT_EQ(result.from, kSeat0);
        EXPECT_FALSE(result.loose);
        EXPECT_FALSE(result.lastLiveTile);
        EXPECT_EQ(Error(model.Discard(kSeat0, T(kWaitingTile))), HandError::WrongPhase);
        EXPECT_EQ(Error(model.DeclareWin(kSeat0)), HandError::WrongPhase);
    }

    TEST(HandModelTurnTest, ASelfDrawWinOnTheLastLiveTileSetsLastLiveTile) {
        FixedWallSpec spec = LapSpec(kWaitingTile);
        spec.liveAfterDeal = 5;
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T(kUselessTile), true).has_value());
        DiscardEach(model, "4m 6m 8p");
        ASSERT_EQ(model.GetWall().LiveCount(), 0u);

        ASSERT_TRUE(model.DeclareWin(kSeat0).has_value());

        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->kind, ResultKind::Win);
        EXPECT_TRUE(model.Result()->lastLiveTile);
    }

    TEST(HandModelTurnTest, ADeclaredSeatWithoutAWinningHandCannotWin) {
        HandModel model = AfterTheDeclaration("5m");
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.DeclareWin(kSeat0)), HandError::CannotWin);
        EXPECT_EQ(Snapshot(model), before);
    }

    TEST(HandModelTurnTest, AWinningHandMayBeDeclined) {
        HandModel model = AfterTheDeclaration(kWaitingTile);

        EXPECT_TRUE(model.Discard(kSeat0, T(kNeededTile)).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_FALSE(model.Result().has_value());
    }

    TEST(HandModelTurnTest, ADrawnBonusTileIsLaidOpenAndReplacedFromTheLooseTiles) {
        FixedWallSpec spec = BaseSpec();
        spec.live = "F1";
        spec.loose = "Gd 3s";
        HandModel model = MakeModel(spec, kSeat0, Wind::East, true);
        const PlayerHand& hand = model.GetHand(kSeat0);

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat0);
        ASSERT_EQ(hand.bonus.size(), 1u);
        EXPECT_EQ(hand.bonus[0], T("F1"));
        EXPECT_EQ(hand.concealed.size(), static_cast<size_t>(kDealtTiles) + 1);
        EXPECT_EQ(hand.CountConcealed(T("Gd")), 1);
        EXPECT_EQ(hand.CountConcealed(T("F1")), 0);
        // The loose tile left the reserve and the live tail refilled it.
        EXPECT_EQ(model.GetWall().LooseCount(), kLooseTiles);
        EXPECT_EQ(model.GetWall().LooseTiles()[0], T("3s"));
    }

    TEST(HandModelTurnTest, AReplacementThatIsABonusTileIsLaidOpenAndReplacedAgain) {
        FixedWallSpec spec = BaseSpec();
        spec.live = "F1";
        spec.loose = "T1 Gd 3s 5s";
        HandModel model = MakeModel(spec, kSeat0, Wind::East, true);
        const PlayerHand& hand = model.GetHand(kSeat0);

        ASSERT_EQ(hand.bonus.size(), 2u);
        EXPECT_EQ(hand.bonus[0], T("F1"));
        EXPECT_EQ(hand.bonus[1], T("T1"));
        EXPECT_EQ(hand.CountConcealed(T("Gd")), 1);
        EXPECT_EQ(hand.concealed.size(), static_cast<size_t>(kDealtTiles) + 1);
        EXPECT_EQ(model.GetPhase(), Phase::Discard);
    }

    TEST(HandModelTurnTest, ASelfDrawWinOnAReplacementTileCountsAsLoose) {
        // East declared, the others discard, East draws a flower and its replacement is the winning tile.
        HandModel model = AfterTheDeclaration("F1", true, std::string(kWaitingTile) + " 3s");
        ASSERT_EQ(model.CurrentSeat(), kSeat0);
        EXPECT_EQ(model.GetHand(kSeat0).bonus.size(), 1u);

        ASSERT_TRUE(model.DeclareWin(kSeat0).has_value());

        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->winningTile, T(kWaitingTile));
        EXPECT_EQ(model.Result()->source, WinSource::SelfDraw);
        EXPECT_TRUE(model.Result()->loose);
    }

    TEST(HandModelTurnTest, ABonusTileDrawnByAnotherSeatIsReplacedToo) {
        FixedWallSpec spec = BaseSpec();
        spec.live = std::string(kUselessTile) + " T2";
        spec.loose = "Gd 3s";
        HandModel model = MakeModel(spec, kSeat0, Wind::East, true);

        ASSERT_TRUE(model.Discard(kSeat0, T(kUselessTile)).has_value());

        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        ASSERT_EQ(model.GetHand(kSeat1).bonus.size(), 1u);
        EXPECT_EQ(model.GetHand(kSeat1).bonus[0], T("T2"));
        EXPECT_EQ(model.GetHand(kSeat1).CountConcealed(T("Gd")), 1);
    }

    TEST(HandModelTurnTest, AnEmptyLiveWallEndsTheHandAsDrawnWhenTheNextDrawIsDue) {
        FixedWallSpec spec = BaseSpec();
        spec.live = std::string(kUselessTile) + " 4m";
        spec.liveAfterDeal = 2;
        HandModel model = MakeModel(spec);
        DiscardEach(model, std::string(kUselessTile) + " 4m");

        // 4m was the last live tile; it is discarded and seat 2 finds nothing to draw.
        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->kind, ResultKind::Drawn);
        EXPECT_EQ(model.Result()->winner, kNoSeat);
        EXPECT_EQ(model.GetHand(kSeat2).concealed.size(), static_cast<size_t>(kDealtTiles));
        EXPECT_EQ(model.Discards().size(), 2u);
        EXPECT_EQ(Error(model.Discard(kSeat2, T("2m"))), HandError::WrongPhase);
    }

    TEST(HandModelTurnTest, TheLastLiveTileIsDrawnAndPlayedBeforeTheHandIsDrawn) {
        FixedWallSpec spec = BaseSpec();
        spec.live = kUselessTile;
        spec.liveAfterDeal = 1;
        HandModel model = MakeModel(spec);
        ASSERT_EQ(model.GetPhase(), Phase::Discard);
        ASSERT_EQ(model.GetWall().LiveCount(), 0u);

        ASSERT_TRUE(model.Discard(kSeat0, T(kUselessTile)).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->kind, ResultKind::Drawn);
    }

    TEST(HandModelTurnTest, AnEmptyLiveWallAtTheStartIsADrawnHand) {
        FixedWallSpec spec = BaseSpec();
        spec.liveAfterDeal = 0;
        const HandModel model = MakeModel(spec);

        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->kind, ResultKind::Drawn);
        EXPECT_EQ(model.GetHand(kSeat0).concealed.size(), static_cast<size_t>(kDealtTiles));
    }

}  // namespace BECore::Mahjong::Tests
