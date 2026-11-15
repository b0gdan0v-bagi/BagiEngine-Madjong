#include <Mahjong/Arena/ArenaTable.h>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>
#include <tests/FixedWall.h>
#include <tests/HandTestSupport.h>

/// About 2 s in Debug.
namespace BECore::Mahjong::Arena {

    namespace {

        constexpr uint32_t kFirstSeed = 4000;
        constexpr int kFewSeeds = 6;
        constexpr int kManyJobs = 3;
        constexpr Seat kSeatA = 0;
        constexpr Seat kSeatB = 1;
        constexpr Seat kSeatD = 3;

        ArenaPlayer Normal() {
            const std::expected<ArenaPlayer, String> player = ParseArenaPlayer("normal");
            EXPECT_TRUE(player.has_value());
            return player.value_or(ArenaPlayer{});
        }

        TableStatsConfig ConfigOf(int count, int jobs) {
            TableStatsConfig config;
            config.first = kFirstSeed;
            config.count = count;
            config.player = Normal();
            config.jobs = jobs;
            return config;
        }

        bool SameStats(const TableStats& a, const TableStats& b) {
            return FormatTableStats(a) == FormatTableStats(b) && a.hands == b.hands && a.drawn == b.drawn && a.wonBySource == b.wonBySource;
        }

    }  // namespace

    /**
     * A won hand, East is seat A and seat B wins (on a discard) with 50; the others stand with 10, 20 and 30. Own turns (the number of
     * declared opponents at each): A 0,1,1 (3 turns); B 2,2 (the winner); C none; D 3,3,3.
     *
     * Another seat won, so every round of every turn is known, and the win is in round `n - k + 1` of turn `k`:
     *  - A: turn 1 (d 0) round 3, turn 2 (d 1) round 2, turn 3 (d 1) round 1. d 0, after i = 1..4 rounds: observed 1 each, won 0 0 1 1.
     *    d 1: observed 2 each, won 1 2 2 2 (the sum of the turns 2 and 3: round 2 and 1).
     *  - D: rounds 3, 2, 1, all with d 3: observed 3 each, won 1 2 3 3.
     * B won itself: no other seat won in its past, and the round after the turn `k` is known only when the turn `k + i` exists: turn 1 knows
     * round 1 (turn 2 exists), turn 2 knows none. So d 2: observed 1 0 0 0, won 0.
     * Scores: the winner is not East: one sample of 50. The standing seats: East (A) 10; not East (C, D) 20 and 30.
     */
    TEST(ArenaTableTest, AHandIsTalliedByHand) {
        HandRecord record;
        record.kind = ResultKind::Win;
        record.winner = kSeatB;
        record.east = kSeatA;
        record.source = WinSource::Discard;
        record.scores = {10, 50, 20, 30};
        OwnTurns turns;
        turns[kSeatA] = {0, 1, 1};
        turns[kSeatB] = {2, 2};
        turns[kSeatD] = {3, 3, 3};

        TableStats stats;
        AddHand(stats, record, turns);

        EXPECT_EQ(stats.hands, 1);
        EXPECT_EQ(stats.drawn, 0);
        EXPECT_EQ(stats.wonBySource[static_cast<size_t>(WinSource::Discard)], 1);
        EXPECT_EQ(stats.wonBySource[static_cast<size_t>(WinSource::SelfDraw)], 0);
        const int64_t observedD0[kTableStatsRounds] = {1, 1, 1, 1};
        const int64_t wonD0[kTableStatsRounds] = {0, 0, 1, 1};
        const int64_t observedD1[kTableStatsRounds] = {2, 2, 2, 2};
        const int64_t wonD1[kTableStatsRounds] = {1, 2, 2, 2};
        const int64_t observedD2[kTableStatsRounds] = {1, 0, 0, 0};
        const int64_t observedD3[kTableStatsRounds] = {3, 3, 3, 3};
        const int64_t wonD3[kTableStatsRounds] = {1, 2, 3, 3};
        for (size_t rounds = 0; rounds < static_cast<size_t>(kTableStatsRounds); ++rounds) {
            EXPECT_EQ(stats.rounds[0][rounds].observed, observedD0[rounds]) << rounds;
            EXPECT_EQ(stats.rounds[0][rounds].won, wonD0[rounds]) << rounds;
            EXPECT_EQ(stats.rounds[1][rounds].observed, observedD1[rounds]) << rounds;
            EXPECT_EQ(stats.rounds[1][rounds].won, wonD1[rounds]) << rounds;
            EXPECT_EQ(stats.rounds[2][rounds].observed, observedD2[rounds]) << rounds;
            EXPECT_EQ(stats.rounds[2][rounds].won, 0) << rounds;
            EXPECT_EQ(stats.rounds[3][rounds].observed, observedD3[rounds]) << rounds;
            EXPECT_EQ(stats.rounds[3][rounds].won, wonD3[rounds]) << rounds;
        }
        EXPECT_EQ(stats.winnerScore[0].n, 0);
        EXPECT_EQ(stats.winnerScore[1].n, 1);
        EXPECT_EQ(stats.winnerScore[1].sum, 50);
        EXPECT_EQ(stats.winnerScore[1].sumSquares, 2500);
        EXPECT_EQ(stats.standingScore[0].n, 1);
        EXPECT_EQ(stats.standingScore[0].sum, 10);
        EXPECT_EQ(stats.standingScore[1].n, 2);
        EXPECT_EQ(stats.standingScore[1].sum, 50);
        EXPECT_EQ(stats.standingScore[1].sumSquares, 20 * 20 + 30 * 30);
    }

