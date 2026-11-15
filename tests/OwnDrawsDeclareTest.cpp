#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/OwnDraws.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>
#include <tests/FixedWall.h>
#include <tests/HandTestSupport.h>

/// The declaration now or later (`ownDrawsDeclareDepth`, `ValueLater`). Views are made by hand: East's turn with 14 tiles.
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kDepth = 2;
        constexpr int kLongWall = 60;
        constexpr int kRandomHands = 12;
        constexpr uint32_t kRandomFirstSeed = 1300;
        constexpr uint32_t kRngSeed = 5;
        constexpr int kRngProbe = 1000;
        constexpr int kAlwaysPercent = 100;

        constexpr OwnDrawsModel kModel{.depth = kDepth};

        // Three sets, a pair of 9s and the open-ended 4p 5p (a wait on 3p and 6p, eight copies); Nw is the tile to throw. Discarding Nw
        // declares the calling hand.
        constexpr const char* kTwoSidedWait = "1p 2p 3p 4s 5s 6s 7m 8m 9m 4p 5p 9s 9s Nw";

        // Three sets, a pair of 9s and 2p 4p, which wait on 3p only. The fourth 3p is the one in the set 1p 2p 3p and the other three lie
        // in the pile: the wait cannot win. Drawing a 4p would make a better wait (2p 4p 4p: discard the 2p, 4p 4p and 9s 9s are a pair
        // each, waiting on 4p and 9s).
        constexpr const char* kDeadWait = "1p 2p 3p 4s 5s 6s 7m 8m 9m 2p 4p 9s 9s Nw";
        constexpr const char* kDeadWaitPile = "3p 3p 3p";

        void ListDiscards(TableView& view) {
            view.legal.clear();
            const int meldCount = MeldCountOf(view);
            const TileCounts meldTiles = CountMeldTiles(view.own.melds);
            TileCounts counts = CountTiles(view.own.concealed);
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (counts[id] == 0) {
                    continue;
                }
                view.legal.push_back(HandAction::MakeDiscard(Tile{id}));
                --counts[id];
                if (IsWaiting(counts, meldCount, meldTiles)) {
                    view.legal.push_back(HandAction::MakeDiscard(Tile{id}, true));
                }
                ++counts[id];
            }
        }

        /// East's view on its turn with the 14 tiles `hand`, the tiles `pile` lying open, `live` tiles in the wall. The listed actions: a
        /// plain discard of every kind held, and the declaring one where the 13 left wait.
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
            const Vector<Tile> open = ParseTiles(pile).value();
            for (const Tile tile : open) {
                ++view.visible[tile.Id()];
            }
            view.liveCount = live;
            ListDiscards(view);
            return view;
        }

        LevelProfile ByDeclaration(int depth = kDepth) {
            LevelProfile profile = kNormalProfile;
            profile.ownDrawsDeclareDepth = depth;
            return profile;
        }

        const DiscardValue* Find(const Vector<DiscardValue>& values, Tile tile) {
            const auto found = eastl::find_if(values.begin(), values.end(), [tile](const DiscardValue& value) { return value.tile == tile; });
            return found == values.end() ? nullptr : &*found;
        }

        bool Declares(const TableView& view) {
            return eastl::any_of(view.legal.begin(), view.legal.end(), [](const HandAction& action) { return action.kind == ActionKind::Discard && action.declareCalling; });
        }

        void ExpectSameValues(const Vector<DiscardValue>& a, const Vector<DiscardValue>& b) {
            ASSERT_EQ(a.size(), b.size());
            for (size_t i = 0; i < a.size(); ++i) {
                EXPECT_EQ(a[i].tile, b[i].tile) << "entry " << i;
                EXPECT_EQ(a[i].winChance, b[i].winChance) << "entry " << i;
                EXPECT_EQ(a[i].meanWinScore, b[i].meanWinScore) << "entry " << i;
                EXPECT_EQ(a[i].expected, b[i].expected) << "entry " << i;
            }
        }

        // East's first draw is the tile of `live`; the others hold `other2` and `other3` (hidden tiles).
        HandModel TurnModel(const char* east13, const char* draw, const char* other2, const char* other3) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = east13;
            spec.hands[kSeat1] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Ww Nw Gd Wd";
            spec.hands[kSeat2] = other2;
            spec.hands[kSeat3] = other3;
            spec.live = draw;
            return MakeModel(spec);
        }

    }  // namespace

    TEST(OwnDrawsDeclareTest, AGoodWaitDeclaresNowBecauseLaterLosesARound) {
        const TableView view = MakeView(kTwoSidedWait);
        ASSERT_TRUE(Declares(view));
        const Vector<DiscardValue> now = ValueWaits(view);
        const Vector<DiscardValue> later = ValueLater(view, kModel);
        ASSERT_FALSE(now.empty());
        ASSERT_FALSE(later.empty());
        EXPECT_GT(now.front().expected, later.front().expected);

        // The same tile thrown without the declaration cannot win in the first round: the chance to win within the horizon is lower, and
        // the hand that does win declared one discard later (the doubles of the first hand are gone), so a win is worth no more.
        const DiscardValue* declared = Find(now, T("Nw"));
        const DiscardValue* undeclared = Find(later, T("Nw"));
        ASSERT_NE(declared, nullptr);
        ASSERT_NE(undeclared, nullptr);
        EXPECT_LT(undeclared->winChance, declared->winChance);
        EXPECT_LE(undeclared->meanWinScore, declared->meanWinScore);
        EXPECT_LT(undeclared->expected, declared->expected);

        MahjongRng rng(kRngSeed);
        const TurnDecision decision = DecideTurn(view, rng, ByDeclaration());
        EXPECT_EQ(decision.step, TurnStep::Declare);
        EXPECT_EQ(decision.action, HandAction::MakeDiscard(T("Nw"), true));
    }

    TEST(OwnDrawsDeclareTest, AWaitWithNoCopyLeftThatCanBecomeADearerWaitIsPlayedOnAndWhatIsReturnedIsThatDiscard) {
        const TableView view = MakeView(kDeadWait, kDeadWaitPile);
        ASSERT_TRUE(Declares(view));
        const Vector<DiscardValue> now = ValueWaits(view);
        const Vector<DiscardValue> later = ValueLater(view, kModel);
        ASSERT_FALSE(now.empty());
        ASSERT_FALSE(later.empty());
        EXPECT_EQ(now.front().winChance, 0) << "the only wait has no copy left";
        EXPECT_GT(later.front().expected, now.front().expected);

        for (uint32_t seed = 1; seed <= 4; ++seed) {
            MahjongRng rng(seed);
            const TurnDecision decision = DecideTurn(view, rng, ByDeclaration());
            EXPECT_EQ(decision.step, TurnStep::Discard) << "rng seed " << seed;
            EXPECT_EQ(decision.action, HandAction::MakeDiscard(later.front().tile)) << "rng seed " << seed;
            EXPECT_FALSE(decision.action.declareCalling);
            EXPECT_TRUE(Has(view.legal, decision.action));
        }
        // Normal declares here.
        MahjongRng rng(kRngSeed);
        EXPECT_EQ(DecideTurn(view, rng, kNormalProfile).step, TurnStep::Declare);
    }

    TEST(OwnDrawsDeclareTest, TheRollOfSkipDeclarationPercentComesFirstAndThenNothingChanges) {
        LevelProfile skipping = kNormalProfile;
        skipping.skipDeclarationPercent = kAlwaysPercent;
        LevelProfile skippingAndValued = skipping;
        skippingAndValued.ownDrawsDeclareDepth = kDepth;
        for (const TableView& view : {MakeView(kTwoSidedWait), MakeView(kDeadWait, kDeadWaitPile)}) {
            MahjongRng normal(kRngSeed);
            MahjongRng valued(kRngSeed);
            const TurnDecision expected = DecideTurn(view, normal, skipping);
            const TurnDecision actual = DecideTurn(view, valued, skippingAndValued);
            EXPECT_EQ(actual.action, expected.action);
            EXPECT_EQ(actual.step, expected.step);
            EXPECT_EQ(valued.Below(kRngProbe), normal.Below(kRngProbe));
        }
    }

    TEST(OwnDrawsDeclareTest, NothingIsValuedLaterWithoutADeclaringDiscardOrADepthOrAnOwnTurn) {
        const TableView view = MakeView(kTwoSidedWait);
        EXPECT_FALSE(ValueLater(view, kModel).empty());
        EXPECT_TRUE(ValueLater(view, OwnDrawsModel{}).empty());

        // 14 tiles that wait on nothing after any discard: no declaring discard is listed.
        const TableView farAway = MakeView("1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s Nw");
        ASSERT_FALSE(Declares(farAway));
        EXPECT_TRUE(ValueLater(farAway, kModel).empty());

        TableView claiming = view;
        claiming.phase = Phase::Claim;
        EXPECT_TRUE(ValueLater(claiming, kModel).empty());
    }

    TEST(OwnDrawsDeclareTest, ADiscardThatDoesNotWaitIsValuedAsByValueDiscards) {
        // Throwing a tile of a set leaves a hand that does not wait: the root rule is not asked, the value is `ValueDiscards`'.
        const TableView view = MakeView(kTwoSidedWait);
        const Vector<DiscardValue> later = ValueLater(view, kModel);
        const Vector<DiscardValue> plain = ValueDiscards(view, kModel);
        for (const DiscardValue& value : later) {
            if (value.tile == T("Nw")) {
                continue;
            }
            const DiscardValue* same = Find(plain, value.tile);
            ASSERT_NE(same, nullptr);
            EXPECT_EQ(same->expected, value.expected) << ToStd(ToString(value.tile));
        }
    }

    TEST(OwnDrawsDeclareTest, TwoTablesThatDifferOnlyInHiddenTilesGiveTheSameValues) {
        const HandModel a = TurnModel("1p 2p 3p 4s 5s 6s 7m 8m 9m 4p 5p 9s 9s", "Nw", "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Rd Wd Nw", "2p 8p 5s 8s 2m 5m 8m Ww Gd Rd Wd 1s 7s");
        const HandModel b = TurnModel("1p 2p 3p 4s 5s 6s 7m 8m 9m 4p 5p 9s 9s", "Nw", "2p 8p 5s 8s 2m 5m 8m Ww Gd Rd Wd 1s 7s", "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Rd Wd Nw");
        const TableView viewA = MakeTableView(a, kSeat0);
        const TableView viewB = MakeTableView(b, kSeat0);
        ASSERT_EQ(viewA.visible, viewB.visible);
        ASSERT_TRUE(Declares(viewA));
        const Vector<DiscardValue> valuesA = ValueLater(viewA, kModel);
        ASSERT_FALSE(valuesA.empty());
        ExpectSameValues(valuesA, ValueLater(viewB, kModel));
        ExpectSameValues(valuesA, ValueLater(viewA, kModel));
    }

    TEST(OwnDrawsDeclareTest, WithTheKnobOffTheTurnIsNormalsAndWithItOnNothingIsDrawnFromTheRng) {
        LevelProfile others = kNormalProfile;
        others.ownDrawsDiscardWins = true;
        others.ownDrawsClaimDepth = 1;
        const LevelProfile on = ByDeclaration(1);
        int declaringTurns = 0;
        int later = 0;
        for (int index = 0; index < kRandomHands; ++index) {
            const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
            Match match;
            ASSERT_TRUE(match.Start(MahjongSettings{}, seed).has_value());
            MahjongRng play(seed);
            HandModel& hand = match.Hand();
            int turn = 0;
            while (hand.GetPhase() != Phase::Finished) {
                if (hand.GetPhase() != Phase::Discard) {
                    for (const Seat seat : hand.PendingClaimSeats()) {
                        if (hand.GetClaimStatus(seat) != ClaimStatus::Waiting) {
                            break;
                        }
                        ASSERT_TRUE(hand.Apply(seat, ChooseClaimAction(MakeTableView(hand, seat), play)).has_value());
                    }
                    continue;
                }
                const Seat actor = hand.CurrentSeat();
                const TableView view = MakeTableView(hand, actor);
                ++turn;
                if (Declares(view) && !view.own.callingDeclared) {
                    ++declaringTurns;
                    const uint32_t rngSeed = seed + static_cast<uint32_t>(turn);
                    MahjongRng normal(rngSeed);
                    MahjongRng off(rngSeed);
                    EXPECT_EQ(DecideTurn(view, off, others).action, DecideTurn(view, normal, kNormalProfile).action) << "hand " << index << " turn " << turn;
                    EXPECT_EQ(off.Below(kRngProbe), normal.Below(kRngProbe));

                    MahjongRng used(rngSeed);
                    MahjongRng fresh(rngSeed);
                    const TurnDecision decision = DecideTurn(view, used, on);
                    (void)DecideTurn(view, fresh, kNormalProfile);
                    EXPECT_TRUE(Has(view.legal, decision.action)) << "hand " << index << " turn " << turn;
                    EXPECT_EQ(used.Below(kRngProbe), fresh.Below(kRngProbe)) << "the rng is where Normal's step 2 leaves it";
                    later += (decision.step == TurnStep::Discard && !decision.action.declareCalling) ? 1 : 0;
                }
                MahjongRng rng(seed + static_cast<uint32_t>(turn));
                ASSERT_TRUE(hand.Apply(actor, ChooseTurnAction(view, rng)).has_value());
            }
        }
        EXPECT_GT(declaringTurns, 0);
        RecordProperty("DeclaringTurns", declaringTurns);
        RecordProperty("PlayedOn", later);
    }

}  // namespace BECore::Mahjong::Tests
