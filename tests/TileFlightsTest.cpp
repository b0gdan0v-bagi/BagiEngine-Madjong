#include "HandTestSupport.h"

#include <Mahjong/Table/TileFlights.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `Table/TileFlights.h` (pure logic): which slots of the new frame fly and from where. The snapshots are made from `MakeTableLayout`
 * and hand-made melds: a discard of an opponent and of the viewer (a tile held twice, a tile drawn and discarded in one batch), a
 * claimed pung, chow and kong (from the pile slot, and from the discarder's hand when the discard and the claim fall into one batch),
 * the other tiles of the set, a concealed and an upgraded kong, the draw, and the cases that plan nothing. `FlightRect` at its ends.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr float kTolerance = 0.001f;
        constexpr int kHandSize = 13;
        constexpr int kWallTiles = 100;
        constexpr Seat kViewer = 0;

        struct PileItem {
            Seat seat;
            Tile tile;
        };

        /// What a frame shows, in the terms of the test; `Snapshot` turns it into a `TableSnapshot` through the real layout.
        struct Scene {
            Seat viewer = kViewer;
            eastl::array<int, kSeatCount> concealed{kHandSize, kHandSize, kHandSize, kHandSize};
            eastl::array<eastl::vector<Meld>, kSeatCount> melds;
            eastl::vector<PileItem> pile;
            int wall = kWallTiles;
            eastl::vector<Tile> viewerHand;
            Seat drawSeat = kNoSeat;
            int drawIndex = -1;
            float viewWidth = kViewWidth;
            float viewHeight = kViewHeight;
        };

        TableSnapshot Snapshot(const Scene& scene) {
            TableInput input;
            input.viewer = scene.viewer;
            input.concealedCount = scene.concealed;
            if (!scene.viewerHand.empty()) {
                input.concealedCount[scene.viewer] = static_cast<int>(scene.viewerHand.size());
            }
            input.melds = scene.melds;
            input.wallRemaining = scene.wall;
            TableSnapshot snapshot;
            for (const PileItem& item : scene.pile) {
                input.discardSeats.push_back(item.seat);
                snapshot.pile.push_back(item.tile);
            }
            snapshot.layout = MakeTableLayout(input, scene.viewWidth, scene.viewHeight);
            snapshot.viewerHand = scene.viewerHand;
            snapshot.melds = scene.melds;
            snapshot.wallRemaining = scene.wall;
            snapshot.drawSeat = scene.drawSeat;
            snapshot.drawIndex = scene.drawIndex;
            return snapshot;
        }

        Meld MakeMeld(MeldKind kind, const char* first, bool concealed, Seat from = kNoSeat, const char* claimed = nullptr) {
            Meld meld;
            meld.kind = kind;
            meld.concealed = concealed;
            meld.first = T(first);
            meld.from = from;
            meld.fromDiscard = from != kNoSeat;
            if (claimed != nullptr) {
                meld.claimed = T(claimed);
            }
            return meld;
        }

        eastl::vector<Tile> Tiles(std::initializer_list<const char*> texts) {
            eastl::vector<Tile> tiles;
            for (const char* text : texts) {
                tiles.push_back(T(text));
            }
            return tiles;
        }

        HandMove ClaimMove(MoveKind kind, Seat seat, const char* tile, Seat from, int meld) {
            HandMove move;
            move.kind = kind;
            move.seat = seat;
            move.tile = T(tile);
            move.from = from;
            move.meld = meld;
            return move;
        }

        const TileSlot* Find(const TableSnapshot& snapshot, const SlotKey& key) {
            for (const TileSlot& slot : snapshot.layout.slots) {
                if (key.Matches(slot)) {
                    return &slot;
                }
            }
            return nullptr;
        }

        const TileFlight* FlightOf(const eastl::vector<TileFlight>& flights, const SlotKey& key) {
            for (const TileFlight& flight : flights) {
                if (flight.key == key) {
                    return &flight;
                }
            }
            return nullptr;
        }

        /// The middle hand slot of `seat` in `snapshot` (`kHandSize` tiles: index 6).
        Rect MiddleOf(const TableSnapshot& snapshot, Seat seat) {
            int count = 0;
            for (const TileSlot& slot : snapshot.layout.slots) {
                count += slot.role == SlotRole::HandTile && slot.seat == seat ? 1 : 0;
            }
            return Find(snapshot, SlotKey{SlotRole::HandTile, seat, count / 2, 0})->rect;
        }

        Rect SlotRectOf(const TableSnapshot& snapshot, const SlotKey& key) {
            const TileSlot* slot = Find(snapshot, key);
            EXPECT_NE(slot, nullptr);
            return slot != nullptr ? slot->rect : Rect{};
        }

        void ExpectRect(const Rect& actual, const Rect& expected) {
            EXPECT_NEAR(actual.x, expected.x, kTolerance);
            EXPECT_NEAR(actual.y, expected.y, kTolerance);
            EXPECT_NEAR(actual.w, expected.w, kTolerance);
            EXPECT_NEAR(actual.h, expected.h, kTolerance);
        }

        /// A flight must name a slot of the new frame, once.
        void ExpectOnlySlotsOfAfter(const eastl::vector<TileFlight>& flights, const TableSnapshot& after) {
            for (size_t i = 0; i < flights.size(); ++i) {
                EXPECT_NE(Find(after, flights[i].key), nullptr) << "flight " << i;
                for (size_t j = i + 1; j < flights.size(); ++j) {
                    EXPECT_FALSE(flights[i].key == flights[j].key) << "flights " << i << " and " << j << " name one slot";
                }
            }
        }

        SlotKey Discard(Seat seat, int index) {
            return SlotKey{SlotRole::DiscardTile, seat, index, 0};
        }

        SlotKey MeldPart(Seat seat, int meld, int part) {
            return SlotKey{SlotRole::MeldTile, seat, meld, part};
        }

        SlotKey Hand(Seat seat, int index) {
            return SlotKey{SlotRole::HandTile, seat, index, 0};
        }

        /// The part of meld 0 of `seat` in `snapshot` that carries the claimed edge, -1 for none.
        int ClaimedPartOf(const TableSnapshot& snapshot, Seat seat, int meld) {
            for (const TileSlot& slot : snapshot.layout.slots) {
                if (slot.role == SlotRole::MeldTile && slot.seat == seat && slot.index == meld && slot.claimEdge != Edge::None) {
                    return slot.part;
                }
            }
            return -1;
        }

    }  // namespace

    TEST(TileFlightsTest, TheConstantsAreTheOnesOfThePlan) {
        EXPECT_FLOAT_EQ(kTileFlightSeconds, 0.3f);
        EXPECT_EQ(kTileFlightEase, EaseType::CubicOut);
    }

    // -- discards --

    TEST(TileFlightsTest, AnOpponentsDiscardFliesFromTheMiddleOfItsHand) {
        Scene before;
        Scene afterScene = before;
        afterScene.concealed[1] = kHandSize - 1;
        afterScene.pile = {{1, T("5p")}};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(flights.size(), 1u);
        EXPECT_TRUE(flights[0].key == Discard(1, 0));
        ExpectRect(flights[0].from, MiddleOf(beforeSnapshot, 1));
        ExpectOnlySlotsOfAfter(flights, after);
    }

    TEST(TileFlightsTest, AnOpponentAtEachPositionFliesFromItsOwnHand) {
        for (Seat seat = 1; seat < kSeatCount; ++seat) {
            Scene before;
            Scene afterScene = before;
            afterScene.pile = {{seat, T("Ew")}};
            const TableSnapshot beforeSnapshot = Snapshot(before);
            const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
            ASSERT_EQ(flights.size(), 1u) << "seat " << seat;
            ExpectRect(flights[0].from, MiddleOf(beforeSnapshot, seat));
        }
    }

    TEST(TileFlightsTest, TheViewersDiscardLeavesFromTheSlotOfThatTile) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "3p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd", "Rd", "Gd"});
        Scene afterScene = before;
        afterScene.viewerHand = Tiles({"1p", "2p", "3p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd", "Rd"});
        afterScene.pile = {{0, T("Gd")}};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 1u);
        EXPECT_TRUE(flights[0].key == Discard(0, 0));
        ExpectRect(flights[0].from, SlotRectOf(beforeSnapshot, Hand(0, 13)));
    }

    TEST(TileFlightsTest, AMiddleTileOfTheViewersHandLeavesFromItsOwnSlot) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "3p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd", "Rd", "Gd"});
        Scene afterScene = before;
        afterScene.viewerHand = Tiles({"1p", "2p", "3p", "4s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd", "Rd", "Gd"});
        afterScene.pile = {{0, T("5s")}};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 1u);
        ExpectRect(flights[0].from, SlotRectOf(beforeSnapshot, Hand(0, 4)));
    }

    TEST(TileFlightsTest, ATileHeldTwiceLeavesFromTheFirstCopyAndTheSecondDiscardFromTheNext) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "5p", "5p", "6p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd"});
        const TableSnapshot beforeSnapshot = Snapshot(before);

        Scene one = before;
        one.viewerHand = Tiles({"1p", "2p", "5p", "6p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd"});
        one.pile = {{0, T("5p")}};
        const auto single = PlanFlights(beforeSnapshot, Snapshot(one), {});
        ASSERT_EQ(single.size(), 1u);
        ExpectRect(single[0].from, SlotRectOf(beforeSnapshot, Hand(0, 2)));

        // two discards of the tile in one batch use the two copies, not one twice
        Scene two = before;
        two.viewerHand = Tiles({"1p", "2p", "6p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd"});
        two.pile = {{0, T("5p")}, {0, T("5p")}};
        const auto both = PlanFlights(beforeSnapshot, Snapshot(two), {});
        ASSERT_EQ(both.size(), 2u);
        ExpectRect(both[0].from, SlotRectOf(beforeSnapshot, Hand(0, 2)));
        ExpectRect(both[1].from, SlotRectOf(beforeSnapshot, Hand(0, 3)));
    }

    TEST(TileFlightsTest, ATileDrawnAndDiscardedInOneBatchLeavesFromTheMiddleOfTheHand) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "3p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd", "Rd"});
        Scene afterScene = before;
        afterScene.pile = {{0, T("Gd")}};  // not in the old hand: drawn and discarded between the two frames
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 1u);
        ExpectRect(flights[0].from, MiddleOf(beforeSnapshot, 0));
    }

    TEST(TileFlightsTest, TwoDiscardsInOneBatchBothFly) {
        Scene before;
        before.pile = {{3, T("1s")}};
        Scene afterScene = before;
        afterScene.pile = {{3, T("1s")}, {0, T("4p")}, {1, T("9s")}};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 2u);
        EXPECT_TRUE(flights[0].key == Discard(0, 1));
        EXPECT_TRUE(flights[1].key == Discard(1, 2));
        ExpectRect(flights[1].from, MiddleOf(beforeSnapshot, 1));
    }

    // -- claims --

    TEST(TileFlightsTest, AClaimedPungFliesFromThePileSlotAndTheOthersFromTheClaimersHand) {
        Scene before;
        before.pile = {{1, T("5p")}};
        Scene afterScene = before;
        afterScene.pile = {};
        afterScene.concealed[2] = kHandSize - 2;
        afterScene.melds[2] = {MakeMeld(MeldKind::Pung, "5p", false, 1, "5p")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const HandMove move = ClaimMove(MoveKind::ClaimPung, 2, "5p", 1, 0);
        const auto flights = PlanFlights(beforeSnapshot, after, {&move, 1});
        ASSERT_EQ(flights.size(), 3u);
        const int claimed = ClaimedPartOf(after, 2, 0);
        ASSERT_GE(claimed, 0);
        for (int part = 0; part < 3; ++part) {
            const TileFlight* flight = FlightOf(flights, MeldPart(2, 0, part));
            ASSERT_NE(flight, nullptr) << "part " << part;
            ExpectRect(flight->from, part == claimed ? SlotRectOf(beforeSnapshot, Discard(1, 0)) : MiddleOf(beforeSnapshot, 2));
        }
        ExpectOnlySlotsOfAfter(flights, after);
    }

    TEST(TileFlightsTest, AClaimedChowFliesTheClaimedTileFromThePileAndTheOtherTwoFromTheHand) {
        Scene before;
        before.pile = {{2, T("9s")}, {1, T("4p")}};
        Scene afterScene = before;
        afterScene.pile = {{2, T("9s")}};
        afterScene.concealed[2] = kHandSize - 2;
        afterScene.melds[2] = {MakeMeld(MeldKind::Chow, "4p", false, 1, "4p")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(flights.size(), 3u);
        EXPECT_EQ(ClaimedPartOf(after, 2, 0), 0);
        ExpectRect(FlightOf(flights, MeldPart(2, 0, 0))->from, SlotRectOf(beforeSnapshot, Discard(1, 1)));
        ExpectRect(FlightOf(flights, MeldPart(2, 0, 1))->from, MiddleOf(beforeSnapshot, 2));
        ExpectRect(FlightOf(flights, MeldPart(2, 0, 2))->from, MiddleOf(beforeSnapshot, 2));
    }

    TEST(TileFlightsTest, AClaimedChowWithTheClaimedTileInTheMiddleMarksThatPart) {
        Scene before;
        before.pile = {{1, T("5p")}};
        Scene afterScene = before;
        afterScene.pile = {};
        afterScene.melds[2] = {MakeMeld(MeldKind::Chow, "4p", false, 1, "5p")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(flights.size(), 3u);
        EXPECT_EQ(ClaimedPartOf(after, 2, 0), 1);
        ExpectRect(FlightOf(flights, MeldPart(2, 0, 1))->from, SlotRectOf(beforeSnapshot, Discard(1, 0)));
        ExpectRect(FlightOf(flights, MeldPart(2, 0, 0))->from, MiddleOf(beforeSnapshot, 2));
    }

    TEST(TileFlightsTest, AClaimedKongFliesFourTilesOneFromThePile) {
        Scene before;
        before.pile = {{3, T("Rd")}};
        Scene afterScene = before;
        afterScene.pile = {};
        afterScene.concealed[1] = kHandSize - 3;
        afterScene.melds[1] = {MakeMeld(MeldKind::Kong, "Rd", false, 3, "Rd")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(flights.size(), 4u);
        const int claimed = ClaimedPartOf(after, 1, 0);
        ASSERT_GE(claimed, 0);
        for (int part = 0; part < 4; ++part) {
            ExpectRect(FlightOf(flights, MeldPart(1, 0, part))->from, part == claimed ? SlotRectOf(beforeSnapshot, Discard(3, 0)) : MiddleOf(beforeSnapshot, 1));
        }
    }

    TEST(TileFlightsTest, TheViewersClaimTakesItsOtherTilesFromTheCopiesInItsHand) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "5p", "5p", "6s", "7s", "8s", "3m", "4m", "5m", "Ew", "Ew", "Rd"});
        before.pile = {{3, T("5p")}};
        Scene afterScene = before;
        afterScene.viewerHand = Tiles({"1p", "2p", "6s", "7s", "8s", "3m", "4m", "5m", "Ew", "Ew", "Rd"});
        afterScene.pile = {};
        afterScene.melds[0] = {MakeMeld(MeldKind::Pung, "5p", false, 3, "5p")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(flights.size(), 3u);
        const int claimed = ClaimedPartOf(after, 0, 0);
        ASSERT_GE(claimed, 0);
        int fromHand = 0;
        for (int part = 0; part < 3; ++part) {
            const TileFlight* flight = FlightOf(flights, MeldPart(0, 0, part));
            ASSERT_NE(flight, nullptr);
            if (part == claimed) {
                ExpectRect(flight->from, SlotRectOf(beforeSnapshot, Discard(3, 0)));
            } else {
                ExpectRect(flight->from, SlotRectOf(beforeSnapshot, Hand(0, 2 + fromHand)));
                ++fromHand;
            }
        }
    }

    TEST(TileFlightsTest, ADiscardAndItsClaimInOneBatchFlyTheClaimedTileFromTheDiscardersHand) {
        Scene before;  // the pile is empty: seat 1 discards the 5p and seat 2 pungs it before the next frame
        Scene afterScene = before;
        afterScene.concealed[2] = kHandSize - 2;
        afterScene.melds[2] = {MakeMeld(MeldKind::Pung, "5p", false, 1, "5p")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const HandMove move = ClaimMove(MoveKind::ClaimPung, 2, "5p", 1, 0);
        const auto flights = PlanFlights(beforeSnapshot, after, {&move, 1});
        ASSERT_EQ(flights.size(), 3u);
        const int claimed = ClaimedPartOf(after, 2, 0);
        for (int part = 0; part < 3; ++part) {
            ExpectRect(FlightOf(flights, MeldPart(2, 0, part))->from, part == claimed ? MiddleOf(beforeSnapshot, 1) : MiddleOf(beforeSnapshot, 2));
        }
    }

    TEST(TileFlightsTest, AClaimAndTheClaimersDiscardInOneBatchFlyAtTheSameTime) {
        // a pending discard of seat 1 is claimed by seat 2, which then discards the 9s: the pile still has one entry, but not the same one
        Scene before;
        before.pile = {{1, T("5p")}};
        Scene afterScene = before;
        afterScene.pile = {{2, T("9s")}};
        afterScene.concealed[2] = kHandSize - 2;
        afterScene.melds[2] = {MakeMeld(MeldKind::Pung, "5p", false, 1, "5p")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const HandMove moves[] = {ClaimMove(MoveKind::ClaimPung, 2, "5p", 1, 0)};
        const auto flights = PlanFlights(beforeSnapshot, after, moves);
        ASSERT_EQ(flights.size(), 4u);
        const int claimed = ClaimedPartOf(after, 2, 0);
        ExpectRect(FlightOf(flights, MeldPart(2, 0, claimed))->from, SlotRectOf(beforeSnapshot, Discard(1, 0)));
        const TileFlight* discard = FlightOf(flights, Discard(2, 0));
        ASSERT_NE(discard, nullptr);
        ExpectRect(discard->from, MiddleOf(beforeSnapshot, 2));
        ExpectOnlySlotsOfAfter(flights, after);
    }

    TEST(TileFlightsTest, AClaimWithoutItsMoveNamesTheDiscarderFromTheMeld) {
        Scene before;
        Scene afterScene = before;
        afterScene.melds[3] = {MakeMeld(MeldKind::Pung, "Wd", false, 2, "Wd")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(flights.size(), 3u);
        ExpectRect(FlightOf(flights, MeldPart(3, 0, ClaimedPartOf(after, 3, 0)))->from, MiddleOf(beforeSnapshot, 2));
    }

    TEST(TileFlightsTest, AMoveOfAnotherSeatDoesNotRenameTheDiscarder) {
        Scene before;
        Scene afterScene = before;
        afterScene.melds[2] = {MakeMeld(MeldKind::Pung, "5p", false, 1, "5p")};
        const HandMove other = ClaimMove(MoveKind::ClaimPung, 3, "5p", 0, 0);
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {&other, 1});
        ExpectRect(FlightOf(flights, MeldPart(2, 0, ClaimedPartOf(after, 2, 0)))->from, MiddleOf(beforeSnapshot, 1));
    }

    // -- kongs --

    TEST(TileFlightsTest, AConcealedKongFliesAllFourTilesFromTheOwnersHand) {
        Scene before;
        Scene afterScene = before;
        afterScene.concealed[1] = kHandSize - 4;
        afterScene.melds[1] = {MakeMeld(MeldKind::Kong, "7s", true)};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(flights.size(), 4u);
        for (int part = 0; part < 4; ++part) {
            const TileFlight* flight = FlightOf(flights, MeldPart(1, 0, part));
            ASSERT_NE(flight, nullptr);
            ExpectRect(flight->from, MiddleOf(beforeSnapshot, 1));
        }
    }

    TEST(TileFlightsTest, TheViewersConcealedKongTakesTheFourCopiesFromItsHand) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "3p", "7s", "7s", "7s", "7s", "4m", "5m", "6m", "Ew", "Ew", "Rd"});
        Scene afterScene = before;
        afterScene.viewerHand = Tiles({"1p", "2p", "3p", "4m", "5m", "6m", "Ew", "Ew", "Rd"});
        afterScene.melds[0] = {MakeMeld(MeldKind::Kong, "7s", true)};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 4u);
        for (int part = 0; part < 4; ++part) {
            ExpectRect(FlightOf(flights, MeldPart(0, 0, part))->from, SlotRectOf(beforeSnapshot, Hand(0, 3 + part)));
        }
    }

    TEST(TileFlightsTest, AnUpgradedKongFliesItsOneNewTile) {
        Scene before;
        before.melds[2] = {MakeMeld(MeldKind::Pung, "5p", false, 1, "5p")};
        Scene afterScene = before;
        afterScene.melds[2] = {MakeMeld(MeldKind::Kong, "5p", false, 1, "5p")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(flights.size(), 1u);
        EXPECT_TRUE(flights[0].key == MeldPart(2, 0, 3));
        ExpectRect(flights[0].from, MiddleOf(beforeSnapshot, 2));
    }

    TEST(TileFlightsTest, TheViewersUpgradedKongTakesTheTileFromItsHand) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "3p", "5p", "4m", "5m", "6m", "Ew", "Ew", "Rd", "Rd", "Gd", "Gd"});
        before.melds[0] = {MakeMeld(MeldKind::Pung, "5p", false, 2, "5p")};
        Scene afterScene = before;
        afterScene.viewerHand = Tiles({"1p", "2p", "3p", "4m", "5m", "6m", "Ew", "Ew", "Rd", "Rd", "Gd", "Gd"});
        afterScene.melds[0] = {MakeMeld(MeldKind::Kong, "5p", false, 2, "5p")};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 1u);
        ExpectRect(flights[0].from, SlotRectOf(beforeSnapshot, Hand(0, 3)));
    }

    // -- the draw --

    TEST(TileFlightsTest, TheViewersDrawFliesFromTheLastWallStackToTheDrawnIndex) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "3p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd", "Rd"});
        Scene afterScene = before;
        afterScene.viewerHand = Tiles({"1p", "2p", "3p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd", "Rd", "Gd"});
        afterScene.wall = kWallTiles - 1;
        afterScene.drawSeat = 0;
        afterScene.drawIndex = 13;
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto flights = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(flights.size(), 1u);
        EXPECT_TRUE(flights[0].key == Hand(0, 13));
        ExpectRect(flights[0].from, SlotRectOf(beforeSnapshot, SlotKey{SlotRole::WallStack, 0, WallStackCount(kWallTiles) - 1, 0}));
    }

    TEST(TileFlightsTest, TheDrawnIndexNeedNotBeTheLastSlot) {
        Scene before;
        Scene afterScene = before;
        afterScene.concealed[0] = kHandSize + 1;
        afterScene.wall = kWallTiles - 1;
        afterScene.drawSeat = 0;
        afterScene.drawIndex = 4;
        const auto flights = PlanFlights(Snapshot(before), Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 1u);
        EXPECT_TRUE(flights[0].key == Hand(0, 4));
    }

    TEST(TileFlightsTest, AnOpponentsDrawFliesToItsLastSlot) {
        for (Seat seat = 1; seat < kSeatCount; ++seat) {
            Scene before;
            Scene afterScene = before;
            afterScene.concealed[seat] = kHandSize + 1;
            afterScene.wall = kWallTiles - 1;
            afterScene.drawSeat = seat;
            afterScene.drawIndex = kHandSize;
            const TableSnapshot beforeSnapshot = Snapshot(before);
            const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
            ASSERT_EQ(flights.size(), 1u) << "seat " << seat;
            EXPECT_TRUE(flights[0].key == Hand(seat, kHandSize));
            ExpectRect(flights[0].from, SlotRectOf(beforeSnapshot, SlotKey{SlotRole::WallStack, 0, WallStackCount(kWallTiles) - 1, 0}));
        }
    }

    TEST(TileFlightsTest, ADiscardAndTheNextSeatsDrawInOneBatchBothFly) {
        Scene before;
        before.concealed[1] = kHandSize + 1;
        Scene afterScene = before;
        afterScene.concealed[1] = kHandSize;
        afterScene.concealed[2] = kHandSize + 1;
        afterScene.pile = {{1, T("3s")}};
        afterScene.wall = kWallTiles - 1;
        afterScene.drawSeat = 2;
        afterScene.drawIndex = kHandSize;
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 2u);
        EXPECT_NE(FlightOf(flights, Discard(1, 0)), nullptr);
        EXPECT_NE(FlightOf(flights, Hand(2, kHandSize)), nullptr);
    }

    TEST(TileFlightsTest, NoDrawFlightWhenTheWallDidNotShrinkOrNobodyDrew) {
        Scene before;
        Scene sameWall = before;
        sameWall.concealed[1] = kHandSize + 1;
        sameWall.drawSeat = 1;
        sameWall.drawIndex = kHandSize;
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(sameWall), {}).empty());

        Scene nobody = sameWall;
        nobody.wall = kWallTiles - 1;
        nobody.drawSeat = kNoSeat;
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(nobody), {}).empty());

        Scene grown = sameWall;
        grown.wall = kWallTiles + 2;
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(grown), {}).empty());
    }

    TEST(TileFlightsTest, NoDrawFlightWhenTheOldFrameShowsNoWall) {
        Scene before;
        Scene afterScene = before;
        afterScene.concealed[1] = kHandSize + 1;
        afterScene.wall = kWallTiles - 1;
        afterScene.drawSeat = 1;
        afterScene.drawIndex = kHandSize;
        TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        ASSERT_EQ(PlanFlights(beforeSnapshot, after, {}).size(), 1u);
        for (auto it = beforeSnapshot.layout.slots.begin(); it != beforeSnapshot.layout.slots.end();) {
            it = it->role == SlotRole::WallStack ? beforeSnapshot.layout.slots.erase(it) : it + 1;
        }
        EXPECT_TRUE(PlanFlights(beforeSnapshot, after, {}).empty());
    }

    // -- nothing to plan --

    TEST(TileFlightsTest, NothingNewPlansNothing) {
        Scene scene;
        scene.pile = {{1, T("5p")}, {2, T("9s")}};
        scene.melds[3] = {MakeMeld(MeldKind::Pung, "Wd", false, 2, "Wd")};
        scene.viewerHand = Tiles({"1p", "2p", "3p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew", "Rd", "Rd"});
        const TableSnapshot snapshot = Snapshot(scene);
        EXPECT_TRUE(PlanFlights(snapshot, snapshot, {}).empty());
    }

    TEST(TileFlightsTest, AShorterTableOrAHandThatShrinksPlansNothing) {
        Scene before;
        before.pile = {{1, T("5p")}};
        before.melds[2] = {MakeMeld(MeldKind::Pung, "Wd", false, 1, "Wd")};
        Scene afterScene = before;
        afterScene.pile = {};
        afterScene.melds[2] = {};
        afterScene.concealed[1] = kHandSize - 1;
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(afterScene), {}).empty());
    }

    TEST(TileFlightsTest, AnEmptyOldFrameAnotherViewerOrAnotherViewSizePlanNothing) {
        Scene before;
        Scene afterScene = before;
        afterScene.pile = {{1, T("5p")}};

        const TableSnapshot empty;
        EXPECT_TRUE(PlanFlights(empty, Snapshot(afterScene), {}).empty());

        Scene otherViewer = afterScene;
        otherViewer.viewer = 2;
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(otherViewer), {}).empty());

        Scene otherSize = afterScene;
        otherSize.viewWidth = 1920.0f;
        otherSize.viewHeight = 1080.0f;
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(otherSize), {}).empty());

        Scene narrower = afterScene;
        narrower.viewWidth = 800.0f;
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(narrower), {}).empty());

        EXPECT_EQ(PlanFlights(Snapshot(before), Snapshot(afterScene), {}).size(), 1u);
    }

    TEST(TileFlightsTest, ATileWithNoHandSlotInTheOldFrameDoesNotFly) {
        Scene before;
        before.concealed[1] = 0;
        Scene afterScene = before;
        afterScene.pile = {{1, T("5p")}};
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(afterScene), {}).empty());
    }

    TEST(TileFlightsTest, AViewerWhoseHandIsHiddenFliesFromTheMiddleOfIt) {
        Scene before;  // viewerHand empty: the hand is not face up, so no tile is known
        Scene afterScene = before;
        afterScene.pile = {{0, T("5p")}};
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 1u);
        ExpectRect(flights[0].from, MiddleOf(beforeSnapshot, 0));
    }

    TEST(TileFlightsTest, EveryFlightNamesASlotOfTheNewFrameAndTheResultIsDeterministic) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "5p", "5p", "6s", "7s", "8s", "3m", "4m", "5m", "Ew", "Ew", "Rd"});
        before.pile = {{3, T("5p")}, {1, T("2s")}};
        Scene afterScene = before;
        afterScene.viewerHand = Tiles({"1p", "2p", "6s", "7s", "8s", "3m", "4m", "5m", "Ew", "Ew", "Rd"});
        afterScene.pile = {{3, T("5p")}, {1, T("2s")}, {2, T("9s")}};
        afterScene.melds[0] = {MakeMeld(MeldKind::Pung, "5p", false, 3, "5p")};
        afterScene.melds[1] = {MakeMeld(MeldKind::Kong, "Rd", true)};
        afterScene.wall = kWallTiles - 2;
        afterScene.drawSeat = 3;
        afterScene.drawIndex = kHandSize;
        afterScene.concealed[3] = kHandSize + 1;
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto first = PlanFlights(beforeSnapshot, after, {});
        const auto second = PlanFlights(beforeSnapshot, after, {});
        ASSERT_FALSE(first.empty());
        ExpectOnlySlotsOfAfter(first, after);
        ASSERT_EQ(first.size(), second.size());
        for (size_t i = 0; i < first.size(); ++i) {
            EXPECT_TRUE(first[i].key == second[i].key);
            ExpectRect(first[i].from, second[i].from);
        }
    }

    // -- FlightRect --

    TEST(TileFlightsTest, FlightRectRunsFromTheSourceToTheTarget) {
        TileFlight flight;
        flight.from = Rect(10.0f, 20.0f, 28.0f, 37.0f);
        const Rect to(110.0f, 220.0f, 24.0f, 32.0f);
        ExpectRect(FlightRect(flight, to, 0.0f), flight.from);
        ExpectRect(FlightRect(flight, to, 1.0f), to);
        ExpectRect(FlightRect(flight, to, 0.5f), Rect(60.0f, 120.0f, 26.0f, 34.5f));
        ExpectRect(FlightRect(flight, to, 0.25f), Rect(35.0f, 70.0f, 27.0f, 35.75f));
    }

    TEST(TileFlightsTest, FlightRectClampsTheProgress) {
        TileFlight flight;
        flight.from = Rect(0.0f, 0.0f, 10.0f, 10.0f);
        const Rect to(100.0f, 50.0f, 20.0f, 20.0f);
        ExpectRect(FlightRect(flight, to, -3.0f), flight.from);
        ExpectRect(FlightRect(flight, to, 7.0f), to);
        ExpectRect(FlightRect(flight, to, std::nanf("")), flight.from);
    }

}  // namespace BECore::Mahjong::Tests
