#include "FixedWall.h"

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Tile.h>
#include <gtest/gtest.h>

/**
 * The two kongs of a seat's own turn (the open kong of a claim has `HandModelClaimTest`): the concealed kong, the upgraded kong and
 * its robbing window, and the loose tiles the kongs draw. Fixed walls say what every seat holds and draws. Pure logic: no engine
 * services.
 *
 * The hands below are built so that a discard opens a window only where a test means it to: no two equal tiles in the hands of
 * different seats that a lap could discard, and the laps discard honours.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr Seat kSeat0 = 0;
        constexpr Seat kSeat1 = 1;
        constexpr Seat kSeat2 = 2;
        constexpr Seat kSeat3 = 3;

        constexpr size_t kTilesInHand = static_cast<size_t>(kDealtTiles);
        constexpr size_t kTilesInKong = static_cast<size_t>(kCopiesPerTile);
        constexpr size_t kMeldedByKong = 3;

        // The three quiet hands of the other seats. They hold no pair, and none of the tiles of East's kongs.
        constexpr const char* kQuiet1 = "2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
        constexpr const char* kQuiet2 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ww Nw Rd Wd";
        constexpr const char* kQuiet3 = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ww Nw Rd Gd";

        // East holds three east winds and draws the fourth.
        constexpr const char* kEastKong = "Ew Ew Ew 1p 4p 7p 1s 4s 7s 1m 4m 7m Gd";
        constexpr const char* kEast = "Ew";
        constexpr const char* kLoose = "8p";

        // East waits on the red dragon (a pair wait) with three east winds in hand and declares on its first discard.
        constexpr const char* kWaitingEast = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd";
        constexpr const char* kWaitingTile = "Rd";
        constexpr const char* kFirstDiscard = "9p";

        Tile T(const char* text) {
            return TryParse(text).value();
        }

        FixedWallSpec QuietSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kEastKong;
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
                text += " melds";
                for (const Meld& meld : hand.melds) {
                    text += " " + std::to_string(static_cast<int>(meld.kind)) + std::string(ToString(meld.first)) + std::to_string(meld.concealed);
                }
                text += " bonus " + std::to_string(hand.bonus.size());
                text += " claim " + std::to_string(static_cast<int>(model.GetClaimStatus(seat)));
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

        /// Each seat in turn discards the next tile of `tiles`; the seats in `declaring` declare the calling hand with their discard.
        /// No discard of a lap may open a claim window.
        void Lap(HandModel& model, const std::string& tiles, std::initializer_list<Seat> declaring = {}) {
            const eastl::vector<Tile> parsed = ParseTiles(tiles).value();
            for (const Tile tile : parsed) {
                const Seat seat = model.CurrentSeat();
                const bool declare = eastl::find(declaring.begin(), declaring.end(), seat) != declaring.end();
                const HandStatus status = model.Discard(seat, tile, declare);
                ASSERT_TRUE(status.has_value()) << "seat " << seat << " cannot discard " << ToString(tile);
                ASSERT_EQ(model.GetPhase(), Phase::Discard) << "the discard of " << ToString(tile) << " by seat " << seat << " opened a window";
            }
        }

        /// The tiles of a hand as the model counts them: the concealed ones and three for each meld, so that it is 14 for the seat to
        /// act and 13 for the others whatever the kongs.
        size_t HeldTiles(const PlayerHand& hand) {
            return hand.concealed.size() + kMeldedByKong * hand.melds.size();
        }

        // ----- the concealed kong -----

        // East draws the fourth east wind at once; the loose tile it gets after the kong is `kLoose`.
        FixedWallSpec ConcealedKongSpec() {
            FixedWallSpec spec = QuietSpec();
            spec.live = kEast;
            spec.loose = std::string(kLoose) + " 4s";
            return spec;
        }

        // ----- the upgraded kong -----

        // Seat 1 holds two 5p. East discards a third one and seat 1 claims the pung; after a lap seat 1 draws the fourth.
        constexpr const char* kFifth = "5p";
        constexpr const char* kUpgradeLoose = "9m";

        FixedWallSpec UpgradeSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
            spec.hands[kSeat1] = "5p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            // Seats 2 and 3 wait on the 5p, and declare when a test says so.
            spec.hands[kSeat2] = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Ew Ew";
            spec.hands[kSeat3] = "4p 6p 1m 2m 3m 4s 5s 6s 7m 8m 9m Ww Ww";
            spec.live = std::string(kFifth) + " Rd Gd Wd " + kFifth;
            spec.loose = std::string(kUpgradeLoose) + " 9s";
            return spec;
        }

        /// Seat 1 is to act with the fourth 5p drawn and an open pung of 5p laid; the seats in `declaring` have declared a calling hand
        /// that waits on 5p.
        HandModel UpgradeModel(std::initializer_list<Seat> declaring = {}) {
            HandModel model = MakeModel(UpgradeSpec());
            EXPECT_TRUE(model.Discard(kSeat0, T(kFifth)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            EXPECT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
            Lap(model, "Wd Rd Gd Wd", declaring);
            EXPECT_EQ(model.GetPhase(), Phase::Discard);
            EXPECT_EQ(model.CurrentSeat(), kSeat1);
            EXPECT_EQ(model.GetHand(kSeat1).CountConcealed(T(kFifth)), 1);
            return model;
        }

        // ----- a win on a loose tile -----

        // East declares on its first discard, the others discard, East draws the fourth east wind, declares the kong and draws the red
        // dragon from the reserve.
        FixedWallSpec LooseWinSpec() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kWaitingEast;
            spec.hands[kSeat1] = kQuiet1;
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = kQuiet3;
            spec.live = std::string(kFirstDiscard) + " Wd Gd Nw " + kEast;
            spec.loose = std::string(kWaitingTile) + " Sw 1m";
            return spec;
        }

        HandModel LooseWinModel(std::optional<int> liveAfterDeal = std::nullopt) {
            FixedWallSpec spec = LooseWinSpec();
            spec.liveAfterDeal = liveAfterDeal;
            HandModel model = MakeModel(spec);
            Lap(model, std::string(kFirstDiscard) + " Wd Gd Nw", {kSeat0});
            EXPECT_TRUE(model.GetHand(kSeat0).callingDeclared);
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            EXPECT_EQ(model.GetHand(kSeat0).CountConcealed(T(kEast)), kCopiesPerTile);
            return model;
        }

    }  // namespace

    TEST(HandModelKongTest, AConcealedKongIsAConcealedMeldThenALooseTileThenTheSeatDiscards) {
        HandModel model = MakeModel(ConcealedKongSpec());
        ASSERT_EQ(model.GetPhase(), Phase::Discard);
        ASSERT_EQ(HeldTiles(model.GetHand(kSeat0)), kTilesInHand + 1);
        ASSERT_EQ(model.GetHand(kSeat0).CountConcealed(T(kEast)), kCopiesPerTile);
        const size_t liveBefore = model.GetWall().LiveCount();

        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T(kEast)).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat0);
        const PlayerHand& hand = model.GetHand(kSeat0);
        ASSERT_EQ(hand.melds.size(), 1u);
        EXPECT_EQ(hand.melds[0].kind, MeldKind::Kong);
        EXPECT_TRUE(hand.melds[0].concealed);
        EXPECT_FALSE(hand.melds[0].fromDiscard);
        EXPECT_EQ(hand.melds[0].first, T(kEast));
        EXPECT_EQ(hand.CountConcealed(T(kEast)), 0);
        EXPECT_EQ(hand.concealed.size(), kTilesInHand + 1 - kTilesInKong + 1);  // four tiles left, the loose tile joined
        EXPECT_EQ(hand.CountConcealed(T(kLoose)), 1);
        EXPECT_EQ(HeldTiles(hand), kTilesInHand + 1);            // 14 after the loose draw
        EXPECT_EQ(model.GetWall().LiveCount(), liveBefore - 1);  // the reserve was refilled from the live tail
        EXPECT_EQ(model.GetWall().LooseCount(), kLooseTiles);
        EXPECT_EQ(Error(model.DeclareWin(kSeat0)), HandError::NotCalling);

        ASSERT_TRUE(model.Discard(kSeat0, T(kLoose)).has_value());
        EXPECT_EQ(HeldTiles(model.GetHand(kSeat0)), kTilesInHand);  // 13 after the discard
        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_EQ(HeldTiles(model.GetHand(kSeat1)), kTilesInHand + 1);
    }

    TEST(HandModelKongTest, FourEqualTilesThatAreNotDeclaredStayInTheHandAndMakeNoMeld) {
        HandModel model = MakeModel(ConcealedKongSpec());
        ASSERT_EQ(model.GetHand(kSeat0).CountConcealed(T(kEast)), kCopiesPerTile);

        ASSERT_TRUE(model.Discard(kSeat0, T("Gd")).has_value());

        const PlayerHand& hand = model.GetHand(kSeat0);
        EXPECT_TRUE(hand.melds.empty());
        EXPECT_EQ(hand.CountConcealed(T(kEast)), kCopiesPerTile);
        EXPECT_EQ(hand.concealed.size(), kTilesInHand);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
    }

    TEST(HandModelKongTest, AConcealedKongThatIsNotPossibleIsRefusedAndNothingChanges) {
        HandModel model = MakeModel(ConcealedKongSpec());
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.DeclareConcealedKong(kSeat1, T(kEast))), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.DeclareConcealedKong(-1, T(kEast))), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.DeclareConcealedKong(kSeat0, T("1p"))), HandError::IllegalKong);  // one copy
        EXPECT_EQ(Error(model.DeclareConcealedKong(kSeat0, T("Rd"))), HandError::IllegalKong);  // none
        EXPECT_EQ(Error(model.DeclareConcealedKong(kSeat0, Tile{})), HandError::IllegalKong);
        EXPECT_EQ(Error(model.UpgradeKong(kSeat0, T(kEast))), HandError::IllegalKong);  // four tiles are no pung
        EXPECT_EQ(Snapshot(model), before);
    }

    TEST(HandModelKongTest, ThreeEqualTilesAreNoKongEvenAfterAnotherKongWasDeclared) {
        FixedWallSpec spec = ConcealedKongSpec();
        spec.hands[kSeat0] = "Ew Ew Ew Sw Sw Sw 1p 4p 7p 1s 4s 7s Gd";
        spec.live = "Sw";
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T("Sw")).has_value());
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.DeclareConcealedKong(kSeat0, T(kEast))), HandError::IllegalKong);
        EXPECT_EQ(Snapshot(model), before);
    }

    TEST(HandModelKongTest, AKongOutsideTheDiscardPhaseIsRefused) {
        FixedWallSpec spec = QuietSpec();
        spec.liveAfterDeal = 0;
        HandModel finished = MakeModel(spec);
        ASSERT_EQ(finished.GetPhase(), Phase::Finished);
        const std::string before = Snapshot(finished);

        EXPECT_EQ(Error(finished.DeclareConcealedKong(kSeat0, T(kEast))), HandError::WrongPhase);
        EXPECT_EQ(Error(finished.UpgradeKong(kSeat0, T(kEast))), HandError::WrongPhase);
        EXPECT_EQ(Snapshot(finished), before);
    }

    TEST(HandModelKongTest, TwoConcealedKongsInOneTurnShrinkTheLiveWallByTwo) {
        FixedWallSpec spec = ConcealedKongSpec();
        spec.hands[kSeat0] = "Ew Ew Ew Sw Sw Sw Sw 1p 4p 7p 1s 4s 7s";
        spec.loose = "8p 2p 6s 6m";
        HandModel model = MakeModel(spec);
        const size_t liveBefore = model.GetWall().LiveCount();

        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T(kEast)).has_value());
        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T("Sw")).has_value());

        const PlayerHand& hand = model.GetHand(kSeat0);
        EXPECT_EQ(hand.melds.size(), 2u);
        EXPECT_EQ(hand.concealed.size(), kTilesInHand + 1 - 2 * kTilesInKong + 2);
        EXPECT_EQ(HeldTiles(hand), kTilesInHand + 1);
        EXPECT_EQ(model.GetWall().LiveCount(), liveBefore - 2);
        EXPECT_EQ(model.GetWall().LooseCount(), kLooseTiles);
        EXPECT_EQ(hand.CountConcealed(T("8p")), 1);
        EXPECT_EQ(hand.CountConcealed(T("2p")), 1);
        ASSERT_TRUE(model.Discard(kSeat0, T("8p")).has_value());
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
    }

    TEST(HandModelKongTest, AKongOpensTheWayToAWinOnTheLooseTile) {
        HandModel model = LooseWinModel();

        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T(kEast)).has_value());
        EXPECT_EQ(model.GetHand(kSeat0).CountConcealed(T(kWaitingTile)), 2);  // the pair, completed by the loose tile
        ASSERT_TRUE(model.DeclareWin(kSeat0).has_value());

        ASSERT_TRUE(model.Result().has_value());
        const HandResult& result = *model.Result();
        EXPECT_EQ(result.kind, ResultKind::Win);
        EXPECT_EQ(result.winner, kSeat0);
        EXPECT_EQ(result.winningTile, T(kWaitingTile));
        EXPECT_EQ(result.source, WinSource::SelfDraw);
        EXPECT_EQ(result.from, kSeat0);
        EXPECT_TRUE(result.loose);
        EXPECT_FALSE(result.lastLiveTile);
    }

    TEST(HandModelKongTest, ALooseRefillThatEmptiesTheLiveWallSetsBothLooseAndLastLiveTile) {
        // Five live tiles are drawn, the sixth is the refill that the loose draw of the kong takes from the live tail.
        constexpr int kLiveWithOneRefill = 6;
        HandModel model = LooseWinModel(kLiveWithOneRefill);
        ASSERT_EQ(model.GetWall().LiveCount(), 1u);

        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T(kEast)).has_value());
        ASSERT_EQ(model.GetWall().LiveCount(), 0u);
        ASSERT_TRUE(model.DeclareWin(kSeat0).has_value());

        ASSERT_TRUE(model.Result().has_value());
        EXPECT_TRUE(model.Result()->loose);
        EXPECT_TRUE(model.Result()->lastLiveTile);
    }

    TEST(HandModelKongTest, ALooseTileThatIsABonusTileIsLaidOpenAndReplacedAgain) {
        FixedWallSpec spec = ConcealedKongSpec();
        spec.loose = "F1 8p 4s 5s";
        HandModel model = MakeModel(spec, kSeat0, Wind::East, true);
        const size_t liveBefore = model.GetWall().LiveCount();

        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T(kEast)).has_value());

        const PlayerHand& hand = model.GetHand(kSeat0);
        ASSERT_EQ(hand.bonus.size(), 1u);
        EXPECT_EQ(hand.bonus[0], T("F1"));
        EXPECT_EQ(hand.CountConcealed(T("8p")), 1);
        EXPECT_EQ(HeldTiles(hand), kTilesInHand + 1);
        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.GetWall().LiveCount(), liveBefore - 2);  // two loose draws, two refills from the live tail
        EXPECT_EQ(model.GetWall().LooseTiles()[0], T("4s"));
    }

    TEST(HandModelKongTest, AKongThatFindsTheReserveEmptyEndsTheHandAsDrawn) {
        FixedWallSpec spec = ConcealedKongSpec();
        spec.loose = "F1 F2";
        spec.liveAfterDeal = 1;  // East draws it: no live tile is left to refill the reserve
        HandModel model = MakeModel(spec, kSeat0, Wind::East, true);
        ASSERT_EQ(model.GetPhase(), Phase::Discard);
        ASSERT_EQ(model.GetWall().LiveCount(), 0u);

        ASSERT_TRUE(model.DeclareConcealedKong(kSeat0, T(kEast)).has_value());

        // Both loose tiles are bonus tiles and the third replacement is not there.
        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->kind, ResultKind::Drawn);
        EXPECT_EQ(model.Result()->winner, kNoSeat);
        EXPECT_EQ(model.GetHand(kSeat0).bonus.size(), 2u);
        EXPECT_EQ(model.GetHand(kSeat0).melds.size(), 1u);
        EXPECT_EQ(Error(model.Discard(kSeat0, T("Gd"))), HandError::WrongPhase);
    }

    TEST(HandModelKongTest, AnUpgradedKongNobodyCanRobIsCompletedAtOnce) {
        // Seats 2 and 3 wait on the 5p but have not declared a calling hand.
        HandModel model = UpgradeModel();
        const PlayerHand& hand = model.GetHand(kSeat1);
        ASSERT_EQ(hand.melds.size(), 1u);
        ASSERT_EQ(hand.melds[0].kind, MeldKind::Pung);
        const size_t liveBefore = model.GetWall().LiveCount();

        ASSERT_TRUE(model.UpgradeKong(kSeat1, T(kFifth)).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        ASSERT_EQ(hand.melds.size(), 1u);
        EXPECT_EQ(hand.melds[0].kind, MeldKind::Kong);
        EXPECT_FALSE(hand.melds[0].concealed);
        EXPECT_EQ(hand.melds[0].first, T(kFifth));
        EXPECT_EQ(hand.melds[0].from, kSeat0);  // the pung came from East's discard
        EXPECT_TRUE(hand.melds[0].fromDiscard);
        EXPECT_EQ(hand.CountConcealed(T(kFifth)), 0);
        EXPECT_EQ(hand.CountConcealed(T(kUpgradeLoose)), 1);
        EXPECT_EQ(HeldTiles(hand), kTilesInHand + 1);
        EXPECT_EQ(model.GetWall().LiveCount(), liveBefore - 1);

        ASSERT_TRUE(model.Discard(kSeat1, T(kUpgradeLoose)).has_value());
        EXPECT_EQ(model.CurrentSeat(), kSeat2);
        EXPECT_EQ(HeldTiles(model.GetHand(kSeat1)), kTilesInHand);
    }

    TEST(HandModelKongTest, ASeatMayRefuseToUpgradeAPungAndDiscardInstead) {
        HandModel model = UpgradeModel();

        ASSERT_TRUE(model.Discard(kSeat1, T("8p")).has_value());

        const PlayerHand& hand = model.GetHand(kSeat1);
        ASSERT_EQ(hand.melds.size(), 1u);
        EXPECT_EQ(hand.melds[0].kind, MeldKind::Pung);
        EXPECT_EQ(hand.CountConcealed(T(kFifth)), 1);  // the fourth tile stays in the hand
        EXPECT_EQ(model.CurrentSeat(), kSeat2);
    }

    TEST(HandModelKongTest, AnUpgradeNeedsAnOpenPungOfThatTile) {
        HandModel model = UpgradeModel();
        const std::string before = Snapshot(model);

        EXPECT_EQ(Error(model.UpgradeKong(kSeat2, T(kFifth))), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.UpgradeKong(kSeat1, T("8p"))), HandError::IllegalKong);  // no pung of 8p
        EXPECT_EQ(Error(model.UpgradeKong(kSeat1, Tile{})), HandError::IllegalKong);
        EXPECT_EQ(Error(model.DeclareConcealedKong(kSeat1, T(kFifth))), HandError::IllegalKong);  // one copy, not four
        EXPECT_EQ(Snapshot(model), before);
    }

    TEST(HandModelKongTest, AnUpgradedKongIsRobbedByADeclaredCallingHandThatTheTileCompletes) {
        HandModel model = UpgradeModel({kSeat2});
        ASSERT_TRUE(model.GetHand(kSeat2).callingDeclared);
        ASSERT_FALSE(model.GetHand(kSeat3).callingDeclared);

        ASSERT_TRUE(model.UpgradeKong(kSeat1, T(kFifth)).has_value());

        // The window: only the declared seat is in it, and it may only claim mahjong.
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        EXPECT_EQ(model.PendingTile(), T(kFifth));
        const eastl::vector<Seat> pending = model.PendingClaimSeats();
        ASSERT_EQ(pending.size(), 1u);
        EXPECT_EQ(pending[0], kSeat2);
        const eastl::vector<ClaimOption> options = model.LegalClaims(kSeat2);
        ASSERT_EQ(options.size(), 1u);
        EXPECT_EQ(options[0].kind, ClaimKind::Mahjong);
        EXPECT_TRUE(model.LegalClaims(kSeat3).empty());
        EXPECT_TRUE(model.LegalClaims(kSeat0).empty());
        EXPECT_TRUE(model.LegalClaims(kSeat1).empty());

        const std::string before = Snapshot(model);
        EXPECT_EQ(Error(model.Claim(kSeat2, ClaimKind::Pung)), HandError::IllegalClaim);
        EXPECT_EQ(Error(model.Claim(kSeat2, ClaimKind::Kong)), HandError::IllegalClaim);
        EXPECT_EQ(Error(model.Claim(kSeat2, ClaimKind::Chow, T("4p"))), HandError::IllegalClaim);
        EXPECT_EQ(Error(model.Claim(kSeat3, ClaimKind::Mahjong)), HandError::NotCalling);
        EXPECT_EQ(Error(model.Claim(kSeat3, ClaimKind::Pung)), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Claim(kSeat1, ClaimKind::Mahjong)), HandError::NotYourTurn);  // the owner of the kong
        EXPECT_EQ(Error(model.Pass(kSeat3)), HandError::NotYourTurn);
        EXPECT_EQ(Error(model.Discard(kSeat1, T("8p"))), HandError::WrongPhase);
        EXPECT_EQ(Error(model.DeclareWin(kSeat1)), HandError::WrongPhase);
        EXPECT_EQ(Error(model.UpgradeKong(kSeat1, T(kFifth))), HandError::WrongPhase);
        EXPECT_EQ(Error(model.DeclareConcealedKong(kSeat1, T(kFifth))), HandError::WrongPhase);
        EXPECT_EQ(Snapshot(model), before);

        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Finished);
        ASSERT_TRUE(model.Result().has_value());
        const HandResult& result = *model.Result();
        EXPECT_EQ(result.kind, ResultKind::Win);
        EXPECT_EQ(result.winner, kSeat2);
        EXPECT_EQ(result.winningTile, T(kFifth));
        EXPECT_EQ(result.source, WinSource::RobbedKong);
        EXPECT_EQ(result.from, kSeat1);  // the owner of the kong
        EXPECT_FALSE(result.loose);
        // The tile was taken from the kong: the owner keeps a pung, the winner holds the tile.
        ASSERT_EQ(model.GetHand(kSeat1).melds.size(), 1u);
        EXPECT_EQ(model.GetHand(kSeat1).melds[0].kind, MeldKind::Pung);
        EXPECT_EQ(model.GetHand(kSeat1).CountConcealed(T(kFifth)), 0);
        EXPECT_EQ(model.GetHand(kSeat2).CountConcealed(T(kFifth)), 1);
        EXPECT_EQ(model.GetHand(kSeat2).concealed.size(), kTilesInHand + 1);
    }

    TEST(HandModelKongTest, ARobberThatPassesLetsTheKongStand) {
        HandModel model = UpgradeModel({kSeat2});
        ASSERT_TRUE(model.UpgradeKong(kSeat1, T(kFifth)).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::RobKong);

        ASSERT_TRUE(model.Pass(kSeat2).has_value());

        EXPECT_EQ(model.GetPhase(), Phase::Discard);
        EXPECT_EQ(model.CurrentSeat(), kSeat1);
        ASSERT_EQ(model.GetHand(kSeat1).melds.size(), 1u);
        EXPECT_EQ(model.GetHand(kSeat1).melds[0].kind, MeldKind::Kong);
        EXPECT_EQ(model.GetHand(kSeat1).CountConcealed(T(kUpgradeLoose)), 1);
        EXPECT_TRUE(model.PendingClaimSeats().empty());
        EXPECT_EQ(model.GetClaimStatus(kSeat2), ClaimStatus::NotInWindow);
        EXPECT_FALSE(model.Result().has_value());
    }

    TEST(HandModelKongTest, TwoRobbersResolveToTheSeatFartherFromTheOwnerWhateverTheOrder) {
        for (const bool nearFirst : {true, false}) {
            HandModel model = UpgradeModel({kSeat2, kSeat3});
            ASSERT_TRUE(model.UpgradeKong(kSeat1, T(kFifth)).has_value());
            ASSERT_EQ(model.GetPhase(), Phase::RobKong);
            ASSERT_EQ(model.PendingClaimSeats().size(), 2u);

            const Seat first = nearFirst ? kSeat2 : kSeat3;
            const Seat second = nearFirst ? kSeat3 : kSeat2;
            ASSERT_TRUE(model.Claim(first, ClaimKind::Mahjong).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::RobKong) << "the window waits for the other seat";
            ASSERT_TRUE(model.Claim(second, ClaimKind::Mahjong).has_value());

            ASSERT_EQ(model.GetPhase(), Phase::Finished);
            ASSERT_TRUE(model.Result().has_value());
            EXPECT_EQ(model.Result()->winner, kSeat3) << "nearFirst " << nearFirst;  // seat 3 is two seats from the owner, seat 2 one
            EXPECT_EQ(model.Result()->source, WinSource::RobbedKong);
            EXPECT_EQ(model.Result()->from, kSeat1);
        }
    }

    TEST(HandModelKongTest, OneRobberAndOnePassGiveTheKongToTheRobber) {
        HandModel model = UpgradeModel({kSeat2, kSeat3});
        ASSERT_TRUE(model.UpgradeKong(kSeat1, T(kFifth)).has_value());

        ASSERT_TRUE(model.Claim(kSeat2, ClaimKind::Mahjong).has_value());
        ASSERT_TRUE(model.Pass(kSeat3).has_value());

        ASSERT_TRUE(model.Result().has_value());
        EXPECT_EQ(model.Result()->winner, kSeat2);
        EXPECT_EQ(model.Result()->source, WinSource::RobbedKong);
    }

}  // namespace BECore::Mahjong::Tests
