#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/OwnDraws.h>
#include <Mahjong/Game/Score.h>
#include <cmath>
#include <gtest/gtest.h>
#include <tests/ComputerTestSupport.h>
#include <tests/FixedWall.h>
#include <tests/HandTestSupport.h>
#include <tests/ScoreTestSupport.h>

namespace BECore::Mahjong::Tests {

    namespace {

        constexpr int kLongWall = 60;
        constexpr int kDepth = 2;
        constexpr int kPerfectChance = static_cast<int>(kOwnDrawsProbBase);
        constexpr double kChanceTolerance = 1e-4;
        constexpr int kMeldSetSize = 3;

        // Two sets, a terminal pung and a tanki wait: discarding 5p leaves "Gd" alone, discarding Gd leaves "5p" alone (3 copies unseen of
        // each). Both fully concealed and declared with the first discard (x4); the pung of 9m is concealed (8 points).
        constexpr const char* kTankiHand = "1p 2p 3p 7p 8p 9p 4s 5s 6s 9m 9m 9m Gd 5p";

        // Two sets and four pairs (a pair of dragons and three pairs of simples, all different suits and ranks): every pair can be broken, and
        // breaking any leaves shanten 1.
        constexpr const char* kPairsHand = "1p 2p 3p 4s 5s 6s Rd Rd 5m 5m 8m 8m 2s 2s";

        // Two sets, 78m and 57p to join, a pair of east winds (East's own wind and the round wind) and two strays: 9s, 2m.
        constexpr const char* kOneFromWaiting = "1p 2p 3p 4s 5s 6s 7m 8m Ew Ew 5p 7p 9s 2m";

        /// The listed actions of `view`: a plain discard of every kind held, and the declaring one where the tiles left wait.
        void ListDiscards(TableView& view) {
            view.legal.clear();
            const int meldCount = MeldCountOf(view);
            const TileCounts meldTiles = CountMeldTiles(view.own.melds);
            TileCounts counts = CountTiles(view.own.concealed);
            for (uint8_t id = 0; id < kKindCount; ++id) {
                if (counts[id] == 0) {
                    continue;
                }
                view.legal.push_back(HandAction::MakeDiscard(Tile{id}));
                --counts[id];
                if (IsWaiting(counts, meldCount, meldTiles)) {
                    view.legal.push_back(HandAction::MakeDiscard(Tile{id}, true));
                }
                ++counts[id];
            }
        }

        /// East's view on its turn with the 14 tiles `hand`, the tiles `pile` lying open, `live` tiles in the wall. Nothing else is
        /// visible. The listed actions: a plain discard of every kind held, and the declaring one where the 13 left wait.
        TableView MakeView(const char* hand, const char* pile = "", int live = kLongWall) {
            TableView view;
            view.seat = kSeat0;
            view.phase = Phase::Discard;
            view.currentSeat = kSeat0;
            view.seatWind = Wind::East;
            view.roundWind = Wind::East;
            view.own.concealed = ParseTiles(hand).value();
            eastl::sort(view.own.concealed.begin(), view.own.concealed.end());
            for (const Tile tile : view.own.concealed) {
                ++view.visible[tile.Id()];
            }
            const Vector<Tile> open = ParseTiles(pile).value();
            for (const Tile tile : open) {
                ++view.visible[tile.Id()];
            }
            view.liveCount = live;
            ListDiscards(view);
            return view;
        }

        const DiscardValue* Find(const Vector<DiscardValue>& values, const char* tile) {
            const auto found = eastl::find_if(values.begin(), values.end(), [tile](const DiscardValue& value) { return value.tile == T(tile); });
            return found == values.end() ? nullptr : &*found;
        }

        void ExpectEqual(const Vector<DiscardValue>& a, const Vector<DiscardValue>& b) {
            ASSERT_EQ(a.size(), b.size());
            for (size_t i = 0; i < a.size(); ++i) {
                EXPECT_EQ(a[i].tile, b[i].tile) << "entry " << i;
                EXPECT_EQ(a[i].winChance, b[i].winChance) << "entry " << i;
                EXPECT_EQ(a[i].meanWinScore, b[i].meanWinScore) << "entry " << i;
                EXPECT_EQ(a[i].valueWhenNotWinning, b[i].valueWhenNotWinning) << "entry " << i;
                EXPECT_EQ(a[i].expected, b[i].expected) << "entry " << i;
            }
        }

        constexpr OwnDrawsModel kWithDiscardWins{.depth = kDepth, .discardWins = true};
        constexpr OwnDrawsModel kOnlyDraws{.depth = kDepth};

        /// The chance of winning on a discard and on a draw within `draws` rounds, for a hand that waits on a share `p` of the unseen
        /// tiles, with nobody else declared: the model of `Waiting` written in doubles.
        struct WinMasses {
            double byDiscard = 0;
            double byDraw = 0;
        };

        WinMasses ExpectedWinMasses(double p, int draws, bool discardWins) {
            const double alive = 1.0 - kOpponentWinPerRoundPermille[0] / static_cast<double>(kPermilleBase);
            const double q = std::min(1.0, kDiscardHitPermille / static_cast<double>(kPermilleBase) * p);
            const double byDiscardInRound = 1.0 - std::pow(1.0 - q, kOpponentDiscardsPerRound);
            WinMasses masses;
            double stay = 1.0;
            for (int round = 1; round <= draws; ++round) {
                const double aliveBefore = std::pow(alive, round - 1);
                if (discardWins) {
                    masses.byDiscard += aliveBefore * stay * byDiscardInRound;
                    stay *= 1.0 - byDiscardInRound;
                }
                masses.byDraw += aliveBefore * stay * p;
                stay *= 1.0 - p;
            }
            return masses;
        }

