#include "HandTestSupport.h"

#include <BECore/Control/Json.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/MahjongStateJson.h>
#include <Mahjong/Online/MahjongSeatedGame.h>
#include <Mahjong/Online/OnlineMirror.h>
#include <gtest/gtest.h>
#include <set>

/**
 * `OnlineMirror` (`Online/OnlineMirror.h`): the table of a view as a display copy. The tables come from the real `MahjongSeatedGame`
 * through `ReadOnlineView`, so the copy is held against the model the server plays: every public fact equal, the own tiles real, no
 * valid tile in a hidden place, the winds and the east right for each viewer. Plain C++: no scene, no `Application`.
 */
namespace BECore::Mahjong::Tests {

    namespace {

        constexpr uint64_t kSeed = 5;
        constexpr int kStepLimit = 40000;
        constexpr int kNoSampling = 1;
        constexpr int kSampleEvery = 4;
        constexpr int kSeedsToPlay = 3;

        Json Parse(const char* text) {
            return ParseJson(text).value();
        }

        MahjongSeatedGame Started(uint64_t seed) {
            MahjongSeatedGame game;
            EXPECT_TRUE(game.Start(seed, Parse("{\"hands\": 8, \"level\": 1}")).has_value());
            return game;
        }

        /// The lobby of a started room: seats 0 and 2 are people (seat 2 is away), the others computers.
        OnlineLobby MakeLobby() {
            OnlineLobby lobby;
            lobby.phase = RoomPhase::Playing;
            for (size_t seat = 0; seat < kSeatCount; ++seat) {
                OnlineLobbySeat& entry = lobby.seats[seat];
                entry.kind = seat % 2 == 0 ? BECore::SeatKind::Human : BECore::SeatKind::Computer;
                entry.name = seat % 2 == 0 ? eastl::string(seat == 0 ? "Ann" : "Cy") : eastl::string();
                entry.away = seat == 2;
            }
            return lobby;
        }

        OnlineTable ReadTable(const MahjongSeatedGame& game, int seat) {
            OnlineTable table;
            const auto read = ReadOnlineView(game.GetSeatView(seat).value(), game.GetSeatActions(seat).value(), table);
            EXPECT_TRUE(read.has_value());
            return table;
        }

        bool StepOnce(MahjongSeatedGame& game) {
            for (int seat = 0; seat < kSeatCount; ++seat) {
                if (const std::optional<Json> action = game.ChooseComputerAction(seat)) {
                    const auto done = game.PerformSeatAction(seat, *action);
                    EXPECT_TRUE(done.has_value());
                    return done.has_value();
                }
            }
            return false;
        }

        /// What the cases have met, to hold that the comparison covers the phases the task names.
        struct Coverage {
            std::set<int> phases;
            int windowViewerWaiting = 0;
            int windowViewerPassed = 0;
            int ownConcealedKong = 0;
            int othersConcealedKong = 0;
            int openKong = 0;
            int wins = 0;
            int draws = 0;
            int matchOver = 0;
            int checks = 0;
        };

        void ExpectMeldEqual(const Meld& shown, const Meld& want, const std::string& where) {
            EXPECT_EQ(shown.kind, want.kind) << where;
            EXPECT_EQ(shown.concealed, want.concealed) << where;
            EXPECT_EQ(shown.first, want.first) << where;
            EXPECT_EQ(shown.from, want.from) << where;
            EXPECT_EQ(shown.fromDiscard, want.fromDiscard) << where;
            EXPECT_EQ(shown.claimed, want.claimed) << where;
        }

        void ExpectOpenMeldsEqual(const eastl::vector<Meld>& shown, const eastl::vector<Meld>& want, const std::string& where) {
            ASSERT_EQ(shown.size(), want.size()) << where;
            for (size_t index = 0; index < want.size(); ++index) {
                ExpectMeldEqual(shown[index], want[index], where + "[" + std::to_string(index) + "]");
            }
        }

        bool NoValidTile(const eastl::vector<Tile>& tiles) {
            return eastl::none_of(tiles.begin(), tiles.end(), [](Tile tile) { return tile.IsValid(); });
        }

