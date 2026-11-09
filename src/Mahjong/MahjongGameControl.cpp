#include "MahjongGameControl.h"

#include <Mahjong/MahjongGame.h>

namespace BECore::Mahjong {

    namespace {

        constexpr size_t kSeatLetterCount = kSeatCount;
        constexpr char kPersonLetter = 'P';
        constexpr char kComputerLetter = 'C';
        constexpr int kChowTiles = 3;
        constexpr int kPungTiles = 3;
        constexpr int kKongTiles = 4;

        constexpr eastl::string_view kTypeDiscard = "discard";
        constexpr eastl::string_view kTypeWin = "win";
        constexpr eastl::string_view kTypeConcealedKong = "concealed_kong";
        constexpr eastl::string_view kTypeUpgradeKong = "upgrade_kong";
        constexpr eastl::string_view kTypeClaim = "claim";
        constexpr eastl::string_view kTypePass = "pass";
        constexpr eastl::string_view kTypeNextHand = "next_hand";

        std::unexpected<ControlError> InvalidParams(eastl::string message) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, std::move(message)));
        }

        std::unexpected<ControlError> NoMatchStarted() {
            return std::unexpected(MakeControlError(GameControlErrors::kNoActiveModel, "no match has been started"));
        }

        std::unexpected<ControlError> IllegalAction(eastl::string message) {
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, std::move(message)));
        }

        eastl::string_view View(std::string_view text) {
            return eastl::string_view(text.data(), text.size());
        }

        // ----- the names of the enums in JSON -----

        eastl::string_view PhaseName(SessionPhase phase) {
            switch (phase) {
                case SessionPhase::Playing:
                    return "playing";
                case SessionPhase::HandOver:
                    return "hand_over";
                case SessionPhase::MatchOver:
                    return "match_over";
            }
            return "playing";
        }

        eastl::string_view TurnPhaseName(Phase phase) {
            switch (phase) {
                case Phase::Discard:
                    return "discard";
                case Phase::Claim:
                    return "claim";
                case Phase::RobKong:
                    return "rob_kong";
                case Phase::Finished:
                    return "finished";
            }
            return "finished";
        }

        eastl::string_view MeldKindName(MeldKind kind) {
            switch (kind) {
                case MeldKind::Chow:
                    return "chow";
                case MeldKind::Pung:
                    return "pung";
                case MeldKind::Kong:
                    return "kong";
            }
            return "pung";
        }

        eastl::string_view ClaimName(ClaimKind kind) {
            switch (kind) {
                case ClaimKind::Chow:
                    return "chow";
                case ClaimKind::Pung:
                    return "pung";
                case ClaimKind::Kong:
                    return "kong";
                case ClaimKind::Mahjong:
                    return "mahjong";
            }
            return "chow";
        }

        eastl::string_view ResultKindName(ResultKind kind) {
            return kind == ResultKind::Win ? "win" : "drawn";
        }

        eastl::string_view SourceName(WinSource source) {
            switch (source) {
                case WinSource::Discard:
                    return "discard";
                case WinSource::SelfDraw:
                    return "self_draw";
                case WinSource::RobbedKong:
                    return "robbed_kong";
            }
            return "self_draw";
        }

        /// The kind of a claim named in JSON, nullopt for another text.
        std::optional<ClaimKind> ClaimFromName(eastl::string_view name) {
            for (const ClaimKind kind : {ClaimKind::Chow, ClaimKind::Pung, ClaimKind::Kong, ClaimKind::Mahjong}) {
                if (name == ClaimName(kind)) {
                    return kind;
                }
            }
            return std::nullopt;
        }

        // ----- the pieces of the state -----

        /// The tiles of a meld as they lie: three in sequence for a chow, three or four equal ones.
        Json MeldTilesToJson(const Meld& meld) {
            Json tiles = Json::array();
            if (meld.kind == MeldKind::Chow) {
                for (int offset = 0; offset < kChowTiles; ++offset) {
                    tiles.push_back(TileToJson(Tile::Suited(meld.first.Suit(), meld.first.Rank() + offset)));
                }
                return tiles;
            }
            const int count = meld.kind == MeldKind::Kong ? kKongTiles : kPungTiles;
            for (int index = 0; index < count; ++index) {
                tiles.push_back(TileToJson(meld.first));
            }
            return tiles;
        }

        Json MeldToJson(const Meld& meld) {
            Json json = Json::object();
            json["kind"] = MakeJsonString(MeldKindName(meld.kind));
            json["tiles"] = MeldTilesToJson(meld);
            json["concealed"] = meld.concealed;
            json["from"] = meld.concealed ? Json(nullptr) : SeatOrNull(meld.from);
            json["fromDiscard"] = meld.fromDiscard;
            json["claimed"] = meld.concealed ? Json(nullptr) : TileToJson(meld.claimed);
            return json;
        }

        Json MeldsToJson(const eastl::vector<Meld>& melds) {
            Json list = Json::array();
            for (const Meld& meld : melds) {
                list.push_back(MeldToJson(meld));
            }
            return list;
        }

        Json LineToJson(const ScoreLine& line) {
            Json json = Json::object();
            json["kind"] = MakeJsonString(EnumUtils<ScoreKind>::ToString(line.kind));
            json["tile"] = TileToJson(line.tile);
            json["points"] = line.points;
            json["doubles"] = line.doubles;
            return json;
        }

        /// What a finished hand came to, all four hands open: see `MahjongGameControl`.
        Json ResultToJson(const HandModel& hand) {
            const HandSettlement settlement = SettleHand(hand);
            Json json = Json::object();
            const HandResult result = hand.Result().value_or(HandResult{});
            json["kind"] = MakeJsonString(ResultKindName(result.kind));
            json["winner"] = SeatOrNull(settlement.winner);
            const bool won = result.kind == ResultKind::Win;
            json["winningTile"] = won ? TileToJson(result.winningTile) : Json(nullptr);
            json["source"] = won ? MakeJsonString(SourceName(result.source)) : Json(nullptr);
            json["from"] = SeatOrNull(result.from);
            json["loose"] = result.loose;
            json["lastLiveTile"] = result.lastLiveTile;

            Json seats = Json::array();
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                const PlayerHand& tiles = hand.GetHand(seat);
                Json entry = Json::object();
                entry["concealed"] = TilesToJson(tiles.concealed);
                entry["melds"] = MeldsToJson(tiles.melds);
                entry["bonus"] = TilesToJson(tiles.bonus);
                entry["breakdown"] = BreakdownToJson(settlement.breakdowns[static_cast<size_t>(seat)]);
                seats.push_back(std::move(entry));
            }
            json["seats"] = std::move(seats);
            json["scores"] = SeatScoresToJson(settlement.scores);
            json["deltas"] = SeatScoresToJson(settlement.deltas);
            return json;
        }

        /// The settlement `next_hand` answers: the outcome and the money, not the breakdowns (they were in `result`).
        Json SettlementToJson(const HandSettlement& settlement) {
            Json json = Json::object();
            json["kind"] = MakeJsonString(ResultKindName(settlement.kind));
            json["winner"] = SeatOrNull(settlement.winner);
            json["scores"] = SeatScoresToJson(settlement.scores);
            json["deltas"] = SeatScoresToJson(settlement.deltas);
            return json;
        }

        /// The seat the client sees through: the person who has to act, else the first person, else kNoSeat (a spectator).
        Seat ViewSeat(const TableSession& session) {
            if (session.ActorIsPerson()) {
                return session.Actor();
            }
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                if (session.KindOf(seat) == SeatKind::Person) {
                    return seat;
                }
            }
            return kNoSeat;
        }

        Json SeatToJson(const TableView& view, Seat viewSeat, const MahjongGame& game, Seat seat) {
            const HandModel& hand = game.GetMatch().Hand();
            const PublicSeat& open = view.seats[static_cast<size_t>(seat)];
            Json json = Json::object();
            json["kind"] = MakeJsonString(game.GetSession().KindOf(seat) == SeatKind::Person ? "person" : "computer");
            json["wind"] = MakeJsonString(EnumUtils<Wind>::ToString(hand.SeatWind(seat)));
            json["score"] = game.GetMatch().Scores()[static_cast<size_t>(seat)];
            json["concealedCount"] = hand.GetHand(seat).concealed.size();
            json["melds"] = MeldsToJson(seat == viewSeat ? view.own.melds : open.openMelds);
            json["concealedKongs"] = open.concealedKongs;
            json["bonus"] = TilesToJson(open.bonus);
            json["discardCount"] = open.discardCount;
            json["calling"] = open.callingDeclared;
            return json;
        }

        // ----- reading what a client sent -----

        std::expected<Tile, ControlError> ReadTile(const Json& object, eastl::string_view key) {
            const auto text = GetJsonString(object, key);
            if (!text) {
                return InvalidParams(Format("'{}' has to be a string with a tile such as \"5p\", \"Ew\", \"Rd\"", key));
            }
            const std::optional<Tile> tile = TryParse(std::string_view(text->data(), text->size()));
            if (!tile) {
                return InvalidParams(Format("'{}' is not a tile: \"{}\"", key, text->c_str()));
            }
            return *tile;
        }

    }  // namespace

    Json TileToJson(Tile tile) {
        return tile.IsValid() ? MakeJsonString(View(ToString(tile))) : Json(nullptr);
    }

    Json SeatOrNull(Seat seat) {
        return IsSeat(seat) ? Json(seat) : Json(nullptr);
    }

    Json TilesToJson(const eastl::vector<Tile>& tiles) {
        Json list = Json::array();
        for (const Tile tile : tiles) {
            list.push_back(TileToJson(tile));
        }
        return list;
    }

    Json SeatScoresToJson(const SeatScores& scores) {
        Json list = Json::array();
        for (const int value : scores) {
            list.push_back(value);
        }
        return list;
    }

    Json BreakdownToJson(const ScoreBreakdown& breakdown) {
        Json lines = Json::array();
        for (const ScoreLine& line : breakdown.lines) {
            lines.push_back(LineToJson(line));
        }
        Json json = Json::object();
        json["won"] = breakdown.won;
        json["lines"] = std::move(lines);
        json["points"] = breakdown.points;
        json["doubles"] = breakdown.doubles;
        json["raw"] = breakdown.raw;
        json["score"] = breakdown.score;
        json["limitHand"] = breakdown.limitHand ? MakeJsonString(EnumUtils<ScoreKind>::ToString(*breakdown.limitHand)) : Json(nullptr);
        return json;
    }

    /// The seat setup of a `seats` string such as "PCCC", nullopt unless it is four letters P or C.
    std::optional<SeatSetup> ReadSeats(const eastl::string& letters) {
        if (letters.size() != kSeatLetterCount) {
            return std::nullopt;
        }
        SeatSetup seats = kDefaultSeatSetup;
        for (size_t seat = 0; seat < kSeatLetterCount; ++seat) {
            if (letters[seat] != kPersonLetter && letters[seat] != kComputerLetter) {
                return std::nullopt;
            }
            seats[seat] = letters[seat] == kPersonLetter ? SeatKind::Person : SeatKind::Computer;
        }
        return seats;
    }

    eastl::string SeatsToLetters(const SeatSetup& seats) {
        eastl::string letters;
        for (const SeatKind kind : seats) {
            letters.push_back(kind == SeatKind::Person ? kPersonLetter : kComputerLetter);
        }
        return letters;
    }

    Json ActionToJson(const HandAction& action) {
        Json json = Json::object();
        switch (action.kind) {
            case ActionKind::Discard:
                json["type"] = MakeJsonString(kTypeDiscard);
                json["tile"] = TileToJson(action.tile);
                json["declareCalling"] = action.declareCalling;
                break;
            case ActionKind::DeclareWin:
                json["type"] = MakeJsonString(kTypeWin);
                break;
            case ActionKind::ConcealedKong:
                json["type"] = MakeJsonString(kTypeConcealedKong);
                json["tile"] = TileToJson(action.tile);
                break;
            case ActionKind::UpgradeKong:
                json["type"] = MakeJsonString(kTypeUpgradeKong);
                json["tile"] = TileToJson(action.tile);
                break;
            case ActionKind::Claim:
                json["type"] = MakeJsonString(kTypeClaim);
                json["claim"] = MakeJsonString(ClaimName(action.claim));
                if (action.claim == ClaimKind::Chow) {
                    json["first"] = TileToJson(action.chowFirst);
                }
                break;
            case ActionKind::Pass:
                json["type"] = MakeJsonString(kTypePass);
                break;
        }
        return json;
    }

    Json NextHandActionToJson() {
        Json json = Json::object();
        json["type"] = MakeJsonString(kTypeNextHand);
        return json;
    }

    std::expected<ControlAction, ControlError> ActionFromJson(const Json& json) {
        const auto type = GetJsonString(json, "type");
        if (!type) {
            return InvalidParams("'type' has to be a string: discard, win, concealed_kong, upgrade_kong, claim, pass or next_hand");
        }
        const eastl::string_view name(type->c_str(), type->size());
        ControlAction action;

        if (name == kTypeNextHand) {
            action.nextHand = true;
        } else if (name == kTypeWin) {
            action.hand = HandAction::MakeDeclareWin();
        } else if (name == kTypePass) {
            action.hand = HandAction::MakePass();
        } else if (name == kTypeDiscard) {
            const auto tile = ReadTile(json, "tile");
            if (!tile) {
                return std::unexpected(tile.error());
            }
            bool declare = false;
            if (FindJsonMember(json, "declareCalling") != nullptr) {
                const auto declared = GetJsonBool(json, "declareCalling");
                if (!declared) {
                    return InvalidParams("'declareCalling' has to be true or false");
                }
                declare = *declared;
            }
            action.hand = HandAction::MakeDiscard(*tile, declare);
        } else if (name == kTypeConcealedKong || name == kTypeUpgradeKong) {
            const auto tile = ReadTile(json, "tile");
            if (!tile) {
                return std::unexpected(tile.error());
            }
            action.hand = name == kTypeConcealedKong ? HandAction::MakeConcealedKong(*tile) : HandAction::MakeUpgradeKong(*tile);
        } else if (name == kTypeClaim) {
            const auto claimName = GetJsonString(json, "claim");
            const std::optional<ClaimKind> claim = claimName ? ClaimFromName(eastl::string_view(claimName->c_str(), claimName->size())) : std::nullopt;
            if (!claim) {
                return InvalidParams("'claim' has to be \"chow\", \"pung\", \"kong\" or \"mahjong\"");
            }
            Tile first;
            if (*claim == ClaimKind::Chow) {
                const auto tile = ReadTile(json, "first");
                if (!tile) {
                    return std::unexpected(tile.error());
                }
                first = *tile;
            }
            action.hand = HandAction::MakeClaim(*claim, first);
        } else {
            return InvalidParams(Format("unknown action type '{}'", name));
        }
        return action;
    }

    std::expected<Json, ControlError> MahjongGameControl::GetState() const {
        const Match& match = _game.GetMatch();
        if (match.GetPhase() == MatchPhase::NotStarted) {
            return NoMatchStarted();
        }
        const TableSession& session = _game.GetSession();
        const HandModel& hand = match.Hand();
        const SessionPhase phase = session.GetPhase();
        // A spectator (no person seat) sees the public table and no own tiles: the view of seat 0 is made only for the numbers every
        // seat sees (and the hands are in `result` once the hand is over).
        const Seat seat = ViewSeat(session);
        const bool spectator = seat == kNoSeat;
        const TableView view = MakeTableView(hand, spectator ? 0 : seat);

        Json turn = Json::object();
        turn["phase"] = MakeJsonString(TurnPhaseName(view.phase));
        turn["current"] = view.phase == Phase::Finished ? Json(nullptr) : SeatOrNull(view.currentSeat);
        turn["pendingTile"] = TileToJson(view.pendingTile);
        turn["yourTurn"] = !spectator && !view.legal.empty();

        Json seats = Json::array();
        for (Seat other = 0; other < kSeatCount; ++other) {
            seats.push_back(SeatToJson(view, seat, _game, other));
        }

        Json discards = Json::array();
        for (const DiscardEntry& entry : hand.Discards()) {
            Json item = Json::object();
            item["tile"] = TileToJson(entry.tile);
            item["seat"] = entry.seat;
            discards.push_back(std::move(item));
        }

        Json state = Json::object();
        state["phase"] = MakeJsonString(PhaseName(phase));
        state["seat"] = SeatOrNull(seat);
        state["seatWind"] = spectator ? Json(nullptr) : MakeJsonString(EnumUtils<Wind>::ToString(view.seatWind));
        state["roundWind"] = MakeJsonString(EnumUtils<Wind>::ToString(view.roundWind));
        state["east"] = hand.East();
        state["hand"] = spectator ? Json::array() : TilesToJson(view.own.concealed);
        state["turn"] = std::move(turn);
        state["seats"] = std::move(seats);
        state["discards"] = std::move(discards);
        state["wall"] = view.liveCount;
        state["handsPlayed"] = match.HandsPlayed();
        state["handsTotal"] = match.HandsTotal();
        state["version"] = session.Version();
        if (phase != SessionPhase::Playing) {
            state["result"] = ResultToJson(hand);
        }
        return state;
    }

    std::expected<Json, ControlError> MahjongGameControl::GetActions() const {
        const Match& match = _game.GetMatch();
        if (match.GetPhase() == MatchPhase::NotStarted) {
            return NoMatchStarted();
        }
        const TableSession& session = _game.GetSession();
        Json actions = Json::array();
        switch (session.GetPhase()) {
            case SessionPhase::Playing:
                if (session.ActorIsPerson()) {
                    for (const HandAction& action : match.Hand().LegalActions(session.Actor())) {
                        actions.push_back(ActionToJson(action));
                    }
                }
                break;
            case SessionPhase::HandOver:
                actions.push_back(NextHandActionToJson());
                break;
            case SessionPhase::MatchOver:
                break;
        }
        return actions;
    }

    std::expected<Json, ControlError> MahjongGameControl::PerformAction(const Json& action) {
        if (_game.GetMatch().GetPhase() == MatchPhase::NotStarted) {
            return NoMatchStarted();
        }
        const std::expected<ControlAction, ControlError> parsed = ActionFromJson(action);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        TableSession& session = _game.GetSession();
        Json outcome = Json::object();
        if (parsed->nextHand) {
            if (session.GetPhase() != SessionPhase::HandOver) {
                return IllegalAction("next_hand is possible only when the hand is over");
            }
            const std::expected<HandSettlement, MatchError> settled = session.NextHand();
            if (!settled) {
                return IllegalAction(Format("the hand cannot be settled: {}", EnumUtils<MatchError>::ToString(settled.error())));
            }
            outcome["applied"] = NextHandActionToJson();
            outcome["settlement"] = SettlementToJson(*settled);
        } else {
            if (session.GetPhase() != SessionPhase::Playing) {
                return IllegalAction("the hand is over: next_hand or reset is possible, nothing else");
            }
            if (!session.ActorIsPerson()) {
                return IllegalAction("it is not a person's turn");
            }
            const eastl::vector<HandAction> legal = _game.GetMatch().Hand().LegalActions(session.Actor());
            if (eastl::find(legal.begin(), legal.end(), parsed->hand) == legal.end()) {
                return IllegalAction("the action is not among the legal actions now (see list_actions)");
            }
            if (const HandStatus done = session.PerformPerson(parsed->hand); !done) {
                return IllegalAction(Format("the action was refused: {}", EnumUtils<HandError>::ToString(done.error())));
            }
            outcome["applied"] = ActionToJson(parsed->hand);
        }

        session.RunComputers();
        _game.CloseSetup();  // a controller that plays is in the game, not in its seat choice
        outcome["phase"] = MakeJsonString(PhaseName(session.GetPhase()));
        outcome["version"] = session.Version();
        return outcome;
    }

    std::expected<void, ControlError> MahjongGameControl::Reset(const Json& options) {
        uint32_t seed = _lastSeed;
        if (FindJsonMember(options, "seed") != nullptr) {
            const auto requested = GetJsonInt(options, "seed");
            if (!requested || *requested < 0 || *requested > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
                return InvalidParams("'seed' has to be an integer from 0 to 4294967295");
            }
            seed = static_cast<uint32_t>(*requested);
        }

        SeatSetup seats = _game.GetSeats();
        if (FindJsonMember(options, "seats") != nullptr) {
            const auto letters = GetJsonString(options, "seats");
            const std::optional<SeatSetup> read = letters ? ReadSeats(*letters) : std::nullopt;
            if (!read) {
                return InvalidParams("'seats' has to be four letters, P for a person and C for a computer, such as \"PCCC\"");
            }
            seats = *read;
        }

        MahjongSettings settings = _game.GetSettings();
        if (FindJsonMember(options, "hands") != nullptr) {
            const auto hands = GetJsonInt(options, "hands");
            if (!hands || (*hands != kShortMatchHands && *hands != kFullMatchHands)) {
                return InvalidParams(Format("'hands' has to be {} or {}", kShortMatchHands, kFullMatchHands));
            }
            settings.handsPerMatch = static_cast<int>(*hands);
        }
        if (FindJsonMember(options, "flowers") != nullptr) {
            const auto flowers = GetJsonBool(options, "flowers");
            if (!flowers) {
                return InvalidParams("'flowers' has to be true or false");
            }
            settings.flowers = *flowers;
        }
        if (!IsPlayable(settings)) {
            return InvalidParams("the settings of the game cannot be played");
        }

        // Every option is read and checked: from here on the match starts and nothing is refused.
        _game.SetSettings(settings);
        _game.SetSeats(seats);
        if (const MatchStatus started = _game.StartMatch(seed); !started) {
            return InvalidParams(Format("the match did not start: {}", EnumUtils<MatchError>::ToString(started.error())));
        }
        _lastSeed = seed;
        _game.GetSession().RunComputers();  // StartMatch closed the seat choice: a reset is a controller entering the game
        return {};
    }

    uint64_t MahjongGameControl::GetStateVersion() const {
        return _game.GetSession().Version();
    }

}  // namespace BECore::Mahjong