        // ----- the golden table: values copied from the build before the evaluator was made fast -----

        /// One position of the golden table: `how` says how it was made.
        struct GoldenSpec {
            const char* how;
            const char* hand;      // the 14 tiles (3n + 2 with the melds)
            const char* heldDrop;  // the tile taken out to make the 3n + 1 hand of `ValueOfHeldHand`
            const char* pile = "";
            int live = kLongWall;
            Wind seatWind = Wind::East;
            Vector<Meld> melds;
            bool declared = false;
            int declaredAt = 0;
            int discardCount = 0;
            const char* bonus = "";
            Vector<Seat> declaredOpponents;
        };

        struct GoldenEntry {
            const char* tile;
            int winChance;
            int meanWinScore;
            int64_t valueWhenNotWinning;
            int64_t expected;
        };

        struct GoldenValues {
            Vector<GoldenEntry> depth1;
            Vector<GoldenEntry> depth2;
            Vector<GoldenEntry> waits;
            int64_t held = 0;
            int64_t heldWithLooseTile = 0;
        };

        Meld OpenPung(const char* tile, Seat from) {
            Meld meld;
            meld.kind = MeldKind::Pung;
            meld.first = T(tile);
            meld.claimed = T(tile);
            meld.from = from;
            meld.fromDiscard = true;
            return meld;
        }

        Meld ConcealedKong(const char* tile) {
            Meld meld;
            meld.kind = MeldKind::Kong;
            meld.concealed = true;
            meld.first = T(tile);
            return meld;
        }

        TableView GoldenView(const GoldenSpec& spec) {
            TableView view = MakeView(spec.hand, spec.pile, spec.live);
            view.seatWind = spec.seatWind;
            view.own.melds = spec.melds;
            for (const Meld& meld : spec.melds) {
                const int tiles = meld.kind == MeldKind::Kong ? kCopiesPerTile : kMeldSetSize;
                view.visible[meld.first.Id()] += static_cast<uint8_t>(tiles);
            }
            view.own.callingDeclared = spec.declared;
            view.own.callingAtDiscardCount = spec.declaredAt;
            view.own.discardCount = spec.discardCount;
            const Vector<Tile> bonus = ParseTiles(spec.bonus).value();
            for (const Tile tile : bonus) {
                view.own.bonus.push_back(tile);
                ++view.visible[tile.Id()];
            }
            for (const Seat seat : spec.declaredOpponents) {
                view.seats[seat].callingDeclared = true;
            }
            ListDiscards(view);
            return view;
        }

        TableView HeldView(const GoldenSpec& spec) {
            TableView view = GoldenView(spec);
            view.own.RemoveConcealed(T(spec.heldDrop));
            return view;
        }

        Vector<GoldenSpec> GoldenSpecs() {
            Vector<GoldenSpec> specs;
            specs.push_back({.how = "the tanki hand of TheDearerWaitIsRankedFirst, East, a long wall", .hand = kTankiHand, .heldDrop = "5p"});
            specs.push_back({.how = "the four pairs of kPairsHand with a Rd lying open", .hand = kPairsHand, .heldDrop = "5m", .pile = "Rd"});
            specs.push_back({.how = "kOneFromWaiting (shanten 1 after 9s), East, a long wall", .hand = kOneFromWaiting, .heldDrop = "9s"});
            specs.push_back({.how = "a declared hand waiting on Gd that drew 7s, 6 own discards, a flower, seat 1 declared too",
                             .hand = "1p 2p 3p 7p 8p 9p 4s 5s 6s 9m 9m 9m Gd 7s",
                             .heldDrop = "7s",
                             .live = 40,
                             .declared = true,
                             .declaredAt = 2,
                             .discardCount = 6,
                             .bonus = "F1",
                             .declaredOpponents = {kSeat1}});
            specs.push_back({.how = "an open pung of Rd and a hand that waits on 6m / 9m after 9s (East pair Ew)",
                             .hand = "1p 2p 3p 4s 5s 6s 7m 8m Ew Ew 9s",
                             .heldDrop = "9s",
                             .pile = "5s",
                             .melds = {OpenPung("Rd", kSeat1)}});
            specs.push_back({.how = "a concealed kong of 9m and a hand that waits on 8p after 2m",
                             .hand = "1p 2p 3p 4s 5s 6s 5m 5m 7p 9p 2m",
                             .heldDrop = "2m",
                             .melds = {ConcealedKong("9m")},
                             .discardCount = 3});
            specs.push_back({.how = "kOneFromWaiting with the South wind pair, South not East, seats 2 and 3 declared",
                             .hand = "1p 2p 3p 4s 5s 6s 7m 8m Sw Sw 5p 7p 9s 2m",
                             .heldDrop = "9s",
                             .seatWind = Wind::South,
                             .declaredOpponents = {kSeat2, kSeat3}});
            specs.push_back({.how = "kOneFromWaiting late in the wall (10 live: 2 draws), all three others declared",
                             .hand = kOneFromWaiting,
                             .heldDrop = "9s",
                             .live = 10,
                             .declaredOpponents = {kSeat1, kSeat2, kSeat3}});
            specs.push_back({.how = "the tanki hand with 3 live tiles: fewer than kSeatCount, no draw left", .hand = kTankiHand, .heldDrop = "5p", .live = kSeatCount - 1});
            specs.push_back({.how = "kPairsHand with 2 live tiles", .hand = kPairsHand, .heldDrop = "5m", .live = kSeatCount - 2});
            specs.push_back({.how = "a hand two sets and two partials at shanten 2, East", .hand = "1p 2p 4p 5p 7s 8s 3m 5m 9m 9m Ew Rd Gd 2s", .heldDrop = "Gd"});
            return specs;
        }