        /// The display copy of `table` against the real model `game` plays, for the viewer `seat`.
        void ExpectMirrorsTheModel(const MahjongSeatedGame& game, int seat, Coverage& coverage) {
            SCOPED_TRACE("viewer " + std::to_string(seat) + ", version " + std::to_string(game.GetStateVersion()));
            const Match& real = game.GetMatch();
            const HandModel& want = real.Hand();
            const OnlineTable table = ReadTable(game, seat);
            const OnlineLobby lobby = MakeLobby();

            const DisplayState state = MakeDisplayState(table, lobby);
            Match shown;
            TableSession session{shown};
            ShowOnlineTable(shown, session, state, true);
            const HandModel& hand = shown.Hand();
            const bool playing = PhaseOfMatch(real) == SessionPhase::Playing;
            ++coverage.checks;
            coverage.phases.insert(static_cast<int>(want.GetPhase()));

            // The match.
            EXPECT_TRUE(shown.IsRemote());
            EXPECT_TRUE(hand.IsDisplayOnly());
            EXPECT_EQ(shown.Scores(), real.Scores());
            EXPECT_EQ(shown.HandsPlayed(), real.HandsPlayed());
            EXPECT_EQ(shown.HandsTotal(), real.HandsTotal());
            EXPECT_EQ(shown.East(), real.East());
            EXPECT_EQ(shown.Winds(), real.Winds());
            EXPECT_EQ(shown.RoundWind(), real.RoundWind());
            EXPECT_EQ(shown.GetPhase(), real.GetPhase());
            EXPECT_EQ(session.GetPhase(), PhaseOfMatch(real));
            EXPECT_EQ(session.Viewer(), seat);
            EXPECT_EQ(session.PersonCount(), 2);
            EXPECT_EQ(session.KindOf(2), SeatKind::Person);
            EXPECT_EQ(session.KindOf(3), SeatKind::Computer);
            EXPECT_EQ(session.NameOf(0), "Ann");
            EXPECT_TRUE(session.NameOf(1).empty());
            if (real.GetPhase() == MatchPhase::Finished) {
                ++coverage.matchOver;
            }

            // The hand: the facts the table reads.
            EXPECT_EQ(hand.GetPhase(), want.GetPhase());
            EXPECT_EQ(hand.East(), want.East());
            EXPECT_EQ(hand.RoundWind(), want.RoundWind());
            EXPECT_EQ(hand.PendingTile(), want.PendingTile());
            EXPECT_EQ(hand.Discards().size(), want.Discards().size());
            for (size_t index = 0; index < want.Discards().size(); ++index) {
                EXPECT_EQ(hand.Discards()[index].tile, want.Discards()[index].tile);
                EXPECT_EQ(hand.Discards()[index].seat, want.Discards()[index].seat);
            }
            EXPECT_EQ(hand.GetWall().LiveCount(), want.GetWall().LiveCount());
            EXPECT_EQ(hand.GetWall().Remaining(), want.GetWall().Remaining());
            for (Seat other = 0; other < kSeatCount; ++other) {
                EXPECT_EQ(hand.SeatWind(other), want.SeatWind(other));
                EXPECT_FALSE(hand.DrawnTile(other).IsValid());
                EXPECT_TRUE(hand.LegalActions(other).empty());
            }
            EXPECT_TRUE(hand.PendingClaimSeats().size() <= 1);

            // The view of the viewer on the copy is the view on the model, except what needs hidden tiles.
            const TableView shownView = MakeTableView(hand, seat);
            const TableView wantView = MakeTableView(want, seat);
            EXPECT_EQ(shownView.seat, wantView.seat);
            EXPECT_EQ(shownView.phase, wantView.phase);
            if (playing) {
                EXPECT_EQ(shownView.currentSeat, wantView.currentSeat);
                EXPECT_EQ(hand.CurrentSeat(), want.CurrentSeat());
            }
            EXPECT_EQ(shownView.pendingTile, wantView.pendingTile);
            EXPECT_EQ(shownView.seatWind, wantView.seatWind);
            EXPECT_EQ(shownView.roundWind, wantView.roundWind);
            EXPECT_EQ(shownView.own.concealed, wantView.own.concealed);
            ExpectOpenMeldsEqual(shownView.own.melds, wantView.own.melds, "own.melds");
            EXPECT_EQ(shownView.own.bonus, wantView.own.bonus);
            EXPECT_EQ(shownView.own.callingDeclared, wantView.own.callingDeclared);
            EXPECT_EQ(shownView.own.discardCount, wantView.own.discardCount);
            EXPECT_EQ(shownView.visible, wantView.visible);
            EXPECT_EQ(shownView.liveCount, wantView.liveCount);
            for (Seat other = 0; other < kSeatCount; ++other) {
                const PublicSeat& shownSeat = shownView.seats[static_cast<size_t>(other)];
                const PublicSeat& wantSeat = wantView.seats[static_cast<size_t>(other)];
                const std::string where = "seats[" + std::to_string(other) + "]";
                ExpectOpenMeldsEqual(shownSeat.openMelds, wantSeat.openMelds, where + ".openMelds");
                EXPECT_EQ(shownSeat.concealedKongs, wantSeat.concealedKongs) << where;
                EXPECT_EQ(shownSeat.bonus, wantSeat.bonus) << where;
                EXPECT_EQ(shownSeat.discardCount, wantSeat.discardCount) << where;
                EXPECT_EQ(shownSeat.callingDeclared, wantSeat.callingDeclared) << where;
                EXPECT_EQ(hand.GetHand(other).concealed.size(), want.GetHand(other).concealed.size()) << where;
                coverage.othersConcealedKong += other != seat && wantSeat.concealedKongs > 0 ? 1 : 0;
            }
            for (const Meld& meld : wantView.own.melds) {
                coverage.ownConcealedKong += meld.concealed ? 1 : 0;
                coverage.openKong += !meld.concealed && meld.kind == MeldKind::Kong ? 1 : 0;
            }

            if (playing) {
                // The claim window: the viewer waits when the model waits for it.
                if (want.GetPhase() == Phase::Claim || want.GetPhase() == Phase::RobKong) {
                    const ClaimStatus status = want.GetClaimStatus(seat);
                    if (status == ClaimStatus::Waiting) {
                        EXPECT_EQ(hand.GetClaimStatus(seat), ClaimStatus::Waiting);
                        ++coverage.windowViewerWaiting;
                    } else if (status != ClaimStatus::NotInWindow) {
                        EXPECT_EQ(hand.GetClaimStatus(seat), ClaimStatus::Passed);
                        ++coverage.windowViewerPassed;
                    }
                    EXPECT_EQ(hand.GetClaimStatus(want.CurrentSeat()), ClaimStatus::NotInWindow);
                } else {
                    EXPECT_TRUE(hand.PendingClaimSeats().empty());
                }
                EXPECT_FALSE(hand.Result().has_value());
                // Nothing hidden is a valid tile: the other hands, their concealed kongs and the wall.
                for (Seat other = 0; other < kSeatCount; ++other) {
                    if (other != seat) {
                        EXPECT_TRUE(NoValidTile(hand.GetHand(other).concealed)) << "seat " << other;
                        for (const Meld& meld : hand.GetHand(other).melds) {
                            EXPECT_TRUE(!meld.concealed || !meld.first.IsValid()) << "seat " << other;
                        }
                    }
                }
                EXPECT_TRUE(NoValidTile(hand.GetWall().LiveTiles()));
                EXPECT_TRUE(NoValidTile(hand.GetWall().LooseTiles()));
                EXPECT_TRUE(NoValidTile(hand.GetWall().DeadWall()));
            } else {
                // Over: all four hands and the result are real.
                ASSERT_TRUE(hand.Result().has_value());
                ASSERT_TRUE(want.Result().has_value());
                EXPECT_EQ(hand.Result()->kind, want.Result()->kind);
                EXPECT_EQ(hand.Result()->winner, want.Result()->winner);
                EXPECT_EQ(hand.Result()->winningTile, want.Result()->kind == ResultKind::Win ? want.Result()->winningTile : Tile{});
                EXPECT_EQ(hand.Result()->from, want.Result()->from);
                EXPECT_EQ(hand.Result()->source, want.Result()->source);
                EXPECT_EQ(hand.Result()->loose, want.Result()->loose);
                EXPECT_EQ(hand.Result()->lastLiveTile, want.Result()->lastLiveTile);
                coverage.wins += want.Result()->kind == ResultKind::Win ? 1 : 0;
                coverage.draws += want.Result()->kind == ResultKind::Drawn ? 1 : 0;
                for (Seat other = 0; other < kSeatCount; ++other) {
                    EXPECT_EQ(hand.GetHand(other).concealed, want.GetHand(other).concealed) << "seat " << other;
                    EXPECT_EQ(hand.GetHand(other).bonus, want.GetHand(other).bonus) << "seat " << other;
                    ASSERT_EQ(hand.GetHand(other).melds.size(), want.GetHand(other).melds.size());
                    for (size_t index = 0; index < want.GetHand(other).melds.size(); ++index) {
                        ExpectMeldEqual(hand.GetHand(other).melds[index], want.GetHand(other).melds[index], "seat " + std::to_string(other));
                    }
                }
            }
        }

