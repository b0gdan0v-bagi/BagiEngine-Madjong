#include "HandTestSupport.h"

#include <Mahjong/Arena/ArenaHand.h>
#include <Mahjong/Arena/ArenaPlayer.h>
#include <Mahjong/Arena/ArenaWorlds.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/Wall.h>
#include <gtest/gtest.h>

/**
 * The players and the one hand of the arena (`src/Mahjong/Arena/`): the text specs of the players, the table of the knobs of
 * `LevelProfile`, the random "seek win" chooser and `PlayArenaHand`. The last part is the point of the arena: it plays the hands of
 * `ComputerStrengthTest` (seeds 4000..4059, Normal on each seat in turn against three random seats) to the same numbers (those of
 * Normal since `waitingKongByPoints` and the claim window that settles early, 2026-11-12): 190 wins of 240, 27 won by the random seats together, 23 drawn hands, +35270 points
 * for the focus seat. Pure logic: no engine services; about
 * 6 s in Debug (2 s of it the search seat with and without a loop over its worlds).
 */
namespace BECore::Mahjong::Arena {

    using namespace ::BECore::Mahjong::Tests;

    namespace {

        constexpr uint32_t kFirstSeed = 4000;
        constexpr int kSeedCount = 60;

        ArenaPlayer Player(std::string_view spec) {
            const std::expected<ArenaPlayer, eastl::string> player = ParseArenaPlayer(spec);
            EXPECT_TRUE(player.has_value()) << spec << ": " << (player.has_value() ? "" : player.error().c_str());
            return player.value_or(ArenaPlayer{});
        }

        std::string ErrorOf(std::string_view spec) {
            const std::expected<ArenaPlayer, eastl::string> player = ParseArenaPlayer(spec);
            EXPECT_FALSE(player.has_value()) << "\"" << spec << "\" should be refused";
            return player.has_value() ? std::string() : std::string(player.error().c_str());
        }

        bool SameProfile(const LevelProfile& left, const LevelProfile& right) {
            return left.skipAcceptancePercent == right.skipAcceptancePercent && left.skipDeclarationPercent == right.skipDeclarationPercent && left.skipClaimPercent == right.skipClaimPercent &&
                   left.takeValueClaimsAtEqualShanten == right.takeValueClaimsAtEqualShanten && left.searchWorlds == right.searchWorlds && left.searchCandidates == right.searchCandidates &&
                   left.searchMargin == right.searchMargin;
        }

        ArenaPlayers Table(const ArenaPlayer& focus, Seat focusSeat, const ArenaPlayer& others) {
            ArenaPlayers players;
            players.fill(others);
            players[focusSeat] = focus;
            return players;
        }

        ::testing::AssertionResult Contains(const std::string& text, const char* part) {
            return text.find(part) != std::string::npos ? ::testing::AssertionSuccess() : ::testing::AssertionFailure() << "\"" << text << "\" lacks \"" << part << "\"";
        }

    }  // namespace

    TEST(ArenaPlayerTest, TheLevelsAndRandomAreRead) {
        EXPECT_TRUE(Player("random").random);
        EXPECT_EQ(Player("random").name, "random");
        for (const char* level : {"easy", "normal"}) {
            const ArenaPlayer player = Player(level);
            EXPECT_FALSE(player.random);
            EXPECT_EQ(player.name, level);
            EXPECT_EQ(PlayerName(player), level);
        }
        EXPECT_TRUE(SameProfile(Player("easy").profile, ProfileOf(ComputerLevel::Easy)));
        EXPECT_TRUE(SameProfile(Player("normal").profile, ProfileOf(ComputerLevel::Normal)));
        EXPECT_FALSE(ParseArenaPlayer("hard").has_value());  // Hard was removed (2026-11-09)
        EXPECT_TRUE(SameProfile(Player("normal").profile, kNormalProfile));
    }

