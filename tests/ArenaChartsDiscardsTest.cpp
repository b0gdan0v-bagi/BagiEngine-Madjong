#include <Mahjong/Arena/ArenaChartsDiscards.h>
#include <Mahjong/Arena/ArenaWorlds.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>

namespace BECore::Mahjong::Arena {

    namespace {

        using namespace Tests;

        constexpr int kEnoughLiveTiles = 60;
        constexpr uint32_t kFirstSeed = 1;
        constexpr int kSeeds = 8;
        constexpr int kWorlds = 24;
        constexpr int kThreads = 3;
        constexpr uint32_t kHandSeed = 77;
        constexpr int kTurnIndex = 3;

        /// The own discard turn of seat 0 holding `concealed` (14 tiles less three a meld): every distinct tile is listed as a plain discard
        /// and the seat sees its own tiles only. A view of a real model is used where the legal list or the seen tiles matter.
        TableView TurnOf(const char* concealed, Wind seatWind = Wind::South, int liveCount = kEnoughLiveTiles) {
            TableView view;
            view.seat = kSeat0;
            view.phase = Phase::Discard;
            view.currentSeat = kSeat0;
            view.seatWind = seatWind;
            view.roundWind = Wind::East;
            view.own.concealed = ParseTiles(concealed).value();
            view.liveCount = liveCount;
            view.visible = CountTiles(view.own.concealed);
            Tile previous;
            for (const Tile tile : view.own.concealed) {
                if (!(tile == previous)) {
                    view.legal.push_back(HandAction::MakeDiscard(tile));
                }
                previous = tile;
            }
            return view;
        }

        Meld OpenPung(const char* tile) {
            Meld meld;
            meld.kind = MeldKind::Pung;
            meld.first = T(tile);
            return meld;
        }

        Meld ConcealedKong(const char* tile) {
            Meld meld;
            meld.kind = MeldKind::Kong;
            meld.concealed = true;
            meld.first = T(tile);
            return meld;
        }

        // The hands below are first turns of dealt walls, found by search; each comment is what the two readings do with the hand (seat wind
        // South, round wind East, so the dragons and Ew are value tiles).
        // Normal throws the 1s, the points reading the 4s, which costs a shanten.
        constexpr const char* kHigherHand = "1p 2p 4p 5p 8p 8p 9p 1s 4s 4s 5s 7s Ew Wd";
        // Normal throws the Wd, the points reading the 8p; the shanten stays.
        constexpr const char* kSameTwoHand = "8p 9p 9p 2s 3s 4s 5s 5s 6s 6s 7s 8s Rd Wd";
        // Normal throws the 9p, the points reading the Wd; the shanten stays.
        constexpr const char* kSameOneHand = "1p 2p 3p 3p 4p 5p 6p 7p 7p 9p 2s 3s 4s Wd";
        // Normal throws the Wd, the points reading the 7p; the shanten stays.
        constexpr const char* kSameThreeHand = "2p 3p 3p 4p 7p 8p 8p 2s 6s 7s 8s 8s Ew Wd";
        // The valued tile is one of Normal's ties here.
        constexpr const char* kTiedHand = "3p 4p 4p 5p 8p 2s 3s 6s 8s 3m 8m Ew Nw Gd";

        DiscardSelection Select(const TableView& view) {
            return SelectDiscardPair(view);
        }

        DiscardClass Measured(const TableView& view) {
            const DiscardSelection selection = Select(view);
            EXPECT_EQ(selection.outcome, DiscardOutcome::Measured);
            return ClassifyDiscard(view, selection.pair);
        }

        // East holds `east` (13 tiles) and draws the first tile of `live`; the others hold the quiet hands unless the test names theirs.
        HandModel TurnModel(const char* east, const String& live, const char* seat2 = kQuiet2, const char* seat3 = kQuiet3) {
            FixedWallSpec spec = SpecOf(east, live);
            spec.hands[kSeat2] = seat2;
            spec.hands[kSeat3] = seat3;
            return MakeModel(spec);
        }

        ArenaPlayers NormalPlayers() {
            const std::expected<ArenaPlayer, String> normal = ParseArenaPlayer("normal");
            EXPECT_TRUE(normal.has_value());
            ArenaPlayers players;
            players.fill(normal.value_or(ArenaPlayer{}));
            return players;
        }

