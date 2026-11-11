#include <Mahjong/Arena/ArenaReport.h>
#include <Mahjong/Arena/ArenaRun.h>
#include <Mahjong/Arena/ArenaWorlds.h>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

/**
 * The run of the arena over seeds (`src/Mahjong/Arena/ArenaRun.h`): `RunArena` plays the seeds `first .. first + count - 1` for every
 * focus player and each of the four seats against `others`, and sums them. Checked here: the totals are the sums of the per-seed rows,
 * one seed equals the four `PlayArenaHand` calls, the mirror identity (Normal against three Normal wins exactly one hand of the four
 * rotations of a seed), determinism, a focus player's result not depending on the other focus players, the numbers of
 * `ComputerStrengthTest` (seeds 4000..4059, Normal against three random seats: 186 wins of 240, the random seats 27 together, 27 drawn
 * hands, +31730 points), a bad config and the threads: the same config at 1, 2 and 5 jobs gives equal results and the same report text,
 * and a hand that fails gives the error of the lowest seed whatever the number of threads. The second level of threads (`WorldThreads`, the
 * worlds of a search decision): the pool runs every index once and serves many calls, a search focus player with few seeds gives the same
 * result and report at 1, 2 and 5 jobs, and the split of `jobs` between the seeds and the worlds is what `ArenaConfig::jobs` says. Pure logic: no engine services; the file takes about 16 s in Debug
 * (few seeds, the 60 seeds of `ComputerStrengthTest` played once for Normal and Easy together; 7 s of it is the search case, the search
 * seeds being the dear ones: a seed is four hands of a search seat, about 1.2 s).
 */
namespace BECore::Mahjong::Arena {

    namespace {

        constexpr uint32_t kFirstSeed = 4000;
        /// The seeds of `ComputerStrengthTest`.
        constexpr int kStrengthSeeds = 60;
        constexpr int kFewSeeds = 4;
        constexpr int kTwoSeeds = 2;
        constexpr int kNoSeeds = 0;
        constexpr int kNegativeSeeds = -3;
        /// The seeds of the thread tests (cheap in Debug) and the thread counts: one, two, and more than a seed per thread cannot be.
        constexpr int kThreadSeeds = 6;
        constexpr int kFewJobs = 2;
        constexpr int kManyJobs = 5;
        constexpr int kJobCounts[] = {1, kFewJobs, kManyJobs};
        constexpr int kTooManyJobs = kMaxJobs + 1;
        /// A search focus player that is cheap in Debug; with few seeds the worlds level of the threads is the one that has work.
        constexpr const char* kSearchFocus = "normal:searchWorlds=1,searchCandidates=2,searchMargin=0";
        constexpr int kLoopIndexes = 6;
        constexpr std::chrono::milliseconds kProbeSleep{5};

        ArenaPlayer Player(std::string_view spec) {
            const std::expected<ArenaPlayer, eastl::string> player = ParseArenaPlayer(spec);
            EXPECT_TRUE(player.has_value()) << std::string(spec.data(), spec.size());
            return player.value_or(ArenaPlayer{});
        }

        ArenaConfig ConfigOf(std::initializer_list<const char*> focus, const char* others, int count, uint32_t first = kFirstSeed) {
            ArenaConfig config;
            config.first = first;
            config.count = count;
            for (const char* spec : focus) {
                config.focus.push_back(Player(spec));
            }
            config.others = Player(others);
            return config;
        }

        ArenaResult PlayConfig(const ArenaConfig& config) {
            std::expected<ArenaResult, eastl::string> result = RunArena(config);
            EXPECT_TRUE(result.has_value()) << (result.has_value() ? "" : result.error().c_str());
            return result.has_value() ? eastl::move(*result) : ArenaResult{};
        }

        std::string ErrorOf(const ArenaConfig& config) {
            const std::expected<ArenaResult, eastl::string> result = RunArena(config);
            EXPECT_FALSE(result.has_value());
            return result.has_value() ? std::string() : std::string(result.error().c_str());
        }

        bool SameTally(const ArenaTally& left, const ArenaTally& right) {
            return left.hands == right.hands && left.wins == right.wins && left.points == right.points && left.drawn == right.drawn && left.declared == right.declared &&
                   left.winScore == right.winScore && left.selfDraws == right.selfDraws && left.lost == right.lost && left.gaveWin == right.gaveWin && left.lostPoints == right.lostPoints &&
                   left.standingScore == right.standingScore && left.winsByKind == right.winsByKind && left.decisions == right.decisions;
        }

        bool SameRows(const eastl::vector<SeedRow>& left, const eastl::vector<SeedRow>& right) {
            if (left.size() != right.size()) {
                return false;
            }
            for (size_t index = 0; index < left.size(); ++index) {
                const SeedRow& a = left[index];
                const SeedRow& b = right[index];
                if (a.seed != b.seed || a.pointsOfFocus != b.pointsOfFocus || a.winsOfFocus != b.winsOfFocus || a.drawn != b.drawn || a.declared != b.declared || a.winScore != b.winScore ||
                    a.gaveWin != b.gaveWin || a.lostHands != b.lostHands || a.standingScore != b.standingScore) {
                    return false;
                }
            }
            return true;
        }