        /// Plays a match to its end, comparing every `every` steps (and the last step of each hand).
        Coverage PlayAndCompare(uint64_t seed, int every) {
            Coverage coverage;
            MahjongSeatedGame game = Started(seed);
            for (int step = 0; step < kStepLimit; ++step) {
                const bool handOver = PhaseOfMatch(game.GetMatch()) != SessionPhase::Playing;
                if (step % every == 0 || handOver) {
                    for (int seat = 0; seat < kSeatCount; ++seat) {
                        ExpectMirrorsTheModel(game, seat, coverage);
                        if (::testing::Test::HasFailure()) {
                            return coverage;
                        }
                    }
                }
                if (game.IsFinished()) {
                    return coverage;
                }
                if (!StepOnce(game)) {
                    ADD_FAILURE() << "nobody could act at step " << step;
                    return coverage;
                }
            }
            ADD_FAILURE() << "the match did not end";
            return coverage;
        }

    }  // namespace

    TEST(MahjongOnlineMirrorTest, ADisplayCopyHoldsEveryPublicFactOfTheModelForEachViewer) {
        Coverage total;
        for (uint64_t seed = kSeed; seed < kSeed + kSeedsToPlay; ++seed) {
            const Coverage seen = PlayAndCompare(seed, seed == kSeed ? kNoSampling : kSampleEvery);
            ASSERT_FALSE(HasFailure()) << "seed " << seed;
            total.phases.insert(seen.phases.begin(), seen.phases.end());
            total.windowViewerWaiting += seen.windowViewerWaiting;
            total.windowViewerPassed += seen.windowViewerPassed;
            total.ownConcealedKong += seen.ownConcealedKong;
            total.othersConcealedKong += seen.othersConcealedKong;
            total.openKong += seen.openKong;
            total.wins += seen.wins;
            total.draws += seen.draws;
            total.matchOver += seen.matchOver;
            total.checks += seen.checks;
        }
        // The comparison met the turn, the claim window with and without a claim for the viewer, the end of a hand won and drawn, the
        // end of the match and kongs.
        EXPECT_EQ(total.phases.size(), 3U);
        EXPECT_GT(total.windowViewerWaiting, 0);
        EXPECT_GT(total.windowViewerPassed, 0);
        EXPECT_GT(total.wins, 0);
        EXPECT_GT(total.matchOver, 0);
        EXPECT_GT(total.openKong, 0);
        EXPECT_GT(total.ownConcealedKong, 0);
        EXPECT_GT(total.othersConcealedKong, 0);
        EXPECT_GT(total.draws, 0);
        EXPECT_GT(total.checks, 0);
    }