        DiscardTurns Collect(uint32_t seed) {
            const std::expected<DiscardTurns, String> turns = CollectDiscardTurns(seed, NormalPlayers());
            EXPECT_TRUE(turns.has_value()) << (turns.has_value() ? "" : turns.error().c_str());
            return turns.value_or(DiscardTurns{});
        }

        String Describe(const DiscardTurns& collected) {
            String text = Format("{} {} {} {} {}", collected.hands, collected.ownTurns, collected.otherStep, collected.noValue, collected.sameAsNormal);
            for (const DiscardTurn& turn : collected.turns) {
                text += Format("\n{} seat {} live {} fast {} valued {} |", turn.index, static_cast<int>(turn.view.seat), turn.view.liveCount, ToString(turn.pair.fast), ToString(turn.pair.valued));
                for (const Tile tile : turn.view.own.concealed) {
                    text += Format(" {}", ToString(tile));
                }
            }
            return text;
        }

        /// East holds the hand of `kHigherHand` less its last tile and draws it: the turn of the tests that measure a row.
        TableView MeasuredTurn() {
            const HandModel model = TurnModel("1p 2p 4p 5p 8p 8p 9p 1s 4s 4s 5s 7s Ew", "Wd Rd Wd 1p");
            return MakeTableView(model, kSeat0);
        }

        DiscardMeasure Measure(const TableView& view, const DiscardPair& pair, const WorldLoop& loop = SerialWorldLoop) {
            const std::expected<DiscardMeasure, String> measured = MeasureDiscardTurn(view, pair, kHandSeed, kTurnIndex, kWorlds, loop);
            EXPECT_TRUE(measured.has_value()) << (measured.has_value() ? "" : measured.error().c_str());
            return measured.value_or(DiscardMeasure{});
        }

        /// The mean points of `action` in the worlds of the two seeded runs the row is made of, played by the test itself.
        double MeanOf(const Vector<Vector<int>>& values, size_t row) {
            Vector<double> doubles;
            for (const int value : values[row]) {
                doubles.push_back(static_cast<double>(value));
            }
            return MeanCi(doubles).mean;
        }

    }  // namespace

    TEST(ArenaChartsDiscardsTest, ShantenBinsAtTheirEdges) {
        EXPECT_EQ(DiscardShantenBin(0), DiscardShanten::One);
        EXPECT_EQ(DiscardShantenBin(1), DiscardShanten::One);
        EXPECT_EQ(DiscardShantenBin(2), DiscardShanten::Two);
        EXPECT_EQ(DiscardShantenBin(3), DiscardShanten::ThreeOrMore);
        EXPECT_EQ(DiscardShantenBin(4), DiscardShanten::ThreeOrMore);
    }

    TEST(ArenaChartsDiscardsTest, CostIsHigherOnlyWhenTheValuedDiscardLeavesAHigherShanten) {
        EXPECT_EQ(ValueCostOf(2, 2), ValueCost::Same);
        EXPECT_EQ(ValueCostOf(2, 3), ValueCost::Higher);
        EXPECT_EQ(ValueCostOf(1, 2), ValueCost::Higher);
    }

    TEST(ArenaChartsDiscardsTest, ValueHeldBinsAtTheirEdges) {
        EXPECT_EQ(ValueHeldBin(0), ValueHeld::None);
        EXPECT_EQ(ValueHeldBin(1), ValueHeld::Single);
        EXPECT_EQ(ValueHeldBin(2), ValueHeld::PairOrSet);
        EXPECT_EQ(ValueHeldBin(3), ValueHeld::PairOrSet);
        EXPECT_EQ(ValueHeldBin(4), ValueHeld::PairOrSet);
    }

