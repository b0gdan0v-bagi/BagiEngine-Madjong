#include "HandTestSupport.h"

#include <Mahjong/Game/Match.h>

/**
 * The match: the start, the bookkeeping of the deal and the count (`NextProgress`, `RoundWindOf`, a table of cases), and the match fed
 * with made-up settlements, so that no hand has to be played. A few tests play one hand with random legal actions to check
 * `EndHand`. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        // The rules written out, not taken from the headers.
        constexpr int kExpectedFullHands = 16;
        constexpr int kExpectedShortHands = 8;
        constexpr int kHandsPerRoundFull = 4;
        constexpr int kHandsPerRoundShort = 2;
        constexpr int kEastTilesInHand = 14;
        constexpr int kOtherTilesInHand = 13;
        constexpr uint32_t kSeed = 7;

        // The change of a made-up hand won by a seat other than East: it sums to zero, like every real one.
        constexpr SeatScores kTransfer = {-30, 90, -30, -30};
        constexpr SeatScores kNoChange = {0, 0, 0, 0};
        constexpr int kTableTotal = kSeatCount * kExpectedStartingScore;

        MahjongSettings Settings(int hands, bool replay = true) {
            MahjongSettings settings;
            settings.handsPerMatch = hands;
            settings.replayDrawnHand = replay;
            return settings;
        }

        Match StartedMatch(const MahjongSettings& settings = MahjongSettings{}, uint32_t seed = kSeed) {
            Match match;
            EXPECT_TRUE(match.Start(settings, seed).has_value());
            return match;
        }

        std::optional<MatchError> ErrorOf(const MatchStatus& status) {
            return status.has_value() ? std::nullopt : std::optional<MatchError>(status.error());
        }

        /// A settlement of the hand in progress that nobody played: the kind, the winner and the change of the points.
        HandSettlement MadeUp(const Match& match, ResultKind kind, Seat winner, const SeatScores& deltas = kNoChange) {
            HandSettlement settlement;
            settlement.kind = kind;
            settlement.winner = winner;
            settlement.east = match.East();
            settlement.roundWind = match.RoundWind();
            settlement.deltas = deltas;
            return settlement;
        }

        HandSettlement EastWins(const Match& match) {
            return MadeUp(match, ResultKind::Win, match.East());
        }
        HandSettlement SomeoneElseWins(const Match& match) {
            return MadeUp(match, ResultKind::Win, NextSeat(match.East()));
        }
        HandSettlement Drawn(const Match& match) {
            return MadeUp(match, ResultKind::Drawn, kNoSeat);
        }

    }  // namespace

    // ----- the start -----

    TEST(MatchTest, AMatchStartsWithTheStartingScoreAnEastAndADealtHand) {
        Match match;
        EXPECT_EQ(match.GetPhase(), MatchPhase::NotStarted);

        ASSERT_TRUE(match.Start(MahjongSettings{}, kSeed).has_value());

        EXPECT_EQ(match.GetPhase(), MatchPhase::InHand);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(match.Scores()[seat], kExpectedStartingScore) << "seat " << seat;
        }
        EXPECT_EQ(match.HandsPlayed(), 0);
        EXPECT_EQ(match.HandsTotal(), kExpectedFullHands);
        EXPECT_TRUE(match.History().empty());
        EXPECT_EQ(match.RoundWind(), Wind::East);
        ASSERT_TRUE(IsSeat(match.East()));
        EXPECT_EQ(match.Winds()[match.East()], Wind::East);
        EXPECT_EQ(match.Winds()[NextSeat(match.East())], Wind::South);
        EXPECT_EQ(match.Winds()[NextSeat(NextSeat(match.East()))], Wind::West);
        EXPECT_EQ(match.Winds()[NextSeat(NextSeat(NextSeat(match.East())))], Wind::North);
    }

    TEST(MatchTest, TheFirstHandIsDealtWithEastHoldingFourteenTiles) {
        const Match match = StartedMatch();
        const HandModel& hand = match.Hand();

        EXPECT_EQ(hand.GetPhase(), Phase::Discard);
        EXPECT_EQ(hand.East(), match.East());
        EXPECT_EQ(hand.RoundWind(), Wind::East);
        EXPECT_EQ(hand.CurrentSeat(), match.East());
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(hand.SeatWind(seat), match.Winds()[seat]) << "seat " << seat;
            const int expected = seat == match.East() ? kEastTilesInHand : kOtherTilesInHand;
            EXPECT_EQ(static_cast<int>(hand.GetHand(seat).concealed.size()), expected) << "seat " << seat;
        }
    }

    TEST(MatchTest, StartingScoreIsASetting) {
        MahjongSettings settings;
        settings.startingScore = 500;

        const Match match = StartedMatch(settings);

        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(match.Scores()[seat], settings.startingScore);
        }
    }

    TEST(MatchTest, TheShortGameHasEightHands) {
        const Match match = StartedMatch(Settings(kExpectedShortHands));

        EXPECT_EQ(match.HandsTotal(), kExpectedShortHands);
    }

    TEST(MatchTest, TheSameSeedIsTheSameMatch) {
        const Match a = StartedMatch(MahjongSettings{}, kSeed);
        const Match b = StartedMatch(MahjongSettings{}, kSeed);

        EXPECT_EQ(a.East(), b.East());
        EXPECT_EQ(Snapshot(a.Hand()), Snapshot(b.Hand()));
    }

    TEST(MatchTest, DifferentSeedsGiveDifferentMatches) {
        const std::string first = Snapshot(StartedMatch(MahjongSettings{}, 1).Hand());
        bool differs = false;
        for (uint32_t seed = 2; seed < 20; ++seed) {
            differs = differs || Snapshot(StartedMatch(MahjongSettings{}, seed).Hand()) != first;
        }

        EXPECT_TRUE(differs);
    }

    TEST(MatchTest, ABadNumberOfHandsIsRefusedAndChangesNothing) {
        for (const int hands : {0, 6, 10, -4, 2}) {
            Match fresh;
            EXPECT_EQ(ErrorOf(fresh.Start(Settings(hands), kSeed)), MatchError::BadSettings) << hands;
            EXPECT_EQ(fresh.GetPhase(), MatchPhase::NotStarted) << hands;

            Match running = StartedMatch();
            ASSERT_TRUE(running.ApplySettlement(SomeoneElseWins(running)).has_value());
            const std::string before = Snapshot(running.Hand());
            const SeatScores scores = running.Scores();
            const Seat east = running.East();

            EXPECT_EQ(ErrorOf(running.Start(Settings(hands), kSeed + 1)), MatchError::BadSettings) << hands;

            EXPECT_EQ(running.GetPhase(), MatchPhase::InHand) << hands;
            EXPECT_EQ(running.HandsPlayed(), 1) << hands;
            EXPECT_EQ(running.History().size(), 1u) << hands;
            EXPECT_EQ(running.East(), east) << hands;
            EXPECT_EQ(running.Scores(), scores) << hands;
            EXPECT_EQ(Snapshot(running.Hand()), before) << hands;
        }
    }

    TEST(MatchTest, StartingAgainBeginsANewMatch) {
        Match match = StartedMatch();
        ASSERT_TRUE(match.ApplySettlement(MadeUp(match, ResultKind::Win, NextSeat(match.East()), kTransfer)).has_value());

        ASSERT_TRUE(match.Start(MahjongSettings{}, kSeed).has_value());

        EXPECT_EQ(match.HandsPlayed(), 0);
        EXPECT_TRUE(match.History().empty());
        EXPECT_EQ(match.Scores()[0], kExpectedStartingScore);
        EXPECT_EQ(match.GetPhase(), MatchPhase::InHand);
    }

    // ----- the bookkeeping -----

    TEST(MatchTest, NextProgressFollowsTheRules) {
        struct Case {
            const char* name;
            ResultKind kind;
            Seat winner;
            bool replay;
            MatchProgress before;
            MatchProgress after;
        };
        const Case cases[] = {
            {"East wins", ResultKind::Win, 2, true, {5, 2}, {6, 2}},
            {"East wins, replay off", ResultKind::Win, 2, false, {5, 2}, {6, 2}},
            {"another seat wins", ResultKind::Win, 3, true, {5, 2}, {6, 3}},
            {"the seat before East wins", ResultKind::Win, 1, true, {5, 2}, {6, 3}},
            {"the deal wraps from seat 3 to seat 0", ResultKind::Win, 1, true, {0, 3}, {1, 0}},
            {"drawn and replayed", ResultKind::Drawn, kNoSeat, true, {5, 2}, {5, 2}},
            {"drawn, not replayed", ResultKind::Drawn, kNoSeat, false, {5, 2}, {6, 3}},
            {"drawn, not replayed, the deal wraps", ResultKind::Drawn, kNoSeat, false, {0, 3}, {1, 0}},
        };

        for (const Case& test : cases) {
            const MatchProgress after = NextProgress(test.before, test.kind, test.winner, Settings(kExpectedFullHands, test.replay));

            EXPECT_EQ(after.handsPlayed, test.after.handsPlayed) << test.name;
            EXPECT_EQ(after.east, test.after.east) << test.name;
        }
    }

    TEST(MatchTest, TheRoundWindFollowsTheCountedHands) {
        for (int played = 0; played < kExpectedFullHands; ++played) {
            EXPECT_EQ(RoundWindOf(played, Settings(kExpectedFullHands)), static_cast<Wind>(played / kHandsPerRoundFull)) << played;
        }
        for (int played = 0; played < kExpectedShortHands; ++played) {
            EXPECT_EQ(RoundWindOf(played, Settings(kExpectedShortHands)), static_cast<Wind>(played / kHandsPerRoundShort)) << played;
        }
        // The edges: the first and the last hand of each quarter of the full game, and past the end.
        EXPECT_EQ(RoundWindOf(3, Settings(kExpectedFullHands)), Wind::East);
        EXPECT_EQ(RoundWindOf(4, Settings(kExpectedFullHands)), Wind::South);
        EXPECT_EQ(RoundWindOf(8, Settings(kExpectedFullHands)), Wind::West);
        EXPECT_EQ(RoundWindOf(12, Settings(kExpectedFullHands)), Wind::North);
        EXPECT_EQ(RoundWindOf(kExpectedFullHands, Settings(kExpectedFullHands)), Wind::North);
        EXPECT_EQ(RoundWindOf(1, Settings(kExpectedShortHands)), Wind::East);
        EXPECT_EQ(RoundWindOf(2, Settings(kExpectedShortHands)), Wind::South);
    }

    TEST(MatchTest, TheMatchIsOverAfterExactlyTheNumberOfHands) {
        EXPECT_FALSE(IsMatchOver(kExpectedFullHands - 1, Settings(kExpectedFullHands)));
        EXPECT_TRUE(IsMatchOver(kExpectedFullHands, Settings(kExpectedFullHands)));
        EXPECT_FALSE(IsMatchOver(kExpectedShortHands - 1, Settings(kExpectedShortHands)));
        EXPECT_TRUE(IsMatchOver(kExpectedShortHands, Settings(kExpectedShortHands)));
    }

    // ----- a match fed with made-up settlements -----

    TEST(MatchTest, AWinByEastKeepsTheDealAndCounts) {
        Match match = StartedMatch();
        const Seat east = match.East();

        ASSERT_TRUE(match.ApplySettlement(EastWins(match)).has_value());

        EXPECT_EQ(match.East(), east);
        EXPECT_EQ(match.HandsPlayed(), 1);
        EXPECT_EQ(match.Hand().East(), east);
        EXPECT_EQ(match.Hand().GetPhase(), Phase::Discard);
    }

    TEST(MatchTest, AWinByAnotherSeatMovesTheDealAndTheWinds) {
        Match match = StartedMatch();
        const Seat east = match.East();

        ASSERT_TRUE(match.ApplySettlement(SomeoneElseWins(match)).has_value());

        EXPECT_EQ(match.East(), NextSeat(east));
        EXPECT_EQ(match.HandsPlayed(), 1);
        EXPECT_EQ(match.Winds()[NextSeat(east)], Wind::East);
        EXPECT_EQ(match.Winds()[east], Wind::North);
        EXPECT_EQ(match.Hand().East(), NextSeat(east));
        EXPECT_EQ(match.Hand().SeatWind(east), Wind::North);
    }

    TEST(MatchTest, ADrawnHandIsReplayedByTheSameEastWhenTheSettingSaysSo) {
        Match match = StartedMatch(Settings(kExpectedFullHands, true));
        const Seat east = match.East();
        const std::string firstHand = Snapshot(match.Hand());

        ASSERT_TRUE(match.ApplySettlement(Drawn(match)).has_value());

        EXPECT_EQ(match.East(), east);
        EXPECT_EQ(match.HandsPlayed(), 0);
        EXPECT_EQ(match.History().size(), 1u);
        EXPECT_EQ(match.GetPhase(), MatchPhase::InHand);
        EXPECT_NE(Snapshot(match.Hand()), firstHand);  // a new wall
    }

    TEST(MatchTest, ADrawnHandCountsAndMovesTheDealWhenItIsNotReplayed) {
        Match match = StartedMatch(Settings(kExpectedFullHands, false));
        const Seat east = match.East();

        ASSERT_TRUE(match.ApplySettlement(Drawn(match)).has_value());

        EXPECT_EQ(match.East(), NextSeat(east));
        EXPECT_EQ(match.HandsPlayed(), 1);
    }

    TEST(MatchTest, TheDeltasChangeTheScoresAndAreKeptInTheHistory) {
        Match match = StartedMatch();
        const HandSettlement settlement = MadeUp(match, ResultKind::Win, NextSeat(match.East()), kTransfer);

        ASSERT_TRUE(match.ApplySettlement(settlement).has_value());

        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(match.Scores()[seat], kExpectedStartingScore + kTransfer[seat]) << "seat " << seat;
        }
        ASSERT_EQ(match.History().size(), 1u);
        EXPECT_EQ(match.History()[0].deltas, kTransfer);
        EXPECT_EQ(match.History()[0].kind, ResultKind::Win);
        EXPECT_EQ(Sum(match.Scores()), kTableTotal);
    }

    TEST(MatchTest, TheMatchEndsAfterExactlyTheNumberOfCountedHandsWhateverTheResults) {
        for (const int hands : {kExpectedFullHands, kExpectedShortHands}) {
            for (const bool replay : {true, false}) {
                for (uint32_t seed = 1; seed <= 12; ++seed) {
                    Match match = StartedMatch(Settings(hands, replay), seed);
                    MahjongRng mix(seed);
                    int counted = 0;
                    int settled = 0;
                    while (match.GetPhase() == MatchPhase::InHand) {
                        // A mix of East wins, other wins and drawn hands.
                        HandSettlement settlement = Drawn(match);
                        switch (mix.Below(3)) {
                            case 0:
                                settlement = EastWins(match);
                                break;
                            case 1:
                                settlement = MadeUp(match, ResultKind::Win, static_cast<Seat>(mix.Below(kSeatCount)), kTransfer);
                                break;
                            default:
                                break;
                        }
                        counted += (settlement.kind == ResultKind::Drawn && replay) ? 0 : 1;
                        ASSERT_TRUE(match.ApplySettlement(settlement).has_value());
                        ++settled;
                        ASSERT_LT(settled, 1000) << "the match does not end";
                        EXPECT_EQ(match.HandsPlayed(), counted);
                        EXPECT_EQ(Sum(match.Scores()), kTableTotal);
                    }

                    EXPECT_EQ(match.GetPhase(), MatchPhase::Finished);
                    EXPECT_EQ(counted, hands);
                    EXPECT_EQ(match.HandsPlayed(), hands);
                    EXPECT_EQ(static_cast<int>(match.History().size()), settled);
                }
            }
        }
    }

    TEST(MatchTest, FourWinsByEastInARowStillPlaySixteenHandsAndTheRoundWindChanges) {
        Match match = StartedMatch();
        const Seat east = match.East();

        for (int played = 0; played < kExpectedFullHands; ++played) {
            ASSERT_EQ(match.GetPhase(), MatchPhase::InHand) << played;
            EXPECT_EQ(match.RoundWind(), static_cast<Wind>(played / kHandsPerRoundFull)) << played;
            EXPECT_EQ(match.Hand().RoundWind(), match.RoundWind()) << played;
            EXPECT_EQ(match.East(), east) << played;
            ASSERT_TRUE(match.ApplySettlement(EastWins(match)).has_value());
        }

        EXPECT_EQ(match.GetPhase(), MatchPhase::Finished);
        EXPECT_EQ(match.HandsPlayed(), kExpectedFullHands);
        EXPECT_EQ(match.East(), east);
    }

    TEST(MatchTest, TheRoundWindOfTheHandInProgressChangesAfterAQuarterOfTheHands) {
        Match match = StartedMatch(Settings(kExpectedShortHands));

        for (int played = 0; played < kExpectedShortHands; ++played) {
            EXPECT_EQ(match.Hand().RoundWind(), static_cast<Wind>(played / kHandsPerRoundShort)) << played;
            ASSERT_TRUE(match.ApplySettlement(SomeoneElseWins(match)).has_value());
        }

        EXPECT_EQ(match.GetPhase(), MatchPhase::Finished);
    }

    TEST(MatchTest, AFinishedMatchKeepsItsLastHandAndRefusesMore) {
        Match match = StartedMatch(Settings(kExpectedShortHands));
        for (int played = 1; played < kExpectedShortHands; ++played) {
            ASSERT_TRUE(match.ApplySettlement(SomeoneElseWins(match)).has_value());
        }
        ASSERT_EQ(match.GetPhase(), MatchPhase::InHand);
        const std::string lastHand = Snapshot(match.Hand());

        ASSERT_TRUE(match.ApplySettlement(MadeUp(match, ResultKind::Win, match.East(), kTransfer)).has_value());

        ASSERT_EQ(match.GetPhase(), MatchPhase::Finished);
        EXPECT_EQ(Snapshot(match.Hand()), lastHand);  // no hand is dealt after the last one
        const SeatScores scores = match.Scores();
        const size_t historySize = match.History().size();
        EXPECT_EQ(ErrorOf(match.ApplySettlement(SomeoneElseWins(match))), MatchError::NotInHand);
        const auto ended = match.EndHand();
        ASSERT_FALSE(ended.has_value());
        EXPECT_EQ(ended.error(), MatchError::NotInHand);
        EXPECT_EQ(match.Scores(), scores);
        EXPECT_EQ(match.History().size(), historySize);
        EXPECT_EQ(match.HandsPlayed(), kExpectedShortHands);
    }

    TEST(MatchTest, StandingsOrderTheSeatsByPointsAndTiesBySeat) {
        Match match = StartedMatch();
        ASSERT_TRUE(match.ApplySettlement(MadeUp(match, ResultKind::Win, 1, {-30, 90, 0, -60})).has_value());
        EXPECT_EQ(match.Standings(), (eastl::array<Seat, kSeatCount>{1, 2, 0, 3}));

        ASSERT_TRUE(match.ApplySettlement(MadeUp(match, ResultKind::Win, 0, {120, -90, 0, -30})).has_value());
        // 2090, 2000, 2000, 1910: seats 1 and 2 are level and stay in seat order.
        EXPECT_EQ(match.Standings(), (eastl::array<Seat, kSeatCount>{0, 1, 2, 3}));
    }

    TEST(MatchTest, TwoSeatsAtTheTopStayInSeatOrder) {
        Match match = StartedMatch();

        ASSERT_TRUE(match.ApplySettlement(MadeUp(match, ResultKind::Win, 0, {10, -20, 0, 10})).has_value());

        EXPECT_EQ(match.Standings(), (eastl::array<Seat, kSeatCount>{0, 3, 2, 1}));
    }

    TEST(MatchTest, StandingsAtTheStartAreInSeatOrder) {
        const Match match = StartedMatch();

        EXPECT_EQ(match.Standings(), (eastl::array<Seat, kSeatCount>{0, 1, 2, 3}));
    }

    // ----- EndHand -----

    TEST(MatchTest, EndHandIsRefusedBeforeTheHandIsFinishedAndChangesNothing) {
        Match match = StartedMatch();
        const std::string before = Snapshot(match.Hand());

        const auto ended = match.EndHand();

        ASSERT_FALSE(ended.has_value());
        EXPECT_EQ(ended.error(), MatchError::HandNotFinished);
        EXPECT_EQ(match.HandsPlayed(), 0);
        EXPECT_TRUE(match.History().empty());
        EXPECT_EQ(Snapshot(match.Hand()), before);
    }

    TEST(MatchTest, EndHandIsRefusedBeforeTheStart) {
        Match match;

        const auto ended = match.EndHand();

        ASSERT_FALSE(ended.has_value());
        EXPECT_EQ(ended.error(), MatchError::NotInHand);
    }

    TEST(MatchTest, EndHandSettlesAPlayedHandAndDealsTheNext) {
        for (const Mode mode : {Mode::SeekWin, Mode::NeverWin}) {
            Match match = StartedMatch(Settings(kExpectedFullHands, false));
            MahjongRng rng(kSeed);
            const Seat east = match.East();
            ASSERT_NO_FATAL_FAILURE(PlayHand(match, mode, rng));
            const HandSettlement expected = SettleHand(match.Hand());

            const auto ended = match.EndHand();

            ASSERT_TRUE(ended.has_value()) << Name(mode);
            const HandSettlement& settlement = ended.value();
            EXPECT_EQ(settlement.kind, expected.kind) << Name(mode);
            EXPECT_EQ(settlement.scores, expected.scores) << Name(mode);
            EXPECT_EQ(settlement.deltas, expected.deltas) << Name(mode);
            ASSERT_EQ(match.History().size(), 1u) << Name(mode);
            EXPECT_EQ(match.History()[0].deltas, settlement.deltas) << Name(mode);
            EXPECT_EQ(Sum(match.Scores()), kTableTotal) << Name(mode);
            EXPECT_EQ(match.HandsPlayed(), 1) << Name(mode);
            const bool eastWon = settlement.kind == ResultKind::Win && settlement.winner == east;
            EXPECT_EQ(match.East(), eastWon ? east : NextSeat(east)) << Name(mode);
            EXPECT_EQ(match.Hand().GetPhase(), Phase::Discard) << Name(mode);  // the next hand is dealt
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_EQ(match.Scores()[seat], kExpectedStartingScore + settlement.deltas[seat]) << Name(mode) << " seat " << seat;
            }
        }
    }

    TEST(MatchTest, ADrawnHandPaysNothingAndIsReplayedByEndHand) {
        Match match = StartedMatch(Settings(kExpectedFullHands, true));
        MahjongRng rng(kSeed);
        const Seat east = match.East();
        ASSERT_NO_FATAL_FAILURE(PlayHand(match, Mode::NeverWin, rng));

        const auto ended = match.EndHand();

        ASSERT_TRUE(ended.has_value());
        EXPECT_EQ(ended->kind, ResultKind::Drawn);
        EXPECT_EQ(ended->deltas, kNoChange);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(match.Scores()[seat], kExpectedStartingScore);
        }
        EXPECT_EQ(match.HandsPlayed(), 0);
        EXPECT_EQ(match.East(), east);
        EXPECT_EQ(match.Hand().GetPhase(), Phase::Discard);  // a fresh wall, the same East
    }

    // ----- a match that is shown, not played -----

    TEST(MatchTest, ShowRemoteTakesOverWhatIsGivenAndPlaysNothing) {
        const Match real = StartedMatch(Settings(kExpectedShortHands));
        HandState state = real.Hand().Capture();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (seat != kSeat2) {
                state.hands[seat].concealed.assign(state.hands[seat].concealed.size(), Tile{});
            }
        }
        state.live.assign(state.live.size(), Tile{});
        const SeatScores scores = {1200, 2300, 3400, 4500};
        const Seat east = NextSeat(real.East());

        Match shown;
        shown.ShowRemote(Settings(kExpectedShortHands), MatchPhase::InHand, 3, east, scores, HandModel::ForDisplay(state));

        EXPECT_TRUE(shown.IsRemote());
        EXPECT_EQ(shown.GetPhase(), MatchPhase::InHand);
        EXPECT_EQ(shown.HandsPlayed(), 3);
        EXPECT_EQ(shown.HandsTotal(), kExpectedShortHands);
        EXPECT_EQ(shown.East(), east);
        EXPECT_EQ(shown.Winds()[east], Wind::East);
        EXPECT_EQ(shown.RoundWind(), RoundWindOf(3, Settings(kExpectedShortHands)));
        EXPECT_EQ(shown.Scores(), scores);
        EXPECT_TRUE(shown.History().empty());
        EXPECT_EQ(shown.Hand().GetWall().Remaining(), real.Hand().GetWall().Remaining());
        EXPECT_EQ(shown.Hand().GetHand(kSeat2).concealed, real.Hand().GetHand(kSeat2).concealed);

        // Nothing deals a real hand from it.
        EXPECT_EQ(ErrorOf(shown.ApplySettlement(Drawn(shown))), MatchError::NotInHand);
        EXPECT_EQ(shown.HandsPlayed(), 3);
        EXPECT_TRUE(shown.History().empty());
        EXPECT_EQ(shown.Scores(), scores);
        EXPECT_TRUE(shown.Hand().IsDisplayOnly());
    }

    TEST(MatchTest, ShowRemoteCanShowAMatchThatIsOverAndAFinishedHandRefusesEndHand) {
        MahjongRng rng(kSeed);
        Match played = StartedMatch();
        ASSERT_NO_FATAL_FAILURE(PlayHand(played, Mode::SeekWin, rng));

        Match shown;
        shown.ShowRemote(MahjongSettings{}, MatchPhase::InHand, 2, 1, kNoChange, HandModel::ForDisplay(played.Hand().Capture()));
        EXPECT_EQ(shown.Hand().GetPhase(), Phase::Finished);
        const auto ended = shown.EndHand();
        ASSERT_FALSE(ended.has_value());
        EXPECT_EQ(ended.error(), MatchError::NotInHand);

        shown.ShowRemote(MahjongSettings{}, MatchPhase::Finished, kExpectedFullHands, 1, kNoChange, HandModel::ForDisplay(played.Hand().Capture()));
        EXPECT_EQ(shown.GetPhase(), MatchPhase::Finished);
        EXPECT_EQ(shown.HandsPlayed(), kExpectedFullHands);
    }

    TEST(MatchTest, AStartAfterShowRemoteIsAnOrdinaryMatch) {
        Match match;
        match.ShowRemote(MahjongSettings{}, MatchPhase::InHand, 5, 2, SeatScores{1, 2, 3, 4}, HandModel::ForDisplay(HandState{}));
        ASSERT_TRUE(match.IsRemote());

        ASSERT_TRUE(match.Start(MahjongSettings{}, kSeed).has_value());

        EXPECT_FALSE(match.IsRemote());
        EXPECT_FALSE(match.Hand().IsDisplayOnly());
        EXPECT_EQ(match.HandsPlayed(), 0);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(match.Scores()[seat], kExpectedStartingScore);
        }
        EXPECT_EQ(Snapshot(match.Hand()), Snapshot(StartedMatch().Hand()));
        const auto ended = match.EndHand();
        ASSERT_FALSE(ended.has_value());
        EXPECT_EQ(ended.error(), MatchError::HandNotFinished);
    }

}  // namespace BECore::Mahjong::Tests