    /// A drawn hand counts a round only where the next turn exists: seat A with two turns knows round 1 of its first turn only; no score is
    /// tallied. East winning is the other index of the winner score.
    TEST(ArenaTableTest, ADrawnHandKnowsOnlyTheRoundsThatCameOut) {
        HandRecord drawn;
        OwnTurns turns;
        turns[kSeatA] = {0, 0};
        TableStats stats;
        AddHand(stats, drawn, turns);
        EXPECT_EQ(stats.hands, 1);
        EXPECT_EQ(stats.drawn, 1);
        EXPECT_EQ(stats.rounds[0][0].observed, 1);
        EXPECT_EQ(stats.rounds[0][1].observed, 0);
        EXPECT_EQ(stats.rounds[0][0].won, 0);
        EXPECT_EQ(stats.winnerScore[0].n + stats.winnerScore[1].n, 0);
        EXPECT_EQ(stats.standingScore[0].n + stats.standingScore[1].n, 0);

        HandRecord eastWon;
        eastWon.kind = ResultKind::Win;
        eastWon.winner = kSeatD;
        eastWon.east = kSeatD;
        eastWon.source = WinSource::SelfDraw;
        eastWon.scores = {0, 0, 0, 70};
        AddHand(stats, eastWon, OwnTurns{});
        EXPECT_EQ(stats.winnerScore[0].sum, 70);
        EXPECT_EQ(stats.wonBySource[static_cast<size_t>(WinSource::SelfDraw)], 1);
        EXPECT_EQ(stats.standingScore[0].n, 0);
        EXPECT_EQ(stats.standingScore[1].n, 3);
    }