    TEST(ArenaPlayerTest, KnobsAreSetOnTheProfileOfTheLevel) {
        const ArenaPlayer claim = Player("normal:skipClaimPercent=35");
        EXPECT_EQ(claim.name, "normal:skipClaimPercent=35");
        EXPECT_EQ(claim.profile.skipClaimPercent, 35);
        EXPECT_EQ(claim.profile.skipAcceptancePercent, 0);
        EXPECT_EQ(claim.profile.skipDeclarationPercent, 0);
        EXPECT_TRUE(claim.profile.takeValueClaimsAtEqualShanten);

        // The knobs start from the level: Easy keeps its other numbers.
        const ArenaPlayer easy = Player("easy:takeValueClaimsAtEqualShanten=1");
        EXPECT_TRUE(easy.profile.takeValueClaimsAtEqualShanten);
        EXPECT_EQ(easy.profile.skipAcceptancePercent, kEasyProfile.skipAcceptancePercent);
        EXPECT_EQ(easy.profile.skipDeclarationPercent, kEasyProfile.skipDeclarationPercent);
        EXPECT_EQ(easy.profile.skipClaimPercent, kEasyProfile.skipClaimPercent);
        EXPECT_FALSE(Player("easy:takeValueClaimsAtEqualShanten=0").profile.takeValueClaimsAtEqualShanten);

        const ArenaPlayer several = Player("easy:skipClaimPercent=20,skipAcceptancePercent=100,skipDeclarationPercent=0");
        EXPECT_EQ(several.profile.skipClaimPercent, 20);
        EXPECT_EQ(several.profile.skipAcceptancePercent, 100);
        EXPECT_EQ(several.profile.skipDeclarationPercent, 0);
        EXPECT_FALSE(Player("normal:skipClaimPercent=0").random);
    }

    TEST(ArenaPlayerTest, TheSearchKnobsAreSetFromTheSpec) {
        const ArenaPlayer search = Player("normal:searchWorlds=8,searchCandidates=3,searchMargin=5");
        EXPECT_EQ(search.profile.searchWorlds, 8);
        EXPECT_EQ(search.profile.searchCandidates, 3);
        EXPECT_EQ(search.profile.searchMargin, 5);
        EXPECT_EQ(search.profile.skipClaimPercent, kNormalProfile.skipClaimPercent);
        // Only the worlds named: the candidates and the margin stay at the neutral defaults.
        const ArenaPlayer worlds = Player("normal:searchWorlds=2");
        EXPECT_EQ(worlds.profile.searchWorlds, 2);
        EXPECT_EQ(worlds.profile.searchCandidates, kNormalProfile.searchCandidates);
        EXPECT_EQ(worlds.profile.searchMargin, kNormalProfile.searchMargin);
        EXPECT_TRUE(Contains(ErrorOf("normal:searchWorlds=4097"), "4097"));
        EXPECT_TRUE(Contains(ErrorOf("normal:searchMargin=101"), "101"));
        EXPECT_TRUE(Contains(ErrorOf("normal:searchCandidates=13"), "13"));
    }