        /// The values of `GoldenSpecs()` in the same order, as the evaluator answered before it was made fast (WP 3 of slice 2): the
        /// optimisation changes no number, so these stay as they are.
        Vector<GoldenValues> GoldenTable() {
            return {
                // the tanki hand of TheDearerWaitIsRankedFirst, East, a long wall
                {
                    .depth1 =
                        {
                            {"5p", 297802, 140, -10127, 243043},
                            {"Gd", 297802, 130, -10127, 225175},
                            {"1p", 58259, 65, -8829, 14400},
                            {"9p", 58259, 65, -8829, 14400},
                            {"9m", 69222, 57, -12891, 11679},
                            {"2p", 40708, 70, -8766, 8689},
                        },
                    .depth2 =
                        {
                            {"5p", 297802, 140, -10127, 243043},
                            {"Gd", 297802, 130, -10127, 225175},
                            {"1p", 106085, 65, -9059, 33261},
                            {"9p", 106085, 65, -9059, 33261},
                            {"9m", 123277, 57, -13180, 30597},
                            {"2p", 73861, 70, -8923, 22757},
                        },
                    .waits =
                        {
                            {"5p", 297802, 140, -10127, 243043},
                            {"Gd", 297802, 130, -10127, 225175},
                        },
                    .held = 243043,
                    .heldWithLooseTile = 255475,
                },
                // the four pairs of kPairsHand with a Rd lying open
                {
                    .depth1 =
                        {
                            {"2s", 12365, 104, -8622, -758},
                            {"5m", 12365, 104, -8622, -758},
                            {"8m", 12365, 104, -8622, -758},
                            {"Rd", 17853, 60, -12542, -5892},
                            {"1p", 0, 0, -8566, -8566},
                            {"4s", 0, 0, -8566, -8566},
                        },
                    .depth2 =
                        {
                            {"2s", 23471, 104, -8646, 6284},
                            {"5m", 23471, 104, -8646, 6284},
                            {"8m", 23471, 104, -8646, 6284},
                            {"Rd", 33791, 60, -12493, 92},
                            {"1p", 2407, 102, -8545, -7044},
                            {"4s", 2407, 102, -8545, -7044},
                        },
                    .waits = {},
                    .held = -8600,
                    .heldWithLooseTile = -9142,
                },
                // kOneFromWaiting (shanten 1 after 9s), East, a long wall
                {
                    .depth1 =
                        {
                            {"9s", 42774, 60, -8765, 7008},
                            {"2m", 42774, 60, -8765, 7008},
                            {"5p", 0, 0, -8401, -8401},
                            {"7p", 0, 0, -8401, -8401},
                            {"8m", 0, 0, -8401, -8401},
                            {"7m", 0, 0, -8401, -8401},
                        },
                    .depth2 =
                        {
                            {"9s", 79212, 60, -8934, 20290},
                            {"2m", 79212, 60, -8934, 20290},
                            {"5p", 26273, 82, -8387, 4736},
                            {"7p", 25392, 83, -8383, 4415},
                            {"8m", 13559, 84, -8334, -1384},
                            {"7m", 11552, 88, -8320, -2109},
                        },
                    .waits = {},
                    .held = -8600,
                    .heldWithLooseTile = -9142,
                },
                // a declared hand waiting on Gd that drew 7s, 6 own discards, a flower, seat 1 declared too
                {
                    .depth1 =
                        {
                            {"Gd", 199527, 280, 67043, 388871},
                            {"4s", 106753, 310, 65952, 257472},
                            {"7s", 106753, 310, 65952, 257472},
                            {"1p", 22563, 282, 65804, 102492},
                            {"9p", 22563, 282, 65804, 102492},
                            {"9m", 41065, 231, -38962, 19640},
                        },
                    .depth2 =
                        {
                            {"Gd", 199527, 280, 67043, 388871},
                            {"4s", 106753, 310, 65952, 257472},
                            {"7s", 106753, 310, 65952, 257472},
                            {"1p", 37988, 282, 66591, 128333},
                            {"9p", 37988, 282, 66591, 128333},
                            {"9m", 64930, 231, -38199, 54392},
                        },
                    .waits =
                        {
                            {"Gd", 199527, 280, 67043, 388871},
                            {"4s", 106753, 310, 65952, 257472},
                            {"7s", 106753, 310, 65952, 257472},
                        },
                    .held = 257472,
                    .heldWithLooseTile = 263234,
                },
                // an open pung of Rd and a hand that waits on 6m / 9m after 9s (East pair Ew)
                {
                    .depth1 =
                        {
                            {"9s", 616468, 120, -7311, 441053},
                            {"8m", 100297, 96, -4327, 53986},
                            {"7m", 79816, 105, -4292, 46406},
                            {"1p", 70210, 60, -4744, 20864},
                            {"4s", 70210, 60, -4744, 20864},
                            {"6s", 70210, 60, -4744, 20864},
                        },
                    .depth2 =
                        {
                            {"9s", 616468, 120, -7311, 441053},
                            {"8m", 170602, 96, -4114, 94969},
                            {"7m", 138143, 105, -4040, 83628},
                            {"1p", 128654, 60, -4895, 42050},
                            {"4s", 128654, 60, -4895, 42050},
                            {"6s", 128654, 60, -4895, 42050},
                        },
                    .waits =
                        {
                            {"9s", 616468, 120, -7311, 441053},
                        },
                    .held = 441053,
                    .heldWithLooseTile = 456312,
                },
                // a concealed kong of 9m and a hand that waits on 8p after 2m
                {
                    .depth1 =
                        {
                            {"2m", 378870, 120, 4245, 275423},
                            {"9p", 124312, 115, 3611, 89241},
                            {"7p", 84248, 118, 3539, 62994},
                            {"1p", 38797, 115, 3460, 30182},
                            {"4s", 38797, 115, 3460, 30182},
                            {"6s", 38797, 115, 3460, 30182},
                        },
                    .depth2 =
                        {
                            {"2m", 378870, 120, 4245, 275423},
                            {"9p", 207763, 115, 3802, 146857},
                            {"7p", 145881, 118, 3668, 106586},
                            {"1p", 72083, 115, 3521, 53158},
                            {"4s", 72083, 115, 3521, 53158},
                            {"6s", 72083, 115, 3521, 53158},
                        },
                    .waits =
                        {
                            {"2m", 378870, 120, 4245, 275423},
                        },
                    .held = 275423,
                    .heldWithLooseTile = 288487,
                },
                // kOneFromWaiting with the South wind pair, South not East, seats 2 and 3 declared
                {
                    .depth1 =
                        {
                            {"9s", 7798, 55, -57214, -55047},
                            {"2m", 7798, 55, -57214, -55047},
                            {"7p", 0, 0, -56777, -56777},
                            {"8m", 0, 0, -56777, -56777},
                            {"7m", 0, 0, -56777, -56777},
                            {"5p", 0, 0, -56778, -56778},
                        },
                    .depth2 =
                        {
                            {"9s", 12462, 55, -57218, -53756},
                            {"2m", 12462, 55, -57218, -53756},
                            {"5p", 3417, 62, -56577, -55539},
                            {"7p", 3295, 62, -56575, -55571},
                            {"8m", 1684, 65, -56575, -56040},
                            {"7m", 1425, 67, -56566, -56100},
                        },
                    .waits = {},
                    .held = -57210,
                    .heldWithLooseTile = -57251,
                },
                // kOneFromWaiting late in the wall (10 live: 2 draws), all three others declared
                {
                    .depth1 =
                        {
                            {"5p", 0, 0, -60866, -60866},
                            {"7p", 0, 0, -60867, -60867},
                            {"8m", 0, 0, -60867, -60867},
                            {"7m", 0, 0, -60867, -60867},
                            {"9s", 2276, 60, -62345, -61383},
                            {"2m", 2276, 60, -62345, -61383},
                        },
                    .depth2 =
                        {
                            {"7m", 0, 0, -60504, -60504},
                            {"7p", 0, 0, -60516, -60516},
                            {"5p", 0, 0, -60520, -60520},
                            {"8m", 0, 0, -60520, -60520},
                            {"9s", 2276, 60, -62345, -61383},
                            {"2m", 2276, 60, -62345, -61383},
                        },
                    .waits = {},
                    .held = -62296,
                    .heldWithLooseTile = -73556,
                },
                // the tanki hand with 3 live tiles: fewer than kSeatCount, no draw left
                {
                    .depth1 =
                        {
                            {"5p", 0, 0, 0, 0},
                            {"Gd", 0, 0, 0, 0},
                            {"9m", 0, 0, 0, 0},
                            {"1p", 0, 0, 0, 0},
                            {"9p", 0, 0, 0, 0},
                            {"2p", 0, 0, 0, 0},
                        },
                    .depth2 =
                        {
                            {"5p", 0, 0, 0, 0},
                            {"Gd", 0, 0, 0, 0},
                            {"9m", 0, 0, 0, 0},
                            {"1p", 0, 0, 0, 0},
                            {"9p", 0, 0, 0, 0},
                            {"2p", 0, 0, 0, 0},
                        },
                    .waits =
                        {
                            {"5p", 0, 0, 0, 0},
                            {"Gd", 0, 0, 0, 0},
                        },
                    .held = 0,
                    .heldWithLooseTile = 20069,
                },
                // kPairsHand with 2 live tiles
                {
                    .depth1 =
                        {
                            {"2s", 0, 0, 0, 0},
                            {"5m", 0, 0, 0, 0},
                            {"8m", 0, 0, 0, 0},
                            {"Rd", 0, 0, 0, 0},
                            {"1p", 0, 0, 0, 0},
                            {"4s", 0, 0, 0, 0},
                        },
                    .depth2 =
                        {
                            {"2s", 0, 0, 0, 0},
                            {"5m", 0, 0, 0, 0},
                            {"8m", 0, 0, 0, 0},
                            {"Rd", 0, 0, 0, 0},
                            {"1p", 0, 0, 0, 0},
                            {"4s", 0, 0, 0, 0},
                        },
                    .waits = {},
                    .held = 0,
                    .heldWithLooseTile = -602,
                },
                // a hand two sets and two partials at shanten 2, East
                {
                    .depth1 =
                        {
                            {"1p", 0, 0, -12238, -12238},
                            {"3m", 0, 0, -12238, -12238},
                            {"2s", 0, 0, -12600, -12600},
                            {"Ew", 0, 0, -12600, -12600},
                            {"Rd", 0, 0, -12600, -12600},
                            {"Gd", 0, 0, -12600, -12600},
                        },
                    .depth2 =
                        {
                            {"1p", 0, 0, -11998, -11998},
                            {"3m", 0, 0, -12007, -12007},
                            {"2s", 0, 0, -12599, -12599},
                            {"Ew", 0, 0, -12599, -12599},
                            {"Rd", 0, 0, -12599, -12599},
                            {"Gd", 0, 0, -12599, -12599},
                        },
                    .waits = {},
                    .held = -12601,
                    .heldWithLooseTile = -13394,
                },
            };
        }

