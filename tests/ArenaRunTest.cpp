#include <Mahjong/Arena/ArenaRun.h>
#include <gtest/gtest.h>

/**
 * The run of the arena over seeds (`src/Mahjong/Arena/ArenaRun.h`): `RunArena` plays the seeds `first .. first + count - 1` for every
 * focus player and each of the four seats against `others`, and sums them. Checked here: the totals are the sums of the per-seed rows,
 * one seed equals the four `PlayArenaHand` calls, the mirror identity (Normal against three Normal wins exactly one hand of the four
 * rotations of a seed), determinism, a focus player's result not depending on the other focus players, the numbers of
 * `ComputerStrengthTest` (seeds 4000..4059, Normal against three random seats: 186 wins of 240, the random seats 27 together, 27 drawn
 * hands, +31730 points) and a bad config. Pure logic: no engine services; the file takes about 5.5 s in Debug (few seeds, the 60 seeds of
 * `ComputerStrengthTest` played once for Normal and Easy together).
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
                   left.winScore == right.winScore && left.winsByKind == right.winsByKind && left.decisions == right.decisions;
        }

        bool SameRows(const eastl::vector<SeedRow>& left, const eastl::vector<SeedRow>& right) {
            if (left.size() != right.size()) {
                return false;
            }
            for (size_t index = 0; index < left.size(); ++index) {
                const SeedRow& a = left[index];
                const SeedRow& b = right[index];
                if (a.seed != b.seed || a.pointsOfFocus != b.pointsOfFocus || a.winsOfFocus != b.winsOfFocus || a.drawn != b.drawn || a.declared != b.declared || a.winScore != b.winScore) {
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
        }
        EXPECT_EQ(result.focus.points, points);
        EXPECT_EQ(result.focus.wins, wins);
        EXPECT_EQ(result.focus.drawn, drawn);
        EXPECT_EQ(result.focus.declared, declared);
        EXPECT_EQ(result.focus.winScore, winScore);

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

}  // namespace BECore::Mahjong::Arena