    TEST(ArenaPlayerTest, TheOraclesAreReadOnTheNormalProfile) {
        static_assert(kProfileFieldCount == 11, "an oracle is no LevelProfile field");
        for (const auto& [spec, kind] : {std::pair<const char*, OracleKind>{"oracle-acceptance", OracleKind::Acceptance}, {"oracle-defence", OracleKind::Defence}}) {
            const ArenaPlayer player = Player(spec);
            EXPECT_EQ(player.oracle, kind) << spec;
            EXPECT_FALSE(player.random);
            EXPECT_EQ(player.name, spec);
            EXPECT_TRUE(SameProfile(player.profile, kNormalProfile));
        }
        const ArenaPlayer knobs = Player("oracle-defence:searchWorlds=2,skipClaimPercent=10");
        EXPECT_EQ(knobs.oracle, OracleKind::Defence);
        EXPECT_EQ(knobs.name, "oracle-defence:searchWorlds=2,skipClaimPercent=10");
        EXPECT_EQ(knobs.profile.searchWorlds, 2);
        EXPECT_EQ(knobs.profile.skipClaimPercent, 10);
        // The search oracles start from the shipped budget and the knobs override it.
        for (const auto& [spec, kind] : {std::pair<const char*, OracleKind>{"oracle-hands", OracleKind::Hands}, {"oracle-whole", OracleKind::WholeHand}}) {
            const ArenaPlayer player = Player(spec);
            EXPECT_EQ(player.oracle, kind) << spec;
            EXPECT_EQ(player.name, spec);
            EXPECT_EQ(player.profile.searchWorlds, kSearchWorlds);
            EXPECT_EQ(player.profile.searchCandidates, kSearchCandidates);
            EXPECT_EQ(player.profile.searchMargin, kSearchMargin);
            EXPECT_EQ(player.profile.skipClaimPercent, kNormalProfile.skipClaimPercent);
        }
        const ArenaPlayer wide = Player("oracle-hands:searchWorlds=8,searchMargin=10");
        EXPECT_EQ(wide.profile.searchWorlds, 8);
        EXPECT_EQ(wide.profile.searchCandidates, kSearchCandidates);
        EXPECT_EQ(wide.profile.searchMargin, 10);
        EXPECT_EQ(wide.name, "oracle-hands:searchWorlds=8,searchMargin=10");
        EXPECT_EQ(Player("oracle-whole:searchCandidates=2").profile.searchCandidates, kSearchMinCandidates);
        // A search oracle with no worlds or with one candidate would be plain Normal.
        EXPECT_TRUE(Contains(ErrorOf("oracle-hands:searchWorlds=0"), "oracle-hands:searchWorlds=0"));
        EXPECT_TRUE(Contains(ErrorOf("oracle-whole:searchCandidates=1"), "plain Normal"));
        EXPECT_TRUE(Contains(ErrorOf("oracle-whole:searchCandidates=0"), "plain Normal"));
        // An ordinary player is no oracle.
        for (const char* spec : {"normal", "easy:skipClaimPercent=20", "random", "7"}) {
            EXPECT_EQ(Player(spec).oracle, OracleKind::None) << spec;
        }
        // A bad oracle name names the part and lists the four oracles.
        for (const char* spec : {"oracle-wise", "oracle-", "oracle-acceptance2:searchWorlds=2"}) {
            const std::string error = ErrorOf(spec);
            EXPECT_TRUE(Contains(error, "oracle")) << error;
            for (const char* name : {"oracle-acceptance", "oracle-hands", "oracle-whole", "oracle-defence"}) {
                EXPECT_TRUE(Contains(error, name)) << error;
            }
        }
        EXPECT_TRUE(Contains(ErrorOf("oracle-wise"), "\"oracle-wise\""));
        EXPECT_TRUE(Contains(ErrorOf("oracle-defence:"), "no knobs"));
        EXPECT_TRUE(Contains(ErrorOf("oracle-defence:searchWorlds=4097"), "4097"));
        // oracle-acceptance plays no search: a search knob is refused by name, the other knobs stay.
        for (const char* spec : {"oracle-acceptance:searchWorlds=2", "oracle-acceptance:skipClaimPercent=5,searchMargin=10", "oracle-acceptance:searchCandidates=4"}) {
            const std::string error = ErrorOf(spec);
            EXPECT_TRUE(Contains(error, "oracle-acceptance")) << error;
            EXPECT_TRUE(Contains(error, "plays no search")) << error;
        }
        EXPECT_TRUE(Contains(ErrorOf("oracle-acceptance:skipClaimPercent=5,searchMargin=10"), "searchMargin"));
        EXPECT_EQ(Player("oracle-acceptance:skipClaimPercent=5").profile.skipClaimPercent, 5);
    }