        void ExpectGolden(const char* how, const char* query, const Vector<DiscardValue>& values, const Vector<GoldenEntry>& golden) {
            ASSERT_EQ(values.size(), golden.size()) << how << ": " << query;
            for (size_t i = 0; i < values.size(); ++i) {
                const GoldenEntry& entry = golden[i];
                EXPECT_EQ(values[i].tile, T(entry.tile)) << how << ": " << query << " entry " << i;
                EXPECT_EQ(values[i].winChance, entry.winChance) << how << ": " << query << " entry " << i;
                EXPECT_EQ(values[i].meanWinScore, entry.meanWinScore) << how << ": " << query << " entry " << i;
                EXPECT_EQ(values[i].valueWhenNotWinning, entry.valueWhenNotWinning) << how << ": " << query << " entry " << i;
                EXPECT_EQ(values[i].expected, entry.expected) << how << ": " << query << " entry " << i;
            }
        }

    }  // namespace

    TEST(OwnDrawsTest, AValuePungOutranksASimplePairAtTheSameRating) {
        // Hand: 123p 456s and four pairs (Rd Rd, 5m 5m, 8m 8m, 2s 2s). Every pair kind has 2 copies unseen. Breaking any pair leaves 2 sets and
        // 3 pairs: shanten 1, accepted by the other three pairs, 3 x 2 = 6 copies, whichever pair is broken: the ratings are equal and
        // `RateDiscards` only breaks the tie by `KeepValue` (the dragon last). A win built on the pair of dragons is a pung of dragons: the
        // evaluator must see it is worth more than a simple pung even though the ratings agree.
        const TableView view = MakeView(kPairsHand);
        TileCounts counts = CountTiles(view.own.concealed);

        const Vector<DiscardOption> rating = RateDiscards(counts, 0, view);
        const Vector<DiscardValue> values = ValueDiscards(view, kDepth);

        ASSERT_GE(rating.size(), 4u);
        EXPECT_EQ(rating[0].acceptance, rating[3].acceptance);
        ASSERT_FALSE(values.empty());
        EXPECT_NE(values.front().tile, T("Rd"));
        const DiscardValue* dragon = Find(values, "Rd");
        const DiscardValue* simple = Find(values, "5m");
        ASSERT_NE(dragon, nullptr);
        ASSERT_NE(simple, nullptr);
        EXPECT_GT(simple->expected, dragon->expected);
        EXPECT_GT(simple->meanWinScore, dragon->meanWinScore);
        // The chances to win are equal: the same number of copies is accepted either way.
        EXPECT_EQ(simple->winChance, dragon->winChance);
    }