    TEST(ArenaChartsDiscardsTest, TheMostCopiesOfAValueTileCountTheMeldsAndNoOtherTile) {
        // Seat wind South, round wind East: the dragons, Sw and Ew are value tiles, Ww and Nw are not.
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Ww Nw Nw 5p")), 0);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Ww Nw Nw Gd")), 1);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Ww Nw Sw Sw")), 2);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Nw Rd")), 3);
        // The dragon, the own wind and the round wind each count; another seat's wind does not.
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Wd Wd Nw Nw 5p")), 2);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Nw Nw 5p")), 2);
        EXPECT_EQ(MostValueCopies(TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Ww Nw Nw 5p", Wind::West)), 2);

        TableView meld = TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Nw 5p");
        meld.own.melds.push_back(OpenPung("Rd"));
        EXPECT_EQ(MostValueCopies(meld), 3);
        // A meld and a copy in the hand are one holding.
        meld.own.concealed = ParseTiles("1p 2p 3p 4s 5s 6s 7m 8m Rd Ww Nw").value();
        EXPECT_EQ(MostValueCopies(meld), 4);
        // A chow holds no value tile; a pung of a plain tile is no value.
        TableView plain = TurnOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Nw 5p");
        plain.own.melds.push_back(OpenPung("4p"));
        EXPECT_EQ(MostValueCopies(plain), 0);
    }

    TEST(ArenaChartsDiscardsTest, TheValuedDiscardThatCostsAShantenIsHigher) {
        const TableView view = TurnOf(kHigherHand, Wind::East);
        const DiscardSelection selection = Select(view);
        ASSERT_EQ(selection.outcome, DiscardOutcome::Measured);
        EXPECT_EQ(selection.pair.fast, T("1s"));
        EXPECT_EQ(selection.pair.valued, T("4s"));

        const DiscardClass cls = ClassifyDiscard(view, selection.pair);
        EXPECT_EQ(cls.cost, ValueCost::Higher);
        EXPECT_EQ(cls.shanten, DiscardShanten::ThreeOrMore);
        EXPECT_EQ(cls.valueHeld, ValueHeld::Single);
        EXPECT_EQ(cls.stage, WallStage::Early);
        EXPECT_EQ(cls.openSets, OpenSets::None);
        EXPECT_TRUE(cls.east);
        EXPECT_FALSE(ClassifyDiscard(TurnOf(kHigherHand, Wind::South), selection.pair).east);
    }

    TEST(ArenaChartsDiscardsTest, TheValuedDiscardThatKeepsTheShantenIsSameAtEveryShanten) {
        struct Case {
            const char* hand;
            const char* fast;
            const char* valued;
            DiscardShanten shanten;
        };
        const Case cases[] = {
            {kSameOneHand, "9p", "Wd", DiscardShanten::One},
            {kSameTwoHand, "Wd", "8p", DiscardShanten::Two},
            {kSameThreeHand, "Wd", "7p", DiscardShanten::ThreeOrMore},
        };
        for (const Case& test : cases) {
            const TableView view = TurnOf(test.hand);
            const DiscardSelection selection = Select(view);
            ASSERT_EQ(selection.outcome, DiscardOutcome::Measured) << test.hand;
            EXPECT_EQ(selection.pair.fast, T(test.fast)) << test.hand;
            EXPECT_EQ(selection.pair.valued, T(test.valued)) << test.hand;
            const DiscardClass cls = ClassifyDiscard(view, selection.pair);
            EXPECT_EQ(cls.cost, ValueCost::Same) << test.hand;
            EXPECT_EQ(cls.shanten, test.shanten) << test.hand;
            EXPECT_EQ(cls.valueHeld, ValueHeld::Single) << test.hand;
            EXPECT_FALSE(cls.east);
        }
    }

    TEST(ArenaChartsDiscardsTest, TheClassFollowsTheStageAndTheOpenMelds) {
        const DiscardPair pair{T("1s"), T("4s")};
        ASSERT_EQ(Select(TurnOf(kHigherHand)).pair, pair);

        // The cut values of the wall are those of the claim charts.
        const auto stageAt = [&pair](int liveCount) { return ClassifyDiscard(TurnOf(kHigherHand, Wind::South, liveCount), pair).stage; };
        EXPECT_EQ(stageAt(kLiveWallAtDeal), WallStage::Early);
        EXPECT_EQ(stageAt(2 * kLiveStageThird + 1), WallStage::Early);
        EXPECT_EQ(stageAt(2 * kLiveStageThird), WallStage::Middle);
        EXPECT_EQ(stageAt(kLiveStageThird + 1), WallStage::Middle);
        EXPECT_EQ(stageAt(kLiveStageThird), WallStage::Late);
        EXPECT_EQ(stageAt(0), WallStage::Late);

        // A hand with melds holds fewer concealed tiles; a concealed kong is not an open set.
        const char* const concealedOf[] = {kHigherHand, "4p 5p 8p 8p 9p 1s 4s 4s 5s 7s Wd", "8p 8p 1s 4s 4s 5s 7s Wd", "1s 4s 4s 7s Wd"};
        const char* const pungs[] = {"1p", "9p", "1m"};
        const auto openSets = [&](int open, bool concealedKong) {
            TableView view = TurnOf(concealedOf[open + (concealedKong ? 1 : 0)]);
            for (int i = 0; i < open; ++i) {
                view.own.melds.push_back(OpenPung(pungs[i]));
            }
            if (concealedKong) {
                view.own.melds.push_back(ConcealedKong("Rd"));
            }
            return ClassifyDiscard(view, pair).openSets;
        };
        EXPECT_EQ(openSets(0, false), OpenSets::None);
        EXPECT_EQ(openSets(0, true), OpenSets::None);
        EXPECT_EQ(openSets(1, false), OpenSets::One);
        EXPECT_EQ(openSets(2, false), OpenSets::TwoOrMore);
        EXPECT_EQ(openSets(1, true), OpenSets::One);
        EXPECT_EQ(openSets(2, true), OpenSets::TwoOrMore);
    }

    TEST(ArenaChartsDiscardsTest, AValuedTileThatIsOneOfNormalsTiesIsNotADisagreement) {
        const TableView view = TurnOf(kTiedHand, Wind::East);
        EXPECT_EQ(Select(view).outcome, DiscardOutcome::SameAsNormal);
    }

    TEST(ArenaChartsDiscardsTest, ATurnWithNoRatedDiscardIsNoValue) {
        TableView view = TurnOf(kHigherHand, Wind::East);
        ASSERT_EQ(Select(view).outcome, DiscardOutcome::Measured);
        view.legal.clear();
        EXPECT_EQ(Select(view).outcome, DiscardOutcome::NoValue);
    }

    TEST(ArenaChartsDiscardsTest, ATurnThatIsNotNormalsStepFiveIsOtherStep) {
        // Step 1: the win is listed.
        HandModel winning = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "9p Wd Gd Nw Rd"));
        Lap(winning, "9p Wd Gd Nw", {kSeat0});
        ASSERT_TRUE(Has(MakeTableView(winning, kSeat0).legal, HandAction::MakeDeclareWin()));
        EXPECT_EQ(Select(MakeTableView(winning, kSeat0)).outcome, DiscardOutcome::OtherStep);

        // Step 2: a discard that declares the calling hand is listed.
        const HandModel calling = MakeModel(SpecOf("1s 2s 3s 4s 5s 6s 7m 8m 9m 5p 6p 6p Ew", "Ew"));
        ASSERT_TRUE(Has(MakeTableView(calling, kSeat0).legal, HandAction::MakeDiscard(T("6p"), true)));
        EXPECT_EQ(Select(MakeTableView(calling, kSeat0)).outcome, DiscardOutcome::OtherStep);

        // Step 4: a declared hand keeps its wait.
        HandModel declared = MakeModel(SpecOf("1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Rd", "Gd Wd Gd Nw 9p"));
        Lap(declared, "Gd Wd Gd Nw", {kSeat0});
        const TableView keeping = MakeTableView(declared, kSeat0);
        ASSERT_TRUE(keeping.own.callingDeclared);
        EXPECT_EQ(Select(keeping).outcome, DiscardOutcome::OtherStep);
    }

    TEST(ArenaChartsDiscardsTest, AViewThatIsNotTheOwnDiscardTurnIsOtherStep) {
        EXPECT_EQ(Select(TableView{}).outcome, DiscardOutcome::OtherStep);
        TableView window = TurnOf(kHigherHand, Wind::East);
        window.phase = Phase::Claim;
        EXPECT_EQ(Select(window).outcome, DiscardOutcome::OtherStep);
        TableView other = TurnOf(kHigherHand, Wind::East);
        other.currentSeat = kSeat1;
        EXPECT_EQ(Select(other).outcome, DiscardOutcome::OtherStep);
    }

    TEST(ArenaChartsDiscardsTest, TwoModelsThatDifferOnlyInHiddenTilesGiveOneSelectionAndOneClass) {
        const char* const east = "1p 2p 4p 5p 8p 8p 9p 1s 4s 4s 5s 7s Ew";
        const HandModel a = TurnModel(east, "Wd Rd Wd 1p");
        const HandModel b = TurnModel(east, "Wd 1p 5m Wd", kQuiet3, kQuiet2);
        const TableView viewA = MakeTableView(a, kSeat0);
        const TableView viewB = MakeTableView(b, kSeat0);
        ASSERT_EQ(viewA.visible, viewB.visible);

        const DiscardSelection first = Select(viewA);
        const DiscardSelection second = Select(viewB);
        ASSERT_EQ(first.outcome, DiscardOutcome::Measured);
        EXPECT_EQ(first.outcome, second.outcome);
        EXPECT_EQ(first.pair, second.pair);
        EXPECT_EQ(ClassifyDiscard(viewA, first.pair), ClassifyDiscard(viewB, second.pair));
    }

    TEST(ArenaChartsDiscardsTest, AHandCollectsTheSameTurnsTwice) {
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + 3; ++seed) {
            EXPECT_EQ(Describe(Collect(seed)), Describe(Collect(seed))) << "seed " << seed;
        }
    }

    TEST(ArenaChartsDiscardsTest, TheCountersAddUpAndEveryKeptViewIsAnOwnDiscardTurnWithAMeasuredPair) {
        int measured = 0;
        int ownTurns = 0;
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + kSeeds; ++seed) {
            const DiscardTurns collected = Collect(seed);
            EXPECT_EQ(collected.hands, 1);
            EXPECT_EQ(collected.ownTurns, collected.otherStep + collected.noValue + collected.sameAsNormal + static_cast<int>(collected.turns.size())) << "seed " << seed;
            int previous = -1;
            for (const DiscardTurn& turn : collected.turns) {
                EXPECT_GT(turn.index, previous);
                EXPECT_LT(turn.index, collected.ownTurns);
                previous = turn.index;
                EXPECT_EQ(turn.view.phase, Phase::Discard);
                EXPECT_EQ(turn.view.currentSeat, turn.view.seat);
                const DiscardSelection selection = SelectDiscardPair(turn.view);
                EXPECT_EQ(selection.outcome, DiscardOutcome::Measured) << "seed " << seed;
                EXPECT_EQ(selection.pair, turn.pair);
            }
            measured += static_cast<int>(collected.turns.size());
            ownTurns += collected.ownTurns;
        }
        EXPECT_GT(measured, 0);
        EXPECT_GT(ownTurns, measured);
    }

    TEST(ArenaChartsDiscardsTest, TheIndexOfATurnCountsEveryOwnTurnOfTheHand) {
        bool gap = false;
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + kSeeds; ++seed) {
            // The same hand watched turn by turn: the number of a measured turn is its place among all the own turns.
            Vector<int> expected;
            int own = 0;
            const HandObserver observer = [&expected, &own](const HandModel& hand, Seat actor) {
                if (hand.GetPhase() == Phase::Discard) {
                    expected.push_back(SelectDiscardPair(MakeTableView(hand, actor)).outcome == DiscardOutcome::Measured ? own : -1);
                    ++own;
                }
            };
            ASSERT_TRUE(PlayArenaHand(seed, NormalPlayers(), false, nullptr, &observer).has_value());
            const DiscardTurns collected = Collect(seed);
            EXPECT_EQ(collected.ownTurns, own);
            size_t next = 0;
            for (size_t place = 0; place < expected.size(); ++place) {
                if (expected[place] < 0) {
                    continue;
                }
                ASSERT_LT(next, collected.turns.size());
                EXPECT_EQ(collected.turns[next].index, expected[place]);
                gap = gap || collected.turns[next].index != static_cast<int>(next);
                ++next;
            }
            EXPECT_EQ(next, collected.turns.size());
        }
        // A turn that is left out lies before a measured one somewhere, so the numbers are not 0, 1, 2, ...
        EXPECT_TRUE(gap);
    }

    TEST(ArenaChartsDiscardsTest, ATurnGivesItsClassAndTheDifferenceOfTheMeansOfTheTwoDiscards) {
        const TableView view = MeasuredTurn();
        const DiscardSelection selection = Select(view);
        ASSERT_EQ(selection.outcome, DiscardOutcome::Measured);
        const DiscardMeasure measured = Measure(view, selection.pair);
        ASSERT_FALSE(measured.dropped);
        EXPECT_EQ(measured.row.cls, ClassifyDiscard(view, selection.pair));
        EXPECT_EQ(measured.row.pair, selection.pair);

        // The same two actions over the same base seed, run by the test.
        Optional<SearchRun> run =
            BeginSearchOver(view, {HandAction::MakeDiscard(selection.pair.fast), HandAction::MakeDiscard(selection.pair.valued)}, Analyzer::BaseSeedOf(AnalysisSeedOf(kHandSeed, kTurnIndex)), kWorlds);
        ASSERT_TRUE(run.has_value());
        run->RunWorlds(SerialWorldLoop);
        const double fast = MeanOf(run->Values(), 0);
        const double valued = MeanOf(run->Values(), 1);
        EXPECT_EQ(measured.row.gain, valued - fast);

        // And the analyzer on the full view prices the two discards alike: its worlds and rollouts are the ones above.
        Analyzer::AnalyzerConfig config;
        config.worlds = kWorlds;
        config.seed = AnalysisSeedOf(kHandSeed, kTurnIndex);
        const std::expected<Analyzer::PositionAnalysis, String> analysis = Analyzer::AnalyzePosition(view, config, SerialWorldLoop);
        ASSERT_TRUE(analysis.has_value());
        const auto analysedMean = [&analysis](Tile tile) {
            for (const Analyzer::ActionRow& row : analysis->rows) {
                if (row.action == HandAction::MakeDiscard(tile)) {
                    return row.ev.mean;
                }
            }
            ADD_FAILURE() << "the analysis has no row for the discard";
            return 0.0;
        };
        EXPECT_EQ(analysedMean(selection.pair.fast), fast);
        EXPECT_EQ(analysedMean(selection.pair.valued), valued);
    }

    TEST(ArenaChartsDiscardsTest, TheSameTurnGivesTheSameRowOnAnyLoop) {
        const TableView view = MeasuredTurn();
        const DiscardSelection selection = Select(view);
        ASSERT_EQ(selection.outcome, DiscardOutcome::Measured);
        const DiscardMeasure serial = Measure(view, selection.pair);
        ASSERT_FALSE(serial.dropped);
        EXPECT_EQ(Measure(view, selection.pair).row.gain, serial.row.gain);
        WorldThreads threads(kThreads);
        const DiscardMeasure threaded = Measure(view, selection.pair, threads.AsLoop());
        EXPECT_EQ(threaded.row.gain, serial.row.gain);
        EXPECT_EQ(threaded.row.cls, serial.row.cls);
    }

    TEST(ArenaChartsDiscardsTest, ATurnThatCannotBeSampledIsDroppedAndTold) {
        TableView view = MeasuredTurn();
        const DiscardSelection selection = Select(view);
        ASSERT_EQ(selection.outcome, DiscardOutcome::Measured);
        view.visible.fill(kCopiesPerTile);
        const DiscardMeasure measured = Measure(view, selection.pair);
        EXPECT_TRUE(measured.dropped);
        EXPECT_FALSE(measured.reason.empty());
    }

    TEST(ArenaChartsDiscardsTest, ABadCallIsAnErrorThatNamesTheSeedAndTheTurn) {
        const TableView view = MeasuredTurn();
        const DiscardSelection selection = Select(view);
        ASSERT_EQ(selection.outcome, DiscardOutcome::Measured);
        const auto expectError = [&](const TableView& turn, const DiscardPair& pair, int worlds) {
            const std::expected<DiscardMeasure, String> measured = MeasureDiscardTurn(turn, pair, kHandSeed, kTurnIndex, worlds, SerialWorldLoop);
            ASSERT_FALSE(measured.has_value());
            EXPECT_NE(measured.error().find(Format("seed {} turn {}", kHandSeed, kTurnIndex)), String::npos) << measured.error().c_str();
        };
        expectError(view, selection.pair, 0);
        // A tile the view does not list.
        expectError(view, DiscardPair{selection.pair.fast, T("Nw")}, kWorlds);
        TableView window = view;
        window.phase = Phase::Claim;
        expectError(window, selection.pair, kWorlds);
    }

}  // namespace BECore::Mahjong::Arena