        /// Everything the run says about a focus player, the nanoseconds apart.
        bool SameFocus(const FocusResult& left, const FocusResult& right) {
            return left.player.name == right.player.name && SameRows(left.rows, right.rows) && SameTally(left.focus, right.focus) && SameTally(left.others, right.others) &&
                   left.winsBySeat == right.winsBySeat;
        }

        /// Normal and Easy against three random seats on the seeds of `ComputerStrengthTest`, played once for the tests that read it.
        const ArenaResult& StrengthRun() {
            static const ArenaResult run = PlayConfig(ConfigOf({"normal", "easy"}, "random", kStrengthSeeds));
            return run;
        }

    }  // namespace

    namespace {

        /// The made-up hands of the tests of the counts: the focus player is "normal" among "random" seats, and the player puts a record
        /// made from the seat the focus player sits on in each of its four rotations.
        using MakeRecord = HandRecord (*)(Seat focusSeat);

        /// Scores and changes of points that tell the seats apart in any sum: every subset of the seats has a sum of its own.
        constexpr SeatScores kMadeUpScores = {10, 20, 40, 80};
        constexpr SeatScores kMadeUpDeltas = {1, 2, 4, 8};
        constexpr int64_t kAllScores = 150;
        constexpr int64_t kAllDeltas = 15;

        HandRecord WonBy(Seat winner, WinSource source, Seat from) {
            HandRecord record;
            record.kind = ResultKind::Win;
            record.winner = winner;
            record.source = source;
            record.from = from;
            record.scores = kMadeUpScores;
            record.deltas = kMadeUpDeltas;
            return record;
        }

        Seat After(Seat seat, int steps) {
            return static_cast<Seat>((seat + steps) % kSeatCount);
        }

