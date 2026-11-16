#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/ComputerWeights.h>
#include <Mahjong/Game/DiscardDanger.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Match.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>
#include <tests/FixedWall.h>
#include <tests/HandTestSupport.h>

namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kRandomHands = 20;
        constexpr int kTurnsPerHand = 30;
        constexpr uint32_t kRandomFirstSeed = 900;
        /// Above every count of the rng that a player draws in one decision.
        constexpr uint32_t kProbeRange = 1u << 30;

        /// Whether the two rngs are at the same place of their streams: the next number of each is the same.
        bool SameStream(MahjongRng& a, MahjongRng& b) {
            return a.Below(kProbeRange) == b.Below(kProbeRange);
        }

        /// Calls `visit(view, hand index, step)` for the view of every own turn (`Phase::Discard`) of random hands played by the
        /// seek-win chooser, so declared hands, melds and kongs are all among them.
        template <typename Visit>
        void ForEachTurn(Visit&& visit) {
            for (int index = 0; index < kRandomHands; ++index) {
                const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
                MahjongSettings settings;
                settings.flowers = (index & 1) != 0;
                Match match;
                ASSERT_TRUE(match.Start(settings, seed).has_value());
                MahjongRng rng(seed);
                HandModel& hand = match.Hand();
                for (int step = 0; step < kTurnsPerHand * kSeatCount && hand.GetPhase() != Phase::Finished; ++step) {
                    const Seat actor = NextActor(hand, rng);
                    ASSERT_NE(actor, kNoSeat);
                    if (hand.GetPhase() == Phase::Discard) {
                        visit(MakeTableView(hand, actor), index, step);
                    }
                    ASSERT_TRUE(hand.Apply(actor, Choose(hand.LegalActions(actor), Mode::SeekWin, rng)).has_value());
                }
            }
        }

        /// Both profiles decide the same turn from equal rngs: the same action and the same place in the stream afterwards.
        struct TurnTally {
            int turns = 0;
            int discards = 0;
            int ties = 0;
            int againstDeclared = 0;
        };

        TurnTally ExpectSameTurns(const LevelProfile& weighted, const LevelProfile& normal, uint32_t rngSalt = 0) {
            TurnTally tally;
            ForEachTurn([&](const TableView& view, int index, int step) {
                MahjongRng first(rngSalt + static_cast<uint32_t>(index * 1000 + step));
                MahjongRng second(rngSalt + static_cast<uint32_t>(index * 1000 + step));
                const TurnDecision a = DecideTurn(view, first, weighted);
                const TurnDecision b = DecideTurn(view, second, normal);
                EXPECT_EQ(a.action, b.action) << "hand " << index << " step " << step;
                EXPECT_EQ(a.step, b.step) << "hand " << index << " step " << step;
                EXPECT_TRUE(SameStream(first, second)) << "hand " << index << " step " << step;
                ++tally.turns;
                if (b.step == TurnStep::Discard) {
                    ++tally.discards;
                    const Vector<DiscardOption> rated = RateDiscards(CountTiles(view.own.concealed), MeldCountOf(view), view);
                    tally.ties += rated.size() > 1 && rated[0].shanten == rated[1].shanten && rated[0].acceptance == rated[1].acceptance && rated[0].keep == rated[1].keep ? 1 : 0;
                    tally.againstDeclared += HasDeclaredOpponent(view) ? 1 : 0;
                }
            });
            return tally;
        }

        const WeightedDiscard* BestOf(const Vector<WeightedDiscard>& rated) {
            const WeightedDiscard* best = nullptr;
            for (const WeightedDiscard& discard : rated) {
                if (best == nullptr || discard.rating > best->rating) {
                    best = &discard;
                }
            }
            return best;
        }

        TableView DiscardView(const char* hand, bool opponentDeclared = false) {
            TableView view;
            view.seat = kSeat0;
            view.phase = Phase::Discard;
            view.currentSeat = kSeat0;
            view.own.concealed = ParseTiles(hand).value();
            eastl::sort(view.own.concealed.begin(), view.own.concealed.end());
            view.visible = CountTiles(view.own.concealed);
            view.seats[kSeat1].callingDeclared = opponentDeclared;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (view.visible[id] > 0) {
                    view.legal.push_back(HandAction::MakeDiscard(Tile{id}));
                }
            }
            return view;
        }

        Tile WeightedChoice(const TableView& view, const LevelProfile& profile, uint32_t seed = 1) {
            MahjongRng rng(seed);
            return ChooseWeightedDiscard(view, rng, profile);
        }

        /// Calls `visit(view, hand index, window)` for the view of every seat that has still to decide in a claim window (pung, kong or
        /// chow listed or not) of random hands; the hands go on by the seek-win chooser.
        template <typename Visit>
        void ForEachWindow(Visit&& visit) {
            int window = 0;
            for (int index = 0; index < kRandomHands; ++index) {
                const uint32_t seed = kRandomFirstSeed + static_cast<uint32_t>(index);
                MahjongSettings settings;
                settings.flowers = (index & 1) != 0;
                Match match;
                ASSERT_TRUE(match.Start(settings, seed).has_value());
                MahjongRng rng(seed);
                HandModel& hand = match.Hand();
                while (hand.GetPhase() != Phase::Finished) {
                    if (hand.GetPhase() == Phase::Discard) {
                        const Seat actor = hand.CurrentSeat();
                        ASSERT_TRUE(hand.Apply(actor, Choose(hand.LegalActions(actor), Mode::SeekWin, rng)).has_value());
                        continue;
                    }
                    ++window;
                    for (const Seat seat : hand.PendingClaimSeats()) {
                        if (hand.GetClaimStatus(seat) != ClaimStatus::Waiting) {
                            break;
                        }
                        visit(MakeTableView(hand, seat), index, window);
                        ASSERT_TRUE(hand.Apply(seat, Choose(hand.LegalActions(seat), Mode::SeekWin, rng)).has_value());
                    }
                }
            }
        }

        struct WindowTally {
            int windows = 0;
            int claims = 0;
            int passes = 0;
            int kongs = 0;
            int lowersAndLosesAcceptance = 0;
            int equalAndGainsAcceptanceOnly = 0;
        };

        WindowTally ExpectSameWindows(const LevelProfile& weighted, const LevelProfile& normal, uint32_t rngSalt = 0) {
            WindowTally tally;
            ForEachWindow([&](const TableView& view, int index, int window) {
                MahjongRng first(rngSalt + static_cast<uint32_t>(index * 1000 + window));
                MahjongRng second(rngSalt + static_cast<uint32_t>(index * 1000 + window));
                const ClaimDecision a = DecideClaim(view, first, weighted);
                const ClaimDecision b = DecideClaim(view, second, normal);
                EXPECT_EQ(a.action, b.action) << "hand " << index << " window " << window;
                EXPECT_EQ(a.step, b.step) << "hand " << index << " window " << window;
                EXPECT_TRUE(SameStream(first, second)) << "hand " << index << " window " << window;
                ++tally.windows;
                tally.claims += b.action.kind == ActionKind::Claim && b.action.claim != ClaimKind::Mahjong ? 1 : 0;
                tally.kongs += b.action.kind == ActionKind::Claim && b.action.claim == ClaimKind::Kong ? 1 : 0;
                tally.passes += b.action.kind == ActionKind::Pass ? 1 : 0;
                if (b.step != ClaimStep::Claim && b.step != ClaimStep::NoGain) {
                    return;
                }
                const int before = Shanten(CountTiles(view.own.concealed), MeldCountOf(view));
                const int acceptanceBefore = AcceptanceOf(view, CountTiles(view.own.concealed), MeldCountOf(view));
                for (const WeightedClaim& claim : RateClaimsWeighted(view, before, weighted)) {
                    const bool value = IsValueClaim(view, claim.action.claim);
                    tally.lowersAndLosesAcceptance += claim.shanten < before && claim.acceptance < acceptanceBefore && claim.acceptable ? 1 : 0;
                    tally.equalAndGainsAcceptanceOnly += claim.shanten == before && claim.acceptance > acceptanceBefore && !value && !claim.acceptable ? 1 : 0;
                }
            });
            return tally;
        }

        constexpr const char* kEast = "1p 4p 7p 1s 4s 7s 1m 4m 7m 2m 5m 8m 9s";
        constexpr const char* kOther2 = "2p 5p 8p 2s 5s 8s 3m 6m 9m Gd Wd 9p 8m";
        constexpr const char* kOther3 = "3p 6p 9p 3s 6s 9s 4m 7m Gd Wd 2m 7p 8s";
        constexpr const char* kPartials = "3p 4p 6p 7p 3s 4s 6s 7s 1m 9m Nw";

        /// East throws `discard`, the only tile of the live wall; seat 1 (South) holds `seat1` and is the seat under test.
        HandModel WindowModel(const String& seat1, const char* discard, Wind roundWind = Wind::East) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = kEast;
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = kOther2;
            spec.hands[kSeat3] = kOther3;
            spec.live = discard;
            HandModel model = MakeModel(spec, kSeat0, roundWind);
            EXPECT_TRUE(model.Discard(kSeat0, T(discard)).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        HandAction ClaimChoice(const TableView& view, const LevelProfile& profile, uint32_t seed = 1) {
            MahjongRng rng(seed);
            return ChooseClaimAction(view, rng, profile);
        }

        /// The weighted choice of every seed 1..8 is `expected` (and listed).
        void ExpectClaimForEverySeed(const TableView& view, const LevelProfile& profile, const HandAction& expected) {
            for (uint32_t seed = 1; seed <= 8; ++seed) {
                EXPECT_EQ(ClaimChoice(view, profile, seed), expected) << "rng seed " << seed;
            }
            EXPECT_TRUE(Has(view.legal, expected));
        }

        const HandAction kPass = HandAction::MakePass();
        const HandAction kPung = HandAction::MakeClaim(ClaimKind::Pung);

    }  // namespace

    // ----- the discard rating -----

    TEST(ComputerWeightsTest, TheRatingIsTheSumOfTheWeightsTimesTheFeatures) {
        LevelProfile profile = kNormalProfile;
        profile.wShanten = 1000;
        profile.wAcceptance = 10;
        profile.wKeep = 3;
        profile.wSafe = 7;
        // shanten 2, acceptance 5, keep 1, danger 1.
        EXPECT_EQ(RateDiscardWeighted(profile, 2, 5, 1, 1), 1000 * (kShantenCeiling - 2) + 10 * 5 + 3 * (kKeepValueTile - 1) + 7 * (kDangerWorst - 1));
        // The largest weights and features do not overflow an int.
        profile.wShanten = profile.wAcceptance = profile.wKeep = profile.wSafe = kWeightMax;
        EXPECT_EQ(RateDiscardWeighted(profile, 0, kAcceptanceMax, 0, 0), static_cast<int64_t>(kWeightMax) * (kShantenCeiling + kAcceptanceMax + kKeepValueTile + kDangerWorst));
    }

    TEST(ComputerWeightsTest, EveryKindHeldIsRatedInTheOrderOfTheTileIds) {
        const TableView view = DiscardView("1p 2p 3p 4p 5p 6p 7p 8p 9p 1m 2m Gd Rd 5s");
        const Vector<WeightedDiscard> rated = RateDiscardsWeighted(view, CountTiles(view.own.concealed), 0, kNormalProfile);
        ASSERT_EQ(rated.size(), 14u);
        for (size_t i = 1; i < rated.size(); ++i) {
            EXPECT_LT(rated[i - 1].tile.Id(), rated[i].tile.Id());
        }
        for (const WeightedDiscard& discard : rated) {
            EXPECT_EQ(discard.rating, RateDiscardWeighted(kNormalProfile, discard.shanten, discard.acceptance, discard.keep, discard.danger));
        }
    }

    // ----- the default weights are Normal -----

    TEST(ComputerWeightsTest, WithTheDefaultWeightsTheTurnIsNormalsChoiceAndNormalsDraws) {
        const TurnTally tally = ExpectSameTurns(Weighted(kNormalProfile), kNormalProfile);
        EXPECT_GT(tally.discards, kRandomHands * kTurnsPerHand / 2);
        EXPECT_GT(tally.ties, 0) << "no tied discard was drawn for, the stream check proves nothing";
    }

    TEST(ComputerWeightsTest, WithTheDangerWeightTheTurnIsNormalsChoiceWithReadDanger) {
        LevelProfile normal = kNormalProfile;
        normal.readDanger = true;
        LevelProfile weighted = Weighted(normal);
        weighted.wSafe = kNormalWeightSafeStep;
        const TurnTally tally = ExpectSameTurns(weighted, normal);
        EXPECT_GT(tally.againstDeclared, 0) << "no turn had a declared opponent";
    }

    TEST(ComputerWeightsTest, TheSkipRollsDrawAsTheUnweightedSeatDoes) {
        for (const int percent : {30, 100}) {
            LevelProfile normal = kNormalProfile;
            normal.skipAcceptancePercent = percent;
            normal.skipDeclarationPercent = percent;
            ExpectSameTurns(Weighted(normal), normal, static_cast<uint32_t>(percent));
        }
    }

    TEST(ComputerWeightsTest, TheLevelsAreUnchangedWhenOnlyTheSwitchIsOn) {
        // Levels 5..10 are Normal's profile: the first four are the different ones.
        for (const LevelProfile* profile : {&kEasyProfile, &kLevel2Profile, &kLevel3Profile, &kLevel4Profile}) {
            EXPECT_GT(ExpectSameTurns(Weighted(*profile), *profile, 7).turns, 0);
        }
    }

    // ----- the value of a discard (`wValueDiscard`) -----

    namespace {

        /// Debug time: the value is searched on every `kValueStride`-th own turn only.
        constexpr int kValueStride = 5;

        /// A weighted profile that rates by the value of the discard alone.
        LevelProfile OnlyTheDiscardValue(int depth = 1) {
            LevelProfile profile = Weighted(kNormalProfile);
            profile.wShanten = profile.wAcceptance = profile.wKeep = profile.wSafe = 0;
            profile.wValueDiscard = 1;
            profile.weightedValueDepth = depth;
            return profile;
        }

    }  // namespace

    TEST(ComputerWeightsTest, WithNoWeightOnTheValueTheDepthDoesNotMatter) {
        for (const int depth : {0, 1, 2}) {
            LevelProfile profile = Weighted(kNormalProfile);
            profile.weightedValueDepth = depth;
            const TurnTally tally = ExpectSameTurns(profile, kNormalProfile, static_cast<uint32_t>(depth));
            EXPECT_GT(tally.ties, 0) << "depth " << depth;
        }
    }

    TEST(ComputerWeightsTest, WithNoDepthTheValueWeightChangesNothing) {
        LevelProfile profile = Weighted(kNormalProfile);
        profile.wValueDiscard = kWeightMax;
        profile.weightedValueDepth = 0;
        EXPECT_GT(ExpectSameTurns(profile, kNormalProfile).discards, 0);
    }

    TEST(ComputerWeightsTest, WithOnlyTheValueWeightedTheDiscardIsOneOfTheBestValue) {
        const LevelProfile profile = OnlyTheDiscardValue();
        int separated = 0;
        int chosenRated = 0;
        ForEachTurn([&](const TableView& view, int index, int step) {
            if (step % kValueStride != 0) {
                return;
            }
            const Vector<DiscardValue> values = ValueDiscards(view, profile.weightedValueDepth);
            if (values.empty()) {
                return;
            }
            int64_t best = values.front().expected;
            int64_t lowest = values.front().expected;
            for (const DiscardValue& value : values) {
                best = value.expected > best ? value.expected : best;
                lowest = value.expected < lowest ? value.expected : lowest;
            }
            // With every rated kind equal the unrated ones tie with them (all 0), so the tile may be any.
            if (best == lowest) {
                return;
            }
            ++separated;
            const Tile chosen = WeightedChoice(view, profile, static_cast<uint32_t>(step));
            for (const DiscardValue& value : values) {
                if (value.tile == chosen) {
                    ++chosenRated;
                    EXPECT_EQ(value.expected, best) << "hand " << index << " step " << step;
                }
            }
        });
        EXPECT_GT(separated, 0);
        EXPECT_EQ(chosenRated, separated) << "a kind that ValueDiscards does not rate was chosen over a rated one that is better";
    }

    TEST(ComputerWeightsTest, TheValueAboveTheLowestIsNeverNegativeAndTheUnratedKindsAreZero) {
        const LevelProfile profile = OnlyTheDiscardValue();
        int rated = 0;
        ForEachTurn([&](const TableView& view, int index, int step) {
            if (step % kValueStride != 0) {
                return;
            }
            const Array<int64_t, kKindCount> above = DiscardValueAbove(view, profile);
            const Vector<DiscardValue> values = ValueDiscards(view, profile.weightedValueDepth);
            Array<bool, kKindCount> listed{};
            int64_t lowest = values.empty() ? 0 : values.front().expected;
            for (const DiscardValue& value : values) {
                lowest = value.expected < lowest ? value.expected : lowest;
            }
            int64_t smallest = values.empty() ? 0 : above[values.front().tile.Id()];
            for (const DiscardValue& value : values) {
                listed[value.tile.Id()] = true;
                EXPECT_EQ(above[value.tile.Id()], value.expected - lowest) << "hand " << index << " step " << step;
                smallest = above[value.tile.Id()] < smallest ? above[value.tile.Id()] : smallest;
                ++rated;
            }
            EXPECT_EQ(smallest, 0) << "the worst rated kind is the zero";
            for (size_t id = 0; id < kKindCount; ++id) {
                EXPECT_GE(above[id], 0) << "hand " << index << " step " << step;
                if (!listed[id]) {
                    EXPECT_EQ(above[id], 0) << "kind " << id << " hand " << index << " step " << step;
                }
            }
            // The rating of step 5 carries it: `wValueDiscard * V` and the nine weights' part, which is 0 here.
            for (const WeightedDiscard& discard : RateDiscardsWeighted(view, CountTiles(view.own.concealed), MeldCountOf(view), profile, true)) {
                EXPECT_EQ(discard.value, above[discard.tile.Id()]);
                EXPECT_EQ(discard.rating, above[discard.tile.Id()]);
            }
        });
        EXPECT_GT(rated, 0);
    }

    TEST(ComputerWeightsTest, TheValueTermIsLeftOutOfARatingThatDoesNotAskForIt) {
        LevelProfile profile = Weighted(kNormalProfile);
        profile.wValueDiscard = 3;
        const TableView view = DiscardView("1p 2p 3p 4p 5p 6p 7p 8p 9p 1m 2m Gd Rd 5s");
        const Vector<WeightedDiscard> without = RateDiscardsWeighted(view, CountTiles(view.own.concealed), 0, profile);
        for (const WeightedDiscard& discard : without) {
            EXPECT_EQ(discard.value, 0);
            EXPECT_EQ(discard.rating, RateDiscardWeighted(kNormalProfile, discard.shanten, discard.acceptance, discard.keep, discard.danger));
        }
    }

    TEST(ComputerWeightsTest, AViewThatIsNotADiscardTurnRatesAsIfTheValueWeightWereZero) {
        TableView view = DiscardView("1p 2p 3p 4p 5p 6p 7p 8p 9p 1m 2m Gd Rd 5s");
        view.phase = Phase::Claim;
        const LevelProfile valued = OnlyTheDiscardValue();
        ASSERT_TRUE(ValueDiscards(view, valued.weightedValueDepth).empty());
        for (const int64_t above : DiscardValueAbove(view, valued)) {
            EXPECT_EQ(above, 0);
        }
        LevelProfile plain = Weighted(kNormalProfile);
        plain.wValueDiscard = 5;
        const TileCounts counts = CountTiles(view.own.concealed);
        const Vector<WeightedDiscard> with = RateDiscardsWeighted(view, counts, 0, plain, true);
        const Vector<WeightedDiscard> zero = RateDiscardsWeighted(view, counts, 0, Weighted(kNormalProfile), true);
        ASSERT_EQ(with.size(), zero.size());
        for (size_t i = 0; i < with.size(); ++i) {
            EXPECT_EQ(with[i].rating, zero[i].rating);
        }
        for (uint32_t seed = 1; seed <= 4; ++seed) {
            EXPECT_EQ(WeightedChoice(view, plain, seed), WeightedChoice(view, Weighted(kNormalProfile), seed));
        }
    }

    TEST(ComputerWeightsTest, TheLargestWeightsAndTheValueDoNotOverflowTheRating) {
        for (const int depth : {1, 2}) {
            LevelProfile profile = WeightedAt(kWeightMax);
            profile.weightedValueDepth = depth;
            int64_t largestValue = 0;
            int checked = 0;
            ForEachTurn([&](const TableView& view, int index, int step) {
                if (step % (kValueStride * 3) != 0) {
                    return;
                }
                const Array<int64_t, kKindCount> above = DiscardValueAbove(view, profile);
                for (const int64_t value : above) {
                    largestValue = value > largestValue ? value : largestValue;
                }
                for (const WeightedDiscard& discard : RateDiscardsWeighted(view, CountTiles(view.own.concealed), MeldCountOf(view), profile, true)) {
                    ++checked;
                    const int64_t featureBound = static_cast<int64_t>(kWeightMax) * (kShantenCeiling + kAcceptanceMax + kKeepValueTile + kDangerWorst);
                    EXPECT_EQ(discard.rating, RateDiscardWeighted(profile, discard.shanten, discard.acceptance, discard.keep, discard.danger, discard.value)) << "hand " << index << " step " << step;
                    EXPECT_GE(discard.rating, static_cast<int64_t>(kWeightMax) * discard.value) << "hand " << index << " step " << step;
                    EXPECT_LE(discard.rating, featureBound + static_cast<int64_t>(kWeightMax) * discard.value) << "hand " << index << " step " << step;
                }
            });
            EXPECT_GT(checked, 0) << "depth " << depth;
            EXPECT_GT(largestValue, 0) << "depth " << depth;
        }
    }

    TEST(ComputerWeightsTest, TheOwnDrawsDepthStillGoesBeforeTheWeightedPath) {
        LevelProfile profile = OnlyTheDiscardValue();
        profile.ownDrawsDepth = 1;
        profile.wShanten = kNormalWeightShanten;
        int decided = 0;
        ForEachTurn([&](const TableView& view, int index, int step) {
            if (step % kValueStride != 0) {
                return;
            }
            MahjongRng rng(static_cast<uint32_t>(index * 1000 + step));
            const TurnDecision decision = DecideTurn(view, rng, profile);
            const Vector<DiscardValue> values = ValueDiscards(view, profile.ownDrawsDepth);
            if (decision.step != TurnStep::Discard || values.empty()) {
                return;
            }
            ++decided;
            EXPECT_EQ(decision.action, HandAction::MakeDiscard(values.front().tile)) << "hand " << index << " step " << step;
        });
        EXPECT_GT(decided, 0);
    }

    // ----- a moved weight moves the choice -----

    TEST(ComputerWeightsTest, WithNoWeightOnAcceptanceTheLowerKeepValueIsDiscardedWhereNormalDiscardsTheLargerAcceptance) {
        // Three sets, 1m 2m and 5s 6s: the dragon is the lone tile and the shanten stays 1 whichever of the others goes.
        const TableView view = DiscardView("1p 2p 3p 4p 5p 6p 7p 8p 9p 1m 2m 5s 6s Gd");
        const TileCounts counts = CountTiles(view.own.concealed);
        const Vector<DiscardOption> normal = RateDiscards(counts, 0, view);
        ASSERT_EQ(normal.front().tile, T("Gd")) << "Normal discards the lone tile: the largest acceptance";
        ASSERT_GT(normal.front().acceptance, normal[1].acceptance);
        ASSERT_EQ(normal.front().shanten, normal[1].shanten);

        LevelProfile profile = Weighted(kNormalProfile);
        profile.wAcceptance = 0;
        EXPECT_EQ(WeightedChoice(view, profile), T("1m")) << "the terminal has the lowest keep value among the discards that keep the shanten";
        for (uint32_t seed = 1; seed <= 4; ++seed) {
            EXPECT_EQ(WeightedChoice(view, profile, seed), T("1m"));
        }
        EXPECT_EQ(WeightedChoice(view, Weighted(kNormalProfile)), T("Gd"));
    }

    TEST(ComputerWeightsTest, WithNoWeightOnShantenOrKeepTheLargestAcceptanceIsDiscardedAtAnyShanten) {
        LevelProfile profile = Weighted(kNormalProfile);
        profile.wShanten = 0;
        profile.wKeep = 0;
        int differs = 0;
        int higherShanten = 0;
        ForEachTurn([&](const TableView& view, int index, int step) {
            const Vector<WeightedDiscard> rated = RateDiscardsWeighted(view, CountTiles(view.own.concealed), MeldCountOf(view), profile);
            const WeightedDiscard* best = BestOf(rated);
            if (best == nullptr) {
                return;
            }
            Vector<Tile> ties;
            for (const WeightedDiscard& discard : rated) {
                EXPECT_LE(discard.acceptance, best->acceptance) << "hand " << index << " step " << step;
                if (discard.acceptance == best->acceptance) {
                    ties.push_back(discard.tile);
                }
            }
            const Tile chosen = WeightedChoice(view, profile, static_cast<uint32_t>(step));
            EXPECT_NE(eastl::find(ties.begin(), ties.end(), chosen), ties.end()) << "hand " << index << " step " << step;
            const Vector<DiscardOption> normal = RateDiscards(CountTiles(view.own.concealed), MeldCountOf(view), view);
            for (const WeightedDiscard& discard : rated) {
                if (discard.tile == chosen) {
                    differs += discard.tile != normal.front().tile ? 1 : 0;
                    higherShanten += discard.shanten > normal.front().shanten ? 1 : 0;
                }
            }
        });
        EXPECT_GT(differs, 0);
        EXPECT_GT(higherShanten, 0) << "no turn gave up a shanten for acceptance";
    }

    TEST(ComputerWeightsTest, TheSafetyWeightDiscardsTheSafeTileWhereTheDefaultsDiscardTheDangerousOne) {
        TableView view = DiscardView("1p 2p 3p 4p 5p 6p 7p 8p 9p 1m 2m Gd Rd 5s", true);
        view.seats[kSeat1].passedKinds[T("Gd").Id()] = 1;
        ASSERT_EQ(DangerOf(view, T("5s")), Danger::High);
        ASSERT_EQ(DangerOf(view, T("Gd")), Danger::Safe);
        LevelProfile safe = Weighted(kNormalProfile);
        safe.wSafe = kNormalWeightSafeStep;
        for (uint32_t seed = 1; seed <= 4; ++seed) {
            EXPECT_EQ(WeightedChoice(view, Weighted(kNormalProfile), seed), T("5s")) << "wSafe 0: the keep value decides";
            EXPECT_EQ(WeightedChoice(view, safe, seed), T("Gd")) << "wSafe above the keep span";
        }
        // The danger is read only with a declared opponent: without one the same weight changes nothing.
        TableView calm = view;
        calm.seats[kSeat1].callingDeclared = false;
        EXPECT_EQ(WeightedChoice(calm, safe), T("5s"));
    }

    TEST(ComputerWeightsTest, ATieIsBrokenByTheRngAndOnlyATieDrawsANumber) {
        const TableView view = DiscardView("1p 2p 3p 4p 5p 6p 7p 8p 9p 1m 2m Gd Rd Wd");
        LevelProfile profile = Weighted(kNormalProfile);
        Set<uint8_t> picked;
        for (uint32_t seed = 1; seed <= 16; ++seed) {
            picked.insert(WeightedChoice(view, profile, seed).Id());
        }
        EXPECT_EQ(picked.size(), 3u) << "the three dragons rate the same";

        const TableView single = DiscardView("1p 2p 3p 4p 5p 6p 7p 8p 9p 1m 2m Gd Rd 5s");
        MahjongRng rng(5);
        MahjongRng untouched(5);
        EXPECT_EQ(ChooseWeightedDiscard(single, rng, profile), T("5s"));
        EXPECT_TRUE(SameStream(rng, untouched)) << "one best discard draws nothing";
    }

    // ----- any weights play legally -----

    TEST(ComputerWeightsTest, ForAnyWeightsTheAnswerIsListed) {
        MahjongRng weights(77);
        Vector<LevelProfile> profiles;
        profiles.push_back(Weighted(kNormalProfile));
        for (int i = 0; i < 6; ++i) {
            profiles.push_back(RandomWeights(weights));
        }
        LevelProfile zeros = Weighted(kNormalProfile);
        zeros.wShanten = zeros.wAcceptance = zeros.wKeep = zeros.wSafe = 0;
        profiles.push_back(zeros);
        LevelProfile largest = Weighted(kNormalProfile);
        largest.wShanten = largest.wAcceptance = largest.wKeep = largest.wSafe = kWeightMax;
        profiles.push_back(largest);
        ForEachTurn([&](const TableView& view, int index, int step) {
            for (const LevelProfile& profile : profiles) {
                // The own-draws value is dear in Debug: a profile that asks for it plays every third of the `kValueStride`-th turns.
                if (profile.wValueDiscard > 0 && step % (kValueStride * 3) != 0) {
                    continue;
                }
                MahjongRng rng(static_cast<uint32_t>(index * 1000 + step));
                const HandAction answer = ChooseTurnAction(view, rng, profile);
                EXPECT_TRUE(Has(view.legal, answer)) << "hand " << index << " step " << step;
            }
        });
    }

    // ----- the claim windows -----

    TEST(ComputerWeightsTest, WithTheDefaultWeightsAWindowIsNormalsAnswerAndNormalsDraws) {
        const WindowTally tally = ExpectSameWindows(Weighted(kNormalProfile), kNormalProfile);
        EXPECT_GT(tally.windows, kRandomHands);
        EXPECT_GT(tally.claims, 0);
        EXPECT_GT(tally.passes, 0);
        EXPECT_GT(tally.kongs, 0);
        EXPECT_GT(tally.lowersAndLosesAcceptance, 0) << "no window had a claim that lowers the shanten and loses acceptance";
        EXPECT_GT(tally.equalAndGainsAcceptanceOnly, 0) << "no window had an equal-shanten claim that only gains acceptance";
    }

    TEST(ComputerWeightsTest, TheClaimRollDrawsAsTheUnweightedSeatDoes) {
        for (const int percent : {30, 100}) {
            LevelProfile normal = kNormalProfile;
            normal.skipClaimPercent = percent;
            ExpectSameWindows(Weighted(normal), normal, static_cast<uint32_t>(percent));
        }
        // Levels 3 and 4 take value claims at an equal shanten, as the weights do (`takeValueClaimsAtEqualShanten` is not read).
        for (const LevelProfile* profile : {&kLevel3Profile, &kLevel4Profile}) {
            ExpectSameWindows(Weighted(*profile), *profile, 5);
        }
    }

    TEST(ComputerWeightsTest, ANamedWindowIsAnsweredAsNormalAnswers) {
        // A value pung at an equal shanten is taken, a plain one passed, a pung that lowers the shanten taken, the kong over the pung,
        // a chow that lowers the shanten taken, a chow that breaks a set passed.
        struct Case {
            const char* hand;
            const char* discard;
            HandAction expected;
        };
        const Vector<Case> cases = {
            {"5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p", kPung},
            {"Rd Rd Rd 1s 2s 3s 4s 5s 6s 1m 9m Nw Ww", "Rd", HandAction::MakeClaim(ClaimKind::Kong)},
            {"4p 6p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p", HandAction::MakeClaim(ClaimKind::Chow, T("4p"))},
            {"4p 5p 6p 1s 2s 3s 4s 5s 6s 9m 9m Nw Ww", "3p", kPass},
        };
        for (const Case& item : cases) {
            const TableView view = MakeTableView(WindowModel(item.hand, item.discard), kSeat1);
            ExpectClaimForEverySeed(view, Weighted(kNormalProfile), item.expected);
        }
        const Vector<Pair<String, const char*>> value = {{String(Format("{} Rd Rd", kPartials)), "Rd"}, {String(Format("{} Sw Sw", kPartials)), "Sw"}, {String(Format("{} Ew Ew", kPartials)), "Ew"}};
        for (const auto& [hand, discard] : value) {
            ExpectClaimForEverySeed(MakeTableView(WindowModel(hand, discard), kSeat1), Weighted(kNormalProfile), kPung);
        }
        const String simple = Format("{} 5m 5m", kPartials);
        ExpectClaimForEverySeed(MakeTableView(WindowModel(simple, "5m"), kSeat1), Weighted(kNormalProfile), kPass);
        // A pung and a chow of the same tile: the one Normal takes.
        const TableView both = MakeTableView(WindowModel("4p 6p 5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m", "5p"), kSeat1);
        ASSERT_TRUE(Has(both.legal, kPung));
        ASSERT_TRUE(Has(both.legal, HandAction::MakeClaim(ClaimKind::Chow, T("4p"))));
        EXPECT_EQ(ClaimChoice(both, Weighted(kNormalProfile)), ClaimChoice(both, kNormalProfile));
    }

    TEST(ComputerWeightsTest, AZeroMarginTakesTheEqualShantenClaimThatOnlyGainsAcceptanceAndAHugeOnePassesEverything) {
        // Four partial sets, a pair of 5m, three single tiles: the pung of the 5m leaves the shanten (3) as it is.
        const TableView plain = MakeTableView(WindowModel(Format("{} 5m 5m", kPartials).c_str(), "5m"), kSeat1);
        const int before = Shanten(CountTiles(plain.own.concealed), 0);
        const Vector<WeightedClaim> rated = RateClaimsWeighted(plain, before, Weighted(kNormalProfile));
        ASSERT_EQ(rated.size(), 1u);
        ASSERT_EQ(rated[0].shanten, before);
        const int gain = rated[0].acceptance - AcceptanceOf(plain, CountTiles(plain.own.concealed), 0);
        ASSERT_GT(gain, 0) << "the pung must gain acceptance for the zero margin to take it";
        ExpectClaimForEverySeed(plain, Weighted(kNormalProfile), kPass);
        LevelProfile zero = Weighted(kNormalProfile);
        zero.wClaimMargin = 0;
        ExpectClaimForEverySeed(plain, zero, kPung);

        const TableView lowers = MakeTableView(WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p"), kSeat1);
        ExpectClaimForEverySeed(lowers, Weighted(kNormalProfile), kPung);
        LevelProfile huge = Weighted(kNormalProfile);
        huge.wClaimMargin = kWeightMax;
        ExpectClaimForEverySeed(lowers, huge, kPass);
        ExpectClaimForEverySeed(plain, huge, kPass);
    }

    TEST(ComputerWeightsTest, TheWallSlopeMakesAnEarlyWallPassWhatALateWallTakes) {
        TableView early = MakeTableView(WindowModel("5p 5p 1s 2s 3s 4s 5s 6s 7m 8m 9m Nw Wd", "5p"), kSeat1);
        const int before = Shanten(CountTiles(early.own.concealed), 0);
        const LevelProfile base = Weighted(kNormalProfile);
        const Vector<WeightedClaim> rated = RateClaimsWeighted(early, before, base);
        ASSERT_EQ(rated.size(), 1u);
        const int64_t pass = RateHandWeighted(base, before, AcceptanceOf(early, CountTiles(early.own.concealed), 0));
        const int64_t slack = rated[0].rating - pass - base.wClaimMargin;
        ASSERT_GT(slack, 0);
        // Slack per tile of the wall: the claim passes the gate while `wClaimWall * liveCount` is below `slack * kWallWeightUnit`.
        LevelProfile sloped = base;
        sloped.wClaimWall = static_cast<int>(slack * 2);
        constexpr int kEarlyLive = 100;
        constexpr int kLateLive = 10;
        TableView late = early;
        early.liveCount = kEarlyLive;
        late.liveCount = kLateLive;
        ExpectClaimForEverySeed(late, sloped, kPung);
        ExpectClaimForEverySeed(early, sloped, kPass);
        // With no slope the wall does not matter.
        ExpectClaimForEverySeed(early, base, kPung);
    }

    // ----- the value of a claim (`wValueClaim`) -----

    namespace {

        /// A weighted profile whose gate and order are the claim's own-draws gain alone: a claim is acceptable exactly when `ValueClaims`
        /// puts it strictly above the pass.
        LevelProfile OnlyTheClaimValue(int depth = 1) {
            LevelProfile profile = Weighted(kNormalProfile);
            profile.wShanten = profile.wAcceptance = profile.wKeep = profile.wSafe = 0;
            profile.wClaimRank = profile.wClaimValue = profile.wClaimMargin = profile.wClaimWall = 0;
            profile.wValueClaim = 1;
            profile.weightedValueDepth = depth;
            return profile;
        }

        ClaimValue ValueOfAnswer(const HandAction& action, int64_t expected) {
            ClaimValue value;
            value.action = action;
            value.value.expected = expected;
            return value;
        }

    }  // namespace

    TEST(ComputerWeightsTest, WithNoWeightOnTheClaimValueTheDepthDoesNotMatterInAWindow) {
        for (const int depth : {0, 1, 2}) {
            LevelProfile profile = Weighted(kNormalProfile);
            profile.weightedValueDepth = depth;
            EXPECT_GT(ExpectSameWindows(profile, kNormalProfile, static_cast<uint32_t>(depth)).claims, 0) << "depth " << depth;
        }
        LevelProfile noDepth = Weighted(kNormalProfile);
        noDepth.wValueClaim = kWeightMax;
        noDepth.weightedValueDepth = 0;
        EXPECT_GT(ExpectSameWindows(noDepth, kNormalProfile).windows, 0);
    }

    TEST(ComputerWeightsTest, WithOnlyTheClaimValueWeightedAClaimIsTakenExactlyWhenItIsWorthMoreThanThePass) {
        const LevelProfile profile = OnlyTheClaimValue();
        int above = 0;
        int notAbove = 0;
        int unique = 0;
        int checked = 0;
        ForEachWindow([&](const TableView& view, int index, int window) {
            if (window % kValueStride != 0) {
                return;
            }
            MahjongRng rng(static_cast<uint32_t>(index * 1000 + window));
            const ClaimDecision decision = DecideClaim(view, rng, profile);
            if (decision.step != ClaimStep::Claim && decision.step != ClaimStep::NoGain) {
                return;
            }
            ++checked;
            const Vector<ClaimValue> values = ValueClaims(view, OwnDrawsModel{.depth = profile.weightedValueDepth});
            int64_t passValue = 0;
            for (const ClaimValue& value : values) {
                passValue = value.action.kind == ActionKind::Pass ? value.value.expected : passValue;
            }
            int64_t bestValue = passValue;
            int bestCount = 0;
            for (const ClaimValue& value : values) {
                if (value.action.kind != ActionKind::Pass && value.value.expected > bestValue) {
                    bestValue = value.value.expected;
                }
            }
            for (const ClaimValue& value : values) {
                bestCount += value.action.kind != ActionKind::Pass && value.value.expected == bestValue ? 1 : 0;
            }
            const char* const where = "hand ";
            if (bestValue > passValue) {
                ++above;
                EXPECT_EQ(decision.action.kind, ActionKind::Claim) << where << index << " window " << window;
                for (const ClaimValue& value : values) {
                    if (value.action == decision.action) {
                        EXPECT_EQ(value.value.expected, bestValue) << where << index << " window " << window;
                    }
                }
                if (bestCount == 1) {
                    ++unique;
                    for (const ClaimValue& value : values) {
                        if (value.action.kind != ActionKind::Pass && value.value.expected == bestValue) {
                            EXPECT_EQ(decision.action, value.action) << where << index << " window " << window;
                        }
                    }
                }
            } else {
                ++notAbove;
                EXPECT_EQ(decision.action, kPass) << where << index << " window " << window;
            }
        });
        EXPECT_GT(checked, 0);
        EXPECT_GT(above, 0) << "no window had a claim worth more than the pass";
        EXPECT_GT(notAbove, 0) << "no window had every claim at or below the pass";
        EXPECT_GT(unique, 0);
    }

    TEST(ComputerWeightsTest, TheGainOfAClaimThatIsNotListedAndOfAnEmptyAnswerIsZero) {
        const HandAction chow = HandAction::MakeClaim(ClaimKind::Chow, T("1p"));
        const HandAction kong = HandAction::MakeClaim(ClaimKind::Kong);
        EXPECT_EQ(ClaimValueGain({}, kPung), 0);
        EXPECT_EQ(ClaimValueGain({ValueOfAnswer(kPung, 900)}, kPung), 0) << "no pass in the answer";
        const Vector<ClaimValue> values{ValueOfAnswer(kPung, 900), ValueOfAnswer(kPass, 400), ValueOfAnswer(chow, 100)};
        EXPECT_EQ(ClaimValueGain(values, kPung), 500);
        EXPECT_EQ(ClaimValueGain(values, kong), 0) << "a kong the answer does not list";
        EXPECT_EQ(ClaimValueGain(values, kPass), 0);
        EXPECT_EQ(ClaimValueGain(values, chow), -300) << "the gain is signed";
        EXPECT_EQ(ClaimValueGain(values, HandAction::MakeClaim(ClaimKind::Chow, T("2p"))), 0) << "another chow is another claim";
    }

    TEST(ComputerWeightsTest, TheGainIsSignedSoAClaimWorthLessThanThePassLowersItsRatingAndCanFailTheGate) {
        LevelProfile normal = Weighted(kNormalProfile);
        LevelProfile valued = normal;
        valued.wValueClaim = kNormalWeightClaimMargin;
        int lowered = 0;
        int raised = 0;
        int failsTheGate = 0;
        ForEachWindow([&](const TableView& view, int index, int window) {
            if (window % kValueStride != 0) {
                return;
            }
            const int before = Shanten(CountTiles(view.own.concealed), MeldCountOf(view));
            const Vector<WeightedClaim> base = RateClaimsWeighted(view, before, normal);
            const Vector<WeightedClaim> with = RateClaimsWeighted(view, before, valued);
            ASSERT_EQ(base.size(), with.size());
            for (size_t i = 0; i < base.size(); ++i) {
                EXPECT_EQ(base[i].valueGain, 0) << "weight 0: no gain is looked at";
                EXPECT_EQ(with[i].rating, base[i].rating + static_cast<int64_t>(valued.wValueClaim) * with[i].valueGain) << "hand " << index << " window " << window;
                lowered += with[i].valueGain < 0 ? 1 : 0;
                raised += with[i].valueGain > 0 ? 1 : 0;
                failsTheGate += with[i].valueGain < 0 && base[i].acceptable && !with[i].acceptable ? 1 : 0;
            }
        });
        EXPECT_GT(lowered, 0) << "no claim was worth less than the pass";
        EXPECT_GT(raised, 0);
        EXPECT_GT(failsTheGate, 0) << "no acceptable claim failed the gate on a negative gain";
    }

    TEST(ComputerWeightsTest, TheLargestWeightsAndTheClaimValueDoNotOverflowTheRating) {
        for (const int depth : {1, 2}) {
            LevelProfile profile = WeightedAt(kWeightMax);
            profile.weightedValueDepth = depth;
            int64_t largest = 0;
            int checked = 0;
            ForEachWindow([&](const TableView& view, int index, int window) {
                if (window % (kValueStride * 3) != 0) {
                    return;
                }
                const int before = Shanten(CountTiles(view.own.concealed), MeldCountOf(view));
                for (const WeightedClaim& claim : RateClaimsWeighted(view, before, profile)) {
                    ++checked;
                    const int64_t gain = claim.valueGain < 0 ? -claim.valueGain : claim.valueGain;
                    largest = gain > largest ? gain : largest;
                    const int64_t features = static_cast<int64_t>(kWeightMax) * (kShantenCeiling + kAcceptanceMax + kClaimRankMax);
                    const int64_t magnitude = claim.rating < 0 ? -claim.rating : claim.rating;
                    EXPECT_LE(magnitude, features + static_cast<int64_t>(kWeightMax) * gain) << "hand " << index << " window " << window;
                    EXPECT_EQ(ClaimPassesGate(profile, claim.rating, 0, false, view.liveCount),
                              (claim.rating - profile.wClaimMargin) * kWallWeightUnit > static_cast<int64_t>(profile.wClaimWall) * view.liveCount);
                }
            });
            EXPECT_GT(checked, 0) << "depth " << depth;
            EXPECT_GT(largest, 0) << "depth " << depth;
        }
    }

    TEST(ComputerWeightsTest, TheOwnDrawsClaimDepthStillGoesBeforeTheWeightedPath) {
        LevelProfile profile = OnlyTheClaimValue();
        profile.ownDrawsClaimDepth = 1;
        profile.wClaimValue = kNormalWeightClaimValue;
        int decided = 0;
        ForEachWindow([&](const TableView& view, int index, int window) {
            if (window % kValueStride != 0) {
                return;
            }
            MahjongRng rng(static_cast<uint32_t>(index * 1000 + window));
            const ClaimDecision decision = DecideClaim(view, rng, profile);
            const int before = Shanten(CountTiles(view.own.concealed), MeldCountOf(view));
            const Vector<ClaimValue> values = ValueClaims(view, OwnDrawsModel{.depth = profile.ownDrawsClaimDepth});
            if (before <= kWaitingShanten || values.empty() || decision.step == ClaimStep::Mahjong) {
                return;
            }
            ++decided;
            EXPECT_EQ(decision.action, values.front().action) << "hand " << index << " window " << window;
        });
        EXPECT_GT(decided, 0);
    }

    TEST(ComputerWeightsTest, ForAnyWeightsTheClaimAnswerIsListed) {
        MahjongRng weights(78);
        Vector<LevelProfile> profiles;
        for (int i = 0; i < 6; ++i) {
            profiles.push_back(RandomWeights(weights));
        }
        LevelProfile zeros = Weighted(kNormalProfile);
        zeros.wShanten = zeros.wAcceptance = zeros.wKeep = zeros.wSafe = zeros.wClaimRank = zeros.wClaimValue = zeros.wClaimMargin = zeros.wClaimWall = 0;
        profiles.push_back(zeros);
        LevelProfile largest = zeros;
        largest.wShanten = largest.wAcceptance = largest.wKeep = largest.wSafe = largest.wClaimRank = largest.wClaimValue = largest.wClaimMargin = largest.wClaimWall = kWeightMax;
        profiles.push_back(largest);
        ForEachWindow([&](const TableView& view, int index, int window) {
            for (const LevelProfile& profile : profiles) {
                MahjongRng rng(static_cast<uint32_t>(index * 1000 + window));
                EXPECT_TRUE(Has(view.legal, ChooseClaimAction(view, rng, profile))) << "hand " << index << " window " << window;
            }
        });
    }

}  // namespace BECore::Mahjong::Tests