    TEST(OwnDrawsTest, TheEvaluatorKeepsTheValuePungWhereTheRatingBreaksIt) {
        // The same hand with one Rd lying in the pile: the pair of dragons has 1 copy unseen. Breaking a simple pair is accepted by
        // Rd 1 + two pairs of 2 = 5 copies, breaking the dragon pair by 2 + 2 + 2 = 6: `RateDiscards` puts the dragon first. The pung of
        // dragons, if drawn, scores more than twice a simple one, which more than pays for the one copy: the evaluator breaks a simple pair.
        const TableView view = MakeView(kPairsHand, "Rd");
        TileCounts counts = CountTiles(view.own.concealed);

        const Vector<DiscardOption> rating = RateDiscards(counts, 0, view);
        const Vector<DiscardValue> values = ValueDiscards(view, kDepth);

        ASSERT_FALSE(rating.empty());
        EXPECT_EQ(rating.front().tile, T("Rd"));
        ASSERT_FALSE(values.empty());
        EXPECT_NE(values.front().tile, T("Rd"));
        const DiscardValue* dragon = Find(values, "Rd");
        ASSERT_NE(dragon, nullptr);
        EXPECT_GT(Find(values, "5m")->winChance, 0);
        EXPECT_GT(values.front().expected, dragon->expected);
        // The dragon discard wins more often (6 copies against 5) and the kept dragon pung wins for more.
        EXPECT_GT(dragon->winChance, values.front().winChance);
        EXPECT_GT(values.front().meanWinScore, dragon->meanWinScore);
    }

    TEST(OwnDrawsTest, TheDearerWaitIsRankedFirst) {
        // Hand: 123p 789p 456s, a concealed pung of 9m and two strays Gd, 5p. Discarding 5p waits on Gd alone, discarding Gd on 5p alone;
        // 3 copies of each are unseen. Points of a win: 20 win + 2 self-draw + 2 only winning tile + 8 concealed terminal pung = 32, and the pair
        // of dragons adds 2 for Gd = 34. Doubles: fully concealed and the declaration with the first discard (the view has made none): x4.
        // 32 x 4 = 128 -> 130, 34 x 4 = 136 -> 140.
        const Vector<DiscardValue> waits = ValueWaits(MakeView(kTankiHand));

        ASSERT_EQ(waits.size(), 2u);
        EXPECT_EQ(waits[0].tile, T("5p"));
        EXPECT_EQ(waits[0].meanWinScore, 140);
        EXPECT_EQ(waits[1].tile, T("Gd"));
        EXPECT_EQ(waits[1].meanWinScore, 130);
        EXPECT_EQ(waits[0].winChance, waits[1].winChance);
        EXPECT_GT(waits[0].expected, waits[1].expected);
    }

