#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Table/CallingMarks.h>
#include <gtest/gtest.h>
#include <set>

/**
 * The discards that keep a declared calling hand waiting (`Table/CallingMarks.h`, pure logic): all false before the declaration, off the
 * viewer's turn, in a window and for no seat; after it exactly the tiles worked out by hand on fixed walls, equal to a brute force on
 * seeded hands (with melds, seven pairs, a hand that has already won) and the same for models that differ only in what the viewer cannot see.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // Seat 0 waits on the 5p (4p 6p, a pair of south winds) and so do the others (`AllDeclaredModel` of the table test).
        constexpr const char* kWaitingHand0 = "4p 6p 1s 2s 3s 4m 5m 6m 7s 8s 9s Sw Sw";
        constexpr const char* kWaitingHand1 = "4p 6p 1m 2m 3m 4s 5s 6s 7m 8m 9m Nw Nw";
        // Seat 1 pungs the 5p of East and keeps 123s 456m 7s 8s and a pair of west winds: waits on 6s and 9s after its discard.
        constexpr const char* kPungWaitingHand = "5p 5p 1s 2s 3s 4m 5m 6m 7s 8s Ww Ww Wd";

        /// Seat 0 has declared with its first discard (and, with `others`, so have the three others; the honours of the live wall: no window
        /// opens); seat 0 is to act again with `lastDraw` drawn. Without `others` seats 1 to 3 hold other hands and the wall has another tail.
        HandModel DeclaredModel(const char* lastDraw = "9s", bool others = true) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kWaitingHand0;
            if (others) {
                spec.hands[kSeat1] = kWaitingHand1;
                spec.hands[kSeat2] = kSeat2Waits;
                spec.hands[kSeat3] = kSeat3Waits;
                spec.live = std::string("Gd Rd Wd Gd ") + lastDraw + " 1p 2p 3p";
            } else {
                spec.hands[kSeat1] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Nw Ww";
                spec.hands[kSeat2] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Sw Ew Nw Ww";
                spec.hands[kSeat3] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Ew Nw Ww Gd";
                spec.live = std::string("Gd Rd Wd Gd ") + lastDraw + " Ew Ww Nw";
            }
            HandModel model = MakeModel(spec);
            if (others) {
                Lap(model, "Gd Rd Wd Gd", {kSeat0, kSeat1, kSeat2, kSeat3});
            } else {
                Lap(model, "Gd Rd Wd Gd", {kSeat0});
            }
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            return model;
        }

        /// The ids of the tiles that are marked.
        std::set<int> Marked(const TableView& view) {
            std::set<int> marked;
            const auto keeps = KeepsWaitingDiscards(view);
            for (size_t id = 0; id < keeps.size(); ++id) {
                if (keeps[id]) {
                    marked.insert(static_cast<int>(id));
                }
            }
            return marked;
        }

        std::set<int> Ids(std::initializer_list<const char*> tiles) {
            std::set<int> ids;
            for (const char* tile : tiles) {
                ids.insert(T(tile).Id());
            }
            return ids;
        }

        /// What the function should say, worked out another way: one copy of each held tile leaves the hand, `WinningTiles` of what stays.
        std::set<int> BruteForce(const PlayerHand& hand) {
            std::set<int> marked;
            for (const Tile tile : hand.concealed) {
                PlayerHand rest = hand;
                rest.RemoveConcealed(tile);
                if (!WinningTiles(rest).empty()) {
                    marked.insert(tile.Id());
                }
            }
            return marked;
        }

        /// A view of seat 0 on its own turn with `hand` declared (the table is empty: only the hand matters here).
        TableView TurnView(const PlayerHand& hand) {
            TableView view;
            view.seat = kSeat0;
            view.phase = Phase::Discard;
            view.currentSeat = kSeat0;
            view.own = hand;
            view.own.callingDeclared = true;
            view.legal.push_back(HandAction::MakeDeclareWin());
            return view;
        }

        /// A hand of 3m + 2 concealed tiles beside `meldCount` open sets (pungs and chows), made of random sets and a pair, one tile of it
        /// exchanged for a random one half of the time, so that some are waiting after a discard and some are not; no kind above four.
        PlayerHand RandomHand(MahjongRng& rng, int meldCount) {
            PlayerHand hand;
            TileCounts used{};
            const auto take = [&](Tile tile) {
                if (used[tile.Id()] >= kCopiesPerTile) {
                    return false;
                }
                ++used[tile.Id()];
                return true;
            };
            const auto randomSuit = [&] { return static_cast<int>(rng.Below(3)); };
            const auto set = [&](bool meld) {
                for (;;) {
                    const int suit = randomSuit();
                    const bool chow = rng.Below(2) == 0;
                    const int rank = static_cast<int>(rng.Below(chow ? kLastChowStartRank : kRanksPerSuit)) + 1;
                    const Tile first = Tile::Suited(static_cast<TileSuit>(suit), rank);
                    TileCounts before = used;
                    bool ok = true;
                    for (int step = 0; step < kTilesPerSet; ++step) {
                        ok = ok && take(chow ? Tile::Suited(first.Suit(), first.Rank() + step) : first);
                    }
                    if (!ok) {
                        used = before;
                        continue;
                    }
                    if (meld) {
                        hand.melds.push_back({.kind = chow ? MeldKind::Chow : MeldKind::Pung, .concealed = false, .first = first});
                    } else {
                        for (int step = 0; step < kTilesPerSet; ++step) {
                            hand.AddConcealed(chow ? Tile::Suited(first.Suit(), first.Rank() + step) : first);
                        }
                    }
                    return;
                }
            };
            for (int i = 0; i < meldCount; ++i) {
                set(true);
            }
            for (int i = 0; i < kSetsInHand - meldCount; ++i) {
                set(false);
            }
            for (;;) {
                const Tile tile = Tile::Suited(static_cast<TileSuit>(randomSuit()), static_cast<int>(rng.Below(kRanksPerSuit)) + 1);
                if (used[tile.Id()] + 2 <= kCopiesPerTile) {
                    used[tile.Id()] += 2;
                    hand.AddConcealed(tile);
                    hand.AddConcealed(tile);
                    break;
                }
            }
            if (rng.Below(2) == 0) {
                const Tile out = hand.concealed[rng.Below(static_cast<uint32_t>(hand.concealed.size()))];
                const Tile in = Tile::Suited(static_cast<TileSuit>(randomSuit()), static_cast<int>(rng.Below(kRanksPerSuit)) + 1);
                if (used[in.Id()] < kCopiesPerTile) {
                    hand.RemoveConcealed(out);
                    hand.AddConcealed(in);
                }
            }
            return hand;
        }

    }  // namespace

    TEST(CallingMarksTest, NothingIsMarkedBeforeTheDeclaration) {
        FixedWallSpec spec;
        spec.hands[kSeat0] = kWaitingHand0;
        spec.hands[kSeat1] = kQuiet1;
        spec.hands[kSeat2] = kQuiet2;
        spec.hands[kSeat3] = kQuiet3;
        spec.live = "9s";
        const HandModel model = MakeModel(spec);
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_EQ(view.phase, Phase::Discard);
        ASSERT_FALSE(view.own.callingDeclared);
        ASSERT_FALSE(view.legal.empty());
        // The hand of 14 does hold a discard that would leave it waiting (it can declare), but it has not declared
        EXPECT_TRUE(Marked(view).empty());
    }

    TEST(CallingMarksTest, AfterTheDeclarationTheTilesThatLeaveTheHandWaitingAreMarked) {
        // 4p 6p 123s 456m 789s 9s and a pair of south winds: the extra 9s is the only discard that keeps the wait on the 5p
        const HandModel model = DeclaredModel("9s");
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(view.own.callingDeclared);
        ASSERT_EQ(view.own.concealed.size(), 14u);
        EXPECT_EQ(Marked(view), Ids({"9s"}));

        // A tile that is no part of the waiting shape is marked as well; a tile of the shape is not
        const HandModel stray = DeclaredModel("Rd");
        const TableView strayView = MakeTableView(stray, kSeat0);
        EXPECT_EQ(Marked(strayView), Ids({"Rd"}));
        EXPECT_EQ(Marked(strayView), BruteForce(strayView.own));
    }

    TEST(CallingMarksTest, AHandThatHasAlreadyWonHasSeveralDiscardsThatKeepItWaiting) {
        // The 5p completes the hand; discarding it, or a tile of a set that can wait on its own, leaves the 13 waiting
        const HandModel model = DeclaredModel("5p");
        const TableView view = MakeTableView(model, kSeat0);
        ASSERT_TRUE(std::any_of(view.legal.begin(), view.legal.end(), [](const HandAction& action) { return action.kind == ActionKind::DeclareWin; }));
        const std::set<int> marked = Marked(view);
        EXPECT_EQ(marked, BruteForce(view.own));
        for (const char* tile : {"5p", "4p", "6p", "Sw", "1s", "4m"}) {
            EXPECT_TRUE(marked.contains(T(tile).Id())) << tile;
        }
    }

    TEST(CallingMarksTest, AllFalseOffTheViewersTurnInAWindowAndForNoSeat) {
        const HandModel model = DeclaredModel("9s");
        ASSERT_TRUE(MakeTableView(model, kSeat0).own.callingDeclared);

        // The other seats have declared too, but it is not their turn
        for (const Seat seat : {kSeat1, kSeat2, kSeat3}) {
            const TableView view = MakeTableView(model, seat);
            ASSERT_TRUE(view.own.callingDeclared);
            EXPECT_TRUE(Marked(view).empty()) << "seat " << seat;
        }

        // The viewer discards and it is the next seat's turn
        HandModel moved = model;
        ASSERT_TRUE(moved.Discard(kSeat0, T("9s")).has_value());
        ASSERT_EQ(moved.GetPhase(), Phase::Discard);
        EXPECT_TRUE(Marked(MakeTableView(moved, kSeat0)).empty());

        // A claim window and a robbing window: no discard to mark in them
        TableView claim = MakeTableView(model, kSeat0);
        claim.phase = Phase::Claim;
        claim.currentSeat = kSeat1;
        EXPECT_TRUE(Marked(claim).empty());
        claim.phase = Phase::RobKong;
        EXPECT_TRUE(Marked(claim).empty());

        // No actions to choose from, no seat
        TableView idle = MakeTableView(model, kSeat0);
        idle.legal.clear();
        EXPECT_TRUE(Marked(idle).empty());
        EXPECT_TRUE(Marked(TableView{}).empty());
        EXPECT_TRUE(Marked(MakeTableView(model, kNoSeat)).empty());
    }

    TEST(CallingMarksTest, AFinishedHandHasNoMarks) {
        HandModel won = DeclaredModel("5p");
        ASSERT_TRUE(won.DeclareWin(kSeat0).has_value());
        ASSERT_EQ(won.GetPhase(), Phase::Finished);
        EXPECT_TRUE(Marked(MakeTableView(won, kSeat0)).empty());
    }

    TEST(CallingMarksTest, TheMeldsOfTheHandAreCounted) {
        // Seat 1 pungs the 5p of East, keeps 123s 456m 7s 8s and a pair of west winds, declares with its discard of the 6th tile, draws a 5p
        FixedWallSpec spec = OpenPungSpec(kPungWaitingHand);
        HandModel model = MakeModel(spec);
        ASSERT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
        ASSERT_EQ(model.GetPhase(), Phase::Claim);
        ASSERT_TRUE(model.Claim(kSeat1, ClaimKind::Pung).has_value());
        Lap(model, "Wd Rd Gd Wd", {kSeat1});
        ASSERT_EQ(model.CurrentSeat(), kSeat1);

        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(view.own.callingDeclared);
        ASSERT_EQ(view.own.melds.size(), 1u);
        ASSERT_EQ(view.own.concealed.size(), 11u);
        // The drawn 5p is the only discard that leaves 123s 456m 7s 8s and the west pair waiting (on 6s and 9s)
        EXPECT_EQ(Marked(view), Ids({"5p"}));
        EXPECT_EQ(Marked(view), BruteForce(view.own));
    }

    TEST(CallingMarksTest, SevenPairsCountAsWaitingAsEverywhere) {
        PlayerHand hand;
        for (const Tile tile : ParseTiles("Ew Ew Ww Ww Nw Nw Rd Rd Wd Wd Gd Gd Sw 1p").value()) {
            hand.AddConcealed(tile);
        }
        const TableView view = TurnView(hand);
        // Six pairs of honours and the 1p: the 1p goes and the south wind is the single wait; the south wind goes and a suit tile
        // would complete a mixed seven pairs, which is none
        EXPECT_EQ(Marked(view), Ids({"1p"}));
        EXPECT_EQ(Marked(view), BruteForce(view.own));

        // Thirteen orphans: one of each and a spare one that waits on any of the thirteen
        PlayerHand orphans;
        for (const Tile tile : ParseTiles("1p 9p 1s 9s 1m 9m Ew Sw Ww Nw Rd Wd Gd 1p").value()) {
            orphans.AddConcealed(tile);
        }
        const TableView orphanView = TurnView(orphans);
        EXPECT_EQ(Marked(orphanView), BruteForce(orphans));
        EXPECT_TRUE(Marked(orphanView).contains(T("1p").Id()));
    }

    TEST(CallingMarksTest, EqualToABruteForceOnTwentySeededHands) {
        MahjongRng rng(7);
        int waitingHands = 0;
        int withMelds = 0;
        for (int round = 0; round < 20; ++round) {
            const int meldCount = round % 3;
            const PlayerHand hand = RandomHand(rng, meldCount);
            ASSERT_EQ(hand.concealed.size(), static_cast<size_t>(kTilesPerSet * (kSetsInHand - meldCount) + kTilesInPair)) << "round " << round;
            const TableView view = TurnView(hand);
            const std::set<int> marked = Marked(view);
            EXPECT_EQ(marked, BruteForce(view.own)) << "round " << round;
            waitingHands += marked.empty() ? 0 : 1;
            withMelds += (meldCount > 0 && !marked.empty()) ? 1 : 0;
        }
        EXPECT_GT(waitingHands, 5) << "the generator must make hands that wait after a discard";
        EXPECT_GT(withMelds, 0) << "and some with melds";
    }

    TEST(CallingMarksTest, ADeclaredHandIsOnlyAskedAboutItsOwnTiles) {
        // Two models that differ in the hands of the others and in the wall behind the draw give the same answer: no peeking
        const HandModel a = DeclaredModel("Rd", true);
        const HandModel b = DeclaredModel("Rd", false);
        const TableView viewA = MakeTableView(a, kSeat0);
        const TableView viewB = MakeTableView(b, kSeat0);
        ASSERT_EQ(viewA.own.concealed, viewB.own.concealed);
        EXPECT_EQ(KeepsWaitingDiscards(viewA), KeepsWaitingDiscards(viewB));
        EXPECT_EQ(Marked(viewA), Ids({"Rd"}));
    }

}  // namespace BECore::Mahjong::Tests