    TEST(MahjongOnlineMirrorTest, TheOwnTilesAreRealAndEveryoneElsesAreInvalidOnAFreshHand) {
        MahjongSeatedGame game = Started(kSeed);
        for (int seat = 0; seat < kSeatCount; ++seat) {
            const OnlineTable table = ReadTable(game, seat);
            const DisplayState state = MakeDisplayState(table, MakeLobby());
            EXPECT_EQ(state.viewer, seat);
            EXPECT_EQ(state.east, game.GetMatch().East());
            EXPECT_EQ(state.hand.east, game.GetMatch().East());
            EXPECT_EQ(state.hand.seatWinds, game.GetMatch().Winds());
            EXPECT_EQ(state.hand.hands[static_cast<size_t>(seat)].concealed, game.GetMatch().Hand().GetHand(seat).concealed);
            for (Seat other = 0; other < kSeatCount; ++other) {
                const size_t index = static_cast<size_t>(other);
                if (other != seat) {
                    EXPECT_EQ(state.hand.hands[index].concealed.size(), game.GetMatch().Hand().GetHand(other).concealed.size());
                    EXPECT_TRUE(NoValidTile(state.hand.hands[index].concealed));
                }
            }
            EXPECT_TRUE(NoValidTile(state.hand.live));
            EXPECT_TRUE(NoValidTile(state.hand.loose));
            EXPECT_TRUE(NoValidTile(state.hand.dead));
            EXPECT_EQ(state.hand.loose.size(), kLooseTiles);
            EXPECT_EQ(state.hand.dead.size(), kDeadWallTiles);
            EXPECT_EQ(state.actor, game.GetMatch().Hand().CurrentSeat());
            EXPECT_EQ(state.names[0], "Ann");
            EXPECT_EQ(state.seats[2], SeatKind::Person);
            EXPECT_EQ(state.seats[1], SeatKind::Computer);
        }
    }

