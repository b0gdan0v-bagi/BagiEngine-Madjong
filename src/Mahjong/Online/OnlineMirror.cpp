#include "OnlineMirror.h"

namespace BECore::Mahjong {

    namespace {

        /// Tiles of the whole set when no bonus tile is in play (a room never offers flowers).
        constexpr int kSetTiles = kKindCount * kCopiesPerTile;
        /// Tiles in a kong: the three of the pung and the fourth.
        constexpr int kKongTileCount = 4;

        int MeldTileCount(const Meld& meld) {
            return meld.kind == MeldKind::Kong ? kKongTileCount : kTilesPerSet;
        }

        /// A meld as the model keeps it: a concealed kong has no discarder and no claimed tile (the view says null).
        Meld Normalized(Meld meld) {
            if (meld.concealed) {
                meld.from = 0;
                meld.claimed = Tile{};
            }
            return meld;
        }

        Meld HiddenKong() {
            Meld meld;
            meld.kind = MeldKind::Kong;
            meld.concealed = true;
            return meld;
        }

        bool Finished(const OnlineTable& table) {
            return table.phase != SessionPhase::Playing && table.result.has_value();
        }

        /// The melds of @p seat as the hand lists them once the hand is over (all four are public then) or while it is played.
        void MeldsOf(const OnlineTable& table, Seat seat, eastl::vector<Meld>& out) {
            out.clear();
            const size_t index = static_cast<size_t>(seat);
            if (Finished(table)) {
                for (const Meld& meld : table.result->seats[index].melds) {
                    out.push_back(Normalized(meld));
                }
                return;
            }
            const OnlineSeat& shown = table.seats[index];
            for (const Meld& meld : shown.melds) {
                out.push_back(Normalized(meld));
            }
            if (seat != table.seat) {
                for (int kong = 0; kong < shown.concealedKongs; ++kong) {
                    out.push_back(HiddenKong());
                }
            }
        }

        /// How many tiles of the set lie in hands, melds, bonus piles and the pile.
        int TilesOutsideTheWall(const DisplayState& state, const OnlineTable& table) {
            int outside = static_cast<int>(table.discards.size());
            for (const PlayerHand& hand : state.hand.hands) {
                outside += static_cast<int>(hand.concealed.size() + hand.bonus.size());
                for (const Meld& meld : hand.melds) {
                    outside += MeldTileCount(meld);
                }
            }
            return outside;
        }

        Seat WaitedSeat(const OnlineTable& table) {
            if (table.phase != SessionPhase::Playing || !IsSeat(table.turn.current)) {
                return kNoSeat;
            }
            if (table.turn.phase == Phase::Discard) {
                return table.turn.current;
            }
            for (int step = 1; step < kSeatCount; ++step) {
                const Seat seat = (table.turn.current + step) % kSeatCount;
                if (seat != table.seat) {
                    return seat;
                }
            }
            return kNoSeat;
        }

        bool HasWindowAction(const OnlineTable& table) {
            for (const ControlAction& action : table.actions) {
                if (!action.nextHand && (action.hand.kind == ActionKind::Pass || action.hand.kind == ActionKind::Claim)) {
                    return true;
                }
            }
            return false;
        }

    }  // namespace

    void DisplayMelds(const OnlineTable& table, Seat seat, eastl::vector<Meld>& out) {
        out.clear();
        if (IsSeat(seat)) {
            MeldsOf(table, seat, out);
        }
    }

    void MakeDisplayState(const OnlineTable& table, const OnlineLobby& lobby, DisplayState& out) {
        const bool over = Finished(table);
        HandState& hand = out.hand;

        hand.settings = MahjongSettings{};
        hand.settings.handsPerMatch = table.handsTotal;
        hand.seatWinds = MakeSeatWinds(table.east);
        hand.roundWind = table.roundWind;
        hand.east = table.east;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const size_t index = static_cast<size_t>(seat);
            const OnlineSeat& shown = table.seats[index];
            PlayerHand& tiles = hand.hands[index];
            tiles.concealed.clear();
            tiles.bonus.clear();
            if (over) {
                const OnlineResultSeat& result = table.result->seats[index];
                tiles.concealed.assign(result.concealed.begin(), result.concealed.end());
                tiles.bonus.assign(result.bonus.begin(), result.bonus.end());
            } else {
                if (seat == table.seat) {
                    tiles.concealed.assign(table.hand.begin(), table.hand.end());
                } else {
                    tiles.concealed.assign(static_cast<size_t>(eastl::max(shown.concealedCount, 0)), Tile{});
                }
                tiles.bonus.assign(shown.bonus.begin(), shown.bonus.end());
            }
            MeldsOf(table, seat, tiles.melds);
            tiles.callingDeclared = shown.calling;
            tiles.discardCount = shown.discardCount;
            tiles.callingAtDiscardCount = shown.discardCount;
        }

