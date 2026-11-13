#include "ComputerTestSupport.h"

#include <Mahjong/Arena/ArenaHand.h>
#include <Mahjong/Arena/ArenaOracle.h>
#include <Mahjong/Arena/ArenaPlayer.h>
#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/HandShape.h>
#include <gtest/gtest.h>

/**
 * The oracle players of the arena (`src/Mahjong/Arena/ArenaOracle.h`): the view of `oracle-acceptance` differs from the real one only in
 * `visible` and counts exactly the copies that can still be drawn; the oracles choose listed actions only and play whole hands;
 * `oracle-defence` is Normal's action except where Normal's discard wins for a declared opponent and a safe discard of equal shanten
 * exists. Pure logic: no engine services; a few seconds in Debug.
 */
namespace BECore::Mahjong::Arena {

    using namespace ::BECore::Mahjong::Tests;

    namespace {

        constexpr uint32_t kFirstSeed = 4000;
        constexpr int kStateSeedCount = 8;
        constexpr int kStateStride = 9;
        constexpr int kDecisionSeedCount = 20;
        constexpr int kDefenceSeedCount = 40;
        constexpr int kWholeHandCount = 10;
        constexpr int kSearchedHandCount = 2;
        constexpr uint32_t kRngStride = 1000;

        ArenaPlayer Player(std::string_view spec) {
            const std::expected<ArenaPlayer, eastl::string> player = ParseArenaPlayer(spec);
            EXPECT_TRUE(player.has_value()) << spec << ": " << (player.has_value() ? "" : player.error().c_str());
            return player.value_or(ArenaPlayer{});
        }

        ArenaPlayers Table(const ArenaPlayer& player) {
            ArenaPlayers players;
            players.fill(player);
            return players;
        }

        /// Normal's action on `view` with a fresh rng of `rngSeed`.
        HandAction NormalOn(const TableView& view, uint32_t rngSeed) {
            MahjongRng rng(rngSeed);
            return view.phase == Phase::Discard ? ChooseTurnAction(view, rng) : ChooseClaimAction(view, rng);
        }

        /// Every position of the hands of `seeds` (Normal on every seat), one in `stride`.
        struct Position {
            HandModel hand;
            Seat actor = kNoSeat;
            uint32_t rngSeed = 0;
        };

        eastl::vector<Position> Positions(int seeds, int stride) {
            eastl::vector<Position> positions;
            const ArenaPlayers players = Table(Player("normal"));
            for (int index = 0; index < seeds; ++index) {
                const uint32_t seed = kFirstSeed + static_cast<uint32_t>(index);
                int step = 0;
                const HandObserver observer = [&](const HandModel& hand, Seat actor) {
                    if (step++ % stride == 0) {
                        positions.push_back({hand, actor, seed * kRngStride + static_cast<uint32_t>(step)});
                    }
                };
                const auto record = PlayArenaHand(seed, players, false, nullptr, &observer);
                EXPECT_TRUE(record.has_value()) << (record.has_value() ? "" : record.error().c_str());
            }
            return positions;
        }

        ::testing::AssertionResult SameButVisible(const TableView& real, const TableView& oracle) {
            const auto differs = [](const char* what) { return ::testing::AssertionFailure() << what << " differs"; };
            if (real.seat != oracle.seat || real.phase != oracle.phase || real.currentSeat != oracle.currentSeat || real.pendingTile != oracle.pendingTile) {
                return differs("seat, phase, current seat or pending tile");
            }
            if (real.seatWind != oracle.seatWind || real.roundWind != oracle.roundWind || real.liveCount != oracle.liveCount) {
                return differs("wind or live count");
            }
            if (real.own.concealed != oracle.own.concealed || real.own.melds.size() != oracle.own.melds.size() || real.own.callingDeclared != oracle.own.callingDeclared) {
                return differs("own hand");
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PublicSeat& a = real.seats[seat];
                const PublicSeat& b = oracle.seats[seat];
                if (a.openMelds.size() != b.openMelds.size() || a.concealedKongs != b.concealedKongs || a.bonusCount != b.bonusCount || a.discardCount != b.discardCount ||
                    a.callingDeclared != b.callingDeclared || a.discardedKinds != b.discardedKinds || a.passedKinds != b.passedKinds) {
                    return differs("a public seat");
                }
            }
            if (real.legal != oracle.legal) {
                return differs("legal actions");
            }
            return ::testing::AssertionSuccess();
        }

        /// The kinds a declared seat other than `seat` wins on.
        TileCounts DeclaredWins(const HandModel& hand, Seat seat) {
            TileCounts wins{};
            for (Seat other = 0; other < kSeatCount; ++other) {
                if (other != seat && hand.GetHand(other).callingDeclared) {
                    for (const Tile tile : WinningTiles(hand.GetHand(other))) {
                        wins[tile.Id()] = 1;
                    }
                }
            }
            return wins;
        }

