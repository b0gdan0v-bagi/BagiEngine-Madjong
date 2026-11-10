#include "HandTestSupport.h"

#include <Mahjong/Table/TileFlights.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `Table/TileFlights.h` (pure logic): which slots of the new frame fly and from where. The snapshots are made from `MakeTableLayout`
 * and hand-made melds: a discard of an opponent and of the viewer (a tile held twice, a tile drawn and discarded in one batch), a
 * claimed pung, chow and kong (from the pile slot, and from the discarder's hand when the discard and the claim fall into one batch),
 * the other tiles of the set, a concealed and an upgraded kong, the draw, and the cases that plan nothing. The turn a flight starts with;
 * `FlightRect` at its ends; `FlightPlacement`: the turn and the size of a tile in the air.
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
            Seat winSeat = kNoSeat;
            int winIndex = -1;
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
            snapshot.winSeat = scene.winSeat;
            snapshot.winIndex = scene.winIndex;
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

    // -- a mahjong on a discard --

    TEST(TileFlightsTest, AMahjongOnADiscardFliesTheWinningTileFromThePileSlotToTheWinnersHandSlotAtEachPosition) {
        for (Seat winner = 0; winner < kSeatCount; ++winner) {
            const Seat discarder = NextSeat(winner);
            Scene before;
            before.pile = {{discarder, T("5p")}};
            Scene afterScene = before;
            afterScene.pile = {};
            afterScene.concealed[winner] = kHandSize + 1;
            afterScene.winSeat = winner;
            afterScene.winIndex = 6;
            const TableSnapshot beforeSnapshot = Snapshot(before);
            const TableSnapshot after = Snapshot(afterScene);
            const auto flights = PlanFlights(beforeSnapshot, after, {});
            ASSERT_EQ(flights.size(), 1u) << "winner " << winner;
            EXPECT_TRUE(flights[0].key == Hand(winner, 6)) << "winner " << winner;
            ExpectRect(flights[0].from, SlotRectOf(beforeSnapshot, Discard(discarder, 0)));
            ExpectOnlySlotsOfAfter(flights, after);
        }
    }

    TEST(TileFlightsTest, AMahjongOnADiscardTakesTheLastPileEntryWhateverTheOtherEntriesAre) {
        Scene before;
        before.pile = {{2, T("1s")}, {3, T("9m")}, {1, T("5p")}};
        Scene afterScene = before;
        afterScene.pile = {{2, T("1s")}, {3, T("9m")}};
        afterScene.concealed[0] = kHandSize + 1;
        afterScene.winSeat = 0;
        afterScene.winIndex = 13;
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 1u);
        EXPECT_TRUE(flights[0].key == Hand(0, 13));
        ExpectRect(flights[0].from, SlotRectOf(beforeSnapshot, Discard(1, 2)));
    }

    TEST(TileFlightsTest, AWinningTileHeldTwiceFliesTheLastCopy) {
        Scene before;
        before.viewerHand = Tiles({"1p", "2p", "3p", "5p", "6p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew"});
        before.pile = {{1, T("5p")}};
        Scene afterScene = before;
        afterScene.pile = {};
        afterScene.viewerHand = Tiles({"1p", "2p", "3p", "5p", "5p", "6p", "4s", "5s", "6s", "7m", "8m", "9m", "Ew", "Ew"});
        afterScene.winSeat = 0;
        afterScene.winIndex = 4;  // the last 5p
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 1u);
        EXPECT_TRUE(flights[0].key == Hand(0, 4));
        ExpectRect(flights[0].from, SlotRectOf(beforeSnapshot, Discard(1, 0)));
    }

    TEST(TileFlightsTest, NoWinFlightWhenBothFramesShowTheWin) {
        Scene before;
        before.pile = {{1, T("5p")}};
        before.concealed[2] = kHandSize + 1;
        before.winSeat = 2;
        before.winIndex = 6;
        Scene afterScene = before;
        afterScene.pile = {};
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(afterScene), {}).empty());
    }

    TEST(TileFlightsTest, NoWinFlightWhenTheOldPileHasNoSuchEntry) {
        Scene before;  // the discard and the win fall into one batch: the pile is empty before
        Scene afterScene = before;
        afterScene.concealed[2] = kHandSize + 1;
        afterScene.winSeat = 2;
        afterScene.winIndex = 6;
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(afterScene), {}).empty());

        // the entry is still in the new pile: it did not leave
        Scene stays = before;
        stays.pile = {{1, T("5p")}};
        Scene staysAfter = stays;
        staysAfter.concealed[2] = kHandSize + 1;
        staysAfter.winSeat = 2;
        staysAfter.winIndex = 6;
        EXPECT_TRUE(PlanFlights(Snapshot(stays), Snapshot(staysAfter), {}).empty());
    }

    TEST(TileFlightsTest, NoWinFlightForASelfDrawOrWithoutAWinner) {
        Scene before;
        before.pile = {{1, T("5p")}};
        Scene afterScene = before;
        afterScene.pile = {};  // the entry is gone (a claimed tile, say) but nobody won on it
        afterScene.concealed[2] = kHandSize + 1;
        EXPECT_TRUE(PlanFlights(Snapshot(before), Snapshot(afterScene), {}).empty());
    }

    TEST(TileFlightsTest, TheOtherSlotsOfTheWinnersHandDoNotFlyAndThePlanIsDeterministic) {
        Scene before;
        before.pile = {{3, T("2m")}};
        Scene afterScene = before;
        afterScene.pile = {};
        afterScene.concealed[1] = kHandSize + 1;
        afterScene.winSeat = 1;
        afterScene.winIndex = 13;
        const TableSnapshot beforeSnapshot = Snapshot(before);
        const TableSnapshot after = Snapshot(afterScene);
        const auto first = PlanFlights(beforeSnapshot, after, {});
        const auto second = PlanFlights(beforeSnapshot, after, {});
        ASSERT_EQ(first.size(), 1u);
        ASSERT_EQ(second.size(), 1u);
        EXPECT_TRUE(first[0].key == second[0].key);
        ExpectRect(first[0].from, second[0].from);
        for (int i = 0; i <= kHandSize; ++i) {
            EXPECT_EQ(FlightOf(first, Hand(1, i)) != nullptr, i == 13) << "slot " << i;
        }
    }

    // -- the turn of the source --

    TEST(TileFlightsTest, AFlightRemembersTheTurnOfTheSlotItLeaves) {
        // A kong of seat 1 (the right seat, turned 270) claimed from the pile of seat 3 (the left seat, turned 90)
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
        ASSERT_FLOAT_EQ(Find(beforeSnapshot, Discard(3, 0))->rotation, kLeftSeatRotation);
        for (int part = 0; part < 4; ++part) {
            EXPECT_FLOAT_EQ(FlightOf(flights, MeldPart(1, 0, part))->fromRotation, part == claimed ? kLeftSeatRotation : kRightSeatRotation) << "part " << part;
        }
    }

    TEST(TileFlightsTest, ADiscardFliesWithTheTurnOfTheHandItLeavesAtEachPosition) {
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            Scene before;
            Scene afterScene = before;
            afterScene.pile = {{seat, T("Ew")}};
            const TableSnapshot beforeSnapshot = Snapshot(before);
            const auto flights = PlanFlights(beforeSnapshot, Snapshot(afterScene), {});
            ASSERT_EQ(flights.size(), 1u) << "seat " << seat;
            const float expected = seat == 1 ? kRightSeatRotation : (seat == 3 ? kLeftSeatRotation : 0.0f);
            EXPECT_FLOAT_EQ(flights[0].fromRotation, expected) << "seat " << seat;
        }
    }

    TEST(TileFlightsTest, ADrawLeavesTheWallUnturned) {
        Scene before;
        Scene afterScene = before;
        afterScene.wall = before.wall - 1;
        afterScene.concealed[3] = kHandSize + 1;
        afterScene.drawSeat = 3;
        afterScene.drawIndex = kHandSize;
        const auto flights = PlanFlights(Snapshot(before), Snapshot(afterScene), {});
        ASSERT_EQ(flights.size(), 1u);
        EXPECT_FLOAT_EQ(flights[0].fromRotation, 0.0f);
    }

    // -- FlightPlacement --

    namespace {

        TileSlot SlotAt(const Rect& rect, float rotation) {
            TileSlot slot;
            slot.rect = rect;
            slot.rotation = rotation;
            return slot;
        }

        TileFlight FlightFrom(const Rect& rect, float rotation) {
            TileFlight flight;
            flight.from = rect;
            flight.fromRotation = rotation;
            return flight;
        }

        void ExpectPlacement(const TilePlacement& actual, const Rect& upright, float rotation, float pivotX, float pivotY) {
            ExpectRect(actual.upright, upright);
            EXPECT_NEAR(actual.rotation, rotation, kTolerance);
            EXPECT_NEAR(actual.pivotX, pivotX, kTolerance);
            EXPECT_NEAR(actual.pivotY, pivotY, kTolerance);
        }

        /// How far apart two turns are, in degrees, the short way: [0, 180].
        float DistanceOf(float a, float b) {
            const float delta = std::fmod(std::fabs(a - b), 360.0f);
            return delta > 180.0f ? 360.0f - delta : delta;
        }

    }  // namespace

    TEST(TileFlightsTest, APlacementIsTheSourceAtZeroAndTheSlotAtOne) {
        // From a left-seat tile (screen 37 x 28, turned 90) to an unturned one
        const TileFlight flight = FlightFrom(Rect(10.0f, 20.0f, 37.0f, 28.0f), kLeftSeatRotation);
        const TileSlot to = SlotAt(Rect(110.0f, 220.0f, 24.0f, 32.0f), 0.0f);
        ExpectPlacement(FlightPlacement(flight, to, 0.0f), UprightRect(flight.from, kLeftSeatRotation), kLeftSeatRotation, 28.5f, 34.0f);
        ExpectPlacement(FlightPlacement(flight, to, 1.0f), to.rect, 0.0f, 122.0f, 236.0f);
        EXPECT_EQ(FlightPlacement(flight, to, 1.0f).rotation, 0.0f) << "landed exactly, not nearly";
        EXPECT_EQ(FlightPlacement(flight, to, 0.0f).rotation, kLeftSeatRotation);
    }

    TEST(TileFlightsTest, AHalfWayTileIsHalfTurnedAndHalfWayBetweenTheUnturnedSizes) {
        const TileFlight flight = FlightFrom(Rect(10.0f, 20.0f, 37.0f, 28.0f), kLeftSeatRotation);
        const TileSlot to = SlotAt(Rect(110.0f, 220.0f, 24.0f, 32.0f), 0.0f);
        // The centre is that of FlightRect (75.25, 135); the unturned size runs 28 x 37 -> 24 x 32; the turn 90 -> 0
        ExpectPlacement(FlightPlacement(flight, to, 0.5f), Rect(75.25f - 13.0f, 135.0f - 17.25f, 26.0f, 34.5f), 45.0f, 75.25f, 135.0f);
    }

    TEST(TileFlightsTest, TheTurnTakesTheShortWayRound) {
        const TileSlot unturned = SlotAt(Rect(100.0f, 100.0f, 24.0f, 32.0f), 0.0f);
        const TileSlot left = SlotAt(Rect(100.0f, 100.0f, 32.0f, 24.0f), kLeftSeatRotation);
        const TileSlot right = SlotAt(Rect(100.0f, 100.0f, 32.0f, 24.0f), kRightSeatRotation);
        const TileFlight fromUnturned = FlightFrom(Rect(0.0f, 0.0f, 24.0f, 32.0f), 0.0f);
        const TileFlight fromLeft = FlightFrom(Rect(0.0f, 0.0f, 32.0f, 24.0f), kLeftSeatRotation);
        const TileFlight fromRight = FlightFrom(Rect(0.0f, 0.0f, 32.0f, 24.0f), kRightSeatRotation);
        // 0 and 270 meet through 315, never through 135
        EXPECT_NEAR(FlightPlacement(fromUnturned, right, 0.5f).rotation, 315.0f, kTolerance);
        EXPECT_NEAR(FlightPlacement(fromRight, unturned, 0.5f).rotation, 315.0f, kTolerance);
        EXPECT_NEAR(FlightPlacement(fromUnturned, right, 0.25f).rotation, 337.5f, kTolerance);
        EXPECT_NEAR(FlightPlacement(fromRight, unturned, 0.75f).rotation, 337.5f, kTolerance);
        EXPECT_NEAR(FlightPlacement(fromUnturned, left, 0.5f).rotation, 45.0f, kTolerance);
        // 90 -> 270 is a half turn either way: it moves evenly
        for (const float t : {0.25f, 0.5f, 0.75f}) {
            const float rotation = FlightPlacement(fromLeft, right, t).rotation;
            EXPECT_NEAR(DistanceOf(rotation, kLeftSeatRotation), 180.0f * t, kTolerance) << "t " << t;
            EXPECT_NEAR(DistanceOf(rotation, kRightSeatRotation), 180.0f * (1.0f - t), kTolerance) << "t " << t;
        }
    }

    TEST(TileFlightsTest, TwoSlotsWithOneTurnGiveTheFlightRectOfToday) {
        for (const float rotation : {0.0f, kLeftSeatRotation, kRightSeatRotation}) {
            const TileFlight flight = FlightFrom(Rect(10.0f, 20.0f, 37.0f, 28.0f), rotation);
            const TileSlot to = SlotAt(Rect(110.0f, 220.0f, 32.0f, 24.0f), rotation);
            for (const float t : {0.0f, 0.1f, 0.5f, 0.9f, 1.0f}) {
                const TilePlacement placement = FlightPlacement(flight, to, t);
                EXPECT_NEAR(placement.rotation, rotation, kTolerance) << "t " << t;
                ExpectRect(placement.upright, UprightRect(FlightRect(flight, to.rect, t), rotation));
            }
        }
    }

    TEST(TileFlightsTest, APlacementClampsTheProgressLikeFlightRect) {
        const TileFlight flight = FlightFrom(Rect(0.0f, 0.0f, 10.0f, 10.0f), kLeftSeatRotation);
        const TileSlot to = SlotAt(Rect(100.0f, 50.0f, 20.0f, 20.0f), 0.0f);
        const TilePlacement start = FlightPlacement(flight, to, 0.0f);
        const TilePlacement end = FlightPlacement(flight, to, 1.0f);
        for (const float t : {-3.0f, std::nanf("")}) {
            ExpectPlacement(FlightPlacement(flight, to, t), start.upright, start.rotation, start.pivotX, start.pivotY);
        }
        ExpectPlacement(FlightPlacement(flight, to, 7.0f), end.upright, end.rotation, end.pivotX, end.pivotY);
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