    TEST(OwnDrawsTest, TheChanceOfAOneWaitHandIsTheChanceOfTheCopiesOverTheDrawsLeft) {
        // Discarding 5p leaves a wait on Gd alone: 3 copies unseen of the 136 - 14 = 122 tiles the view cannot see, so a draw wins with
        // p = 3 / 122. The wall of 40 tiles gives the seat 10 draws; with no declared opponent the chance that nobody else has won
        // before draw i is (1 - h)^(i - 1), h = `kOpponentWinPerRoundPermille[0]` / 1000. The chance to win within the draws is the sum over i of that x (1 - p)^(i - 1) x p.
        constexpr int kWall = 40;
        constexpr int kDraws = kWall / kSeatCount;
        const double p = 3.0 / 122.0;
        const double alive = 1.0 - kOpponentWinPerRoundPermille[0] / static_cast<double>(kPermilleBase);
        double expected = 0;
        for (int i = 1; i <= kDraws; ++i) {
            expected += std::pow(alive * (1.0 - p), i - 1) * p;
        }

        const Vector<DiscardValue> values = ValueDiscards(MakeView(kTankiHand, "", kWall), kDepth);

        const DiscardValue* tanki = Find(values, "5p");
        ASSERT_NE(tanki, nullptr);
        EXPECT_NEAR(tanki->winChance / static_cast<double>(kOwnDrawsProbBase), expected, kChanceTolerance);
        EXPECT_EQ(tanki->meanWinScore, 140);
    }

    TEST(OwnDrawsTest, DeclaredOpponentsLowerTheChanceToWin) {
        TableView quiet = MakeView(kTankiHand);
        TableView busy = quiet;
        busy.seats[kSeat1].callingDeclared = true;
        busy.seats[kSeat2].callingDeclared = true;

        const Vector<DiscardValue> quietValues = ValueDiscards(quiet, kDepth);
        const Vector<DiscardValue> busyValues = ValueDiscards(busy, kDepth);

        ASSERT_NE(Find(quietValues, "5p"), nullptr);
        ASSERT_NE(Find(busyValues, "5p"), nullptr);
        EXPECT_LT(Find(busyValues, "5p")->winChance, Find(quietValues, "5p")->winChance);
    }

    TEST(OwnDrawsTest, TwoTablesThatDifferOnlyInHiddenTilesGiveTheSameValues) {
        // East holds the same 13 tiles and draws the same tile; the other hands and the rest of the live wall differ.
        const char* east = "1p 2p 3p 4s 5s 6s 7m 8m Ew Ew 5p 7p 9s";
        FixedWallSpec first = SpecOf(east, "2m");
        FixedWallSpec second = SpecOf(east, "2m Rd Rd Rd");
        second.hands[kSeat1] = kQuiet3;
        second.hands[kSeat2] = kQuiet1;
        second.hands[kSeat3] = kQuiet2;
        const TableView viewA = MakeTableView(MakeModel(first), kSeat0);
        const TableView viewB = MakeTableView(MakeModel(second), kSeat0);

        const Vector<DiscardValue> a = ValueDiscards(viewA, kDepth + 1);
        const Vector<DiscardValue> b = ValueDiscards(viewB, kDepth + 1);

        ASSERT_FALSE(a.empty());
        ExpectEqual(a, b);
        ExpectEqual(ValueWaits(viewA), ValueWaits(viewB));
    }

    TEST(OwnDrawsTest, TheSameInputGivesTheSameOutputAlsoAfterAnotherCall) {
        const TableView view = MakeView(kOneFromWaiting);
        const Vector<DiscardValue> first = ValueDiscards(view, kDepth + 1);

        const Vector<DiscardValue> other = ValueDiscards(MakeView(kPairsHand, "Rd"), kDepth);
        const Vector<DiscardValue> second = ValueDiscards(view, kDepth + 1);
        const Vector<DiscardValue> third = ValueDiscards(view, kDepth + 1);

        ASSERT_FALSE(other.empty());
        ASSERT_FALSE(first.empty());
        ExpectEqual(first, second);
        ExpectEqual(first, third);
    }

    TEST(OwnDrawsTest, WithNoDrawLeftTheHandCannotWin) {
        // A wall of 3 tiles leaves the seat 3 / 4 = 0 draws: no win, and a hand that reaches the end of the wall is a drawn hand (0).
        const TableView view = MakeView(kTankiHand, "", kSeatCount - 1);
        const Vector<DiscardValue> values = ValueDiscards(view, kDepth);
        TileCounts counts = CountTiles(view.own.concealed);

        ASSERT_FALSE(values.empty());
        for (const DiscardValue& value : values) {
            EXPECT_EQ(value.winChance, 0) << ToString(value.tile).data();
            EXPECT_EQ(value.meanWinScore, 0) << ToString(value.tile).data();
            EXPECT_EQ(value.expected, 0) << ToString(value.tile).data();
        }
        // Equal values keep the order of the rating.
        EXPECT_EQ(values.front().tile, RateDiscards(counts, 0, view).front().tile);
        for (const DiscardValue& wait : ValueWaits(view)) {
            EXPECT_EQ(wait.winChance, 0);
        }
    }

    TEST(OwnDrawsTest, ThereIsNothingToRateOutsideTheSeatsOwnDiscardTurn) {
        TableView claim = MakeView(kTankiHand);
        claim.phase = Phase::Claim;
        TableView other = MakeView(kTankiHand);
        other.currentSeat = kSeat1;
        TableView none;

        EXPECT_TRUE(ValueDiscards(claim, kDepth).empty());
        EXPECT_TRUE(ValueDiscards(other, kDepth).empty());
        EXPECT_TRUE(ValueDiscards(none, kDepth).empty());
        EXPECT_TRUE(ValueDiscards(MakeView(kTankiHand), 0).empty());
        EXPECT_TRUE(ValueDiscards(MakeView(kTankiHand), -1).empty());
        EXPECT_TRUE(ValueWaits(claim).empty());
        EXPECT_TRUE(ValueWaits(other).empty());
        EXPECT_TRUE(ValueWaits(none).empty());
    }