        hand.discards.assign(table.discards.begin(), table.discards.end());
        hand.phase = over || table.phase != SessionPhase::Playing ? Phase::Finished : table.turn.phase;
        hand.current = hand.phase == Phase::Finished ? table.east : table.turn.current;
        hand.drawnTile = Tile{};
        hand.drawnLoose = false;
        hand.claimStatus = {};
        hand.claims = {};
        hand.pendingTile = Tile{};
        if (hand.phase == Phase::Claim || hand.phase == Phase::RobKong) {
            hand.pendingTile = table.turn.pendingTile;
            const bool windowAction = HasWindowAction(table);
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (seat != hand.current) {
                    hand.claimStatus[static_cast<size_t>(seat)] = seat == table.seat && windowAction ? ClaimStatus::Waiting : ClaimStatus::Passed;
                }
            }
        }
        hand.result.reset();
        if (over) {
            HandResult result;
            result.kind = table.result->kind;
            result.winner = table.result->winner;
            result.winningTile = table.result->winningTile;
            result.source = table.result->source;
            result.from = table.result->from;
            result.loose = table.result->loose;
            result.lastLiveTile = table.result->lastLiveTile;
            hand.result = result;
        } else if (hand.phase == Phase::Finished) {
            hand.result = HandResult{};
        }
        hand.lastWindow.reset();
        hand.windowSerial = 0;

        const int live = eastl::max(table.wall, 0);
        const int reserve = eastl::clamp(kSetTiles - TilesOutsideTheWall(out, table) - live - static_cast<int>(kDeadWallTiles), 0, static_cast<int>(kLooseTiles));
        hand.live.assign(static_cast<size_t>(live), Tile{});
        hand.loose.assign(static_cast<size_t>(reserve), Tile{});
        hand.dead.assign(kDeadWallTiles, Tile{});

        out.settings = hand.settings;
        out.matchOver = table.phase == SessionPhase::MatchOver;
        out.handsPlayed = table.handsPlayed;
        // After the last hand `Match::East()` is the dealer of the hand that would come next, the view's `east` the last hand's.
        out.east = out.matchOver && over ? NextProgress(MatchProgress{table.handsPlayed, table.east}, table.result->kind, table.result->winner, hand.settings).east : table.east;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const size_t index = static_cast<size_t>(seat);
            const OnlineLobbySeat& lobbySeat = lobby.seats[index];
            out.scores[index] = table.seats[index].score;
            out.seats[index] = lobbySeat.kind == BECore::SeatKind::Human ? SeatKind::Person : SeatKind::Computer;
            out.names[index].assign(lobbySeat.name);
        }
        out.levels = UniformLevels(kDefaultLevel);
        out.viewer = table.seat;
        out.actor = table.actions.empty() ? WaitedSeat(table) : table.seat;
    }

    DisplayState MakeDisplayState(const OnlineTable& table, const OnlineLobby& lobby) {
        DisplayState state;
        MakeDisplayState(table, lobby, state);
        return state;
    }

    void ShowOnlineTable(Match& match, TableSession& session, const DisplayState& state, bool newTable) {
        match.ShowRemote(state.settings, state.matchOver ? MatchPhase::Finished : MatchPhase::InHand, state.handsPlayed, state.east, state.scores, HandModel::ForDisplay(state.hand));
        session.ShowRemote(state.seats, state.levels, state.names, state.viewer, state.actor, newTable);
    }

    TableKey KeyOf(const OnlineTable& table) {
        return TableKey{true, table.handsPlayed, table.east, table.seat, table.version, table.phase};
    }

    bool IsAnotherTable(const TableKey& before, const TableKey& now) {
        if (!before.valid) {
            return true;
        }
        if (before.handsPlayed != now.handsPlayed || before.east != now.east || before.seat != now.seat || now.version < before.version) {
            return true;
        }
        return before.phase != SessionPhase::Playing && now.phase == SessionPhase::Playing;
    }

}  // namespace BECore::Mahjong
