#include "ComputerTestSupport.h"
#include "FixedWall.h"
#include "HandTestSupport.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/ComputerWorld.h>
#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Game/MahjongRng.h>
#include <gtest/gtest.h>
#include <set>

/**
 * `SampleWorld` (Game/ComputerWorld.h): a whole hand consistent with what one seat sees. The central invariant: for the view of every
 * decision of seeded random playouts (flowers off and on, the turn, the claim window, the robbing window, concealed and upgraded kongs,
 * declared opponents) the view of the same seat on the sampled world equals the view field by field, every kind has all its copies over
 * the world, and the world can be played on; plus determinism, no peeking, the waits of declared opponents, the flowers inference, the
 * failure cases and the frequency of a kind in an opponent's hand. Pure logic: no engine services.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint32_t kPlayoutSeedCount = 30;
        /// Worlds drawn from every view of a playout (more would be slow in Debug; the fixed hands below draw many more).
        constexpr int kWorldsPerView = 2;
        constexpr int kFixedWorldCount = 40;
        constexpr int kFrequencyWorlds = 1500;
        /// How far the mean number of copies of a kind in an opponent's hand may be from its expectation (about 4 sigma).
        constexpr double kFrequencyTolerance = 0.07;
        constexpr uint32_t kRngDraws = 3;
        constexpr uint32_t kRngRange = 1000000000;
        constexpr int kStepLimit = 2000;
        /// A declared opponent with at least this many melds (4 hidden tiles or fewer) waits in the sample almost always (3 and 4 melds:
        /// every sample waited when this was measured); with 2 melds (7 hidden tiles) about half of the samples do, with fewer almost never.
        constexpr int kMeldsThatWaitEasily = 3;
        constexpr double kEasyWaitShare = 0.95;
        constexpr int kMeldsThatWaitOften = 2;
        constexpr double kOftenWaitShare = 0.35;

        std::string MeldText(const Meld& meld) {
            return Name(meld.kind) + std::string(ToString(meld.first)) + "/" + std::to_string(meld.concealed) + "/" + std::to_string(meld.from) + "/" + std::to_string(meld.fromDiscard) + "/" +
                   std::string(ToString(meld.claimed));
        }

        std::string MeldsText(const eastl::vector<Meld>& melds) {
            std::string text;
            for (const Meld& meld : melds) {
                text += " " + MeldText(meld);
            }
            return text;
        }

        std::string TilesText(const eastl::vector<Tile>& tiles) {
            std::string text;
            for (const Tile tile : tiles) {
                text += " " + std::string(ToString(tile));
            }
            return text;
        }

        /// The first field in which two views differ, or "" when they are equal.
        std::string FirstDifference(const TableView& a, const TableView& b) {
            if (a.seat != b.seat) {
                return "seat";
            }
            if (a.phase != b.phase) {
                return "phase " + Name(a.phase) + " against " + Name(b.phase);
            }
            if (a.currentSeat != b.currentSeat) {
                return "currentSeat";
            }
            if (a.pendingTile != b.pendingTile) {
                return "pendingTile";
            }
            if (a.seatWind != b.seatWind || a.roundWind != b.roundWind) {
                return "winds";
            }
            if (TilesText(a.own.concealed) != TilesText(b.own.concealed)) {
                return "own concealed";
            }
            if (MeldsText(a.own.melds) != MeldsText(b.own.melds)) {
                return "own melds";
            }
            if (TilesText(a.own.bonus) != TilesText(b.own.bonus) || a.own.callingDeclared != b.own.callingDeclared || a.own.callingAtDiscardCount != b.own.callingAtDiscardCount ||
                a.own.discardCount != b.own.discardCount) {
                return "own bonus or calling fields";
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PublicSeat& x = a.seats[seat];
                const PublicSeat& y = b.seats[seat];
                if (MeldsText(x.openMelds) != MeldsText(y.openMelds)) {
                    return "seat " + std::to_string(seat) + " open melds";
                }
                if (x.concealedKongs != y.concealedKongs || TilesText(x.bonus) != TilesText(y.bonus) || x.bonusCount != y.bonusCount || x.discardCount != y.discardCount ||
                    x.callingDeclared != y.callingDeclared) {
                    return "seat " + std::to_string(seat) + " public fields";
                }
            }
            if (a.visible != b.visible) {
                return "visible";
            }
            if (a.liveCount != b.liveCount) {
                return "liveCount " + std::to_string(a.liveCount) + " against " + std::to_string(b.liveCount);
            }
            if (a.legal != b.legal) {
                return "legal";
            }
            return {};
        }

        /// Every tile of the set once over the hands, melds, bonus tiles, pile and the three parts of the wall.
        ::testing::AssertionResult EveryTileOnce(const HandModel& world) {
            std::array<int, kTileIdCount> counts{};
            const auto add = [&counts](Tile tile, int copies = 1) { counts[tile.Id()] += copies; };
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PlayerHand& hand = world.GetHand(seat);
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
                        add(meld.first, meld.kind == MeldKind::Kong ? kCopiesPerTile : kTilesPerSet);
                    }
                }
            }
            for (const DiscardEntry& entry : world.Discards()) {
                add(entry.tile);
            }
            for (const Tile tile : world.GetWall().LiveTiles()) {
                add(tile);
            }
            for (const Tile tile : world.GetWall().LooseTiles()) {
                add(tile);
            }
            for (const Tile tile : world.GetWall().DeadWall()) {
                add(tile);
            }
            for (uint8_t id = 0; id < kTileIdCount; ++id) {
                const Tile tile{id};
                const int expected = tile.IsBonus() ? (world.GetSettings().flowers ? 1 : 0) : kCopiesPerTile;
                if (counts[id] != expected) {
                    return ::testing::AssertionFailure() << ToString(tile) << " is there " << counts[id] << " times, not " << expected;
                }
            }
            return ::testing::AssertionSuccess();
        }

        /// The world is the view's: equal view, every tile once, and every listed action is accepted on it (a rollout can start).
        void ExpectWorldFits(const TableView& view, const HandModel& world, const std::string& where) {
            EXPECT_EQ(FirstDifference(view, MakeTableView(world, view.seat)), "") << where;
            EXPECT_TRUE(EveryTileOnce(world)) << where;
            for (const HandAction& action : view.legal) {
                HandModel copy = world;
                EXPECT_TRUE(copy.Apply(view.seat, action).has_value()) << where << ": " << Name(action.kind) << " " << ToString(action.tile) << " is refused in the world";
            }
        }

        /// The world drawn `count` times from the view of every seat that has something to do; every world must fit.
        int ExpectWorldsFit(const HandModel& model, uint32_t seed, int count, const std::string& where) {
            int drawn = 0;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const TableView view = MakeTableView(model, seat);
                if (view.legal.empty()) {
                    continue;
                }
                MahjongRng rng(seed * 7 + static_cast<uint32_t>(seat));
                for (int i = 0; i < count; ++i) {
                    const std::optional<HandModel> world = SampleWorld(view, rng);
                    const std::string at = where + " seat " + std::to_string(seat) + " world " + std::to_string(i);
                    EXPECT_TRUE(world.has_value()) << at;
                    if (world.has_value()) {
                        ExpectWorldFits(view, *world, at);
                        ++drawn;
                    }
                }
            }
            return drawn;
        }

        HandModel BeginSeed(uint32_t seed, MahjongRng& rng) {
            MahjongSettings settings;
            settings.flowers = seed % 2 == 1;
            const Seat east = static_cast<Seat>((seed / 2) % kSeatCount);
            HandModel model;
            EXPECT_TRUE(model.Begin(settings, MakeWall(rng, settings, east), MakeSeatWinds(east), Wind::East).has_value());
            return model;
        }

        /// What a test needs of a world to compare two of them: the data of `Capture`, as text.
        std::string WorldText(const HandModel& world) {
            const HandState state = world.Capture();
            std::string text = Snapshot(world) + " flowers " + std::to_string(state.settings.flowers) + " east " + std::to_string(state.east) + " | loose" + TilesText(state.loose) + " | live" +
                               TilesText(state.live) + " | dead" + TilesText(state.dead) + " | pile";
            for (const DiscardEntry& entry : state.discards) {
                text += " " + std::string(ToString(entry.tile)) + "@" + std::to_string(entry.seat);
            }
            text += " | drawn " + std::string(ToString(state.drawnTile)) + " pending " + std::string(ToString(state.pendingTile));
            for (const PlayerHand& hand : state.hands) {
                text += " | " + MeldsText(hand.melds) + " calling " + std::to_string(hand.callingAtDiscardCount);
            }
            return text;
        }

        /// The next numbers of a copy of `rng`, to tell whether two generators stand in the same place.
        std::string RngState(MahjongRng rng) {
            std::string text;
            for (uint32_t i = 0; i < kRngDraws; ++i) {
                text += std::to_string(rng.Below(kRngRange)) + " ";
            }
            return text;
        }

        /// East has declared with the first discard on a hand that waits on 8m and Ew and draws the 8m: `DeclareWin` is listed.
        HandModel SelfDrawWinModel() {
            FixedWallSpec spec;
            spec.hands[kSeat0] = "1p 1p 1p 2p 3p 4p 5s 6s 7s 8m 8m Ew Ew";
            spec.hands[kSeat1] = kQuiet1;
            spec.hands[kSeat2] = kQuiet2;
            spec.hands[kSeat3] = kQuiet3;
            spec.live = "9s 1s 4s 7s 8m";
            HandModel model = MakeModel(spec);
            Lap(model, "9s Wd Wd Gd", {kSeat0});
            EXPECT_EQ(model.CurrentSeat(), kSeat0);
            return model;
        }

        /// East discards a 5p that seat 1 can pung: the claim window.
        HandModel ClaimWindow() {
            HandModel model = MakeModel(OpenPungSpec(kSeat1Hand));
            EXPECT_TRUE(model.Discard(kSeat0, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Claim);
            return model;
        }

        /// Seat 1 upgrades its pung of 5p and seat 2, which has declared, may rob the kong.
        HandModel RobbingWindow() {
            HandModel model = UpgradeModel({kSeat2});
            EXPECT_TRUE(model.UpgradeKong(kSeat1, T("5p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::RobKong);
            return model;
        }

        /// East holds four 1p and declares the concealed kong, then discards: seat 0 has a concealed kong, the others see it.
        HandModel AfterConcealedKong() {
            HandModel model = MakeModel(SpecOf("1p 1p 1p 1p 2s 3s 9s 1m 4m 7m Ew Sw Rd", "Wd"));
            EXPECT_TRUE(model.DeclareConcealedKong(kSeat0, T("1p")).has_value());
            EXPECT_EQ(model.GetPhase(), Phase::Discard);
            return model;
        }

    }  // namespace

    TEST(ComputerWorldTest, TheViewOfTheWorldIsTheViewForEveryDecisionOfRandomPlayouts) {
        int views = 0;
        int worlds = 0;
        int declaredOpponents = 0;
        std::set<int> phases;
        for (uint32_t seed = 0; seed < kPlayoutSeedCount; ++seed) {
            MahjongRng rng(seed + 500);
            HandModel model = BeginSeed(seed, rng);
            const Mode mode = (seed / (2 * kSeatCount)) % 2 == 0 ? Mode::SeekWin : Mode::NeverWin;
            for (int step = 0; model.GetPhase() != Phase::Finished; ++step) {
                ASSERT_LT(step, kStepLimit) << "seed " << seed;
                const std::string where = "seed " + std::to_string(seed) + " step " + std::to_string(step);
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    const TableView view = MakeTableView(model, seat);
                    views += view.legal.empty() ? 0 : 1;
                    for (Seat other = 0; other < kSeatCount; ++other) {
                        if (!view.legal.empty() && other != seat) {
                            declaredOpponents += view.seats[other].callingDeclared ? 1 : 0;
                        }
                    }
                    if (!view.legal.empty()) {
                        phases.insert(static_cast<int>(view.phase));
                    }
                }
                worlds += ExpectWorldsFit(model, seed * 1000 + static_cast<uint32_t>(step), kWorldsPerView, where);
                if (HasFatalFailure()) {
                    return;
                }
                const Seat actor = NextActor(model, rng);
                ASSERT_TRUE(IsSeat(actor)) << where;
                ASSERT_TRUE(model.Apply(actor, Choose(model.LegalActions(actor), mode, rng)).has_value()) << where;
            }
        }
        EXPECT_EQ(worlds, views * kWorldsPerView) << "every view of a hand in play gave a world";
        EXPECT_GT(views, 1000);
        EXPECT_TRUE(phases.count(static_cast<int>(Phase::Discard)) > 0);
        EXPECT_TRUE(phases.count(static_cast<int>(Phase::Claim)) > 0);
        EXPECT_GT(declaredOpponents, 0) << "the playouts reach views with a declared opponent";
    }

    TEST(ComputerWorldTest, TheFixedHandsOfTheKongsTheClaimsAndTheWinGiveWorldsThatFit) {
        // A claim window, with the pung and the pass on offer.
        EXPECT_GT(ExpectWorldsFit(ClaimWindow(), 1, kFixedWorldCount, "claim window"), 0);

        // The turn with an upgraded kong on offer, the opponents declared or not.
        {
            const HandModel upgrade = UpgradeModel({kSeat2, kSeat3});
            ASSERT_TRUE(Has(upgrade.LegalActions(kSeat1), HandAction::MakeUpgradeKong(T("5p"))));
            EXPECT_GT(ExpectWorldsFit(upgrade, 2, kFixedWorldCount, "upgrade turn"), 0);
            const TableView view = MakeTableView(upgrade, kSeat1);
            MahjongRng rng(3);
            std::optional<HandModel> world = SampleWorld(view, rng);
            ASSERT_TRUE(world.has_value());
            EXPECT_EQ(world->DrawnTile(kSeat1), T("5p")) << "the tile of the listed upgrade is the drawn one";
            EXPECT_TRUE(world->Apply(kSeat1, HandAction::MakeUpgradeKong(T("5p"))).has_value());
        }

        // The robbing window: seat 2 may rob, seat 3 has not declared and is out of it.
        {
            const HandModel robbing = RobbingWindow();
            EXPECT_GT(ExpectWorldsFit(robbing, 4, kFixedWorldCount, "robbing window"), 0);
            const TableView view = MakeTableView(robbing, kSeat2);
            MahjongRng rng(5);
            const std::optional<HandModel> world = SampleWorld(view, rng);
            ASSERT_TRUE(world.has_value());
            EXPECT_EQ(world->GetPhase(), Phase::RobKong);
            EXPECT_EQ(world->CurrentSeat(), kSeat1);
            EXPECT_EQ(world->GetHand(kSeat1).CountConcealed(T("5p")), 1) << "the owner holds the tile that is being added";
            EXPECT_EQ(world->Capture().drawnTile, T("5p"));
        }

        // A concealed kong of East, seen by the others (they sample its kind) and by East (its own view).
        {
            const HandModel kong = AfterConcealedKong();
            EXPECT_GT(ExpectWorldsFit(kong, 6, kFixedWorldCount, "concealed kong"), 0);
            HandModel afterDiscard = kong;
            ASSERT_TRUE(afterDiscard.Discard(kSeat0, T("Rd")).has_value());
            ASSERT_NE(afterDiscard.GetPhase(), Phase::Finished);
            EXPECT_GT(ExpectWorldsFit(afterDiscard, 7, kFixedWorldCount, "concealed kong, discarded"), 0);
            const TableView otherView = MakeTableView(afterDiscard, afterDiscard.PendingClaimSeats().empty() ? afterDiscard.CurrentSeat() : afterDiscard.PendingClaimSeats().front());
            ASSERT_EQ(otherView.seats[kSeat0].concealedKongs, 1);
            MahjongRng rng(8);
            for (int i = 0; i < kFixedWorldCount; ++i) {
                const std::optional<HandModel> world = SampleWorld(otherView, rng);
                ASSERT_TRUE(world.has_value());
                int kongs = 0;
                for (const Meld& meld : world->GetHand(kSeat0).melds) {
                    kongs += meld.concealed && meld.kind == MeldKind::Kong ? 1 : 0;
                }
                EXPECT_EQ(kongs, 1);
            }
        }

        // The self-draw win on offer: the drawn tile is one that completes the hand, and the win can be declared in the world.
        {
            const HandModel winning = SelfDrawWinModel();
            const TableView view = MakeTableView(winning, kSeat0);
            ASSERT_TRUE(Has(view.legal, HandAction::MakeDeclareWin()));
            EXPECT_GT(ExpectWorldsFit(winning, 9, kFixedWorldCount, "self-draw win"), 0);
            MahjongRng rng(10);
            for (int i = 0; i < kFixedWorldCount; ++i) {
                std::optional<HandModel> world = SampleWorld(view, rng);
                ASSERT_TRUE(world.has_value());
                EXPECT_TRUE(world->DrawnTile(kSeat0).IsValid());
                ASSERT_TRUE(world->Apply(kSeat0, HandAction::MakeDeclareWin()).has_value());
                ASSERT_TRUE(world->Result().has_value());
                EXPECT_EQ(world->Result()->kind, ResultKind::Win);
                EXPECT_EQ(world->Result()->source, WinSource::SelfDraw);
            }
        }
    }

    TEST(ComputerWorldTest, ADrawnTileIsSetOnlyWhereTheLegalActionsNeedIt) {
        // A plain turn: no upgrade, no win on offer; nothing is drawn in the world.
        const HandModel plain = MakeModel(SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "Wd"));
        const TableView view = MakeTableView(plain, kSeat0);
        MahjongRng rng(11);
        const std::optional<HandModel> world = SampleWorld(view, rng);
        ASSERT_TRUE(world.has_value());
        EXPECT_FALSE(world->DrawnTile(kSeat0).IsValid());
        EXPECT_EQ(world->GetPhase(), Phase::Discard);
        EXPECT_EQ(world->CurrentSeat(), kSeat0);
        EXPECT_EQ(world->GetHand(kSeat0).concealed.size(), 14U);
        for (Seat seat = kSeat1; seat < kSeatCount; ++seat) {
            EXPECT_EQ(world->GetHand(seat).concealed.size(), 13U) << "seat " << seat;
        }
    }

    TEST(ComputerWorldTest, TheSameViewAndTheSameRngGiveTheSameWorldAndTheSameRngState) {
        const eastl::vector<HandModel> models = {UpgradeModel({kSeat2, kSeat3}), ClaimWindow(), RobbingWindow(), AfterConcealedKong(), SelfDrawWinModel()};
        for (size_t index = 0; index < models.size(); ++index) {
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const TableView view = MakeTableView(models[index], seat);
                if (view.legal.empty()) {
                    continue;
                }
                MahjongRng a(77 + static_cast<uint32_t>(index));
                MahjongRng b(77 + static_cast<uint32_t>(index));
                const std::optional<HandModel> worldA = SampleWorld(view, a);
                const std::optional<HandModel> worldB = SampleWorld(view, b);
                ASSERT_TRUE(worldA.has_value() && worldB.has_value()) << "model " << index << " seat " << seat;
                EXPECT_EQ(WorldText(*worldA), WorldText(*worldB)) << "model " << index << " seat " << seat;
                EXPECT_EQ(RngState(a), RngState(b)) << "model " << index << " seat " << seat;
                // Another rng state gives another world (the hidden tiles are drawn, not copied from anywhere).
                MahjongRng c(78 + static_cast<uint32_t>(index));
                const std::optional<HandModel> worldC = SampleWorld(view, c);
                ASSERT_TRUE(worldC.has_value());
                EXPECT_NE(WorldText(*worldA), WorldText(*worldC)) << "model " << index << " seat " << seat;
            }
        }
    }

    TEST(ComputerWorldTest, TwoTablesThatDifferOnlyInTheOtherHandsAndTheLiveWallGiveTheSameWorlds) {
        // The same two models as in the no-peeking test of the view: seat 0 sees the same, everything else differs.
        FixedWallSpec first;
        first.hands[kSeat0] = "1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd";
        first.hands[kSeat1] = kQuiet1;
        first.hands[kSeat2] = kQuiet2;
        first.hands[kSeat3] = kQuiet3;
        first.live = "Wd Nw Ww Sw";
        FixedWallSpec second = first;
        second.hands[kSeat1] = "4p 7p 4s 7s 4m 7m Ew Ew Ew Gd Gd Rd Rd";
        second.hands[kSeat2] = "2p 2p 5p 5p 8p 8p 2s 2s 5s 5s 8s 8s Wd";
        second.hands[kSeat3] = "Ww Ww Nw Nw Sw Sw 1p 1p 1s 1s 1m 1m Nw";
        second.live = "Wd Gd Rd Wd";
        const HandModel modelA = MakeModel(first);
        const HandModel modelB = MakeModel(second);

        const TableView viewA = MakeTableView(modelA, kSeat0);
        const TableView viewB = MakeTableView(modelB, kSeat0);
        ASSERT_EQ(FirstDifference(viewA, viewB), "");
        for (uint32_t seed = 1; seed <= 5; ++seed) {
            MahjongRng a(seed);
            MahjongRng b(seed);
            const std::optional<HandModel> worldA = SampleWorld(viewA, a);
            const std::optional<HandModel> worldB = SampleWorld(viewB, b);
            ASSERT_TRUE(worldA.has_value() && worldB.has_value());
            EXPECT_EQ(WorldText(*worldA), WorldText(*worldB)) << "rng seed " << seed;
            EXPECT_EQ(RngState(a), RngState(b)) << "rng seed " << seed;
        }

        // And the worlds are not the real tables: the truth is one of very many consistent worlds.
        MahjongRng rng(99);
        const std::optional<HandModel> world = SampleWorld(viewA, rng);
        ASSERT_TRUE(world.has_value());
        EXPECT_NE(world->GetHand(kSeat1).concealed, modelA.GetHand(kSeat1).concealed);
    }

    TEST(ComputerWorldTest, ADeclaredOpponentWithFewHiddenTilesWaitsInTheSample) {
        // Seats 2 and 3 have declared (13 hidden tiles each, a hard case); seat 1 holds an open pung (10 hidden tiles).
        const HandModel model = UpgradeModel({kSeat2, kSeat3});
        const TableView view = MakeTableView(model, kSeat1);
        ASSERT_TRUE(view.seats[kSeat2].callingDeclared);
        ASSERT_TRUE(view.seats[kSeat3].callingDeclared);
        MahjongRng rng(21);
        int waitingThirteen = 0;
        for (int i = 0; i < kFixedWorldCount; ++i) {
            const std::optional<HandModel> world = SampleWorld(view, rng);
            ASSERT_TRUE(world.has_value());
            for (const Seat seat : {kSeat2, kSeat3}) {
                const PlayerHand& hand = world->GetHand(seat);
                const TileCounts counts = CountTiles(hand.concealed);
                waitingThirteen += IsWaiting(counts, static_cast<int>(hand.melds.size()), CountMeldTiles(hand.melds)) ? 1 : 0;
            }
        }
        // A random hand of 13 tiles almost never waits; the tries make it happen now and then. Nothing is promised beyond "sometimes".
        EXPECT_GT(waitingThirteen, 0) << "no declared opponent of 13 hidden tiles ever waited in " << kFixedWorldCount * 2 << " samples";

        // Over the playouts: a declared opponent with melds waits in almost every sample, with two melds in about half of them.
        int easy = 0;
        int easyWaiting = 0;
        int often = 0;
        int oftenWaiting = 0;
        for (uint32_t seed = 0; seed < kPlayoutSeedCount * 2; ++seed) {
            MahjongRng playRng(seed + 900);
            HandModel play = BeginSeed(seed, playRng);
            for (int step = 0; play.GetPhase() != Phase::Finished && step < kStepLimit; ++step) {
                const Seat actor = NextActor(play, playRng);
                const TableView actorView = MakeTableView(play, actor);
                MahjongRng worldRng(seed * 13 + static_cast<uint32_t>(step));
                if (const std::optional<HandModel> world = SampleWorld(actorView, worldRng)) {
                    for (Seat other = 0; other < kSeatCount; ++other) {
                        const PlayerHand& hand = world->GetHand(other);
                        const int melds = static_cast<int>(hand.melds.size());
                        // The robbed kong's owner holds one tile more than its 13: it waits without the drawn tile, not looked at here.
                        const bool thirteen = hand.concealed.size() + kTilesPerSet * hand.melds.size() == static_cast<size_t>(kDealtTiles);
                        if (other == actor || !actorView.seats[other].callingDeclared || !thirteen) {
                            continue;
                        }
                        const bool waits = IsWaiting(CountTiles(hand.concealed), melds, CountMeldTiles(hand.melds));
                        if (melds >= kMeldsThatWaitEasily) {
                            ++easy;
                            easyWaiting += waits ? 1 : 0;
                        } else if (melds == kMeldsThatWaitOften) {
                            ++often;
                            oftenWaiting += waits ? 1 : 0;
                        }
                    }
                }
                ASSERT_TRUE(play.Apply(actor, Choose(play.LegalActions(actor), Mode::SeekWin, playRng)).has_value());
            }
        }
        EXPECT_GT(easy, 0);
        EXPECT_GT(often, 0);
        EXPECT_GE(static_cast<double>(easyWaiting), kEasyWaitShare * static_cast<double>(easy)) << easyWaiting << " of " << easy << " declared opponents with 3 or 4 melds waited";
        EXPECT_GE(static_cast<double>(oftenWaiting), kOftenWaitShare * static_cast<double>(often)) << oftenWaiting << " of " << often << " declared opponents with 2 melds waited";
    }

    TEST(ComputerWorldTest, TheWorldHasBonusTilesExactlyWhenTheSetHas) {
        // Playouts: the sample knows from the view alone whether the set has flowers (odd seeds do).
        for (uint32_t seed = 0; seed < 12; ++seed) {
            MahjongRng rng(seed + 300);
            HandModel model = BeginSeed(seed, rng);
            for (int step = 0; model.GetPhase() != Phase::Finished && step < kStepLimit; ++step) {
                const Seat actor = NextActor(model, rng);
                const TableView view = MakeTableView(model, actor);
                MahjongRng worldRng(seed * 17 + static_cast<uint32_t>(step));
                const std::optional<HandModel> world = SampleWorld(view, worldRng);
                ASSERT_TRUE(world.has_value()) << "seed " << seed << " step " << step;
                ASSERT_EQ(world->GetSettings().flowers, seed % 2 == 1) << "seed " << seed << " step " << step;
                ASSERT_TRUE(model.Apply(actor, Choose(model.LegalActions(actor), Mode::SeekWin, rng)).has_value());
            }
        }

        // A hand with the flowers in the set and none dealt: no tile of them is open, the shortfall of the wall says it all.
        FixedWallSpec spec = SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "Wd");
        for (const bool flowers : {false, true}) {
            const HandModel model = MakeModel(spec, 0, Wind::East, flowers);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                ASSERT_EQ(model.GetHand(seat).bonus.size(), 0U);
            }
            const TableView view = MakeTableView(model, kSeat0);
            MahjongRng rng(31);
            const std::optional<HandModel> world = SampleWorld(view, rng);
            ASSERT_TRUE(world.has_value()) << "flowers " << flowers;
            EXPECT_EQ(world->GetSettings().flowers, flowers);
            int bonusInWall = 0;
            for (const Tile tile : world->GetWall().LiveTiles()) {
                bonusInWall += tile.IsBonus() ? 1 : 0;
            }
            for (const Tile tile : world->GetWall().LooseTiles()) {
                bonusInWall += tile.IsBonus() ? 1 : 0;
            }
            for (const Tile tile : world->GetWall().DeadWall()) {
                bonusInWall += tile.IsBonus() ? 1 : 0;
            }
            EXPECT_EQ(bonusInWall, flowers ? static_cast<int>(kBonusCount) : 0);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                for (const Tile tile : world->GetHand(seat).concealed) {
                    EXPECT_FALSE(tile.IsBonus()) << "a bonus tile in a hidden hand";
                }
            }
            EXPECT_TRUE(EveryTileOnce(*world));
        }
    }

    TEST(ComputerWorldTest, AViewThatNoTableCouldHaveGivesNothing) {
        const HandModel model = UpgradeModel();
        const TableView good = MakeTableView(model, kSeat1);
        MahjongRng rng(41);
        ASSERT_TRUE(SampleWorld(good, rng).has_value());

        // Not the seat's turn (no legal action), the view of no seat, a hand that is over.
        EXPECT_FALSE(SampleWorld(MakeTableView(model, kSeat0), rng).has_value());
        EXPECT_FALSE(SampleWorld(MakeTableView(model, kNoSeat), rng).has_value());
        EXPECT_FALSE(SampleWorld(TableView{}, rng).has_value());
        {
            HandModel play = MakeModel(SpecOf("1p 4p 7p 1s 4s 7s 1m 4m 7m Sw Ew Gd Rd", "Wd"));
            MahjongRng playRng(5);
            while (play.GetPhase() != Phase::Finished) {
                const Seat actor = NextActor(play, playRng);
                ASSERT_TRUE(play.Apply(actor, Choose(play.LegalActions(actor), Mode::NeverWin, playRng)).has_value());
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                EXPECT_FALSE(SampleWorld(MakeTableView(play, seat), rng).has_value()) << "seat " << seat;
            }
        }

        // A wall that is too long or too short for the tiles that are left.
        {
            TableView view = good;
            view.liveCount += 5;
            EXPECT_FALSE(SampleWorld(view, rng).has_value()) << "five live tiles too many";
        }
        {
            TableView view = good;
            view.liveCount -= 1;
            EXPECT_FALSE(SampleWorld(view, rng).has_value()) << "a loose tile too many";
        }
        // A kong too many (no hand holds so many tiles), and more concealed kongs than kinds with four copies unseen.
        {
            TableView view = good;
            view.seats[kSeat2].concealedKongs = kSetsInHand + 1;
            EXPECT_FALSE(SampleWorld(view, rng).has_value());
        }
        {
            TableView view = good;
            view.seats[kSeat2].concealedKongs = 1;
            for (uint8_t id = 0; id < kKindCount; ++id) {
                view.visible[id] = static_cast<uint8_t>(kCopiesPerTile - 3);
            }
            EXPECT_FALSE(SampleWorld(view, rng).has_value()) << "no kind has four unseen copies for the kong";
        }
        // The discards disagree with the pile: a seat claims to have discarded a tile that is not there.
        {
            TableView view = good;
            view.seats[kSeat3].discardCount += 1;
            EXPECT_FALSE(SampleWorld(view, rng).has_value());
        }
        // A robbed kong with no pung to rob.
        {
            const HandModel robbing = RobbingWindow();
            TableView view = MakeTableView(robbing, kSeat2);
            ASSERT_TRUE(SampleWorld(view, rng).has_value());
            view.pendingTile = T("6p");
            EXPECT_FALSE(SampleWorld(view, rng).has_value());
        }
        // A claim window with no pending tile.
        {
            TableView view = MakeTableView(ClaimWindow(), kSeat1);
            ASSERT_TRUE(SampleWorld(view, rng).has_value());
            view.pendingTile = Tile{};
            EXPECT_FALSE(SampleWorld(view, rng).has_value());
        }
        // A listed action that no sample of the view can reproduce: a win on a hand that does not win.
        {
            TableView view = good;
            view.legal.push_back(HandAction::MakeDeclareWin());
            EXPECT_FALSE(SampleWorld(view, rng).has_value());
        }
    }

    TEST(ComputerWorldTest, AKindLandsInAnOpponentsHandInProportionToItsUnseenCopies) {
        MahjongRng startRng(1);
        const HandModel model = BeginSeed(0, startRng);
        const TableView view = MakeTableView(model, model.CurrentSeat());
        ASSERT_FALSE(view.legal.empty());
        int pool = 0;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            pool += UnseenCopies(view, Tile{id});
        }
        const double share = static_cast<double>(kDealtTiles) / static_cast<double>(pool);

        // Kinds with 4, 3 and 2 unseen copies: how many of them an opponent holds, on average, is unseen * 13 / pool.
        eastl::vector<Tile> kinds;
        for (const int unseen : {4, 3, 2}) {
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (UnseenCopies(view, Tile{id}) == unseen) {
                    kinds.push_back(Tile{id});
                    break;
                }
            }
        }
        ASSERT_EQ(kinds.size(), 3U) << "the deal leaves kinds with four, three and two unseen copies";
        eastl::array<eastl::array<double, kSeatCount>, 3> held{};
        MahjongRng rng(51);
        for (int i = 0; i < kFrequencyWorlds; ++i) {
            const std::optional<HandModel> world = SampleWorld(view, rng);
            ASSERT_TRUE(world.has_value());
            for (size_t k = 0; k < kinds.size(); ++k) {
                for (Seat seat = 0; seat < kSeatCount; ++seat) {
                    held[k][seat] += world->GetHand(seat).CountConcealed(kinds[k]);
                }
            }
        }
        for (size_t k = 0; k < kinds.size(); ++k) {
            const double expected = UnseenCopies(view, kinds[k]) * share;
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat == view.seat) {
                    continue;
                }
                EXPECT_NEAR(held[k][seat] / kFrequencyWorlds, expected, kFrequencyTolerance) << ToString(kinds[k]) << " in the hand of seat " << seat;
            }
        }
    }

}  // namespace BECore::Mahjong::Tests