    TEST(ArenaPlayerTest, ABadSpecIsAnErrorThatNamesThePart) {
        EXPECT_TRUE(Contains(ErrorOf(""), "empty"));
        EXPECT_TRUE(Contains(ErrorOf("Normal"), "\"Normal\""));
        EXPECT_TRUE(Contains(ErrorOf("medium"), "\"medium\""));
        EXPECT_TRUE(Contains(ErrorOf("hard "), "\"hard \""));
        EXPECT_TRUE(Contains(ErrorOf("random:skipClaimPercent=5"), "random"));
        EXPECT_TRUE(Contains(ErrorOf("normal:"), "no knobs"));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaimPercent"), "\"skipClaimPercent\""));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaimPercent="), "no whole number"));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaimPercent=ten"), "\"ten\""));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaimPercent=5x"), "\"5x\""));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaimPercent=101"), "101"));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaimPercent=-1"), "-1"));
        EXPECT_TRUE(Contains(ErrorOf("normal:takeValueClaimsAtEqualShanten=2"), "takeValueClaimsAtEqualShanten"));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaim=5"), "\"skipClaim\""));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaimPercent=5,skipClaimPercent=6"), "twice"));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaimPercent=5,"), "no knob=value"));
        EXPECT_TRUE(Contains(ErrorOf("normal:skipClaimPercent=5,,skipAcceptancePercent=5"), "no knob=value"));
    }

    TEST(ArenaPlayerTest, TheKnobsTableCoversEveryFieldOfTheProfile) {
        // Each row sets its own field and no other; between them the rows set all ten fields of `LevelProfile` (an eleventh field
        // without a row stops the build in ArenaPlayer.cpp, which names every field).
        eastl::array<bool, kProfileFieldCount> moved{};
        for (const ProfileKnob& knob : kProfileKnobs) {
            ASSERT_NE(knob.percent == nullptr, knob.flag == nullptr) << knob.name.data() << ": exactly one member";
            const std::string name(knob.name.data(), knob.name.size());
            // Normal has every percent, the worlds, the margin and the depth at 0, the candidates at their default and the take-value flag on (the pick-wait and
            // danger flags off): the other value is the largest value, and for a flag the opposite of Normal's.
            const int other = knob.percent != nullptr ? knob.maxValue : (kNormalProfile.*knob.flag ? 0 : 1);
            const ArenaPlayer player = Player("normal:" + name + "=" + std::to_string(other));
            const LevelProfile& set = player.profile;
            const bool changed[kProfileFieldCount] = {set.skipAcceptancePercent != kNormalProfile.skipAcceptancePercent,
                                                      set.skipDeclarationPercent != kNormalProfile.skipDeclarationPercent,
                                                      set.skipClaimPercent != kNormalProfile.skipClaimPercent,
                                                      set.takeValueClaimsAtEqualShanten != kNormalProfile.takeValueClaimsAtEqualShanten,
                                                      set.searchWorlds != kNormalProfile.searchWorlds,
                                                      set.searchCandidates != kNormalProfile.searchCandidates,
                                                      set.searchMargin != kNormalProfile.searchMargin,
                                                      set.ownDrawsDepth != kNormalProfile.ownDrawsDepth,
                                                      set.ownDrawsPickWait != kNormalProfile.ownDrawsPickWait,
                                                      set.readDanger != kNormalProfile.readDanger,
                                                      set.waitingKongByPoints != kNormalProfile.waitingKongByPoints};
            EXPECT_EQ(std::count(changed, changed + kProfileFieldCount, true), 1) << name;
            for (size_t index = 0; index < kProfileFieldCount; ++index) {
                moved[index] = moved[index] || changed[index];
            }
            // The largest value is accepted, one above it is refused.
            EXPECT_FALSE(ParseArenaPlayer("normal:" + name + "=" + std::to_string(knob.maxValue + 1)).has_value()) << name;
            EXPECT_TRUE(ParseArenaPlayer("normal:" + name + "=" + std::to_string(knob.maxValue)).has_value()) << name;
        }
        EXPECT_EQ(static_cast<size_t>(std::count(moved.begin(), moved.end(), true)), kProfileFieldCount);
    }

    TEST(ArenaPlayerTest, TheRandomChooserPicksOnlyListedActionsAndWinsWhenItCan) {
        const HandAction win = HandAction::MakeDeclareWin();
        const HandAction mahjong = HandAction::MakeClaim(ClaimKind::Mahjong);
        const HandAction pung = HandAction::MakeClaim(ClaimKind::Pung);
        const HandAction pass = HandAction::MakePass();
        const HandAction discard = HandAction::MakeDiscard(T("5p"));
        MahjongRng rng(7);
        for (int round = 0; round < 20; ++round) {
            EXPECT_EQ(ChooseSeekWin({discard, win, pass}, rng).kind, ActionKind::DeclareWin);
            EXPECT_EQ(ChooseSeekWin({pass, pung, mahjong}, rng).claim, ClaimKind::Mahjong);
            // A claim is preferred to a plain discard and a pass.
            EXPECT_EQ(ChooseSeekWin({pass, pung}, rng).kind, ActionKind::Claim);
            EXPECT_EQ(ChooseSeekWin({discard}, rng).kind, ActionKind::Discard);
        }
    }

    TEST(ArenaPlayerTest, TheRandomChooserIsTheSeekWinChooserOfTheTests) {
        // On the actions of real hands (every seat of 12 played hands, at every step) the arena's chooser takes the action and the
        // draws of `Choose(..., Mode::SeekWin, ...)` of `HandTestSupport.h`.
        const MahjongSettings settings;
        int compared = 0;
        for (uint32_t seed = 100; seed < 112; ++seed) {
            MahjongRng wallRng(seed);
            const Seat east = static_cast<Seat>(seed % kSeatCount);
            HandModel hand;
            ASSERT_TRUE(hand.Begin(settings, MakeWall(wallRng, settings, east), MakeSeatWinds(east), Wind::East).has_value());
            MahjongRng left(seed);
            MahjongRng right(seed);
            MahjongRng play(seed + 1);
            while (hand.GetPhase() != Phase::Finished) {
                const Seat actor = ComputerTable::NextActor(hand);
                const eastl::vector<HandAction> actions = hand.LegalActions(actor);
                ASSERT_FALSE(actions.empty());
                const HandAction arena = ChooseSeekWin(actions, left);
                const HandAction tests = Choose(actions, Mode::SeekWin, right);
                ASSERT_TRUE(Has(actions, arena)) << "seed " << seed;
                ASSERT_TRUE(arena == tests) << "seed " << seed;
                ++compared;
                // The hand goes on with a random action so that many kinds of window are met.
                ASSERT_TRUE(hand.Apply(actor, actions[play.Below(static_cast<uint32_t>(actions.size()))]).has_value());
            }
        }
        EXPECT_GT(compared, 500);
    }

    TEST(ArenaPlayerTest, AHandIsDeterministicAndKeepsItsBooks) {
        const ArenaPlayers players = Table(Player("normal"), 1, Player("random"));
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + 8; ++seed) {
            const std::expected<HandRecord, eastl::string> first = PlayArenaHand(seed, players);
            const std::expected<HandRecord, eastl::string> second = PlayArenaHand(seed, players);
            ASSERT_TRUE(first.has_value()) << first.error().c_str();
            ASSERT_TRUE(second.has_value());
            EXPECT_EQ(first->kind, second->kind);
            EXPECT_EQ(first->winner, second->winner);
            EXPECT_EQ(first->scores, second->scores);
            EXPECT_EQ(first->deltas, second->deltas);
            EXPECT_EQ(first->declared, second->declared);
            EXPECT_EQ(first->decisions, second->decisions);
            EXPECT_EQ(first->winKinds, second->winKinds);
            EXPECT_EQ(Sum(first->deltas), 0);
            EXPECT_EQ(first->kind == ResultKind::Win, IsSeat(first->winner));
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_GT(first->decisions[seat], 0) << "seed " << seed << " seat " << seat;
                EXPECT_EQ(first->nanoseconds[seat], 0) << "no timing asked for";
            }
        }
    }

    TEST(ArenaPlayerTest, ASearchSeatPlaysTheSameHandWithAndWithoutALoopOverItsWorlds) {
        // Seat 1 searches (one world, two candidates, no margin: it leaves Normal's choice where the world says so), the others are random.
        const ArenaPlayers players = Table(Player("normal:searchWorlds=1,searchCandidates=2,searchMargin=0"), 1, Player("random"));
        constexpr uint32_t kSearchSeeds = 2;
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + kSearchSeeds; ++seed) {
            const std::expected<HandRecord, eastl::string> plain = PlayArenaHand(seed, players);
            ASSERT_TRUE(plain.has_value()) << plain.error().c_str();
            EXPECT_GT(plain->decisions[1], 0);
            for (const int threads : {1, 2, 5}) {
                WorldThreads pool(threads);
                const WorldLoop loop = pool.AsLoop();
                const std::expected<HandRecord, eastl::string> looped = PlayArenaHand(seed, players, false, &loop);
                ASSERT_TRUE(looped.has_value()) << looped.error().c_str();
                EXPECT_EQ(looped->kind, plain->kind) << "seed " << seed << " threads " << threads;
                EXPECT_EQ(looped->winner, plain->winner) << "seed " << seed << " threads " << threads;
                EXPECT_EQ(looped->scores, plain->scores) << "seed " << seed << " threads " << threads;
                EXPECT_EQ(looped->deltas, plain->deltas) << "seed " << seed << " threads " << threads;
                EXPECT_EQ(looped->declared, plain->declared) << "seed " << seed << " threads " << threads;
                EXPECT_EQ(looped->decisions, plain->decisions) << "seed " << seed << " threads " << threads;
                EXPECT_EQ(looped->winKinds, plain->winKinds) << "seed " << seed << " threads " << threads;
            }
        }
    }

    TEST(ArenaPlayerTest, ALoopChangesNothingForPlayersThatDoNotSearch) {
        const ArenaPlayers players = Table(Player("normal"), 0, Player("random"));
        WorldThreads pool(3);
        const WorldLoop loop = pool.AsLoop();
        const std::expected<HandRecord, eastl::string> plain = PlayArenaHand(kFirstSeed, players);
        const std::expected<HandRecord, eastl::string> looped = PlayArenaHand(kFirstSeed, players, true, &loop);
        ASSERT_TRUE(plain.has_value());
        ASSERT_TRUE(looped.has_value());
        EXPECT_EQ(looped->deltas, plain->deltas);
        EXPECT_EQ(looped->decisions, plain->decisions);
        EXPECT_GT(looped->nanoseconds[0], 0) << "the timing is kept with a loop";
    }

    TEST(ArenaPlayerTest, TimingIsKeptOnlyWhenAskedFor) {
        const ArenaPlayers players = Table(Player("normal"), 0, Player("random"));
        const std::expected<HandRecord, eastl::string> plain = PlayArenaHand(kFirstSeed, players, false);
        const std::expected<HandRecord, eastl::string> timed = PlayArenaHand(kFirstSeed, players, true);
        ASSERT_TRUE(plain.has_value());
        ASSERT_TRUE(timed.has_value());
        // The play is the same; only the clock is added.
        EXPECT_EQ(plain->deltas, timed->deltas);
        EXPECT_EQ(plain->decisions, timed->decisions);
        EXPECT_GT(timed->nanoseconds[0], 0);
    }

    TEST(ArenaPlayerTest, WinKindsAreTheKindsOfTheSettlement) {
        // Four Normal players play like `ComputerTable(seed)` (the same seat seeds), so the same hand is finished here and settled.
        const MahjongSettings settings;
        const ArenaPlayers players = Table(Player("normal"), 0, Player("normal"));
        int wins = 0;
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + 24; ++seed) {
            MahjongRng wallRng(seed);
            const Seat east = static_cast<Seat>(seed % kSeatCount);
            HandModel hand;
            ASSERT_TRUE(hand.Begin(settings, MakeWall(wallRng, settings, east), MakeSeatWinds(east), Wind::East).has_value());
            ComputerTable table(seed);
            ASSERT_TRUE(table.PlayHand(hand).has_value());
            const HandSettlement settlement = SettleHand(hand);

            const std::expected<HandRecord, eastl::string> record = PlayArenaHand(seed, players);
            ASSERT_TRUE(record.has_value()) << record.error().c_str();
            EXPECT_EQ(record->winner, settlement.winner) << "seed " << seed;
            EXPECT_EQ(record->scores, settlement.scores);
            EXPECT_EQ(record->deltas, settlement.deltas);
            if (!IsSeat(settlement.winner)) {
                EXPECT_TRUE(record->winKinds.empty());
                continue;
            }
            ++wins;
            eastl::vector<ScoreKind> expected;
            for (const ScoreLine& line : settlement.breakdowns[settlement.winner].lines) {
                if (eastl::find(expected.begin(), expected.end(), line.kind) == expected.end()) {
                    expected.push_back(line.kind);
                }
            }
            EXPECT_EQ(record->winKinds, expected) << "seed " << seed;
            EXPECT_FALSE(record->winKinds.empty());
            // Each kind once.
            for (size_t i = 0; i < record->winKinds.size(); ++i) {
                EXPECT_EQ(std::count(record->winKinds.begin(), record->winKinds.end(), record->winKinds[i]), 1);
            }
        }
        EXPECT_GT(wins, 0);
    }

    TEST(ArenaPlayerTest, TheArenaPlaysTheHandsOfComputerStrengthTest) {
        // Normal on each seat in turn against three random seats, seeds 4000..4059: the numbers of `ComputerStrengthTest.cpp`.
        const ArenaPlayer normal = Player("normal");
        const ArenaPlayer random = Player("random");
        int hands = 0;
        int focusWins = 0;
        int randomWins = 0;
        int drawn = 0;
        int focusPoints = 0;
        for (int index = 0; index < kSeedCount; ++index) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const std::expected<HandRecord, eastl::string> record = PlayArenaHand(kFirstSeed + static_cast<uint32_t>(index), Table(normal, seat, random));
                ASSERT_TRUE(record.has_value()) << record.error().c_str();
                ++hands;
                focusWins += record->winner == seat ? 1 : 0;
                randomWins += IsSeat(record->winner) && record->winner != seat ? 1 : 0;
                drawn += record->kind == ResultKind::Drawn ? 1 : 0;
                focusPoints += record->deltas[seat];
            }
        }
        EXPECT_EQ(hands, 240);
        EXPECT_EQ(focusWins, 190);
        EXPECT_EQ(randomWins, 27);
        EXPECT_EQ(drawn, 23);
        EXPECT_EQ(focusPoints, 35270);
    }

    TEST(ArenaPlayerTest, AProfilePlayerPlaysLikeTheComputerPlayerOfItsLevel) {
        // The Easy profile through the arena is `ComputerPlayer(seed, Easy)`: four Easy seats play the hand of `ComputerTable(seed, Easy)`.
        const MahjongSettings settings;
        const ArenaPlayers players = Table(Player("easy"), 0, Player("easy"));
        for (uint32_t seed = kFirstSeed; seed < kFirstSeed + 12; ++seed) {
            MahjongRng wallRng(seed);
            const Seat east = static_cast<Seat>(seed % kSeatCount);
            HandModel hand;
            ASSERT_TRUE(hand.Begin(settings, MakeWall(wallRng, settings, east), MakeSeatWinds(east), Wind::East).has_value());
            ComputerTable table(seed, ComputerLevel::Easy);
            ASSERT_TRUE(table.PlayHand(hand).has_value());
            const HandSettlement settlement = SettleHand(hand);
            const std::expected<HandRecord, eastl::string> record = PlayArenaHand(seed, players);
            ASSERT_TRUE(record.has_value());
            EXPECT_EQ(record->deltas, settlement.deltas) << "seed " << seed;
            EXPECT_EQ(record->winner, settlement.winner) << "seed " << seed;
        }
    }

}  // namespace BECore::Mahjong::Arena
