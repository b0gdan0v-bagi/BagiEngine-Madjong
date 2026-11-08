#include "HandTestSupport.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/Deal.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/TileSet.h>
#include <Mahjong/Game/Wall.h>
#include <gtest/gtest.h>

/**
 * Whole hands played with random legal actions, for many seeds. After every action the invariants of a hand hold: no tile is lost or
 * made, the hands have the size their state asks for, the dead wall is untouched, a result exists exactly when the hand is
 * finished, and every action of every `LegalActions` list is accepted by `Apply`; for the first seeds also the other way round, an action
 * is accepted only when it is listed. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeedCount = 200;
        /// The seeds whose every step also tries every canonical action of the actor (a few hundred `Apply` calls per step).
        constexpr uint32_t kBruteForceSeedCount = 20;
        /// Far more than a hand needs: a draw, a discard and the odd claim per tile of the wall.
        constexpr int kStepLimit = 2000;
        constexpr size_t kMeldedByAnySet = 3;

        struct Outcome {
            int wins = 0;
            int draws = 0;
        };

        /// The tiles in the model, counted by kind: hands (concealed, melds with a kong as four, laid-open bonus tiles), the pile and
        /// every part of the wall. The tile of a claim that is waiting is in the pile, the fourth tile of a kong that is waiting is in
        /// the hand; both are counted once.
        std::array<int, kTileIdCount> CountEveryTile(const HandModel& model) {
            std::array<int, kTileIdCount> counts{};
            const auto add = [&counts](Tile tile, int copies = 1) { counts[tile.Id()] += copies; };
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PlayerHand& hand = model.GetHand(seat);
                for (const Tile tile : hand.concealed) {
                    add(tile);
                }
                for (const Tile tile : hand.bonus) {
                    add(tile);
                }
                for (const Meld& meld : hand.melds) {
                    if (meld.kind == MeldKind::Chow) {
                        for (const Tile member : ChowTiles(meld.first)) {
                            add(member);
                        }
                    } else {
                        add(meld.first, meld.kind == MeldKind::Kong ? kCopiesPerTile : static_cast<int>(kMeldedByAnySet));
                    }
                }
            }
            for (const DiscardEntry& entry : model.Discards()) {
                add(entry.tile);
            }
            for (const Tile tile : model.GetWall().LiveTiles()) {
                add(tile);
            }
            for (const Tile tile : model.GetWall().LooseTiles()) {
                add(tile);
            }
            for (const Tile tile : model.GetWall().DeadWall()) {
                add(tile);
            }
            return counts;
        }

        /// The tiles of a seat as the model counts a hand: the concealed ones and three for each meld.
        size_t HeldTiles(const PlayerHand& hand) {
            return hand.concealed.size() + kMeldedByAnySet * hand.melds.size();
        }

        /// How many tiles a seat holds in the state the model is in.
        size_t ExpectedHeldTiles(const HandModel& model, Seat seat) {
            const size_t thirteen = static_cast<size_t>(kDealtTiles);
            switch (model.GetPhase()) {
                case Phase::Discard:
                case Phase::RobKong:
                    return seat == model.CurrentSeat() ? thirteen + 1 : thirteen;
                case Phase::Claim:
                    return thirteen;
                case Phase::Finished:
                    return model.Result().has_value() && model.Result()->kind == ResultKind::Win && model.Result()->winner == seat ? thirteen + 1 : thirteen;
            }
            return thirteen;
        }

        /// The invariants that hold between two actions. Returns false (after reporting) at the first one that does not.
        bool CheckInvariants(const HandModel& model, bool flowers, const eastl::vector<Tile>& deadWall, const std::string& where) {
            const size_t setSize = flowers ? kTileSetSizeWithBonus : kTileSetSize;
            const std::array<int, kTileIdCount> counts = CountEveryTile(model);
            size_t total = 0;
            for (uint8_t id = 0; id < kTileIdCount; ++id) {
                const Tile tile{id};
                const int expected = tile.IsBonus() ? (flowers ? 1 : 0) : kCopiesPerTile;
                if (counts[id] != expected) {
                    ADD_FAILURE() << where << ": " << ToString(tile) << " is there " << counts[id] << " times, not " << expected;
                    return false;
                }
                total += static_cast<size_t>(counts[id]);
            }
            if (total != setSize) {
                ADD_FAILURE() << where << ": " << total << " tiles, not " << setSize;
                return false;
            }
            const Wall& wall = model.GetWall();
            if (wall.Remaining() != wall.LiveCount() + wall.LooseCount() + wall.DeadWallSize()) {
                ADD_FAILURE() << where << ": the wall does not add up";
                return false;
            }
            if (wall.DeadWall() != deadWall || wall.DeadWallSize() != kDeadWallTiles) {
                ADD_FAILURE() << where << ": the dead wall changed";
                return false;
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (HeldTiles(model.GetHand(seat)) != ExpectedHeldTiles(model, seat)) {
                    ADD_FAILURE() << where << ": seat " << seat << " holds " << HeldTiles(model.GetHand(seat)) << " tiles, not " << ExpectedHeldTiles(model, seat);
                    return false;
                }
            }
            if (model.Result().has_value() != (model.GetPhase() == Phase::Finished)) {
                ADD_FAILURE() << where << ": the result and the phase disagree";
                return false;
            }
            return true;
        }

        /// Plays one hand to its end; `withBruteForce` also checks at every step that the actor's canonical actions are accepted exactly
        /// when listed. Returns false when an invariant broke or the hand did not end.
        bool Playout(uint32_t seed, Mode mode, Outcome& outcome, bool withBruteForce = false) {
            MahjongRng rng(seed);
            MahjongSettings settings;
            settings.flowers = seed % 2 == 1;
            const Seat east = static_cast<Seat>((seed / 2) % kSeatCount);
            HandModel model;
            if (!model.Begin(settings, MakeWall(rng, settings, east), MakeSeatWinds(east), Wind::East).has_value()) {
                ADD_FAILURE() << "seed " << seed << ": Begin refused the seat winds";
                return false;
            }
            const eastl::vector<Tile> deadWall = model.GetWall().DeadWall();
            const std::string seedText = "seed " + std::to_string(seed);
            if (!CheckInvariants(model, settings.flowers, deadWall, seedText + " after Begin")) {
                return false;
            }

            for (int step = 0; step < kStepLimit && model.GetPhase() != Phase::Finished; ++step) {
                const std::string where = seedText + " step " + std::to_string(step);
                const Seat actor = NextActor(model, rng);
                if (!IsSeat(actor)) {
                    ADD_FAILURE() << where << ": nobody is to act in phase " << Name(model.GetPhase());
                    return false;
                }
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    for (const HandAction& action : model.LegalActions(seat)) {
                        HandModel copy = model;
                        if (!copy.Apply(seat, action).has_value()) {
                            ADD_FAILURE() << where << ": seat " << seat << " is offered " << Name(action.kind) << " " << ToString(action.tile) << " but Apply refuses it";
                            return false;
                        }
                    }
                }
                if (withBruteForce) {
                    const ::testing::AssertionResult exact = AcceptedExactlyWhenListed(model, actor);
                    if (!exact) {
                        ADD_FAILURE() << where << ": " << exact.message();
                        return false;
                    }
                }
                const eastl::vector<HandAction> actions = model.LegalActions(actor);
                if (actions.empty()) {
                    ADD_FAILURE() << where << ": seat " << actor << " is to act but has no legal action";
                    return false;
                }
                const HandAction action = Choose(actions, mode, rng);
                const HandStatus status = model.Apply(actor, action);
                if (!status.has_value()) {
                    ADD_FAILURE() << where << ": " << Name(action.kind) << " was refused: " << Name(status.error());
                    return false;
                }
                if (!CheckInvariants(model, settings.flowers, deadWall, where)) {
                    return false;
                }
            }

            if (model.GetPhase() != Phase::Finished || !model.Result().has_value()) {
                ADD_FAILURE() << seedText << ": the hand did not end in " << kStepLimit << " steps";
                return false;
            }
            const HandResult& result = *model.Result();
            if (result.kind == ResultKind::Win) {
                ++outcome.wins;
                if (!IsSeat(result.winner) || !result.winningTile.IsValid() || !IsSeat(result.from)) {
                    ADD_FAILURE() << seedText << ": a win with no winner, tile or origin";
                    return false;
                }
            } else {
                ++outcome.draws;
                if (result.winner != kNoSeat) {
                    ADD_FAILURE() << seedText << ": a drawn hand with a winner";
                    return false;
                }
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (!model.LegalActions(seat).empty()) {
                    ADD_FAILURE() << seedText << ": a finished hand offers actions to seat " << seat;
                    return false;
                }
            }
            return true;
        }

    }  // namespace

    TEST(HandModelPlayoutTest, RandomLegalPlayKeepsEveryInvariantAndEveryHandEnds) {
        Outcome outcome;
        for (uint32_t seed = 0; seed < kSeedCount; ++seed) {
            // Even seeds are played by the winner, odd seeds by the player who never wins: a drawn hand is certain for the latter.
            // The flowers setting and the seat of East follow the seed too, so each combination is played in both modes.
            const Mode mode = (seed / (2 * kSeatCount)) % 2 == 0 ? Mode::SeekWin : Mode::NeverWin;
            ASSERT_TRUE(Playout(seed, mode, outcome, seed < kBruteForceSeedCount)) << "seed " << seed;
        }
        EXPECT_GT(outcome.wins, 0);
        EXPECT_GT(outcome.draws, 0);
    }

    TEST(HandModelPlayoutTest, ThePlayerWhoNeverWinsEndsEveryHandAsDrawn) {
        Outcome outcome;
        for (uint32_t seed = 0; seed < kSeatCount * 2; ++seed) {
            ASSERT_TRUE(Playout(seed, Mode::NeverWin, outcome)) << "seed " << seed;
        }
        EXPECT_EQ(outcome.wins, 0);
        EXPECT_EQ(outcome.draws, static_cast<int>(kSeatCount * 2));
    }

}  // namespace BECore::Mahjong::Tests