        FocusResult PlayMadeUp(MakeRecord make) {
            const ArenaHandPlayer player = [make](uint32_t, const ArenaPlayers& players, bool, const WorldLoop&) -> std::expected<HandRecord, eastl::string> {
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    if (players[seat].name == "normal") {
                        return make(seat);
                    }
                }
                return std::unexpected(eastl::string("no focus seat"));
            };
            const std::expected<ArenaResult, eastl::string> result = RunArena(ConfigOf({"normal"}, "random", 1), player);
            EXPECT_TRUE(result.has_value()) << (result.has_value() ? "" : result.error().c_str());
            return result.has_value() ? result->focus.at(0) : FocusResult{};
        }

    }  // namespace

    TEST(ArenaRunTest, AnotherSeatWinsOnTheFocusSeatsDiscard) {
        const FocusResult result = PlayMadeUp([](Seat focusSeat) { return WonBy(After(focusSeat, 1), WinSource::Discard, focusSeat); });
        // The focus player loses all four hands and gives the win in each; its scores and changes of points are all four seats' ones.
        EXPECT_EQ(result.focus.lost, 4);
        EXPECT_EQ(result.focus.gaveWin, 4);
        EXPECT_EQ(result.focus.selfDraws, 0);
        EXPECT_EQ(result.focus.wins, 0);
        EXPECT_EQ(result.focus.standingScore, kAllScores);
        EXPECT_EQ(result.focus.lostPoints, kAllDeltas);
        // The three others: one wins, two lose, and none of them discarded the winning tile; each seat is a loser in two rotations.
        EXPECT_EQ(result.others.wins, 4);
        EXPECT_EQ(result.others.lost, 8);
        EXPECT_EQ(result.others.gaveWin, 0);
        EXPECT_EQ(result.others.standingScore, 2 * kAllScores);
        EXPECT_EQ(result.others.lostPoints, 2 * kAllDeltas);
        ASSERT_EQ(result.rows.size(), 1u);
        EXPECT_EQ(result.rows[0].gaveWin, 4);
        EXPECT_EQ(result.rows[0].lostHands, 4);
        EXPECT_EQ(result.rows[0].standingScore, kAllScores);
    }

    TEST(ArenaRunTest, AnotherSeatWinsOnAThirdSeatsDiscard) {
        const FocusResult result = PlayMadeUp([](Seat focusSeat) { return WonBy(After(focusSeat, 1), WinSource::Discard, After(focusSeat, 2)); });
        // The focus player loses but did not give the win; in every rotation one of the others did.
        EXPECT_EQ(result.focus.lost, 4);
        EXPECT_EQ(result.focus.gaveWin, 0);
        EXPECT_EQ(result.focus.standingScore, kAllScores);
        EXPECT_EQ(result.others.lost, 8);
        EXPECT_EQ(result.others.gaveWin, 4);
        EXPECT_EQ(result.rows.at(0).gaveWin, 0);
        EXPECT_EQ(result.rows.at(0).lostHands, 4);
    }

    TEST(ArenaRunTest, TheFocusSeatDrawsTheWinningTileItself) {
        const FocusResult result = PlayMadeUp([](Seat focusSeat) { return WonBy(focusSeat, WinSource::SelfDraw, focusSeat); });
        EXPECT_EQ(result.focus.wins, 4);
        EXPECT_EQ(result.focus.selfDraws, 4);
        EXPECT_EQ(result.focus.winScore, kAllScores);
        EXPECT_EQ(result.focus.lost, 0);
        EXPECT_EQ(result.focus.gaveWin, 0);
        EXPECT_EQ(result.focus.standingScore, 0);
        EXPECT_EQ(result.focus.lostPoints, 0);
        // The three others lose every hand and discarded nothing; each seat is a loser in three rotations.
        EXPECT_EQ(result.others.lost, 12);
        EXPECT_EQ(result.others.gaveWin, 0);
        EXPECT_EQ(result.others.selfDraws, 0);
        EXPECT_EQ(result.others.standingScore, 3 * kAllScores);
        EXPECT_EQ(result.others.lostPoints, 3 * kAllDeltas);
        EXPECT_EQ(result.rows.at(0).winsOfFocus, 4);
        EXPECT_EQ(result.rows.at(0).lostHands, 0);
        EXPECT_EQ(result.rows.at(0).standingScore, 0);
    }

    TEST(ArenaRunTest, AnotherSeatDrawsTheWinningTileItself) {
        const FocusResult result = PlayMadeUp([](Seat focusSeat) { return WonBy(After(focusSeat, 1), WinSource::SelfDraw, After(focusSeat, 1)); });
        // Nobody gave the win: the focus player lost, and the self-draws are the others'.
        EXPECT_EQ(result.focus.lost, 4);
        EXPECT_EQ(result.focus.gaveWin, 0);
        EXPECT_EQ(result.focus.selfDraws, 0);
        EXPECT_EQ(result.others.selfDraws, 4);
        EXPECT_EQ(result.others.gaveWin, 0);
        EXPECT_EQ(result.rows.at(0).gaveWin, 0);
        EXPECT_EQ(result.rows.at(0).lostHands, 4);
    }

    TEST(ArenaRunTest, ARobbedKongOfTheFocusSeatIsAGivenWin) {
        const FocusResult result = PlayMadeUp([](Seat focusSeat) { return WonBy(After(focusSeat, 1), WinSource::RobbedKong, focusSeat); });
        EXPECT_EQ(result.focus.gaveWin, 4);
        EXPECT_EQ(result.focus.lost, 4);
        EXPECT_EQ(result.others.gaveWin, 0);
        EXPECT_EQ(result.rows.at(0).gaveWin, 4);
    }

    TEST(ArenaRunTest, ADrawnHandIsNeitherWonNorLost) {
        const FocusResult result = PlayMadeUp([](Seat) {
            HandRecord record;
            record.scores = kMadeUpScores;
            return record;
        });
        for (const ArenaTally* tally : {&result.focus, &result.others}) {
            EXPECT_EQ(tally->wins, 0);
            EXPECT_EQ(tally->lost, 0);
            EXPECT_EQ(tally->gaveWin, 0);
            EXPECT_EQ(tally->selfDraws, 0);
            EXPECT_EQ(tally->standingScore, 0);
            EXPECT_EQ(tally->lostPoints, 0);
        }
        EXPECT_EQ(result.focus.drawn, 4);
        EXPECT_EQ(result.rows.at(0).drawn, 4);
        EXPECT_EQ(result.rows.at(0).lostHands, 0);
        EXPECT_EQ(result.rows.at(0).gaveWin, 0);
    }

    TEST(ArenaRunTest, TheTotalsAreTheSumsOfTheRows) {
        const ArenaResult run = PlayConfig(ConfigOf({"normal"}, "random", kFewSeeds));
        ASSERT_EQ(run.focus.size(), 1u);
        const FocusResult& result = run.focus[0];
        EXPECT_EQ(run.first, kFirstSeed);
        EXPECT_EQ(run.count, kFewSeeds);
        ASSERT_EQ(result.rows.size(), static_cast<size_t>(kFewSeeds));

        int64_t points = 0;
        int64_t wins = 0;
        int64_t drawn = 0;
        int64_t declared = 0;
        int64_t winScore = 0;
        int64_t gaveWin = 0;
        int64_t lostHands = 0;
        int64_t standingScore = 0;
        for (size_t index = 0; index < result.rows.size(); ++index) {
            const SeedRow& row = result.rows[index];
            EXPECT_EQ(row.seed, kFirstSeed + index);
            EXPECT_GE(row.winsOfFocus, 0);
            EXPECT_LE(row.winsOfFocus + row.drawn, kSeatCount) << "a hand is won by the focus player, by another seat or drawn";
            EXPECT_LE(row.declared, kSeatCount);
            points += row.pointsOfFocus;
            wins += row.winsOfFocus;
            drawn += row.drawn;
            declared += row.declared;
            winScore += row.winScore;
            gaveWin += row.gaveWin;
            lostHands += row.lostHands;
            standingScore += row.standingScore;
            EXPECT_LE(row.gaveWin, row.lostHands);
            EXPECT_LE(row.lostHands, kSeatCount);
        }
        EXPECT_EQ(result.focus.points, points);
        EXPECT_EQ(result.focus.wins, wins);
        EXPECT_EQ(result.focus.drawn, drawn);
        EXPECT_EQ(result.focus.declared, declared);
        EXPECT_EQ(result.focus.winScore, winScore);
        EXPECT_EQ(result.focus.gaveWin, gaveWin);
        EXPECT_EQ(result.focus.lost, lostHands);
        EXPECT_EQ(result.focus.standingScore, standingScore);

        // The hands: 4 per seed for the focus player, 12 seat-hands per seed for the three others.
        EXPECT_EQ(result.focus.hands, kSeatCount * kFewSeeds);
        EXPECT_EQ(result.others.hands, (kSeatCount - 1) * kSeatCount * kFewSeeds);
        // Wins of the focus player by seat add up to its wins.
        EXPECT_EQ(result.winsBySeat[0] + result.winsBySeat[1] + result.winsBySeat[2] + result.winsBySeat[3], result.focus.wins);
        // Every hand is won by the focus player, by one of the others or drawn; a drawn hand counts once for each of the three others.
        EXPECT_EQ(result.focus.wins + result.others.wins + result.focus.drawn, result.focus.hands);
        EXPECT_EQ(result.others.drawn, (kSeatCount - 1) * result.focus.drawn);
        // The points are zero-sum: what the focus player gains the others lose.
        EXPECT_EQ(result.focus.points + result.others.points, 0);
        // A winner always has a winning score and a drawn hand pays nothing.
        EXPECT_GT(result.focus.wins, 0);
        EXPECT_GE(result.focus.winScore, result.focus.wins * kWinPoints);
        EXPECT_GE(result.others.winScore, result.others.wins * kWinPoints);
        EXPECT_GT(result.focus.decisions, 0);
        EXPECT_GT(result.others.decisions, 0);
    }

    TEST(ArenaRunTest, OneSeedIsTheFourRotationsOfPlayArenaHand) {
        constexpr uint32_t kSeed = 4007;
        const ArenaPlayer focus = Player("normal");
        const ArenaPlayer others = Player("random");
        const ArenaResult run = PlayConfig(ConfigOf({"normal"}, "random", 1, kSeed));
        const FocusResult& result = run.focus.at(0);
        ASSERT_EQ(result.rows.size(), 1u);

        SeedRow expected;
        int64_t otherWins = 0;
        int64_t otherPoints = 0;
        for (Seat focusSeat = 0; focusSeat < kSeatCount; ++focusSeat) {
            ArenaPlayers players;
            players.fill(others);
            players[focusSeat] = focus;
            const std::expected<HandRecord, eastl::string> record = PlayArenaHand(kSeed, players);
            ASSERT_TRUE(record.has_value());
            expected.pointsOfFocus += record->deltas[focusSeat];
            expected.winsOfFocus += record->winner == focusSeat ? 1 : 0;
            expected.drawn += record->kind == ResultKind::Drawn ? 1 : 0;
            expected.declared += record->declared[focusSeat] ? 1 : 0;
            expected.winScore += record->winner == focusSeat ? record->scores[focusSeat] : 0;
            otherWins += IsSeat(record->winner) && record->winner != focusSeat ? 1 : 0;
            otherPoints -= record->deltas[focusSeat];
            EXPECT_EQ(result.winsBySeat[focusSeat], record->winner == focusSeat ? 1 : 0);
        }
        const SeedRow& row = result.rows[0];
        EXPECT_EQ(row.seed, kSeed);
        EXPECT_EQ(row.pointsOfFocus, expected.pointsOfFocus);
        EXPECT_EQ(row.winsOfFocus, expected.winsOfFocus);
        EXPECT_EQ(row.drawn, expected.drawn);
        EXPECT_EQ(row.declared, expected.declared);
        EXPECT_EQ(row.winScore, expected.winScore);
        EXPECT_EQ(result.focus.wins, expected.winsOfFocus);
        EXPECT_EQ(result.others.wins, otherWins);
        EXPECT_EQ(result.others.points, otherPoints);
    }

    TEST(ArenaRunTest, NormalAgainstNormalIsAMirror) {
        // All four seats play by the same rules and the seat seeds do not depend on who is the focus player, so the four rotations
        // replay one game: the focus player wins exactly the rotation in which it sits on the winner's seat, and its points add up to 0.
        constexpr int kSeeds = 8;
        const ArenaResult run = PlayConfig(ConfigOf({"normal"}, "normal", kSeeds));
        const FocusResult& result = run.focus.at(0);
        int wonSeeds = 0;
        int drawnSeeds = 0;
        for (const SeedRow& row : result.rows) {
            EXPECT_EQ(row.pointsOfFocus, 0) << "seed " << row.seed;
            if (row.drawn == 0) {
                EXPECT_EQ(row.winsOfFocus, 1) << "seed " << row.seed;
                ++wonSeeds;
            } else {
                EXPECT_EQ(row.drawn, kSeatCount) << "seed " << row.seed;
                EXPECT_EQ(row.winsOfFocus, 0) << "seed " << row.seed;
                ++drawnSeeds;
            }
        }
        EXPECT_GT(wonSeeds, 0);
        EXPECT_EQ(result.focus.wins, wonSeeds);
        EXPECT_EQ(result.focus.points, 0);
        EXPECT_EQ(result.focus.drawn, drawnSeeds * kSeatCount);
        // In each of the other three rotations of a won seed one of the others wins.
        EXPECT_EQ(result.others.wins, (kSeatCount - 1) * result.focus.wins);
        // The four rotations replay one game, so the three others do what the focus player does, three times.
        EXPECT_EQ(result.others.gaveWin, (kSeatCount - 1) * result.focus.gaveWin);
        EXPECT_EQ(result.others.selfDraws, (kSeatCount - 1) * result.focus.selfDraws);
        EXPECT_EQ(result.others.lost, (kSeatCount - 1) * result.focus.lost);
        EXPECT_EQ(result.others.standingScore, (kSeatCount - 1) * result.focus.standingScore);
        EXPECT_EQ(result.others.lostPoints, (kSeatCount - 1) * result.focus.lostPoints);
    }

    TEST(ArenaRunTest, AWonHandIsASelfDrawOrSomeoneGaveTheWin) {
        const FocusResult& result = StrengthRun().focus.at(0);
        // Over all four seats every won hand that was not a self-draw was given by exactly one seat.
        EXPECT_EQ(result.focus.gaveWin + result.others.gaveWin, result.focus.wins + result.others.wins - result.focus.selfDraws - result.others.selfDraws);
        for (const ArenaTally* tally : {&result.focus, &result.others}) {
            EXPECT_LE(tally->selfDraws, tally->wins);
            EXPECT_LE(tally->gaveWin, tally->lost);
        }
        // Every hand is won by the focus player (it did not lose it) or by another seat (it lost it) or drawn.
        EXPECT_EQ(result.focus.lost, result.others.wins);
        EXPECT_EQ(result.focus.wins + result.focus.lost + result.focus.drawn, result.focus.hands);
        EXPECT_GT(result.focus.selfDraws, 0);
        EXPECT_GT(result.focus.gaveWin, 0);
        EXPECT_LT(result.focus.lostPoints, 0);
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + kFewSeeds; ++seed) {
            ArenaPlayers players;
            players.fill(Player("normal"));
            const std::expected<HandRecord, eastl::string> record = PlayArenaHand(seed, players);
            ASSERT_TRUE(record.has_value());
            EXPECT_EQ(IsSeat(record->from), record->kind == ResultKind::Win) << "seed " << seed;
            if (record->source == WinSource::SelfDraw && IsSeat(record->winner)) {
                EXPECT_EQ(record->from, record->winner);
            }
        }
    }

    TEST(ArenaRunTest, TheSameConfigGivesTheSameResult) {
        const ArenaConfig config = ConfigOf({"normal", "easy:skipClaimPercent=20"}, "random", kFewSeeds);
        const ArenaResult first = PlayConfig(config);
        const ArenaResult second = PlayConfig(config);
        ASSERT_EQ(first.focus.size(), second.focus.size());
        for (size_t index = 0; index < first.focus.size(); ++index) {
            EXPECT_TRUE(SameFocus(first.focus[index], second.focus[index])) << index;
        }
    }

    TEST(ArenaRunTest, AFocusPlayerDoesNotDependOnTheOthersInTheList) {
        const ArenaResult both = PlayConfig(ConfigOf({"normal", "easy"}, "random", kFewSeeds));
        const ArenaResult justNormal = PlayConfig(ConfigOf({"normal"}, "random", kFewSeeds));
        const ArenaResult justEasy = PlayConfig(ConfigOf({"easy"}, "random", kFewSeeds));
        const ArenaResult swapped = PlayConfig(ConfigOf({"easy", "normal"}, "random", kFewSeeds));
        ASSERT_EQ(both.focus.size(), 2u);
        EXPECT_EQ(both.focus[0].player.name, "normal");
        EXPECT_EQ(both.focus[1].player.name, "easy");
        EXPECT_TRUE(SameFocus(both.focus[0], justNormal.focus[0]));
        EXPECT_TRUE(SameFocus(both.focus[1], justEasy.focus[0]));
        EXPECT_TRUE(SameFocus(swapped.focus[0], justEasy.focus[0]));
        EXPECT_TRUE(SameFocus(swapped.focus[1], justNormal.focus[0]));
        // The same walls for every focus player: the same player twice in the list gives the same rows.
        const ArenaResult twin = PlayConfig(ConfigOf({"normal", "normal"}, "random", kFewSeeds));
        EXPECT_TRUE(SameRows(twin.focus[0].rows, twin.focus[1].rows));
    }

    TEST(ArenaRunTest, NormalAgainstRandomIsTheStrengthTest) {
        // The numbers of `ComputerStrengthTest.cpp` (and of its comment): 186 wins of 240 hands, the three random seats 27 together,
        // 27 drawn hands, +31730 points for the computer seat.
        const FocusResult& normal = StrengthRun().focus.at(0);
        EXPECT_EQ(normal.focus.hands, 240);
        EXPECT_EQ(normal.focus.wins, 186);
        EXPECT_EQ(normal.others.wins, 27);
        EXPECT_EQ(normal.focus.drawn, 27);
        EXPECT_EQ(normal.focus.points, 31730);
        EXPECT_EQ(normal.others.points, -31730);
        EXPECT_EQ(normal.focus.declared, 224) << "the declarations measured by the strength test";
        EXPECT_EQ(normal.rows.size(), static_cast<size_t>(kStrengthSeeds));
    }

    TEST(ArenaRunTest, EasyWinsLessThanNormalOnTheSameHands) {
        const ArenaResult& run = StrengthRun();
        const FocusResult& normal = run.focus.at(0);
        const FocusResult& easy = run.focus.at(1);
        EXPECT_EQ(easy.player.name, "easy");
        EXPECT_EQ(easy.focus.hands, normal.focus.hands);
        EXPECT_LT(easy.focus.wins, normal.focus.wins);
        EXPECT_LT(easy.focus.points, normal.focus.points);
        EXPECT_GT(easy.focus.wins, easy.others.wins) << "still well above a random seat";
        EXPECT_GT(easy.focus.drawn, normal.focus.drawn);
    }

    TEST(ArenaRunTest, TheKindsAreCountedOncePerWonHand) {
        const FocusResult& result = StrengthRun().focus.at(0);
        for (const ArenaTally* tally : {&result.focus, &result.others}) {
            int64_t sum = 0;
            for (size_t kind = 0; kind < kScoreKindCount; ++kind) {
                EXPECT_LE(tally->winsByKind[kind], tally->wins) << static_cast<int>(kind);
                EXPECT_GE(tally->winsByKind[kind], 0);
                sum += tally->winsByKind[kind];
            }
            // A won hand has at least one line (a limit hand is its one line), so the kinds add up to at least the wins.
            EXPECT_GE(sum, tally->wins);
            // The 20 points of the win are in every hand that is not a limit hand.
            EXPECT_GT(tally->winsByKind[static_cast<size_t>(ScoreKind::Win)], 0);
        }
    }

    TEST(ArenaRunTest, TimingIsKeptOnlyWhenAskedFor) {
        ArenaConfig config = ConfigOf({"normal"}, "normal", kTwoSeeds);
        const ArenaResult quiet = PlayConfig(config);
        EXPECT_EQ(quiet.focus[0].focus.nanoseconds, 0);
        EXPECT_EQ(quiet.focus[0].others.nanoseconds, 0);
        config.timing = true;
        const ArenaResult timed = PlayConfig(config);
        EXPECT_TRUE(timed.timing);
        EXPECT_GT(timed.focus[0].focus.nanoseconds, 0);
        EXPECT_GT(timed.focus[0].others.nanoseconds, 0);
        // The decisions are counted either way and do not depend on the clock.
        EXPECT_GT(quiet.focus[0].focus.decisions, 0);
        EXPECT_EQ(timed.focus[0].focus.decisions, quiet.focus[0].focus.decisions);
        EXPECT_TRUE(SameRows(timed.focus[0].rows, quiet.focus[0].rows));
    }

    TEST(ArenaRunTest, ARandomPlayerCanBeTheFocus) {
        const ArenaResult run = PlayConfig(ConfigOf({"random"}, "random", kFewSeeds));
        EXPECT_EQ(run.focus.at(0).focus.hands, kSeatCount * kFewSeeds);
        EXPECT_EQ(run.focus.at(0).focus.points + run.focus.at(0).others.points, 0);
    }

    TEST(ArenaRunTest, ABadConfigIsAnError) {
        EXPECT_FALSE(ErrorOf(ConfigOf({"normal"}, "random", kNoSeeds)).empty());
        EXPECT_NE(ErrorOf(ConfigOf({"normal"}, "random", kNegativeSeeds)).find("-3"), std::string::npos);
        EXPECT_NE(ErrorOf(ConfigOf({}, "random", kFewSeeds)).find("no focus"), std::string::npos);
        // The seeds run past the largest one.
        const std::string past = ErrorOf(ConfigOf({"normal"}, "random", kTwoSeeds, std::numeric_limits<uint32_t>::max()));
        EXPECT_NE(past.find("4294967295"), std::string::npos) << past;
    }

    TEST(ArenaRunTest, TheThreadsDoNotChangeTheResult) {
        ArenaConfig config = ConfigOf({"normal", "easy:skipClaimPercent=20"}, "random", kThreadSeeds);
        EXPECT_EQ(config.jobs, kDefaultJobs) << "the default is one thread";
        const ArenaResult single = PlayConfig(config);
        const eastl::string singleReport = FormatArenaReport(single);
        EXPECT_FALSE(singleReport.empty());
        for (const int jobs : kJobCounts) {
            config.jobs = jobs;
            const ArenaResult threaded = PlayConfig(config);
            ASSERT_EQ(threaded.focus.size(), single.focus.size()) << jobs;
            for (size_t index = 0; index < single.focus.size(); ++index) {
                EXPECT_TRUE(SameFocus(single.focus[index], threaded.focus[index])) << "jobs " << jobs << " focus " << index;
            }
            EXPECT_EQ(FormatArenaReport(threaded), singleReport) << "jobs " << jobs;
        }
    }

    TEST(ArenaRunTest, ASearchFocusPlayerWithFewSeedsGivesTheSameResultAtAnyJobs) {
        // The run at `jobs` 1 on two seeds is the one the others are compared with: all of it against 5 jobs (2 seed threads of 2 world
        // threads each), and its first seed alone, which is the run of one seed, against 2 and 5 jobs (one seed thread of 2 and 5 world
        // threads). A search seed is the dear part of the file in Debug, so no run is made twice.
        ArenaConfig config = ConfigOf({kSearchFocus}, "random", kTwoSeeds);
        const ArenaResult single = PlayConfig(config);
        const eastl::string singleReport = FormatArenaReport(single);
        EXPECT_GT(single.focus.at(0).focus.decisions, 0);
        config.jobs = kManyJobs;
        const ArenaResult threaded = PlayConfig(config);
        ASSERT_EQ(threaded.focus.size(), single.focus.size());
        EXPECT_TRUE(SameFocus(single.focus[0], threaded.focus[0])) << "seeds " << kTwoSeeds << " jobs " << config.jobs;
        EXPECT_EQ(FormatArenaReport(threaded), singleReport) << "seeds " << kTwoSeeds << " jobs " << config.jobs;

        config.count = 1;
        for (const int jobs : {kFewJobs, kManyJobs}) {
            config.jobs = jobs;
            const ArenaResult one = PlayConfig(config);
            ASSERT_EQ(one.focus.size(), single.focus.size());
            ASSERT_EQ(one.focus[0].rows.size(), 1u);
            ASSERT_FALSE(single.focus[0].rows.empty());
            EXPECT_TRUE(SameRows(one.focus[0].rows, {single.focus[0].rows.front()})) << "one seed, jobs " << jobs;
        }
    }

    TEST(ArenaRunTest, TheJobsAreSplitBetweenTheSeedsAndTheWorlds) {
        struct Split {
            int jobs;
            int seeds;
            int worldThreads;
        };
        // Seeds first; what is left over (the integer division) goes to the worlds of each seed thread; never more than `jobs` in all.
        for (const Split split : {Split{1, 3, 1}, Split{3, 6, 1}, Split{5, 2, 2}, Split{5, 1, 5}, Split{9, 4, 2}}) {
            std::atomic<int> running{0};
            std::atomic<int> peakOfAll{0};
            std::atomic<int> peakOfOneLoop{0};
            const ArenaHandPlayer probing = [&](uint32_t, const ArenaPlayers&, bool, const WorldLoop& worlds) -> std::expected<HandRecord, eastl::string> {
                std::atomic<int> inLoop{0};
                std::atomic<int> peak{0};
                worlds(kLoopIndexes, [&](int) {
                    const int now = ++inLoop;
                    const int all = ++running;
                    for (int seen = peak.load(); now > seen && !peak.compare_exchange_weak(seen, now);) {
                    }
                    for (int seen = peakOfAll.load(); all > seen && !peakOfAll.compare_exchange_weak(seen, all);) {
                    }
                    std::this_thread::sleep_for(kProbeSleep);
                    --running;
                    --inLoop;
                });
                for (int seen = peakOfOneLoop.load(); peak.load() > seen && !peakOfOneLoop.compare_exchange_weak(seen, peak.load());) {
                }
                return HandRecord{};
            };
            ArenaConfig config = ConfigOf({"normal"}, "random", split.seeds);
            config.jobs = split.jobs;
            const std::expected<ArenaResult, eastl::string> result = RunArena(config, probing);
            ASSERT_TRUE(result.has_value()) << result.error().c_str();
            EXPECT_LE(peakOfOneLoop.load(), split.worldThreads) << "jobs " << split.jobs << " seeds " << split.seeds;
            EXPECT_LE(peakOfAll.load(), split.jobs) << "jobs " << split.jobs << " seeds " << split.seeds;
            if (split.worldThreads > 1) {
                EXPECT_GT(peakOfOneLoop.load(), 1) << "the worlds level is not used at jobs " << split.jobs << " seeds " << split.seeds;
            }
        }
    }

    TEST(ArenaRunTest, WorldThreadsRunEveryIndexOnceAndServeManyCalls) {
        for (const int threads : {-1, 0, 1, 2, 5}) {
            WorldThreads pool(threads);
            EXPECT_EQ(pool.Threads(), std::max(threads, 1));
            const WorldLoop loop = pool.AsLoop();
            // `count` 0 does nothing, 1 is not spread, fewer than the threads and many more than the threads are all covered.
            for (int round = 0; round < 3; ++round) {
                for (const int count : {0, 1, 3, 50}) {
                    std::vector<std::atomic<int>> hits(static_cast<size_t>(count));
                    loop(count, [&](int index) { ++hits[static_cast<size_t>(index)]; });
                    for (int index = 0; index < count; ++index) {
                        EXPECT_EQ(hits[static_cast<size_t>(index)].load(), 1) << "threads " << threads << " count " << count << " index " << index;
                    }
                }
            }
            // The results of the bodies are in the caller's memory when `ForEach` returns, with no other synchronisation.
            std::vector<int> squares(100);
            pool.ForEach(100, [&](int index) { squares[static_cast<size_t>(index)] = index * index; });
            for (int index = 0; index < 100; ++index) {
                EXPECT_EQ(squares[static_cast<size_t>(index)], index * index);
            }
        }
    }

    TEST(ArenaRunTest, TheTimingSumsTheThreads) {
        ArenaConfig config = ConfigOf({"normal"}, "normal", kThreadSeeds);
        config.timing = true;
        const ArenaResult single = PlayConfig(config);
        config.jobs = 2;
        const ArenaResult threaded = PlayConfig(config);
        EXPECT_GT(threaded.focus[0].focus.nanoseconds, 0);
        EXPECT_GT(threaded.focus[0].others.nanoseconds, 0);
        // The clock is the only difference: the decisions and the rows are those of one thread.
        EXPECT_EQ(threaded.focus[0].focus.decisions, single.focus[0].focus.decisions);
        EXPECT_TRUE(SameRows(threaded.focus[0].rows, single.focus[0].rows));
    }

    TEST(ArenaRunTest, AFailingHandGivesTheErrorOfTheLowestSeedWhateverTheThreads) {
        // Seeds 4002 and 4004 fail (the second one for the second focus player only); the others are played as they are.
        const ArenaHandPlayer failing = [](uint32_t seed, const ArenaPlayers& players, bool timing, const WorldLoop& worlds) -> std::expected<HandRecord, eastl::string> {
            if (seed == 4004 || (seed == 4002 && players[0].name != "normal" && players[1].name != "normal" && players[2].name != "normal" && players[3].name != "normal")) {
                return std::unexpected(Format("seed {}: made up", seed));
            }
            return PlayArenaHand(seed, players, timing, &worlds);
        };
        ArenaConfig config = ConfigOf({"normal", "easy"}, "random", kThreadSeeds);
        for (const int jobs : kJobCounts) {
            config.jobs = jobs;
            const std::expected<ArenaResult, eastl::string> result = RunArena(config, failing);
            ASSERT_FALSE(result.has_value()) << jobs;
            EXPECT_EQ(std::string(result.error().c_str()), "focus easy: seed 4002: made up") << "jobs " << jobs;
        }
        // Only a later seed fails: it is the error, again for every thread count.
        const ArenaHandPlayer later = [](uint32_t seed, const ArenaPlayers& players, bool timing, const WorldLoop& worlds) -> std::expected<HandRecord, eastl::string> {
            if (seed >= 4004) {
                return std::unexpected(Format("seed {}: made up", seed));
            }
            return PlayArenaHand(seed, players, timing, &worlds);
        };
        for (const int jobs : kJobCounts) {
            config.jobs = jobs;
            const std::expected<ArenaResult, eastl::string> result = RunArena(config, later);
            ASSERT_FALSE(result.has_value()) << jobs;
            EXPECT_EQ(std::string(result.error().c_str()), "focus normal: seed 4004: made up") << "jobs " << jobs;
        }
    }

    TEST(ArenaRunTest, TheNumberOfThreadsIsChecked) {
        ArenaConfig config = ConfigOf({"normal"}, "random", kTwoSeeds);
        config.jobs = 0;
        EXPECT_NE(ErrorOf(config).find("number of threads is 0"), std::string::npos);
        config.jobs = kTooManyJobs;
        EXPECT_NE(ErrorOf(config).find("from 1 to 256"), std::string::npos);
        config.jobs = -1;
        EXPECT_FALSE(ErrorOf(config).empty());
        // More threads than seeds is fine: the extra ones are not started.
        config.jobs = kMaxJobs;
        EXPECT_EQ(PlayConfig(config).focus.at(0).rows.size(), static_cast<size_t>(kTwoSeeds));
    }

}  // namespace BECore::Mahjong::Arena
