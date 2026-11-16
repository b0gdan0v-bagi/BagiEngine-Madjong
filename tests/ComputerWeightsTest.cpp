#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/ComputerWeights.h>
#include <Mahjong/Game/DiscardDanger.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/Match.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>
#include <tests/HandTestSupport.h>

namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kRandomHands = 30;
        constexpr int kTurnsPerHand = 30;
        constexpr uint32_t kRandomFirstSeed = 900;
        /// Above every count of the rng that a player draws in one decision.
        constexpr uint32_t kProbeRange = 1u << 30;

        LevelProfile Weighted(LevelProfile profile) {
            profile.weighted = true;
            return profile;
        }

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
        // Levels 5..10 are Normal's profile: the first five are the different ones.
        for (const LevelProfile* profile : {&kEasyProfile, &kLevel2Profile, &kLevel3Profile, &kLevel4Profile}) {
            EXPECT_GT(ExpectSameTurns(Weighted(*profile), *profile, 7).turns, 0);
        }
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
        (void)ChooseWeightedDiscard(single, rng, profile);
        EXPECT_TRUE(SameStream(rng, untouched)) << "one best discard draws nothing";
    }

    // ----- any weights play legally -----

    TEST(ComputerWeightsTest, ForAnyWeightsTheAnswerIsListed) {
        MahjongRng weights(77);
        Vector<LevelProfile> profiles;
        profiles.push_back(Weighted(kNormalProfile));
        for (int i = 0; i < 6; ++i) {
            LevelProfile profile = Weighted(kNormalProfile);
            profile.wShanten = static_cast<int>(weights.Below(kWeightMax + 1));
            profile.wAcceptance = static_cast<int>(weights.Below(kWeightMax + 1));
            profile.wKeep = static_cast<int>(weights.Below(kWeightMax + 1));
            profile.wSafe = static_cast<int>(weights.Below(kWeightMax + 1));
            profiles.push_back(profile);
        }
        LevelProfile zeros = Weighted(kNormalProfile);
        zeros.wShanten = zeros.wAcceptance = zeros.wKeep = zeros.wSafe = 0;
        profiles.push_back(zeros);
        LevelProfile largest = Weighted(kNormalProfile);
        largest.wShanten = largest.wAcceptance = largest.wKeep = largest.wSafe = kWeightMax;
        profiles.push_back(largest);
        ForEachTurn([&](const TableView& view, int index, int step) {
            for (const LevelProfile& profile : profiles) {
                MahjongRng rng(static_cast<uint32_t>(index * 1000 + step));
                const HandAction answer = ChooseTurnAction(view, rng, profile);
                EXPECT_TRUE(Has(view.legal, answer)) << "hand " << index << " step " << step;
            }
        });
    }

}  // namespace BECore::Mahjong::Tests