        // East discards one of the two honours it holds alone: the tile it throws (Ww) is the only one seat 1 waits on (a tanki on Ww),
        // and Nw is the safe discard of the same shanten (both keep four sets and a single tile, a calling hand).
        FixedWallSpec DefenceSpec(const char* seat1) {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1m 2m 3m 4p 5p 6p 7s 8s 9s Gd Gd Gd Ww";
            spec.hands[kSeat1] = seat1;
            spec.hands[kSeat2] = "2p 5p 8p 2s 5s 8s 2m 5m 8m Sw Gd Wd Rd";
            spec.hands[kSeat3] = "3p 6p 9p 3s 6s 9s 3m 6m 9m Sw Rd Wd Nw";
            spec.live = "Nw";
            return spec;
        }

        constexpr const char* kTankiOnWw = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ew Ew Ew Ww";
        constexpr const char* kShanponOnWwNw = "1p 2p 3p 4s 5s 6s 7m 8m 9m Ww Ww Nw Nw";

        /// The hand of `DefenceSpec(seat1)` with seat 1 declared (the calling hand is the state's own: the lap that would declare it
        /// is not played).
        HandModel DefenceModel(const char* seat1, bool seat1Declared) {
            HandState state = MakeModel(DefenceSpec(seat1)).Capture();
            state.hands[kSeat1].callingDeclared = seat1Declared;
            std::expected<HandModel, eastl::string> restored = HandModel::Restore(std::move(state));
            EXPECT_TRUE(restored.has_value()) << (restored.has_value() ? "" : restored.error().c_str());
            return restored.value_or(HandModel{});
        }

        HandAction OracleDefence(const HandModel& hand, Seat seat, uint32_t rngSeed) {
            MahjongRng rng(rngSeed);
            return ChooseOracle(hand, seat, Player("oracle-defence"), rng, nullptr);
        }

    }  // namespace

    TEST(ArenaOracleTest, TheAcceptanceViewDiffersFromTheRealViewOnlyInVisible) {
        const eastl::vector<Position> positions = Positions(kStateSeedCount, kStateStride);
        ASSERT_GE(positions.size(), 30U);
        bool sawDiscard = false;
        bool sawClaim = false;
        for (const Position& position : positions) {
            const TableView real = MakeTableView(position.hand, position.actor);
            const TableView oracle = TrueAcceptanceView(position.hand, position.actor);
            EXPECT_TRUE(SameButVisible(real, oracle));
            sawDiscard = sawDiscard || real.phase == Phase::Discard;
            sawClaim = sawClaim || real.phase == Phase::Claim;

            TileCounts drawable{};
            for (const Tile tile : position.hand.GetWall().LiveTiles()) {
                ++drawable[tile.Id()];
            }
            for (const Tile tile : position.hand.GetWall().LooseTiles()) {
                ++drawable[tile.Id()];
            }
            for (uint8_t id = 0; id < kTileIdCount; ++id) {
                const Tile tile{id};
                if (tile.IsPlayable()) {
                    EXPECT_EQ(UnseenCopies(oracle, tile), drawable[id]) << ToString(tile);
                }
            }
        }
        EXPECT_TRUE(sawDiscard);
        EXPECT_TRUE(sawClaim);
    }

    TEST(ArenaOracleTest, TheAcceptanceOracleChoosesListedActionsAndSometimesDiffersFromNormal) {
        const ArenaPlayer oracle = Player("oracle-acceptance");
        int differs = 0;
        for (const Position& position : Positions(kDecisionSeedCount, 1)) {
            const TableView view = MakeTableView(position.hand, position.actor);
            MahjongRng rng(position.rngSeed);
            const HandAction action = ChooseOracle(position.hand, position.actor, oracle, rng, nullptr);
            EXPECT_TRUE(Has(view.legal, action)) << "seed " << position.rngSeed;
            differs += action != NormalOn(view, position.rngSeed) ? 1 : 0;
        }
        EXPECT_GT(differs, 0) << "an oracle that is on never decides otherwise than Normal";
    }

    TEST(ArenaOracleTest, TheDefenceKeepsNormalsActionWhenItIsSafeOrNothingSaferExists) {
        // Seat 1 waits on Ww alone: East's Normal throws Ww or Nw (a tie it breaks by its rng); Ww is replaced by Nw, Nw stands.
        const HandModel waitsOnWw = DefenceModel(kTankiOnWw, true);
        int replaced = 0;
        int kept = 0;
        for (uint32_t rngSeed = 1; rngSeed <= 20; ++rngSeed) {
            const TableView view = MakeTableView(waitsOnWw, kSeat0);
            const HandAction normal = NormalOn(view, rngSeed);
            ASSERT_EQ(normal.kind, ActionKind::Discard);
            const HandAction defence = OracleDefence(waitsOnWw, kSeat0, rngSeed);
            EXPECT_TRUE(Has(view.legal, defence));
            if (normal.tile == T("Ww")) {
                EXPECT_EQ(defence, HandAction::MakeDiscard(T("Nw"), normal.declareCalling));
                ++replaced;
            } else {
                EXPECT_EQ(defence, normal);
                ++kept;
            }
        }
        EXPECT_GT(replaced, 0);
        EXPECT_GT(kept, 0);

        // Seat 1 waits on both honours: no safe discard of equal shanten exists, Normal's action stands.
        const HandModel waitsOnBoth = DefenceModel(kShanponOnWwNw, true);
        for (uint32_t rngSeed = 1; rngSeed <= 20; ++rngSeed) {
            EXPECT_EQ(OracleDefence(waitsOnBoth, kSeat0, rngSeed), NormalOn(MakeTableView(waitsOnBoth, kSeat0), rngSeed));
        }

        // The same hand without the declaration: nobody is a danger.
        const HandModel undeclared = DefenceModel(kTankiOnWw, false);
        for (uint32_t rngSeed = 1; rngSeed <= 20; ++rngSeed) {
            EXPECT_EQ(OracleDefence(undeclared, kSeat0, rngSeed), NormalOn(MakeTableView(undeclared, kSeat0), rngSeed));
        }
    }