    TEST(OwnDrawsTest, ADeeperLookWinsMoreOftenFromAHandOneTileFromWaiting) {
        // Discarding 9s leaves 123p 456s, 78m and 57p to join and a pair of Ew: shanten 1. Depth 1 sees one improving draw, then the hand
        // waits and the rest of the wall is closed form; every further depth sees one more draw that can improve, so the chance to win grows.
        const TableView view = MakeView(kOneFromWaiting);
        int previous = -1;
        for (int depth = 1; depth <= kOwnDrawsMaxDepth; ++depth) {
            const Vector<DiscardValue> values = ValueDiscards(view, depth);
            const DiscardValue* value = Find(values, "9s");
            ASSERT_NE(value, nullptr) << "depth " << depth;
            EXPECT_GT(value->winChance, previous) << "depth " << depth;
            previous = value->winChance;
        }
        EXPECT_LT(previous, kPerfectChance);
    }

    TEST(OwnDrawsTest, ADepthAboveTheMaximumIsTheMaximum) {
        const TableView view = MakeView(kOneFromWaiting);

        ExpectEqual(ValueDiscards(view, kOwnDrawsMaxDepth), ValueDiscards(view, kOwnDrawsMaxDepth + 3));
    }

    TEST(OwnDrawsTest, OnlyListedPlainDiscardsAreRatedBestFirst) {
        TableView view = MakeView(kTankiHand);
        // Take the plain discard of 5p out of the list: only its declaring variant stays, which is not a candidate.
        view.legal.erase(eastl::remove(view.legal.begin(), view.legal.end(), HandAction::MakeDiscard(T("5p"))), view.legal.end());

        const Vector<DiscardValue> values = ValueDiscards(view, kDepth);

        ASSERT_FALSE(values.empty());
        EXPECT_LE(values.size(), static_cast<size_t>(kOwnDrawsMaxCandidates));
        EXPECT_EQ(Find(values, "5p"), nullptr);
        for (size_t i = 0; i < values.size(); ++i) {
            EXPECT_TRUE(Has(view.legal, HandAction::MakeDiscard(values[i].tile))) << ToString(values[i].tile).data();
            EXPECT_FALSE(Has(view.legal, HandAction::MakeDiscard(values[i].tile, true)) && !Has(view.legal, HandAction::MakeDiscard(values[i].tile)));
            if (i > 0) {
                EXPECT_LE(values[i].expected, values[i - 1].expected) << "entry " << i;
                for (size_t j = 0; j < i; ++j) {
                    EXPECT_NE(values[i].tile, values[j].tile);
                }
            }
        }
    }

    TEST(OwnDrawsTest, ValueWaitsListsOnlyTheDiscardsThatDeclare) {
        const TableView view = MakeView(kTankiHand);

        const Vector<DiscardValue> waits = ValueWaits(view);

        ASSERT_EQ(waits.size(), 2u);
        for (const DiscardValue& wait : waits) {
            EXPECT_TRUE(Has(view.legal, HandAction::MakeDiscard(wait.tile, true))) << ToString(wait.tile).data();
        }
        EXPECT_TRUE(ValueWaits(MakeView(kPairsHand)).empty());
    }

    TEST(OwnDrawsTest, TheValuesOfTheGoldenPositionsDoNotMove) {
        const Vector<GoldenSpec> specs = GoldenSpecs();
        const Vector<GoldenValues> table = GoldenTable();
        ASSERT_EQ(specs.size(), table.size());
        for (size_t i = 0; i < specs.size(); ++i) {
            const GoldenSpec& spec = specs[i];
            const TableView view = GoldenView(spec);
            const TableView held = HeldView(spec);
            ExpectGolden(spec.how, "depth 1", ValueDiscards(view, 1), table[i].depth1);
            ExpectGolden(spec.how, "depth 2", ValueDiscards(view, 2), table[i].depth2);
            ExpectGolden(spec.how, "waits", ValueWaits(view), table[i].waits);
            EXPECT_EQ(ValueOfHeldHand(held), table[i].held) << spec.how;
            EXPECT_EQ(ValueOfHeldHand(held, 1), table[i].heldWithLooseTile) << spec.how;
        }
    }

    TEST(OwnDrawsTest, AWinOnADiscardRaisesTheChanceOfAOneWaitHandByTheHandCount) {
        // The hand of `TheChanceOfAOneWaitHandIsTheChanceOfTheCopiesOverTheDrawsLeft`: a wait on Gd with 3 copies of the 122 unseen tiles,
        // a wall of 40 tiles (10 rounds), nobody else declared. Each round the three others discard first: one of them is a Gd with the
        // chance `kDiscardHitPermille` of 3 / 122, and the seat then draws as before.
        constexpr int kWall = 40;
        constexpr int kRounds = kWall / kSeatCount;
        const double p = 3.0 / 122.0;
        const WinMasses off = ExpectedWinMasses(p, kRounds, false);
        const WinMasses on = ExpectedWinMasses(p, kRounds, true);
        const TableView view = MakeView(kTankiHand, "", kWall);

        const Vector<DiscardValue> onlyDraws = ValueDiscards(view, kOnlyDraws);
        const Vector<DiscardValue> bothWays = ValueDiscards(view, kWithDiscardWins);
        const DiscardValue* without = Find(onlyDraws, "5p");
        const DiscardValue* with = Find(bothWays, "5p");

        ASSERT_NE(without, nullptr);
        ASSERT_NE(with, nullptr);
        EXPECT_NEAR(without->winChance / static_cast<double>(kOwnDrawsProbBase), off.byDraw, kChanceTolerance);
        EXPECT_NEAR(with->winChance / static_cast<double>(kOwnDrawsProbBase), on.byDraw + on.byDiscard, kChanceTolerance);
        EXPECT_GT(with->winChance, without->winChance);
        EXPECT_GT(with->expected, without->expected);
    }

