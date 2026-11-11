#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/OwnDraws.h>
#include <cmath>
#include <gtest/gtest.h>

/**
 * `ValueDiscards` and `ValueWaits`: a discard rated in points from the seat's own draws, on positions worked out by hand (a comment
 * each). Views are made by hand (`MakeView`) so that the visible copies, the wall and the legal list are exactly what the case says;
 * the no-peeking case goes through two models. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kLongWall = 60;
        constexpr int kDepth = 2;
        constexpr int kPerfectChance = static_cast<int>(kOwnDrawsProbBase);
        constexpr double kChanceTolerance = 1e-4;

        // Two sets, a terminal pung and a tanki wait: discarding 5p leaves "Gd" alone, discarding Gd leaves "5p" alone (3 copies unseen of
        // each). Both fully concealed and declared with the first discard (x4); the pung of 9m is concealed (8 points).
        constexpr const char* kTankiHand = "1p 2p 3p 7p 8p 9p 4s 5s 6s 9m 9m 9m Gd 5p";

        // Two sets and four pairs (a pair of dragons and three pairs of simples, all different suits and ranks): every pair can be broken, and
        // breaking any leaves shanten 1.
        constexpr const char* kPairsHand = "1p 2p 3p 4s 5s 6s Rd Rd 5m 5m 8m 8m 2s 2s";

        // Two sets, 78m and 57p to join, a pair of east winds (East's own wind and the round wind) and two strays: 9s, 2m.
        constexpr const char* kOneFromWaiting = "1p 2p 3p 4s 5s 6s 7m 8m Ew Ew 5p 7p 9s 2m";

        /// East's view on its turn with the 14 tiles `hand`, the tiles `pile` lying open, `live` tiles in the wall. Nothing else is
        /// visible. The listed actions: a plain discard of every kind held, and the declaring one where the 13 left wait.
        TableView MakeView(const char* hand, const char* pile = "", int live = kLongWall) {
            TableView view;
            view.seat = kSeat0;
            view.phase = Phase::Discard;
            view.currentSeat = kSeat0;
            view.seatWind = Wind::East;
            view.roundWind = Wind::East;
            view.own.concealed = ParseTiles(hand).value();
            eastl::sort(view.own.concealed.begin(), view.own.concealed.end());
            for (const Tile tile : view.own.concealed) {
                ++view.visible[tile.Id()];
            }
            const eastl::vector<Tile> open = ParseTiles(pile).value();
            for (const Tile tile : open) {
                ++view.visible[tile.Id()];
            }
            view.liveCount = live;
            TileCounts counts = CountTiles(view.own.concealed);
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (counts[id] == 0) {
                    continue;
                }
                view.legal.push_back(HandAction::MakeDiscard(Tile{id}));
                --counts[id];
                if (IsWaiting(counts, 0, TileCounts{})) {
                    view.legal.push_back(HandAction::MakeDiscard(Tile{id}, true));
                }
                ++counts[id];
            }
            return view;
        }

        const DiscardValue* Find(const eastl::vector<DiscardValue>& values, const char* tile) {
            const auto found = eastl::find_if(values.begin(), values.end(), [tile](const DiscardValue& value) { return value.tile == T(tile); });
            return found == values.end() ? nullptr : &*found;
        }

        void ExpectEqual(const eastl::vector<DiscardValue>& a, const eastl::vector<DiscardValue>& b) {
            ASSERT_EQ(a.size(), b.size());
            for (size_t i = 0; i < a.size(); ++i) {
                EXPECT_EQ(a[i].tile, b[i].tile) << "entry " << i;
                EXPECT_EQ(a[i].winChance, b[i].winChance) << "entry " << i;
                EXPECT_EQ(a[i].meanWinScore, b[i].meanWinScore) << "entry " << i;
                EXPECT_EQ(a[i].valueWhenNotWinning, b[i].valueWhenNotWinning) << "entry " << i;
                EXPECT_EQ(a[i].expected, b[i].expected) << "entry " << i;
            }
        }

    }  // namespace

    TEST(OwnDrawsTest, AValuePungOutranksASimplePairAtTheSameRating) {
        // Hand: 123p 456s and four pairs (Rd Rd, 5m 5m, 8m 8m, 2s 2s). Every pair kind has 2 copies unseen. Breaking any pair leaves 2 sets and
        // 3 pairs: shanten 1, accepted by the other three pairs, 3 x 2 = 6 copies, whichever pair is broken: the ratings are equal and
        // `RateDiscards` only breaks the tie by `KeepValue` (the dragon last). A win built on the pair of dragons is a pung of dragons: the
        // evaluator must see it is worth more than a simple pung even though the ratings agree.
        const TableView view = MakeView(kPairsHand);
        TileCounts counts = CountTiles(view.own.concealed);

        const eastl::vector<DiscardOption> rating = RateDiscards(counts, 0, view);
        const eastl::vector<DiscardValue> values = ValueDiscards(view, kDepth);

        ASSERT_GE(rating.size(), 4u);
        EXPECT_EQ(rating[0].acceptance, rating[3].acceptance);
        ASSERT_FALSE(values.empty());
        EXPECT_NE(values.front().tile, T("Rd"));
        const DiscardValue* dragon = Find(values, "Rd");
        const DiscardValue* simple = Find(values, "5m");
        ASSERT_NE(dragon, nullptr);
        ASSERT_NE(simple, nullptr);
        EXPECT_GT(simple->expected, dragon->expected);
        EXPECT_GT(simple->meanWinScore, dragon->meanWinScore);
        // The chances to win are equal: the same number of copies is accepted either way.
        EXPECT_EQ(simple->winChance, dragon->winChance);
    }

    TEST(OwnDrawsTest, TheEvaluatorKeepsTheValuePungWhereTheRatingBreaksIt) {
        // The same hand with one Rd lying in the pile: the pair of dragons has 1 copy unseen. Breaking a simple pair is accepted by
        // Rd 1 + two pairs of 2 = 5 copies, breaking the dragon pair by 2 + 2 + 2 = 6: `RateDiscards` puts the dragon first. The pung of
        // dragons, if drawn, scores more than twice a simple one, which more than pays for the one copy: the evaluator breaks a simple pair.
        const TableView view = MakeView(kPairsHand, "Rd");
        TileCounts counts = CountTiles(view.own.concealed);

        const eastl::vector<DiscardOption> rating = RateDiscards(counts, 0, view);
        const eastl::vector<DiscardValue> values = ValueDiscards(view, kDepth);

        ASSERT_FALSE(rating.empty());
        EXPECT_EQ(rating.front().tile, T("Rd"));
        ASSERT_FALSE(values.empty());
        EXPECT_NE(values.front().tile, T("Rd"));
        const DiscardValue* dragon = Find(values, "Rd");
        ASSERT_NE(dragon, nullptr);
        EXPECT_GT(Find(values, "5m")->winChance, 0);
        EXPECT_GT(values.front().expected, dragon->expected);
        // The dragon discard wins more often (6 copies against 5) and the kept dragon pung wins for more.
        EXPECT_GT(dragon->winChance, values.front().winChance);
        EXPECT_GT(values.front().meanWinScore, dragon->meanWinScore);
    }

    TEST(OwnDrawsTest, TheDearerWaitIsRankedFirst) {
        // Hand: 123p 789p 456s, a concealed pung of 9m and two strays Gd, 5p. Discarding 5p waits on Gd alone, discarding Gd on 5p alone;
        // 3 copies of each are unseen. Points of a win: 20 win + 2 self-draw + 2 only winning tile + 8 concealed terminal pung = 32, and the pair
        // of dragons adds 2 for Gd = 34. Doubles: fully concealed and the declaration with the first discard (the view has made none): x4.
        // 32 x 4 = 128 -> 130, 34 x 4 = 136 -> 140.
        const eastl::vector<DiscardValue> waits = ValueWaits(MakeView(kTankiHand));

        ASSERT_EQ(waits.size(), 2u);
        EXPECT_EQ(waits[0].tile, T("5p"));  // discarding 5p keeps the wait on Gd
        EXPECT_EQ(waits[0].meanWinScore, 140);
        EXPECT_EQ(waits[1].tile, T("Gd"));
        EXPECT_EQ(waits[1].meanWinScore, 130);
        EXPECT_EQ(waits[0].winChance, waits[1].winChance);
        EXPECT_GT(waits[0].expected, waits[1].expected);
    }

    TEST(OwnDrawsTest, TheChanceOfAOneWaitHandIsTheChanceOfTheCopiesOverTheDrawsLeft) {
        // Discarding 5p leaves a wait on Gd alone: 3 copies unseen of the 136 - 14 = 122 tiles the view cannot see, so a draw wins with
        // p = 3 / 122. The wall of 40 tiles gives the seat 10 draws; with no declared opponent the chance that nobody else has won
        // before draw i is (1 - 15/1000)^(i - 1). The chance to win within the draws is the sum over i of that x (1 - p)^(i - 1) x p.
        constexpr int kWall = 40;
        constexpr int kDraws = kWall / kSeatCount;
        const double p = 3.0 / 122.0;
        const double alive = 1.0 - kOpponentWinPerRoundPermille[0] / static_cast<double>(kPermilleBase);
        double expected = 0;
        for (int i = 1; i <= kDraws; ++i) {
            expected += std::pow(alive * (1.0 - p), i - 1) * p;
        }

        const eastl::vector<DiscardValue> values = ValueDiscards(MakeView(kTankiHand, "", kWall), kDepth);

        const DiscardValue* tanki = Find(values, "5p");
        ASSERT_NE(tanki, nullptr);
        EXPECT_NEAR(tanki->winChance / static_cast<double>(kOwnDrawsProbBase), expected, kChanceTolerance);
        EXPECT_EQ(tanki->meanWinScore, 140);
    }

    TEST(OwnDrawsTest, DeclaredOpponentsLowerTheChanceToWin) {
        TableView quiet = MakeView(kTankiHand);
        TableView busy = quiet;
        busy.seats[kSeat1].callingDeclared = true;
        busy.seats[kSeat2].callingDeclared = true;

        const eastl::vector<DiscardValue> quietValues = ValueDiscards(quiet, kDepth);
        const eastl::vector<DiscardValue> busyValues = ValueDiscards(busy, kDepth);

        ASSERT_NE(Find(quietValues, "5p"), nullptr);
        ASSERT_NE(Find(busyValues, "5p"), nullptr);
        EXPECT_LT(Find(busyValues, "5p")->winChance, Find(quietValues, "5p")->winChance);
    }

    TEST(OwnDrawsTest, TwoTablesThatDifferOnlyInHiddenTilesGiveTheSameValues) {
        // East holds the same 13 tiles and draws the same tile; the other hands and the rest of the live wall differ.
        const char* east = "1p 2p 3p 4s 5s 6s 7m 8m Ew Ew 5p 7p 9s";
        FixedWallSpec first = SpecOf(east, "2m");
        FixedWallSpec second = SpecOf(east, "2m Rd Rd Rd");
        second.hands[kSeat1] = kQuiet3;
        second.hands[kSeat2] = kQuiet1;
        second.hands[kSeat3] = kQuiet2;
        const TableView viewA = MakeTableView(MakeModel(first), kSeat0);
        const TableView viewB = MakeTableView(MakeModel(second), kSeat0);

        const eastl::vector<DiscardValue> a = ValueDiscards(viewA, kDepth + 1);
        const eastl::vector<DiscardValue> b = ValueDiscards(viewB, kDepth + 1);

        ASSERT_FALSE(a.empty());
        ExpectEqual(a, b);
        ExpectEqual(ValueWaits(viewA), ValueWaits(viewB));
    }

    TEST(OwnDrawsTest, TheSameInputGivesTheSameOutputAlsoAfterAnotherCall) {
        const TableView view = MakeView(kOneFromWaiting);
        const eastl::vector<DiscardValue> first = ValueDiscards(view, kDepth + 1);

        const eastl::vector<DiscardValue> other = ValueDiscards(MakeView(kPairsHand, "Rd"), kDepth);
        const eastl::vector<DiscardValue> second = ValueDiscards(view, kDepth + 1);
        const eastl::vector<DiscardValue> third = ValueDiscards(view, kDepth + 1);

        ASSERT_FALSE(other.empty());
        ASSERT_FALSE(first.empty());
        ExpectEqual(first, second);
        ExpectEqual(first, third);
    }

    TEST(OwnDrawsTest, WithNoDrawLeftTheHandCannotWin) {
        // A wall of 3 tiles leaves the seat 3 / 4 = 0 draws: no win, and a hand that reaches the end of the wall is a drawn hand (0).
        const TableView view = MakeView(kTankiHand, "", kSeatCount - 1);
        const eastl::vector<DiscardValue> values = ValueDiscards(view, kDepth);
        TileCounts counts = CountTiles(view.own.concealed);

        ASSERT_FALSE(values.empty());
        for (const DiscardValue& value : values) {
            EXPECT_EQ(value.winChance, 0) << ToString(value.tile).data();
            EXPECT_EQ(value.meanWinScore, 0) << ToString(value.tile).data();
            EXPECT_EQ(value.expected, 0) << ToString(value.tile).data();
        }
        // Equal values keep the order of the rating.
        EXPECT_EQ(values.front().tile, RateDiscards(counts, 0, view).front().tile);
        for (const DiscardValue& wait : ValueWaits(view)) {
            EXPECT_EQ(wait.winChance, 0);
        }
    }

    TEST(OwnDrawsTest, ThereIsNothingToRateOutsideTheSeatsOwnDiscardTurn) {
        TableView claim = MakeView(kTankiHand);
        claim.phase = Phase::Claim;
        TableView other = MakeView(kTankiHand);
        other.currentSeat = kSeat1;
        TableView none;

        EXPECT_TRUE(ValueDiscards(claim, kDepth).empty());
        EXPECT_TRUE(ValueDiscards(other, kDepth).empty());
        EXPECT_TRUE(ValueDiscards(none, kDepth).empty());
        EXPECT_TRUE(ValueDiscards(MakeView(kTankiHand), 0).empty());
        EXPECT_TRUE(ValueDiscards(MakeView(kTankiHand), -1).empty());
        EXPECT_TRUE(ValueWaits(claim).empty());
        EXPECT_TRUE(ValueWaits(other).empty());
        EXPECT_TRUE(ValueWaits(none).empty());
    }

    TEST(OwnDrawsTest, ADeeperLookWinsMoreOftenFromAHandOneTileFromWaiting) {
        // Discarding 9s leaves 123p 456s, 78m and 57p to join and a pair of Ew: shanten 1. Depth 1 sees one improving draw, then the hand
        // waits and the rest of the wall is closed form; every further depth sees one more draw that can improve, so the chance to win grows.
        const TableView view = MakeView(kOneFromWaiting);
        int previous = -1;
        for (int depth = 1; depth <= kOwnDrawsMaxDepth; ++depth) {
            const eastl::vector<DiscardValue> values = ValueDiscards(view, depth);
            const DiscardValue* value = Find(values, "9s");
            ASSERT_NE(value, nullptr) << "depth " << depth;
            EXPECT_GT(value->winChance, previous) << "depth " << depth;
            previous = value->winChance;
        }
        EXPECT_LT(previous, kPerfectChance);
    }

    TEST(OwnDrawsTest, ADepthAboveTheMaximumIsTheMaximum) {
        const TableView view = MakeView(kOneFromWaiting);

        ExpectEqual(ValueDiscards(view, kOwnDrawsMaxDepth), ValueDiscards(view, kOwnDrawsMaxDepth + 3));
    }

    TEST(OwnDrawsTest, OnlyListedPlainDiscardsAreRatedBestFirst) {
        TableView view = MakeView(kTankiHand);
        // Take the plain discard of 5p out of the list: only its declaring variant stays, which is not a candidate.
        view.legal.erase(eastl::remove(view.legal.begin(), view.legal.end(), HandAction::MakeDiscard(T("5p"))), view.legal.end());

        const eastl::vector<DiscardValue> values = ValueDiscards(view, kDepth);

        ASSERT_FALSE(values.empty());
        EXPECT_LE(values.size(), static_cast<size_t>(kOwnDrawsMaxCandidates));
        EXPECT_EQ(Find(values, "5p"), nullptr);
        for (size_t i = 0; i < values.size(); ++i) {
            EXPECT_TRUE(Has(view.legal, HandAction::MakeDiscard(values[i].tile))) << ToString(values[i].tile).data();
            EXPECT_FALSE(Has(view.legal, HandAction::MakeDiscard(values[i].tile, true)) && !Has(view.legal, HandAction::MakeDiscard(values[i].tile)));
            if (i > 0) {
                EXPECT_LE(values[i].expected, values[i - 1].expected) << "entry " << i;
                for (size_t j = 0; j < i; ++j) {
                    EXPECT_NE(values[i].tile, values[j].tile);
                }
            }
        }
    }

    TEST(OwnDrawsTest, ValueWaitsListsOnlyTheDiscardsThatDeclare) {
        const TableView view = MakeView(kTankiHand);

        const eastl::vector<DiscardValue> waits = ValueWaits(view);

        ASSERT_EQ(waits.size(), 2u);
        for (const DiscardValue& wait : waits) {
            EXPECT_TRUE(Has(view.legal, HandAction::MakeDiscard(wait.tile, true))) << ToString(wait.tile).data();
        }
        // A hand that does not wait has no declaring discard.
        EXPECT_TRUE(ValueWaits(MakeView(kPairsHand)).empty());
    }

}  // namespace BECore::Mahjong::Tests
