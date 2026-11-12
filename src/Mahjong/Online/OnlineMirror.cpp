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
        return TableKey{true, table.handsPlayed, table.east, table.seat, table.version, table.phase, table.wall};
    }

    bool IsAnotherTable(const TableKey& before, const TableKey& now) {
        if (!before.valid) {
            return true;
        }
        // The settlement of the last hand counts it (`handsPlayed` moves) and leaves the same hand on show.
        const bool matchEnded = before.phase == SessionPhase::HandOver && now.phase == SessionPhase::MatchOver && before.handsPlayed + 1 == now.handsPlayed;
        if ((before.handsPlayed != now.handsPlayed && !matchEnded) || before.east != now.east || before.seat != now.seat || now.version < before.version || now.wall > before.wall) {
            return true;
        }
        return before.phase != SessionPhase::Playing && now.phase == SessionPhase::Playing;
    }

    void MoveTracker::Reset() {
        _moves.clear();
        _key = TableKey{};
    }

    void MoveTracker::Take(const OnlineTable& table, Snapshot& into) {
        into.pile.assign(table.discards.begin(), table.discards.end());
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const size_t index = static_cast<size_t>(seat);
            into.open[index].clear();
            into.concealedKongs[index] = 0;
            MeldsOf(table, seat, _melds);
            for (const Meld& meld : _melds) {
                if (meld.concealed) {
                    ++into.concealedKongs[index];
                } else {
                    into.open[index].push_back(meld);
                }
            }
            into.calling[index] = table.seats[index].calling;
        }
    }

    const eastl::vector<HandMove>& MoveTracker::Follow(const OnlineTable& table) {
        const TableKey now = KeyOf(table);
        if (IsAnotherTable(_key, now)) {
            _moves.clear();
            Take(table, _before);
        } else if (now.version != _key.version) {
            if (Explain(table)) {
                _moves.insert(_moves.end(), _step.begin(), _step.end());
            }
            _before = _now;
        }
        _key = now;
        return _moves;
    }

    bool MoveTracker::Explain(const OnlineTable& table) {
        _step.clear();
        Take(table, _now);
        const Snapshot& before = _before;
        const Snapshot& now = _now;

        // The pile: what stayed (a common prefix), the entries that left (at most the claimed discard) and the new ones.
        size_t kept = 0;
        while (kept < before.pile.size() && kept < now.pile.size() && before.pile[kept].tile == now.pile[kept].tile && before.pile[kept].seat == now.pile[kept].seat) {
            ++kept;
        }
        const size_t gone = before.pile.size() - kept;
        if (gone > 1) {
            return false;
        }

        // The melds: new ones are appended, a pung may have become a kong, none vanishes.
        struct SeatChange {
            eastl::vector<size_t> upgraded;
            size_t firstNewOpen = 0;
            int newConcealed = 0;
        };
        eastl::array<SeatChange, kSeatCount> changes;
        int claims = 0;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const size_t index = static_cast<size_t>(seat);
            const eastl::vector<Meld>& was = before.open[index];
            const eastl::vector<Meld>& is = now.open[index];
            if (is.size() < was.size() || now.concealedKongs[index] < before.concealedKongs[index]) {
                return false;
            }
            for (size_t meld = 0; meld < was.size(); ++meld) {
                if (is[meld].kind == MeldKind::Kong && was[meld].kind == MeldKind::Pung) {
                    changes[index].upgraded.push_back(meld);
                } else if (is[meld].kind != was[meld].kind || is[meld].first != was[meld].first) {
                    return false;
                }
            }
            changes[index].firstNewOpen = was.size();
            changes[index].newConcealed = now.concealedKongs[index] - before.concealedKongs[index];
            for (size_t meld = was.size(); meld < is.size(); ++meld) {
                ++claims;
            }
        }

        // The claim that took the last discard.
        std::optional<HandMove> claim;
        if (gone == 1) {
            const DiscardEntry& taken = before.pile.back();
            for (Seat seat = 0; seat < kSeatCount && !claim; ++seat) {
                const size_t index = static_cast<size_t>(seat);
                for (size_t meld = changes[index].firstNewOpen; meld < now.open[index].size(); ++meld) {
                    const Meld& laid = now.open[index][meld];
                    if (laid.fromDiscard && laid.claimed == taken.tile && laid.from == taken.seat) {
                        HandMove move;
                        move.kind = laid.kind == MeldKind::Chow ? MoveKind::ClaimChow : (laid.kind == MeldKind::Pung ? MoveKind::ClaimPung : MoveKind::ClaimKong);
                        move.seat = seat;
                        move.tile = laid.claimed;
                        move.from = laid.from;
                        move.meld = static_cast<int>(meld);
                        claim = move;
                        --claims;
                        changes[index].firstNewOpen = meld + 1;
                        break;
                    }
                }
            }
            const bool wonOnIt = table.result.has_value() && table.result->kind == ResultKind::Win && table.result->source == WinSource::Discard;
            if (!claim && !wonOnIt) {
                return false;
            }
        }
        if (claims != 0) {
            return false;
        }

        // The moves, in this order: the claim, then for each new discard the kongs of its seat before it, then the kongs that are left.
        // The melds' indices are those of the display list of the new table.
        eastl::array<eastl::vector<int>, kSeatCount> openAt;
        eastl::array<eastl::vector<int>, kSeatCount> concealedAt;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            MeldsOf(table, seat, _melds);
            for (size_t position = 0; position < _melds.size(); ++position) {
                (_melds[position].concealed ? concealedAt : openAt)[static_cast<size_t>(seat)].push_back(static_cast<int>(position));
            }
        }
        const auto indexOf = [](const eastl::vector<int>& positions, size_t number) { return number < positions.size() ? positions[number] : kNoMeld; };
        const auto emitKongs = [&](Seat seat) {
            const size_t index = static_cast<size_t>(seat);
            SeatChange& change = changes[index];
            for (const size_t meld : change.upgraded) {
                HandMove move;
                move.kind = MoveKind::UpgradeKong;
                move.seat = seat;
                move.tile = now.open[index][meld].first;
                move.meld = indexOf(openAt[index], meld);
                _step.push_back(move);
            }
            change.upgraded.clear();
            const size_t firstConcealed = concealedAt[index].size() - static_cast<size_t>(change.newConcealed);
            for (int kong = 0; kong < change.newConcealed; ++kong) {
                HandMove move;
                move.kind = MoveKind::ConcealedKong;
                move.seat = seat;
                move.meld = indexOf(concealedAt[index], firstConcealed + static_cast<size_t>(kong));
                _step.push_back(move);
            }
            change.newConcealed = 0;
        };
        // New open melds that are not the claim of the last discard cannot come from a claim: the checks above refused them.
        // The claim found its meld among the open ones; its position in the list is another number when an own concealed kong lies before it.
        if (claim) {
            claim->meld = indexOf(openAt[static_cast<size_t>(claim->seat)], static_cast<size_t>(claim->meld));
            _step.push_back(*claim);
        }
        // A declaration turned on in this step belongs to the discard of its seat; with two discards of one seat in the step it cannot
        // be told which, so none is marked.
        eastl::array<int, kSeatCount> discardsHere{};
        for (size_t entry = kept; entry < now.pile.size(); ++entry) {
            if (IsSeat(now.pile[entry].seat)) {
                ++discardsHere[static_cast<size_t>(now.pile[entry].seat)];
            }
        }
        // A kong comes before the discard that ends its turn. The seat whose turn is still on (or that won with the kong) has made no
        // discard after it: it can have none in the step; any other seat has exactly one. With more, the kong may lie before any of
        // them (the step is several turns of the seat) and nothing is said.
        const Seat midTurn = table.result.has_value() ? table.result->winner : (table.turn.phase == Phase::Discard ? table.turn.current : kNoSeat);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const size_t index = static_cast<size_t>(seat);
            if (!changes[index].upgraded.empty() || changes[index].newConcealed > 0) {
                if (discardsHere[index] != (seat == midTurn ? 0 : 1)) {
                    return false;
                }
            }
        }
        for (size_t entry = kept; entry < now.pile.size(); ++entry) {
            const Seat seat = now.pile[entry].seat;
            if (!IsSeat(seat)) {
                return false;
            }
            const size_t index = static_cast<size_t>(seat);
            emitKongs(seat);
            HandMove move;
            move.kind = MoveKind::Discard;
            move.seat = seat;
            move.tile = now.pile[entry].tile;
            move.declaredCalling = now.calling[index] && !before.calling[index] && discardsHere[index] == 1;
            _step.push_back(move);
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            emitKongs(seat);
        }
        return true;
    }

}  // namespace BECore::Mahjong