    TEST(MahjongOnlineMirrorTest, TheReserveIsWhatTheTileCountLeavesAfterTheLiveWallRanOut) {
        // A table with an empty live wall: the reserve is the set (136) minus what lies outside the wall minus the dead wall. The
        // pile of the made-up table holds whatever is needed to make the count.
        MahjongSeatedGame game = Started(kSeed);
        OnlineTable table = ReadTable(game, 0);
        const OnlineLobby lobby = MakeLobby();
        constexpr int kSet = 136;
        const int outsideAtTheDeal = kSeatCount * 13 + 1;
        ASSERT_EQ(static_cast<int>(table.discards.size()), 0);
        table.wall = 0;
        for (const int reserve : {2, 1, 0}) {
            table.discards.clear();
            const int outside = kSet - reserve - static_cast<int>(kDeadWallTiles);
            for (int count = 0; count < outside - outsideAtTheDeal; ++count) {
                table.discards.push_back({T("1p"), 1});
            }
            const DisplayState state = MakeDisplayState(table, lobby);
            EXPECT_EQ(state.hand.loose.size(), static_cast<size_t>(reserve));
            EXPECT_TRUE(state.hand.live.empty());
            EXPECT_EQ(state.hand.dead.size(), kDeadWallTiles);
        }
    }

    TEST(MahjongOnlineMirrorTest, TheActorIsTheViewerWhenItHasActionsElseTheSeatTheHandWaitsFor) {
        MahjongSeatedGame game = Started(kSeed);
        const Seat mover = game.GetMatch().Hand().CurrentSeat();
        for (int seat = 0; seat < kSeatCount; ++seat) {
            const DisplayState state = MakeDisplayState(ReadTable(game, seat), MakeLobby());
            EXPECT_EQ(state.actor, mover) << "viewer " << seat;
        }
    }

    TEST(MahjongOnlineMirrorTest, AnotherTableIsANewHandAMatchAViewThatWentBackOrAReplayedHand) {
        MahjongSeatedGame game = Started(kSeed);
        const OnlineTable first = ReadTable(game, 0);
        TableKey key = KeyOf(first);
        EXPECT_TRUE(IsAnotherTable(TableKey{}, key));

        // Inside one hand nothing is another table.
        for (int step = 0; step < 20 && PhaseOfMatch(game.GetMatch()) == SessionPhase::Playing; ++step) {
            ASSERT_TRUE(StepOnce(game));
            const TableKey now = KeyOf(ReadTable(game, 0));
            EXPECT_FALSE(IsAnotherTable(key, now));
            key = now;
        }
        // A version that went back (another room).
        TableKey back = key;
        back.version = key.version - 1;
        EXPECT_TRUE(IsAnotherTable(key, back));
        // Another seat.
        TableKey elsewhere = key;
        elsewhere.seat = 3;
        EXPECT_TRUE(IsAnotherTable(key, elsewhere));
        // The hand ended and the next one is played (the same keys when a drawn hand is replayed).
        TableKey ended = key;
        ended.phase = SessionPhase::HandOver;
        EXPECT_TRUE(IsAnotherTable(ended, key));
        EXPECT_FALSE(IsAnotherTable(key, ended));
        TableKey nextHand = key;
        nextHand.handsPlayed = key.handsPlayed + 1;
        EXPECT_TRUE(IsAnotherTable(key, nextHand));
        TableKey nextEast = key;
        nextEast.east = NextSeat(key.east);
        EXPECT_TRUE(IsAnotherTable(key, nextEast));
    }

    TEST(MahjongOnlineMirrorTest, ShowOnlineTableMovesTheStartCountOnlyOnANewTable) {
        MahjongSeatedGame game = Started(kSeed);
        const OnlineLobby lobby = MakeLobby();
        Match shown;
        TableSession session{shown};
        DisplayState state;
        MakeDisplayState(ReadTable(game, 1), lobby, state);
        ShowOnlineTable(shown, session, state, true);
        const uint64_t starts = session.StartCount();
        const uint64_t version = session.Version();
        ASSERT_TRUE(StepOnce(game));
        MakeDisplayState(ReadTable(game, 1), lobby, state);
        ShowOnlineTable(shown, session, state, false);
        EXPECT_EQ(session.StartCount(), starts);
        EXPECT_GT(session.Version(), version);
        EXPECT_EQ(shown.Hand().Discards().size(), game.GetMatch().Hand().Discards().size());
    }

}  // namespace BECore::Mahjong::Tests