    TEST(ArenaOracleTest, TheDefenceAvoidsARobbedKongWhenAnEqualDiscardExists) {
        // Seat 1 has an open pung of 5p and the fourth 5p drawn; seat 2 has declared and waits on the 5p.
        const HandModel hand = UpgradeModel({kSeat2});
        const TableView view = MakeTableView(hand, kSeat1);
        ASSERT_TRUE(Has(view.legal, HandAction::MakeUpgradeKong(T("5p"))));
        const TileCounts wins = DeclaredWins(hand, kSeat1);
        ASSERT_EQ(wins[T("5p").Id()], 1);
        int kongs = 0;
        int moved = 0;
        for (uint32_t rngSeed = 1; rngSeed <= 5; ++rngSeed) {
            const HandAction normal = NormalOn(view, rngSeed);
            const HandAction defence = OracleDefence(hand, kSeat1, rngSeed);
            EXPECT_TRUE(Has(view.legal, defence));
            kongs += normal.kind == ActionKind::UpgradeKong ? 1 : 0;
            moved += defence != normal ? 1 : 0;
            if (normal.kind != ActionKind::UpgradeKong) {
                EXPECT_EQ(defence, normal);
                continue;
            }
            // The kong is robbed: the answer is a discard that wins for nobody and leaves the lowest shanten a discard can, or the kong
            // when no such discard exists.
            if (defence.kind == ActionKind::Discard) {
                EXPECT_EQ(wins[defence.tile.Id()], 0);
                const TileCounts counts = CountTiles(view.own.concealed);
                EXPECT_EQ(RateDiscards(counts, MeldCountOf(view), view).front().shanten, [&] {
                    for (const DiscardOption& option : RateDiscards(counts, MeldCountOf(view), view)) {
                        if (option.tile == defence.tile) {
                            return option.shanten;
                        }
                    }
                    return -2;
                }());
            } else {
                EXPECT_EQ(defence, normal);
            }
        }
        EXPECT_GT(kongs, 0) << "Normal never upgrades the kong in this position, so the test checks nothing";
        EXPECT_GT(moved, 0);
    }

    TEST(ArenaOracleTest, TheDefenceNeverLeavesTheLegalListAndOnlyMovesAWinningDiscard) {
        int replaced = 0;
        const ArenaPlayer defence = Player("oracle-defence");
        for (const Position& position : Positions(kDefenceSeedCount, 1)) {
            const TableView view = MakeTableView(position.hand, position.actor);
            MahjongRng rng(position.rngSeed);
            const HandAction action = ChooseOracle(position.hand, position.actor, defence, rng, nullptr);
            EXPECT_TRUE(Has(view.legal, action)) << "seed " << position.rngSeed;
            const HandAction normal = NormalOn(view, position.rngSeed);
            if (action == normal) {
                continue;
            }
            ++replaced;
            const TileCounts wins = DeclaredWins(position.hand, position.actor);
            EXPECT_EQ(view.phase, Phase::Discard);
            EXPECT_EQ(action.kind, ActionKind::Discard);
            EXPECT_EQ(wins[normal.tile.Id()], 1) << "a safe action was replaced";
            EXPECT_EQ(wins[action.tile.Id()], 0);
            EXPECT_EQ(action.declareCalling, normal.declareCalling);
        }
        EXPECT_GT(replaced, 0) << "the defence never moved a discard in the hands played";
    }

    TEST(ArenaOracleTest, FourOracleSeatsPlayWholeHands) {
        for (const char* spec : {"oracle-acceptance", "oracle-defence", "oracle-defence:searchWorlds=2"}) {
            const ArenaPlayers players = Table(Player(spec));
            // The search of the last spec plays whole hands in each decision: a few hands are enough.
            const int hands = std::string_view(spec).find("searchWorlds") == std::string_view::npos ? kWholeHandCount : kSearchedHandCount;
            for (int index = 0; index < hands; ++index) {
                const auto record = PlayArenaHand(kFirstSeed + static_cast<uint32_t>(index), players);
                EXPECT_TRUE(record.has_value()) << spec << ": " << (record.has_value() ? "" : record.error().c_str());
            }
        }
    }

}  // namespace BECore::Mahjong::Arena