    TEST(OwnDrawsTest, AWinOnADiscardOpensTheSetThatHoldsTheTileAndTheMeanIsTheWeightedOne) {
        // 123p 456s 789m, a pair of 5p and a pair of Gd. Both 5p are lying open too, so the wait is on Gd alone: 2 copies of the 120 unseen
        // tiles. A Gd by self-draw makes a concealed pung of dragons; on a discard the pung is open (fewer points, no concealed hand).
        // The mean score of a hand that can win either way lies between the two scores, at the weights of the two chances.
        constexpr int kWall = 40;
        constexpr int kRounds = kWall / kSeatCount;
        const double p = 2.0 / 120.0;
        const WinMasses masses = ExpectedWinMasses(p, kRounds, true);
        const TableView view = MakeView("1p 2p 3p 4s 5s 6s 7m 8m 9m 5p 5p Gd Gd Ew", "5p 5p", kWall);

        // Declared with the first discard, as the view says (no discard made); the pile holds no Gd.
        ScoreInput selfDraw = MakeWinnerInput("1p 2p 3p 4s 5s 6s 7m 8m 9m 5p 5p Gd Gd Gd", "Gd", WinSource::SelfDraw);
        selfDraw.hand.callingDeclared = true;
        ScoreInput onDiscard = MakeWinnerInput("1p 2p 3p 4s 5s 6s 7m 8m 9m 5p 5p Gd Gd Gd", "Gd", WinSource::Discard);
        onDiscard.hand.callingDeclared = true;
        onDiscard.result.from = kSeat1;
        const int selfDrawScore = ScoreSeat(selfDraw).score;
        const int discardScore = ScoreSeat(onDiscard).score;
        ASSERT_GT(selfDrawScore, discardScore);

        const Vector<DiscardValue> onlyDraws = ValueDiscards(view, kOnlyDraws);
        const Vector<DiscardValue> bothWays = ValueDiscards(view, kWithDiscardWins);
        const DiscardValue* without = Find(onlyDraws, "Ew");
        const DiscardValue* both = Find(bothWays, "Ew");

        ASSERT_NE(without, nullptr);
        ASSERT_NE(both, nullptr);
        EXPECT_EQ(without->meanWinScore, selfDrawScore);
        const double mean = (masses.byDraw * selfDrawScore + masses.byDiscard * discardScore) / (masses.byDraw + masses.byDiscard);
        EXPECT_NEAR(both->meanWinScore, mean, 1.0);
        EXPECT_LT(both->meanWinScore, selfDrawScore);
        EXPECT_GT(both->meanWinScore, discardScore);
    }

    TEST(OwnDrawsTest, WithTheKnobOffTheValuesAreTheOnesOfOnlyTheDraws) {
        for (const GoldenSpec& spec : GoldenSpecs()) {
            const TableView view = GoldenView(spec);
            ExpectEqual(ValueDiscards(view, OwnDrawsModel{.depth = kDepth}), ValueDiscards(view, kDepth));
            ExpectEqual(ValueWaits(view, OwnDrawsModel{.discardWins = false}), ValueWaits(view));
            EXPECT_EQ(ValueOfHeldHand(HeldView(spec), 1, OwnDrawsModel{}), ValueOfHeldHand(HeldView(spec), 1)) << spec.how;
        }
    }

    TEST(OwnDrawsTest, AWinOnADiscardIsWorthSomethingToEveryHandThatCanWait) {
        // Waits and held hands too: the discard wins add to the chance of winning and to the value of a wait with unseen copies.
        const TableView view = MakeView(kTankiHand);

        const Vector<DiscardValue> off = ValueWaits(view);
        const Vector<DiscardValue> on = ValueWaits(view, kWithDiscardWins);

        ASSERT_EQ(off.size(), on.size());
        for (const DiscardValue& wait : on) {
            const DiscardValue* before = Find(off, ToString(wait.tile).data());
            ASSERT_NE(before, nullptr);
            EXPECT_GT(wait.winChance, before->winChance) << ToString(wait.tile).data();
            EXPECT_LE(wait.winChance, kPerfectChance);
            EXPECT_GT(wait.expected, before->expected) << ToString(wait.tile).data();
        }
        TableView held = view;
        held.own.RemoveConcealed(T("5p"));
        EXPECT_GT(ValueOfHeldHand(held, 0, kWithDiscardWins), ValueOfHeldHand(held));
    }

    TEST(OwnDrawsTest, TwoTablesThatDifferOnlyInHiddenTilesGiveTheSameValuesWithTheDiscardWins) {
        const char* east = "1p 2p 3p 4s 5s 6s 7m 8m Ew Ew 5p 7p 9s";
        FixedWallSpec first = SpecOf(east, "2m");
        FixedWallSpec second = SpecOf(east, "2m Rd Rd Rd");
        second.hands[kSeat1] = kQuiet3;
        second.hands[kSeat2] = kQuiet1;
        second.hands[kSeat3] = kQuiet2;
        const TableView viewA = MakeTableView(MakeModel(first), kSeat0);
        const TableView viewB = MakeTableView(MakeModel(second), kSeat0);
        const OwnDrawsModel model{.depth = kDepth + 1, .discardWins = true};

        const Vector<DiscardValue> a = ValueDiscards(viewA, model);

        ASSERT_FALSE(a.empty());
        ExpectEqual(a, ValueDiscards(viewB, model));
        ExpectEqual(ValueWaits(viewA, model), ValueWaits(viewB, model));
        ExpectEqual(a, ValueDiscards(viewA, model));
    }

}  // namespace BECore::Mahjong::Tests
