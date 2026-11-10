#include "ComputerTestSupport.h"
#include "HandTestSupport.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongRng.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>
#include <Mahjong/Game/Wall.h>
#include <gtest/gtest.h>
#include <set>

/**
 * `HandModel::Capture` / `Restore`: a hand rebuilt from its data is the same hand. For the states of seeded random playouts (flowers
 * off and on, every phase) and of fixed hands (an upgraded kong, the robbing window, a concealed kong, a claim) the restored model has
 * the same data, the same snapshot, the same legal actions, and playing the same random actions on both gives the same hands and the
 * same result; states no play could reach are refused with a text. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kSeedCount = 120;
        /// How many random actions both models are played on after a restore.
        constexpr int kContinuationSteps = 30;
        /// A continuation is played from every step whose number divides by this.
        constexpr int kContinuationEvery = 6;
        constexpr int kContinuationSeedCount = 40;

        void AppendTiles(std::string& text, const eastl::vector<Tile>& tiles) {
            for (const Tile tile : tiles) {
                text += " ";
                text += ToString(tile);
            }
        }

        /// Every field of a state as text, so that two states compare as strings.
        std::string Describe(const HandState& state) {
            std::string text = "settings " + std::to_string(state.settings.handsPerMatch) + "/" + std::to_string(state.settings.flowers) + "/" + std::to_string(state.settings.replayDrawnHand) + "/" +
                               std::to_string(state.settings.limit) + "/" + std::to_string(state.settings.startingScore);
            text += " winds";
            for (const Wind wind : state.seatWinds) {
                text += " " + std::to_string(static_cast<int>(wind));
            }
            text += " round " + std::to_string(static_cast<int>(state.roundWind)) + " east " + std::to_string(state.east);
            text += " | loose";
            AppendTiles(text, state.loose);
            text += " | live";
            AppendTiles(text, state.live);
            text += " | dead";
            AppendTiles(text, state.dead);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PlayerHand& hand = state.hands[seat];
                text += " | seat " + std::to_string(seat) + ":";
                AppendTiles(text, hand.concealed);
                text += " melds";
                for (const Meld& meld : hand.melds) {
                    text += " " + std::to_string(static_cast<int>(meld.kind)) + "/" + std::to_string(meld.concealed) + "/" + std::string(ToString(meld.first)) + "/" + std::to_string(meld.from) + "/" +
                            std::to_string(meld.fromDiscard) + "/" + std::string(ToString(meld.claimed));
                }
                text += " bonus";
                AppendTiles(text, hand.bonus);
                text += " calling " + std::to_string(hand.callingDeclared) + "/" + std::to_string(hand.callingAtDiscardCount) + "/" + std::to_string(hand.discardCount);
                text += " status " + std::to_string(static_cast<int>(state.claimStatus[seat])) + " claim " + std::to_string(static_cast<int>(state.claims[seat].kind)) + "/" +
                        std::string(ToString(state.claims[seat].chowFirst));
            }
            text += " | pile";
            for (const DiscardEntry& entry : state.discards) {
                text += " " + std::string(ToString(entry.tile)) + "@" + std::to_string(entry.seat);
            }
            text += " | moves";
            for (const HandMove& move : state.moves) {
                text += " " + std::to_string(static_cast<int>(move.kind)) + "/" + std::to_string(move.seat) + "/" + std::string(ToString(move.tile)) + "/" + std::to_string(move.from) + "/" +
                        std::to_string(move.meld) + "/" + std::to_string(move.declaredCalling);
            }
            text += " | phase " + std::to_string(static_cast<int>(state.phase)) + " current " + std::to_string(state.current) + " drawn " + std::string(ToString(state.drawnTile)) + "/" +
                    std::to_string(state.drawnLoose) + " pending " + std::string(ToString(state.pendingTile));
            if (state.result.has_value()) {
                const HandResult& result = *state.result;
                text += " result " + std::to_string(static_cast<int>(result.kind)) + "/" + std::to_string(result.winner) + "/" + std::string(ToString(result.winningTile)) + "/" +
                        std::to_string(static_cast<int>(result.source)) + "/" + std::to_string(result.from) + "/" + std::to_string(result.loose) + "/" + std::to_string(result.lastLiveTile);
            } else {
                text += " open";
            }
            text += " | windows " + std::to_string(state.windowSerial);
            if (state.lastWindow.has_value()) {
                const ClaimWindowResult& window = *state.lastWindow;
                text += " last " + std::to_string(window.serial) + "/" + std::to_string(window.from) + "/" + std::string(ToString(window.tile)) + "/" + std::to_string(window.winner) + "/" +
                        std::to_string(static_cast<int>(window.winningKind)) + " beaten";
                for (const LostClaim& lost : window.beaten) {
                    text += " " + std::to_string(lost.seat) + "/" + std::to_string(static_cast<int>(lost.kind)) + "/" + std::string(ToString(lost.chowFirst));
                }
            }
            return text;
        }

        /// The legal actions of every seat as one text.
        std::string LegalText(const HandModel& model) {
            std::string text;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                text += "[" + std::to_string(seat) + ":";
                for (const HandAction& action : model.LegalActions(seat)) {
                    text += " " + Name(action.kind) + "/" + std::string(ToString(action.tile)) + "/" + std::to_string(action.declareCalling) + "/" + Name(action.claim) + "/" +
                            std::string(ToString(action.chowFirst));
                }
                text += "]";
            }
            return text;
        }

        /// What the two models answer to the questions a front end asks, as text (false and a message when they differ).
        ::testing::AssertionResult SameHand(const HandModel& original, const HandModel& restored) {
            if (Describe(original.Capture()) != Describe(restored.Capture())) {
                return ::testing::AssertionFailure() << "the data differ:\n  " << Describe(original.Capture()) << "\n  " << Describe(restored.Capture());
            }
            if (Snapshot(original) != Snapshot(restored)) {
                return ::testing::AssertionFailure() << "the snapshots differ:\n  " << Snapshot(original) << "\n  " << Snapshot(restored);
            }
            if (LegalText(original) != LegalText(restored)) {
                return ::testing::AssertionFailure() << "the legal actions differ:\n  " << LegalText(original) << "\n  " << LegalText(restored);
            }
            if (original.PendingClaimSeats() != restored.PendingClaimSeats() || original.PendingTile() != restored.PendingTile() || original.CurrentSeat() != restored.CurrentSeat()) {
                return ::testing::AssertionFailure() << "the window differs";
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (original.DrawnTile(seat) != restored.DrawnTile(seat) || original.SeatWind(seat) != restored.SeatWind(seat)) {
                    return ::testing::AssertionFailure() << "seat " << seat << " differs in the drawn tile or the wind";
                }
            }
            return ::testing::AssertionSuccess();
        }

        /// `Restore(Capture(model))` succeeds and is the same hand; returns it (a failure is reported and gives a copy of `model`).
        HandModel RestoredCopy(const HandModel& model, const std::string& where) {
            std::expected<HandModel, eastl::string> restored = HandModel::Restore(model.Capture());
            if (!restored.has_value()) {
                ADD_FAILURE() << where << ": Restore refused a captured state: " << restored.error().c_str();
                return model;
            }
            EXPECT_TRUE(SameHand(model, *restored)) << where;
            return *restored;
        }

        /// Plays `steps` random legal actions on both models, the same ones, and compares after each. Stops at the end of the hand.
        void PlayAlike(HandModel a, HandModel b, uint32_t seed, int steps, const std::string& where) {
            MahjongRng rngA(seed);
            MahjongRng rngB(seed);
            for (int step = 0; step < steps && a.GetPhase() != Phase::Finished; ++step) {
                const std::string at = where + " continuation step " + std::to_string(step);
                const Seat actorA = NextActor(a, rngA);
                const Seat actorB = NextActor(b, rngB);
                ASSERT_EQ(actorA, actorB) << at;
                ASSERT_TRUE(IsSeat(actorA)) << at;
                const eastl::vector<HandAction> actions = a.LegalActions(actorA);
                ASSERT_FALSE(actions.empty()) << at;
                const HandAction actionA = Choose(actions, Mode::SeekWin, rngA);
                const HandAction actionB = Choose(b.LegalActions(actorB), Mode::SeekWin, rngB);
                ASSERT_EQ(actionA, actionB) << at;
                ASSERT_EQ(Error(a.Apply(actorA, actionA)), Error(b.Apply(actorB, actionB))) << at;
                ASSERT_TRUE(SameHand(a, b)) << at;
            }
            ASSERT_EQ(a.GetPhase(), b.GetPhase()) << where;
            if (a.Result().has_value()) {
                ASSERT_TRUE(b.Result().has_value()) << where;
                EXPECT_EQ(Describe(a.Capture()), Describe(b.Capture())) << where;
            }
        }

        /// A started hand of seed `seed`: flowers on odd seeds, East at a seat that follows the seed, as the playout test does.
        HandModel BeginSeed(uint32_t seed, MahjongRng& rng) {
            MahjongSettings settings;
            settings.flowers = seed % 2 == 1;
            const Seat east = static_cast<Seat>((seed / 2) % kSeatCount);
            HandModel model;
            EXPECT_TRUE(model.Begin(settings, MakeWall(rng, settings, east), MakeSeatWinds(east), Wind::East).has_value());
            return model;
        }

        /// What the playouts have seen, to hold that they cover the phases and the kinds of move.
        struct Coverage {
            std::set<int> phases;
            std::set<int> moves;
            std::set<int> statuses;
            int flowerHands = 0;
            int bonusTilesSeen = 0;
            int finishedWins = 0;
        };

        void Note(const HandModel& model, Coverage& coverage) {
            coverage.phases.insert(static_cast<int>(model.GetPhase()));
            for (const HandMove& move : model.Moves()) {
                coverage.moves.insert(static_cast<int>(move.kind));
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                coverage.statuses.insert(static_cast<int>(model.GetClaimStatus(seat)));
                coverage.bonusTilesSeen += static_cast<int>(model.GetHand(seat).bonus.size());
            }
        }

        /// A started model whose state is changed by `change` (on the captured state), restored: the result of the refusal.
        template <typename Change>
        std::expected<HandModel, eastl::string> RestoreChanged(const HandModel& model, Change change) {
            HandState state = model.Capture();
            change(state);
            return HandModel::Restore(state);
        }

        template <typename Change>
        void ExpectRefused(const HandModel& model, const char* part, Change change) {
            const std::expected<HandModel, eastl::string> restored = RestoreChanged(model, change);
            ASSERT_FALSE(restored.has_value()) << "a state that was changed to " << part << " was accepted";
            EXPECT_NE(restored.error().find(part), eastl::string::npos) << "the text \"" << restored.error().c_str() << "\" does not name \"" << part << "\"";
        }

        /// East holds four 1p and the quiet hands: a concealed kong can be declared at once.
        HandModel ConcealedKongModel() {
            return MakeModel(SpecOf("1p 1p 1p 1p 2s 3s 9s 1m 4m 7m Ew Sw Rd", "Wd"));
        }

        /// Seat 1 has claimed East's 5p and discarded in its lap; it holds the fourth 5p and an open pung: an upgrade is legal.
        HandModel UpgradeTurn() {
            return UpgradeModel();
        }

        /// The same, with seat 2 declared and waiting on the 5p: the upgrade opens the robbing window.
        HandModel RobbingWindow(std::initializer_list<Seat> declaring = {kSeat2}) {
            HandModel model = UpgradeModel(declaring);
            EXPECT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::RobKong);
            return model;
        }

        /// East has discarded a 5p that seat 1 can pung: the claim window.
        HandModel ClaimWindow() {
            HandModel model = MakeModel(OpenPungSpec(kSeat1Hand));
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

    }  // namespace

    TEST(HandModelRestoreTest, EveryStateOfRandomPlayoutsRestoresToTheSameHand) {
        Coverage coverage;
        for (uint32_t seed = 0; seed < kSeedCount; ++seed) {
            MahjongRng rng(seed);
            HandModel model = BeginSeed(seed, rng);
            const Mode mode = (seed / (2 * kSeatCount)) % 2 == 0 ? Mode::SeekWin : Mode::NeverWin;
            for (int step = 0; step < 2000; ++step) {
                const std::string where = "seed " + std::to_string(seed) + " step " + std::to_string(step);
                Note(model, coverage);
                const HandModel restored = RestoredCopy(model, where);
                if (HasFatalFailure()) {
                    return;
                }
                if (model.GetPhase() == Phase::Finished) {
                    coverage.finishedWins += model.Result()->kind == ResultKind::Win ? 1 : 0;
                    break;
                }
                const Seat actor = NextActor(model, rng);
                ASSERT_TRUE(IsSeat(actor)) << where;
                ASSERT_TRUE(model.Apply(actor, Choose(model.LegalActions(actor), mode, rng)).has_value()) << where;
            }
            ASSERT_EQ(model.GetPhase(), Phase::Finished) << "seed " << seed;
        }
        // The playouts reach the turn, the claim window and the end, every claim state, claims, and flowers (the robbing window and
        // the kongs are rare in random play: the fixed hands below cover them).
        EXPECT_EQ(coverage.phases.size(), 3U);
        EXPECT_TRUE(coverage.statuses.count(static_cast<int>(ClaimStatus::Waiting)) > 0);
        EXPECT_TRUE(coverage.statuses.count(static_cast<int>(ClaimStatus::Passed)) > 0);
        EXPECT_TRUE(coverage.statuses.count(static_cast<int>(ClaimStatus::Claimed)) > 0);
        EXPECT_TRUE(coverage.moves.count(static_cast<int>(MoveKind::Discard)) > 0);
        EXPECT_TRUE(coverage.moves.count(static_cast<int>(MoveKind::ClaimChow)) > 0);
        EXPECT_TRUE(coverage.moves.count(static_cast<int>(MoveKind::ClaimPung)) > 0);
        EXPECT_GT(coverage.bonusTilesSeen, 0);
        EXPECT_GT(coverage.finishedWins, 0);
    }

    TEST(HandModelRestoreTest, ARestoredHandPlaysOnLikeTheOriginal) {
        for (uint32_t seed = 0; seed < kContinuationSeedCount; ++seed) {
            MahjongRng rng(seed + 1000);
            HandModel model = BeginSeed(seed, rng);
            for (int step = 0; model.GetPhase() != Phase::Finished; ++step) {
                if (step % kContinuationEvery == 0) {
                    const std::string where = "seed " + std::to_string(seed) + " step " + std::to_string(step);
                    const HandModel restored = RestoredCopy(model, where);
                    PlayAlike(model, restored, seed * 31 + static_cast<uint32_t>(step), kContinuationSteps, where);
                    if (HasFatalFailure()) {
                        return;
                    }
                }
                const Seat actor = NextActor(model, rng);
                ASSERT_TRUE(IsSeat(actor));
                ASSERT_TRUE(model.Apply(actor, Choose(model.LegalActions(actor), Mode::SeekWin, rng)).has_value());
            }
        }
    }

    TEST(HandModelRestoreTest, TheKongsTheClaimsAndTheRobbingWindowRestore) {
        // The turn of an upgraded kong, with the upgrade legal.
        HandModel upgrade = UpgradeTurn();
        ASSERT_TRUE(Has(upgrade.LegalActions(kSeat1), HandAction::MakeUpgradeKong(T("5p"))));
        PlayAlike(upgrade, RestoredCopy(upgrade, "upgrade turn"), 5, kContinuationSteps, "upgrade turn");

        // The robbing window: the owner, the tile, a seat that may rob.
        HandModel robbing = RobbingWindow();
        const HandModel robbingRestored = RestoredCopy(robbing, "robbing window");
        EXPECT_EQ(robbingRestored.GetPhase(), Phase::RobKong);
        EXPECT_EQ(robbingRestored.PendingTile(), T("5p"));
        EXPECT_EQ(robbingRestored.PendingClaimSeats(), robbing.PendingClaimSeats());
        PlayAlike(robbing, robbingRestored, 6, kContinuationSteps, "robbing window");

        // The robbing window with one seat passed, and the robbery carried out on the restored copy.
        HandModel twoRobbers = RobbingWindow({kSeat2, kSeat3});
        ASSERT_EQ(twoRobbers.PendingClaimSeats().size(), 2U);
        ASSERT_TRUE(twoRobbers.Pass(kSeat3).has_value());
        HandModel half = RestoredCopy(twoRobbers, "robbing window, one seat passed");
        EXPECT_EQ(half.GetClaimStatus(kSeat3), ClaimStatus::Passed);
        ASSERT_TRUE(half.Claim(kSeat2, ClaimKind::Mahjong).has_value());
        EXPECT_EQ(half.GetPhase(), Phase::Finished);
        EXPECT_EQ(half.Result()->source, WinSource::RobbedKong);
        EXPECT_EQ(Describe(RestoredCopy(half, "the robbed hand").Capture()), Describe(half.Capture()));

        // A claim window; then one claim made, one seat still to decide.
        HandModel claim = ClaimWindow();
        PlayAlike(claim, RestoredCopy(claim, "claim window"), 7, kContinuationSteps, "claim window");

        // After a claim (the seat holds one tile too many and drew nothing).
        ASSERT_TRUE(claim.Claim(kSeat1, ClaimKind::Pung).has_value());
        ASSERT_EQ(claim.GetPhase(), Phase::Discard);
        const HandModel afterClaim = RestoredCopy(claim, "after a claim");
        EXPECT_FALSE(afterClaim.DrawnTile(kSeat1).IsValid());
        EXPECT_FALSE(Has(afterClaim.LegalActions(kSeat1), HandAction::MakeUpgradeKong(T("5p"))));
        PlayAlike(claim, afterClaim, 8, kContinuationSteps, "after a claim");

        // After a concealed kong: the loose tile is the drawn one.
        HandModel kong = ConcealedKongModel();
        ASSERT_TRUE(kong.DeclareConcealedKong(kSeat0, T("1p")).has_value());
        const HandModel afterKong = RestoredCopy(kong, "after a concealed kong");
        EXPECT_TRUE(afterKong.DrawnTile(kSeat0).IsValid());
        EXPECT_EQ(afterKong.GetHand(kSeat0).melds.size(), 1U);
        EXPECT_TRUE(afterKong.GetHand(kSeat0).melds[0].concealed);
        PlayAlike(kong, afterKong, 9, kContinuationSteps, "after a concealed kong");

        // After an upgrade that nobody robbed.
        HandModel done = UpgradeTurn();
        ASSERT_TRUE(done.UpgradeKong(kSeat1, T("5p")).has_value());
        ASSERT_EQ(done.GetPhase(), Phase::Discard);
        const HandModel afterUpgrade = RestoredCopy(done, "after an upgrade");
        EXPECT_EQ(afterUpgrade.GetHand(kSeat1).melds[0].kind, MeldKind::Kong);
        PlayAlike(done, afterUpgrade, 10, kContinuationSteps, "after an upgrade");
    }

    TEST(HandModelRestoreTest, ADrawnHandAndAWonHandRestore) {
        for (const uint32_t seed : {0U, 3U, 8U, 11U}) {
            MahjongRng rng(seed);
            HandModel model = BeginSeed(seed, rng);
            const Mode mode = seed % 2 == 0 ? Mode::NeverWin : Mode::SeekWin;
            while (model.GetPhase() != Phase::Finished) {
                const Seat actor = NextActor(model, rng);
                ASSERT_TRUE(model.Apply(actor, Choose(model.LegalActions(actor), mode, rng)).has_value());
            }
            const HandModel restored = RestoredCopy(model, "seed " + std::to_string(seed));
            EXPECT_EQ(restored.GetPhase(), Phase::Finished);
            EXPECT_EQ(restored.Result()->kind, model.Result()->kind);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_TRUE(restored.LegalActions(seat).empty());
            }
        }
    }

    TEST(HandModelRestoreTest, TheConcealedTilesOfAStateAreSorted) {
        const HandModel model = MakeModel(SpecOf("9m 1p 5s 2p 7m 1s 3p Ew Rd 4p 6p 8p 2m", "Wd"));
        HandState state = model.Capture();
        eastl::reverse(state.hands[kSeat0].concealed.begin(), state.hands[kSeat0].concealed.end());
        const std::expected<HandModel, eastl::string> restored = HandModel::Restore(state);
        ASSERT_TRUE(restored.has_value()) << restored.error().c_str();
        EXPECT_EQ(restored->GetHand(kSeat0).concealed, model.GetHand(kSeat0).concealed);
    }

    TEST(HandModelRestoreTest, StatesNoPlayCouldReachAreRefusedWithAText) {
        const HandModel turn = MakeModel(SpecOf("1p 2p 3p 5s 6s 7s 2m 3m 4m Ew Ew Rd Rd", "Wd"));
        const HandModel claim = ClaimWindow();
        const HandModel robbing = RobbingWindow();
        const HandModel flowers = MakeModel(SpecOf("1p 2p 3p 5s 6s 7s 2m 3m 4m Ew Ew Rd Rd", "Wd"), 0, Wind::East, true);
        ASSERT_TRUE(HandModel::Restore(turn.Capture()).has_value());
        ASSERT_TRUE(HandModel::Restore(flowers.Capture()).has_value());

        // The tiles.
        ExpectRefused(turn, "1p is there 5 times", [](HandState& state) {
            const auto other = eastl::find_if(state.live.begin(), state.live.end(), [](Tile tile) { return tile != Tile{static_cast<uint8_t>(0)}; });
            *other = Tile{static_cast<uint8_t>(0)};
        });
        ExpectRefused(turn, "is missing", [](HandState& state) { state.live.pop_back(); });
        ExpectRefused(turn, "F1 is there 1 times, only 0 exist", [](HandState& state) { state.hands[kSeat3].bonus.push_back(Tile::FromFlower(0)); });
        ExpectRefused(flowers, "only 0 exist", [](HandState& state) { state.settings.flowers = false; });
        ExpectRefused(turn, "is missing", [](HandState& state) { state.settings.flowers = true; });
        ExpectRefused(turn, "not a playable tile", [](HandState& state) { state.hands[kSeat2].concealed.back() = Tile{static_cast<uint8_t>(kTileIdCount)}; });
        ExpectRefused(turn, "wall holds a tile that is not a tile", [](HandState& state) { state.live.back() = Tile{static_cast<uint8_t>(kTileIdCount)}; });

        // The wall.
        ExpectRefused(turn, "dead wall holds", [](HandState& state) {
            state.live.push_back(state.dead.back());
            state.dead.pop_back();
        });
        ExpectRefused(turn, "loose reserve holds", [](HandState& state) {
            state.loose.push_back(state.live.back());
            state.live.pop_back();
        });

        // The seat winds and the seats.
        ExpectRefused(turn, "East exactly once", [](HandState& state) { state.seatWinds = {Wind::East, Wind::East, Wind::West, Wind::North}; });
        ExpectRefused(turn, "make seat 0 East", [](HandState& state) { state.east = 2; });
        ExpectRefused(turn, "is no seat", [](HandState& state) { state.current = kNoSeat; });

        // The hand sizes.
        ExpectRefused(turn, "holds 14 tiles, a hand in this phase holds 13", [](HandState& state) {
            state.hands[kSeat1].concealed.push_back(state.live.back());
            state.live.pop_back();
        });
        ExpectRefused(turn, "holds 13 tiles, a hand in this phase holds 14", [](HandState& state) {
            state.live.push_back(state.hands[kSeat0].concealed.back());
            state.hands[kSeat0].concealed.pop_back();
        });
        ExpectRefused(turn, "chow that cannot exist", [](HandState& state) {
            Meld meld;
            meld.kind = MeldKind::Chow;
            meld.first = T("8p");
            state.hands[kSeat3].melds.push_back(meld);
        });

        // The phase and the window.
        ExpectRefused(claim, "no playable tile is pending", [](HandState& state) { state.pendingTile = Tile{}; });
        ExpectRefused(claim, "not the last discard", [](HandState& state) { state.current = kSeat2; });
        ExpectRefused(claim, "is in the window but has no claim", [](HandState& state) { state.claimStatus[kSeat2] = ClaimStatus::Waiting; });
        ExpectRefused(claim, "could claim the pending tile but is not in the window", [](HandState& state) { state.claimStatus[kSeat1] = ClaimStatus::NotInWindow; });
        ExpectRefused(claim, "every seat has decided", [](HandState& state) { state.claimStatus[kSeat1] = ClaimStatus::Passed; });
        ExpectRefused(claim, "does not fit its status or that it cannot make", [](HandState& state) {
            state.claimStatus[kSeat1] = ClaimStatus::Claimed;
            state.claims[kSeat1] = {ClaimKind::Kong, Tile{}};
        });
        ExpectRefused(claim, "does not fit its status or that it cannot make", [](HandState& state) { state.claims[kSeat1] = {ClaimKind::Pung, Tile{}}; });
        ExpectRefused(claim, "whose tile is pending, has a claim status", [](HandState& state) { state.claimStatus[kSeat0] = ClaimStatus::Waiting; });
        ExpectRefused(claim, "a drawn tile is kept", [](HandState& state) { state.drawnTile = state.hands[kSeat1].concealed.front(); });
        ExpectRefused(robbing, "no open pung of it", [](HandState& state) { state.pendingTile = T("6p"); });
        ExpectRefused(turn, "no window is open", [](HandState& state) { state.pendingTile = T("5p"); });
        ExpectRefused(turn, "but no window is open", [](HandState& state) { state.claimStatus[kSeat1] = ClaimStatus::Waiting; });
        ExpectRefused(turn, "drawn tile is not in the hand", [](HandState& state) { state.drawnTile = T("9s"); });

        // The result.
        ExpectRefused(turn, "result exists exactly when", [](HandState& state) { state.result = HandResult{}; });
        ExpectRefused(turn, "result exists exactly when", [](HandState& state) { state.phase = Phase::Finished; });
        ExpectRefused(turn, "does not fit its kind", [](HandState& state) {
            state.phase = Phase::Finished;
            HandResult result;
            result.kind = ResultKind::Win;
            state.result = result;
        });
    }

}  // namespace BECore::Mahjong::Tests