    /// The observer only reads: the record of a hand with it equals the record without; the turns it collects are in range, East's first
    /// turn is with nobody declared, and no seat has more turns than decisions.
    TEST(ArenaTableTest, TheObserverCollectsTurnsAndChangesNothing) {
        ArenaPlayers players;
        players.fill(Normal());
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + static_cast<uint32_t>(kFewSeeds); ++seed) {
            OwnTurns turns;
            const HandObserver observer = MakeOwnTurnObserver(turns);
            const std::expected<HandRecord, String> watched = PlayArenaHand(seed, players, false, nullptr, &observer);
            const std::expected<HandRecord, String> plain = PlayArenaHand(seed, players);
            ASSERT_TRUE(watched.has_value() && plain.has_value()) << seed;
            EXPECT_EQ(watched->winner, plain->winner) << seed;
            EXPECT_EQ(watched->scores, plain->scores) << seed;
            EXPECT_EQ(watched->deltas, plain->deltas) << seed;
            EXPECT_EQ(watched->decisions, plain->decisions) << seed;
            EXPECT_EQ(watched->east, static_cast<Seat>(seed % kSeatCount)) << seed;
            ASSERT_FALSE(turns[watched->east].empty()) << seed;
            EXPECT_EQ(turns[watched->east].front(), 0) << seed;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_LE(static_cast<int>(turns[seat].size()), watched->decisions[seat]) << seed;
                for (const int declared : turns[seat]) {
                    EXPECT_GE(declared, 0);
                    EXPECT_LE(declared, static_cast<int>(kSeatCount - 1));
                }
            }
        }
    }

    /**
     * Seat 1 holds 1p..9p 1s 2s 3s 5m (it waits on 5m only) and declares by discarding its drawn Rd. The wall deals: seat 0 Ew, seat 1
     * Rd, seat 2 Ew, seat 3 5m, seat 0 Sw, seat 1 Gd, and every draw is discarded at once. Exposures of seat 1's wait, each from its
     * view before the discard (13 own tiles; u = 136 - 13 - the pile; w = 4 - its own 5m - the 5m in the pile):
     *  - seat 2 discards Ew: pile Ew, Rd = 2, u 121, w 3: 3000000 * 1/121 = 24793 millionths (the half of u added first, as the code rounds);
     *  - seat 3 discards 5m, the hit (seat 1 passes): pile 3, u 120, w 3: 25000;
     *  - seat 0 discards Sw: pile 4 (the 5m too), u 119, w 2: 16807.
     * So 3 exposures, 66600 millionths expected, 1 hit. Seat 1's own draw Gd (the exposure of a draw, taken with the Gd counted
     * unseen again: pile 5, 13 own tiles + the drawn one, so u 136 - 18 = 118, w 4 - 1 - 1 = 2): 16949, no hit.
     */
    TEST(ArenaTableTest, TheHitsOfADeclaredSeatAreCountedByHand) {
        using Tests::kSeat1;
        Tests::FixedWallSpec spec;
        spec.hands[0] = Tests::kQuiet1;
        spec.hands[1] = "1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 5m";
        spec.hands[2] = Tests::kQuiet2;
        spec.hands[3] = Tests::kQuiet3;
        spec.live = "Ew Rd Ew 5m Sw Gd";
        HandModel hand = Tests::MakeModel(spec);
        HandHits hits;
        const HandObserver observer = MakeHitObserver(hits);
        const auto act = [&](Seat seat, const HandAction& action) {
            observer(hand, seat);
            ASSERT_TRUE(hand.Apply(seat, action).has_value());
        };
        act(0, HandAction::MakeDiscard(Tests::T("Ew")));
        act(1, HandAction::MakeDiscard(Tests::T("Rd"), true));
        act(2, HandAction::MakeDiscard(Tests::T("Ew")));
        act(3, HandAction::MakeDiscard(Tests::T("5m")));
        ASSERT_EQ(hand.GetPhase(), Phase::Claim);
        act(kSeat1, HandAction{});
        act(0, HandAction::MakeDiscard(Tests::T("Sw")));
        ASSERT_EQ(hand.GetPhase(), Phase::Discard);
        EXPECT_EQ(hand.CurrentSeat(), kSeat1);
        observer(hand, kSeat1);
        observer(hand, kNoSeat);

        EXPECT_EQ(hits.discards.exposures, 3);
        EXPECT_EQ(hits.discards.expectedMicro, 24793 + 25000 + 16807);
        EXPECT_EQ(hits.discards.hits, 1);
        EXPECT_EQ(hits.draws.exposures, 1);
        EXPECT_EQ(hits.draws.expectedMicro, 16949);
        EXPECT_EQ(hits.draws.hits, 0);
    }

    TEST(ArenaTableTest, TheRunIsTheSameAtAnyNumberOfJobs) {
        const std::expected<TableStats, String> single = RunTableStats(ConfigOf(kFewSeeds, 1));
        const std::expected<TableStats, String> many = RunTableStats(ConfigOf(kFewSeeds, kManyJobs));
        ASSERT_TRUE(single.has_value() && many.has_value());
        EXPECT_TRUE(SameStats(*single, *many));
        EXPECT_EQ(single->hands, kFewSeeds);
        int64_t won = 0;
        for (const int64_t count : single->wonBySource) {
            won += count;
        }
        EXPECT_EQ(won + single->drawn, single->hands);
        for (const auto& row : single->rounds) {
            for (size_t rounds = 0; rounds < static_cast<size_t>(kTableStatsRounds); ++rounds) {
                EXPECT_LE(row[rounds].won, row[rounds].observed);
                if (rounds > 0) {
                    EXPECT_LE(row[rounds].observed, row[rounds - 1].observed);
                    EXPECT_GE(row[rounds].won, row[rounds - 1].won);
                }
            }
        }
        EXPECT_EQ(single->standingScore[0].n + single->standingScore[1].n, (won) * (kSeatCount - 1));
    }

    TEST(ArenaTableTest, TheReportNamesWhatItMeasures) {
        const std::expected<TableStats, String> stats = RunTableStats(ConfigOf(2, 1));
        ASSERT_TRUE(stats.has_value());
        const String text = FormatTableStats(*stats);
        const StringView view(text.data(), text.size());
        EXPECT_NE(view.find("Table statistics: four normal seats, seeds 4000 .. 4001 (2 hands)"), StringView::npos);
        EXPECT_NE(view.find("(1-h)^i"), StringView::npos);
        EXPECT_NE(view.find("winner, East"), StringView::npos);
        EXPECT_NE(view.find("standing, not East"), StringView::npos);
        EXPECT_NE(view.find("discard of another"), StringView::npos);
        EXPECT_NE(view.find("own draw (check)"), StringView::npos);
        EXPECT_EQ(FormatTableStats(*stats), text);
    }

    TEST(ArenaTableTest, ABadConfigIsRefused) {
        EXPECT_FALSE(RunTableStats(ConfigOf(0, 1)).has_value());
        EXPECT_FALSE(RunTableStats(ConfigOf(1, 0)).has_value());
    }

}  // namespace BECore::Mahjong::Arena
