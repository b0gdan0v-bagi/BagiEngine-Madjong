#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/OwnDraws.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>
#include <tests/FixedWall.h>
#include <tests/HandTestSupport.h>

/// The claim window valued by the own-draws evaluator (`ValueClaims`, `ownDrawsClaimDepth`). Windows are made by hand: seat 1 (South,
/// round East) holds `hand` and East has just discarded `pending`; `pile` lies open besides (it only takes unseen copies away).
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kDepth = 2;
        constexpr int kShortWall = 60;
        constexpr int kRandomHands = 6;
        constexpr uint32_t kRandomFirstSeed = 900;
        constexpr uint32_t kRngSeed = 7;
        constexpr int kRngProbe = 1000;

        constexpr OwnDrawsModel kModel{.depth = kDepth};

        const HandAction kPung = HandAction::MakeClaim(ClaimKind::Pung);
        const HandAction kKong = HandAction::MakeClaim(ClaimKind::Kong);

        HandAction Chow(const char* first) {
            return HandAction::MakeClaim(ClaimKind::Chow, T(first));
        }

        /// The listed answers of seat 1 to a discard of East: a pass, a pung or a kong of the pending kind, and every chow it holds the
        /// two other tiles of.
        TableView ClaimView(const char* hand, const char* pending, const char* pile = "", int live = kShortWall) {
            TableView view;
            view.seat = kSeat1;
            view.phase = Phase::Claim;
            view.currentSeat = kSeat0;
            view.seatWind = Wind::South;
            view.roundWind = Wind::East;
            view.own.concealed = ParseTiles(hand).value();
            eastl::sort(view.own.concealed.begin(), view.own.concealed.end());
            view.pendingTile = T(pending);
            for (const Tile tile : view.own.concealed) {
                ++view.visible[tile.Id()];
            }
            const Vector<Tile> open = ParseTiles(pile).value();
            for (const Tile tile : open) {
                ++view.visible[tile.Id()];
            }
            ++view.visible[view.pendingTile.Id()];
            view.liveCount = live;

            const TileCounts counts = CountTiles(view.own.concealed);
            const Tile tile = view.pendingTile;
            view.legal.push_back(HandAction::MakePass());
            if (counts[tile.Id()] >= kPungTilesFromHand) {
                view.legal.push_back(kPung);
            }
            if (counts[tile.Id()] >= kKongTilesFromHand) {
                view.legal.push_back(kKong);
            }
            if (tile.IsSuit()) {
                const int firstId = tile.Id() - tile.Rank() + 1;
                for (int first = eastl::max(1, tile.Rank() - 2); first <= eastl::min(tile.Rank(), kRanksPerSuit - 2); ++first) {
                    bool held = true;
                    for (int rank = first; rank < first + 3; ++rank) {
                        held = held && (rank == tile.Rank() || counts[static_cast<size_t>(firstId + rank - 1)] > 0);
                    }
                    if (held) {
                        view.legal.push_back(HandAction::MakeClaim(ClaimKind::Chow, Tile{static_cast<uint8_t>(firstId + first - 1)}));
                    }
                }
            }
            return view;
        }

        const ClaimValue* Find(const Vector<ClaimValue>& values, const HandAction& action) {
            const auto found = eastl::find_if(values.begin(), values.end(), [&action](const ClaimValue& value) { return value.action == action; });
            return found == values.end() ? nullptr : &*found;
        }

        // Seat 1 holds three concealed pungs of one suit, a pair of 5p, a lone 8p and 7s: one set from waiting. A pung of the 5p the
        // others discard makes four sets and a lone tile (a wait), but opens the hand: no concealed doubles any more.
        constexpr const char* kConcealedPungs = "1p 1p 1p 2p 2p 2p 3p 3p 3p 5p 5p 8p 7s";
        // The copies that would complete the lone tile after a pung (the other three 7s and 8p) lie in the pile: such a wait is worth
        // nothing, so the pung buys no speed.
        constexpr const char* kDeadLoneTiles = "7s 7s 7s 8p 8p 8p";

        // Two sets, two single sets-to-be (4s5s, 7m8m), a pair that is a pung's worth: the hand is 1 from waiting with a pair of `pair`
        // and still 1 after a pung of it (the partial sets beyond the sets needed count for nothing).
        String PairAndTwoPungs(const char* pair) {
            return Format("{} 1p 1p 1p 9p 9p 9p 4s 5s 7m 8m 9s", pair);
        }

        /// `view` as a discard turn of the held hand and `stray`: what the claim window would be if the seat's hand were that and it
        /// had to throw the stray (the same copies seen, the same wall).
        TableView DiscardTurnOf(TableView view, Tile stray) {
            view.phase = Phase::Discard;
            view.currentSeat = view.seat;
            view.pendingTile = Tile{};
            view.own.concealed.push_back(stray);
            eastl::sort(view.own.concealed.begin(), view.own.concealed.end());
            view.legal.clear();
            for (const Tile tile : view.own.concealed) {
                if (view.legal.empty() || view.legal.back().tile != tile) {
                    view.legal.push_back(HandAction::MakeDiscard(tile));
                }
            }
            return view;
        }

        // The window of a fixed wall: East throws `discard` (the only tile of `live`) and seat 1 holds `seat1`; the others hold `other2` and
        // `other3`.
        HandModel WindowModel(const String& seat1, const char* discard, const char* other2, const char* other3) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s";
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = other2;
            spec.hands[kSeat3] = other3;
            spec.live = discard;
            HandModel model = MakeModel(spec);
            EXPECT_TRUE(model.Discard(kSeat0, T(discard)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        void ExpectSameValues(const Vector<ClaimValue>& a, const Vector<ClaimValue>& b) {
            ASSERT_EQ(a.size(), b.size());
            for (size_t i = 0; i < a.size(); ++i) {
                EXPECT_EQ(a[i].action, b[i].action) << "entry " << i;
                EXPECT_EQ(a[i].value.tile, b[i].value.tile) << "entry " << i;
                EXPECT_EQ(a[i].value.winChance, b[i].value.winChance) << "entry " << i;
                EXPECT_EQ(a[i].value.meanWinScore, b[i].value.meanWinScore) << "entry " << i;
                EXPECT_EQ(a[i].value.valueWhenNotWinning, b[i].value.valueWhenNotWinning) << "entry " << i;
                EXPECT_EQ(a[i].value.expected, b[i].value.expected) << "entry " << i;
            }
        }

        LevelProfile ByClaims(int depth = kDepth) {
            LevelProfile profile = kNormalProfile;
            profile.ownDrawsClaimDepth = depth;
            return profile;
        }

    }  // namespace

    // ----- the view after a claim -----

    TEST(OwnDrawsClaimTest, TheViewAfterAPungOrAChowIsTheSeatsDiscardTurnWithOneMeldMore) {
        // A pung of 5m takes two 5m out of the hand; a chow 4m 5m 6m takes the 4m and the 6m. Nothing is drawn and no copy changes.
        const TableView window = ClaimView("5m 5m 4m 6m 1p 2p 3p 4s 5s 6s 7s 8s 9s", "5m");
        for (const HandAction& claim : {kPung, Chow("4m")}) {
            const TableView after = ViewAfterClaim(window, claim);
            EXPECT_EQ(after.phase, Phase::Discard);
            EXPECT_EQ(after.currentSeat, kSeat1);
            EXPECT_FALSE(after.pendingTile.IsValid());
            EXPECT_EQ(after.own.concealed.size(), window.own.concealed.size() - 2);
            ASSERT_EQ(after.own.melds.size(), 1U);
            EXPECT_FALSE(after.own.melds.front().concealed);
            EXPECT_EQ(after.own.melds.front().kind, claim.claim == ClaimKind::Pung ? MeldKind::Pung : MeldKind::Chow);
            EXPECT_EQ(after.own.melds.front().first, claim.claim == ClaimKind::Pung ? T("5m") : T("4m"));
            EXPECT_EQ(after.visible, window.visible);
            EXPECT_EQ(after.liveCount, window.liveCount);
            // The list is a plain discard of each kind that is held, and nothing else.
            for (const HandAction& action : after.legal) {
                EXPECT_EQ(action.kind, ActionKind::Discard);
                EXPECT_FALSE(action.declareCalling);
            }
            size_t kinds = 0;
            for (const uint8_t copies : CountTiles(after.own.concealed)) {
                kinds += copies > 0 ? 1 : 0;
            }
            EXPECT_EQ(after.legal.size(), kinds);
        }
        EXPECT_EQ(ConcealedAfterClaim(window, kPung), CountTiles(ViewAfterClaim(window, kPung).own.concealed));
    }

    TEST(OwnDrawsClaimTest, TheViewAfterAKongIsStillTheWindowBecauseTheLooseTileComesFirst) {
        const TableView window = ClaimView("Rd Rd Rd 1p 2p 3p 4s 5s 6s 7s 8s 9s 9m", "Rd");
        const TableView after = ViewAfterClaim(window, kKong);
        EXPECT_EQ(after.phase, Phase::Claim);
        EXPECT_EQ(after.own.concealed.size(), window.own.concealed.size() - kKongTilesFromHand);
        ASSERT_EQ(after.own.melds.size(), 1U);
        EXPECT_EQ(after.own.melds.front().kind, MeldKind::Kong);
        EXPECT_EQ(after.visible, window.visible);
    }

    // ----- what is valued -----

    TEST(OwnDrawsClaimTest, NothingIsValuedOutsideAClaimWindowOrWithoutAClaimOrWithoutADepth) {
        const TableView window = ClaimView(kConcealedPungs, "5p");
        EXPECT_FALSE(ValueClaims(window, kModel).empty());
        EXPECT_TRUE(ValueClaims(window, OwnDrawsModel{}).empty()) << "depth 0";

        TableView discarding = window;
        discarding.phase = Phase::Discard;
        EXPECT_TRUE(ValueClaims(discarding, kModel).empty());

        TableView discarder = window;
        discarder.currentSeat = discarder.seat;
        EXPECT_TRUE(ValueClaims(discarder, kModel).empty()) << "the discarder has nothing to claim";

        // Only a pass is listed: no seat 1 tile pairs with the 9m.
        EXPECT_TRUE(ValueClaims(ClaimView(kConcealedPungs, "9m"), kModel).empty());
        EXPECT_TRUE(ValueClaims(TableView{}, kModel).empty());
    }

    TEST(OwnDrawsClaimTest, EveryListedClaimAndThePassAreValuedBestFirstWithPlainData) {
        // Three 6m: a pung and a kong, and the chows 4m 5m 6m and 5m 6m 7m.
        const TableView window = ClaimView("4m 5m 6m 6m 6m 7m 1p 2p 3p 4s 5s 6s Nw", "6m");
        const Vector<ClaimValue> values = ValueClaims(window, kModel);
        ASSERT_EQ(values.size(), window.legal.size()) << "the pass and each claim once";
        for (const HandAction& action : window.legal) {
            EXPECT_NE(Find(values, action), nullptr) << static_cast<int>(action.claim);
        }
        for (size_t i = 1; i < values.size(); ++i) {
            EXPECT_GE(values[i - 1].value.expected, values[i].value.expected) << "entry " << i;
        }
        const ClaimValue* pass = Find(values, HandAction::MakePass());
        ASSERT_NE(pass, nullptr);
        EXPECT_FALSE(pass->value.tile.IsValid());
        const ClaimValue* kong = Find(values, kKong);
        ASSERT_NE(kong, nullptr);
        EXPECT_FALSE(kong->value.tile.IsValid());
        const ClaimValue* pung = Find(values, kPung);
        ASSERT_NE(pung, nullptr);
        EXPECT_TRUE(pung->value.tile.IsValid());
    }

    // ----- (a) a double against a tile -----

    TEST(OwnDrawsClaimTest, APungThatOpensAConcealedHandIsTakenForTheTileAndPassedWhenTheTileBringsNothing) {
        // The hand is worth much concealed (concealed pungs, one suit, no chow: the doubles). A pung of 5p makes it a wait on a lone tile
        // with three copies unseen: the speed is worth more than the doubles lost, so it is taken.
        const TableView open = ClaimView(kConcealedPungs, "5p");
        const Vector<ClaimValue> taken = ValueClaims(open, kModel);
        ASSERT_FALSE(taken.empty());
        EXPECT_EQ(taken.front().action, kPung);
        EXPECT_GT(taken.front().value.expected, Find(taken, HandAction::MakePass())->value.expected);

        // The same hand with the copies of the lone tiles lying in the pile: the pung's wait is as good as a draw, the hand loses
        // the doubles and nothing else: it passes.
        const TableView dead = ClaimView(kConcealedPungs, "5p", kDeadLoneTiles);
        const Vector<ClaimValue> passed = ValueClaims(dead, kModel);
        ASSERT_FALSE(passed.empty());
        EXPECT_EQ(passed.front().action, HandAction::MakePass());
        const ClaimValue* pung = Find(passed, kPung);
        ASSERT_NE(pung, nullptr);
        EXPECT_LT(pung->value.meanWinScore, passed.front().value.meanWinScore) << "the doubles are what the pung costs";

        // And the chooser follows the value, whatever Normal says (the pung lowers the shanten, so Normal takes it in both).
        MahjongRng rng(kRngSeed);
        EXPECT_EQ(DecideClaim(open, rng, ByClaims()).action, kPung);
        EXPECT_EQ(DecideClaim(dead, rng, ByClaims()).action, HandAction::MakePass());
        EXPECT_EQ(DecideClaim(dead, rng, kNormalProfile).action, kPung);
    }

    // ----- (b) a pung at an equal shanten -----

    TEST(OwnDrawsClaimTest, AValuePungAtAnEqualShantenIsTakenByValueAndASimplePungThatCostsTheConcealedBonusIsPassed) {
        const TableView dragons = ClaimView(PairAndTwoPungs("Rd Rd").c_str(), "Rd");
        const TableView simples = ClaimView(PairAndTwoPungs("5m 5m").c_str(), "5m");
        for (const TableView* view : {&dragons, &simples}) {
            ASSERT_EQ(Shanten(CountTiles(view->own.concealed), 0), 1);
            ASSERT_EQ(RateClaim(*view, kPung).shanten, 1) << "an equal shanten: the pung changes the kind of the hand, not its distance";
        }
        MahjongRng rng(kRngSeed);
        // A pung of a dragon: the double of the set pays for the concealed bonus, and the pair of dragons (two points) is no loss.
        EXPECT_EQ(DecideClaim(dragons, rng, ByClaims()).action, kPung);
        // A pung of a simple tile: nothing pays for it.
        EXPECT_EQ(DecideClaim(simples, rng, ByClaims()).action, HandAction::MakePass());
        // Normal passes the simple one at an equal shanten as well, and takes the dragon's.
        EXPECT_EQ(DecideClaim(simples, rng, kNormalProfile).action, HandAction::MakePass());
        EXPECT_EQ(DecideClaim(dragons, rng, kNormalProfile).action, kPung);
    }

    // ----- (c) the same clock -----

    TEST(OwnDrawsClaimTest, ThePassIsWorthWhatTheHeldHandAndAStrayToThrowAreWorthOnTheSameDraws) {
        // A claim whose result is the held hand and a stray tile to throw would be worth the held hand: the pass sees the same number of
        // draws, the same wall and the same `Alive` as a discard turn does.
        const Tile stray = T("5p");
        const TableView window = ClaimView(kConcealedPungs, "5p");
        const Vector<ClaimValue> values = ValueClaims(window, kModel);
        const ClaimValue* pass = Find(values, HandAction::MakePass());
        ASSERT_NE(pass, nullptr);

        const Vector<DiscardValue> discards = ValueDiscards(DiscardTurnOf(window, stray), kModel);
        const auto thrown = eastl::find_if(discards.begin(), discards.end(), [stray](const DiscardValue& value) { return value.tile == stray; });
        ASSERT_NE(thrown, discards.end());
        EXPECT_EQ(pass->value.expected, thrown->expected);
        EXPECT_EQ(pass->value.winChance, thrown->winChance);
        EXPECT_EQ(pass->value.expected, ValueOfHeldHand(window, 0, kModel)) << "the held hand, as `RateWaitingKong` values it, with the depth";
    }

    TEST(OwnDrawsClaimTest, APungOrAChowIsValuedAsTheBestDiscardTurnAfterIt) {
        const TableView window = ClaimView("4m 5m 6m 6m 1p 2p 3p 4s 5s 6s 7s 9p Nw", "6m");
        const Vector<ClaimValue> values = ValueClaims(window, kModel);
        for (const HandAction& claim : {kPung, Chow("4m"), Chow("5m")}) {
            const ClaimValue* found = Find(values, claim);
            if (found == nullptr) {
                ASSERT_NE(claim.claim, ClaimKind::Pung) << "the pung is listed";
                continue;
            }
            const Vector<DiscardValue> after = ValueDiscards(ViewAfterClaim(window, claim), kModel);
            ASSERT_FALSE(after.empty());
            EXPECT_EQ(found->value.expected, after.front().expected);
            EXPECT_EQ(found->value.tile, after.front().tile);
        }
    }

    // ----- (d) a chow with two ways -----

    TEST(OwnDrawsClaimTest, OfTwoChowsTheDearerIsFirstAndEqualOnesKeepTheOrderOfTheList) {
        // 3m 4m 6m 7m and a 5m: a chow 3m (345) or 5m (567) leaves a pair of tiles that count for a set, the chow 4m (456) leaves 3m and 7m
        // alone: the first two are worth the same (the list's order decides), the third less.
        const TableView window = ClaimView("1p 2p 3p 4s 5s 6s 3m 4m 6m 7m 7p 9s Nw", "5m");
        const Vector<ClaimValue> values = ValueClaims(window, kModel);
        const ClaimValue* first = Find(values, Chow("3m"));
        const ClaimValue* second = Find(values, Chow("5m"));
        const ClaimValue* third = Find(values, Chow("4m"));
        ASSERT_NE(first, nullptr);
        ASSERT_NE(second, nullptr);
        ASSERT_NE(third, nullptr);
        EXPECT_EQ(first->value.expected, second->value.expected);
        EXPECT_GT(first->value.expected, third->value.expected);
        EXPECT_EQ(values.front().action, Chow("3m"));
        EXPECT_EQ(values[1].action, Chow("5m"));

        MahjongRng rng(kRngSeed);
        EXPECT_EQ(DecideClaim(window, rng, ByClaims()).action, Chow("3m"));
    }

    // ----- the kong -----

    TEST(OwnDrawsClaimTest, AKongIsValuedAsTheHeldHandAfterItWithTheLooseTileAsAnExtraDraw) {
        const TableView window = ClaimView("Rd Rd Rd 1p 2p 3p 4s 5s 6s 7m 8m 5p 7p", "Rd");
        const Vector<ClaimValue> values = ValueClaims(window, kModel);
        const ClaimValue* kong = Find(values, kKong);
        ASSERT_NE(kong, nullptr);
        EXPECT_EQ(kong->value.expected, ValueOfHeldHand(ViewAfterClaim(window, kKong), kKongLooseDraws, kModel)) << "the convention of RateWaitingKong";
    }

    // ----- no peeking, no rng, the knob off -----

    TEST(OwnDrawsClaimTest, TwoWindowsThatDifferOnlyInHiddenTilesGiveTheSameValues) {
        const char* const hand = "4m 5m 6m 6m 6m 7m 1p 2p 3p 4s 5s 6s Nw";
        const HandModel a = WindowModel(hand, "6m", "2p 5p 8p 2s 5s 8s 3m 9m 1s Gd Wd 9p 8m", "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s");
        const HandModel b = WindowModel(hand, "6m", "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s", "2p 5p 8p 2s 5s 8s 3m 9m 1s Gd Wd 9p 8m");
        const TableView viewA = MakeTableView(a, kSeat1);
        const TableView viewB = MakeTableView(b, kSeat1);
        ASSERT_EQ(viewA.visible, viewB.visible);
        const Vector<ClaimValue> valuesA = ValueClaims(viewA, kModel);
        ASSERT_FALSE(valuesA.empty());
        ExpectSameValues(valuesA, ValueClaims(viewB, kModel));
        ExpectSameValues(valuesA, ValueClaims(viewA, kModel));
    }

    TEST(OwnDrawsClaimTest, WithTheKnobOffTheChoiceIsNormalsAndWithItOnNothingIsDrawnFromTheRng) {
        LevelProfile others = kNormalProfile;
        others.ownDrawsDepth = 1;
        others.ownDrawsPickWait = true;
        others.ownDrawsDiscardWins = true;
        const LevelProfile on = ByClaims(1);
        int windows = 0;
        int claimsTaken = 0;
        for (int index = 0; index < kRandomHands; ++index) {
            const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
            Match match;
            ASSERT_TRUE(match.Start(MahjongSettings{}, seed).has_value());
            MahjongRng play(seed);
            HandModel& hand = match.Hand();
            while (hand.GetPhase() != Phase::Finished) {
                if (hand.GetPhase() == Phase::Discard) {
                    const Seat actor = hand.CurrentSeat();
                    ASSERT_TRUE(hand.Apply(actor, Choose(hand.LegalActions(actor), Mode::SeekWin, play)).has_value());
                    continue;
                }
                for (const Seat seat : hand.PendingClaimSeats()) {
                    if (hand.GetClaimStatus(seat) != ClaimStatus::Waiting) {
                        break;
                    }
                    const TableView view = MakeTableView(hand, seat);
                    ++windows;
                    MahjongRng normal(seed + static_cast<uint32_t>(windows));
                    MahjongRng off(seed + static_cast<uint32_t>(windows));
                    EXPECT_EQ(DecideClaim(view, off, others).action, DecideClaim(view, normal, kNormalProfile).action) << "window " << windows;
                    EXPECT_EQ(off.Below(kRngProbe), normal.Below(kRngProbe)) << "the rng is where Normal leaves it, window " << windows;

                    MahjongRng used(seed + static_cast<uint32_t>(windows));
                    MahjongRng fresh(seed + static_cast<uint32_t>(windows));
                    const HandAction answer = DecideClaim(view, used, on).action;
                    EXPECT_TRUE(Has(view.legal, answer)) << "window " << windows;
                    EXPECT_EQ(used.Below(kRngProbe), fresh.Below(kRngProbe)) << "nothing is drawn, window " << windows;
                    claimsTaken += answer.kind == ActionKind::Claim ? 1 : 0;
                    ASSERT_TRUE(hand.Apply(seat, answer).has_value()) << "window " << windows;
                }
            }
        }
        EXPECT_GT(windows, kRandomHands);
        EXPECT_GT(claimsTaken, 0) << "a valued seat that never claims";
    }

    TEST(OwnDrawsClaimTest, AWaitingHandStillPassesBeforeTheValueIsAsked) {
        // Four sets would need the pair of Nw as a pair: the hand waits on 4p and 7p with it, and a pung of Nw would break the wait.
        const TableView window = ClaimView("1p 2p 3p 4s 5s 6s 7m 8m 9m Nw Nw 5p 6p", "Nw");
        ASSERT_TRUE(Has(window.legal, kPung));
        MahjongRng rng(kRngSeed);
        const ClaimDecision decision = DecideClaim(window, rng, ByClaims());
        EXPECT_EQ(decision.action, HandAction::MakePass());
        EXPECT_EQ(decision.step, ClaimStep::WaitingHand);
    }

}  // namespace BECore::Mahjong::Tests
