#include "MahjongGameControl.h"

#include <BECore/Control/Json.h>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/MahjongStateJson.h>

namespace BECore::Mahjong {

    namespace {

        constexpr size_t kSeatLetterCount = kSeatCount;
        constexpr char kPersonLetter = 'P';
        constexpr char kComputerLetter = 'C';

        constexpr StringView kTypeDiscard = "discard";
        constexpr StringView kTypeWin = "win";
        constexpr StringView kTypeConcealedKong = "concealed_kong";
        constexpr StringView kTypeUpgradeKong = "upgrade_kong";
        constexpr StringView kTypeClaim = "claim";
        constexpr StringView kTypePass = "pass";
        constexpr StringView kTypeNextHand = "next_hand";

        std::unexpected<ControlError> InvalidParams(String message) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, std::move(message)));
        }

        std::unexpected<ControlError> NoMatchStarted() {
            return std::unexpected(MakeControlError(GameControlErrors::kNoActiveModel, "no match has been started"));
        }

        std::unexpected<ControlError> InARoom() {
            return std::unexpected(MakeControlError(GameControlErrors::kNoActiveModel, "the game is playing in an online room"));
        }

        std::unexpected<ControlError> IllegalAction(String message) {
            return std::unexpected(MakeControlError(GameControlErrors::kIllegalAction, std::move(message)));
        }

        StringView View(StringView text) {
            return text;
        }

        /// The `levels` of a `reset`: an array of four integers 1..10; nullopt for anything else (a number above INT64_MAX included, as
        /// `GetJsonInt` refuses it).
        Optional<SeatLevels> ReadResetLevels(const Json& list) {
            if (!list.is_array() || list.size() != kSeatCount) {
                return std::nullopt;
            }
            SeatLevels levels = UniformLevels(kDefaultLevel);
            size_t index = 0;
            for (const Json& item : list) {
                if (!item.is_number_integer()) {
                    return std::nullopt;
                }
                int64_t number = 0;
                if (item.is_number_unsigned()) {
                    const uint64_t value = item.get<uint64_t>();
                    if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                        return std::nullopt;
                    }
                    number = static_cast<int64_t>(value);
                } else {
                    number = item.get<int64_t>();
                }
                const Optional<ComputerLevel> level = LevelOf(number);
                if (!level) {
                    return std::nullopt;
                }
                levels[index++] = *level;
            }
            return levels;
        }

        // ----- the names of the enums in JSON -----

        StringView ClaimName(ClaimKind kind) {
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

        /// The kind of a claim named in JSON, nullopt for another text.
        Optional<ClaimKind> ClaimFromName(StringView name) {
            for (const ClaimKind kind : {ClaimKind::Chow, ClaimKind::Pung, ClaimKind::Kong, ClaimKind::Mahjong}) {
                if (name == ClaimName(kind)) {
                    return kind;
                }
            }
            return std::nullopt;
        }

        // ----- the pieces of the state -----

        Json LineToJson(const ScoreLine& line) {
            Json json = Json::object();
            json["kind"] = MakeJsonString(EnumUtils<ScoreKind>::ToString(line.kind));
            json["tile"] = TileToJson(line.tile);
            json["points"] = line.points;
            json["doubles"] = line.doubles;
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

        // ----- reading what a client sent -----

        std::expected<Tile, ControlError> ReadTile(const Json& object, StringView key) {
            const auto text = GetJsonString(object, key);
            if (!text) {
                return InvalidParams(Format("'{}' has to be a string with a tile such as \"5p\", \"Ew\", \"Rd\"", key));
            }
            const Optional<Tile> tile = TryParse(StringView(text->data(), text->size()));
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

    Json TilesToJson(const Vector<Tile>& tiles) {
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
    Optional<SeatSetup> ReadSeats(const String& letters) {
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

    String SeatsToLetters(const SeatSetup& seats) {
        String letters;
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
        const StringView name(type->c_str(), type->size());
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
            const Optional<ClaimKind> claim = claimName ? ClaimFromName(StringView(claimName->c_str(), claimName->size())) : std::nullopt;
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
        if (_game.IsOnlineTable()) {
            return InARoom();
        }
        const Match& match = _game.GetLocalMatch();
        if (match.GetPhase() == MatchPhase::NotStarted) {
            return NoMatchStarted();
        }
        const TableSession& session = _game.GetLocalSession();
        // A spectator (no person seat) sees the public table and no own tiles: the view of seat 0 is made only for the numbers every
        // seat sees (and the hands are in `result` once the hand is over).
        const Seat seat = ViewSeat(session);
        const TableView view = MakeTableView(match.Hand(), seat == kNoSeat ? 0 : seat);

        StateJsonContext context{match};
        context.phase = session.GetPhase();
        context.version = session.Version();
        context.viewSeat = seat;
        context.kinds = &session.Seats();
        context.levels = &session.Levels();
        Json state = TableStateToJson(context, view);
        state["theme"] = MakeJsonString(_game.GetTheme().ToStringView());  // this machine's choice: not in the online view that `TableStateToJson` shares
        return state;
    }

    std::expected<Json, ControlError> MahjongGameControl::GetActions() const {
        if (_game.IsOnlineTable()) {
            return InARoom();
        }
        const Match& match = _game.GetLocalMatch();
        if (match.GetPhase() == MatchPhase::NotStarted) {
            return NoMatchStarted();
        }
        const TableSession& session = _game.GetLocalSession();
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
        if (_game.IsOnlineTable()) {
            return InARoom();
        }
        if (_game.GetLocalMatch().GetPhase() == MatchPhase::NotStarted) {
            return NoMatchStarted();
        }
        const std::expected<ControlAction, ControlError> parsed = ActionFromJson(action);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }

        TableSession& session = _game.GetLocalSession();
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
            const Vector<HandAction> legal = _game.GetLocalMatch().Hand().LegalActions(session.Actor());
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
        _game.CloseRules();  // nor in the pages of the rules
        outcome["phase"] = MakeJsonString(PhaseName(session.GetPhase()));
        outcome["version"] = session.Version();
        return outcome;
    }

    std::expected<void, ControlError> MahjongGameControl::Reset(const Json& options) {
        if (_game.IsOnlineTable()) {
            return InARoom();
        }
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
            const Optional<SeatSetup> read = letters ? ReadSeats(*letters) : std::nullopt;
            if (!read) {
                return InvalidParams("'seats' has to be four letters, P for a person and C for a computer, such as \"PCCC\"");
            }
            seats = *read;
        }

        SeatLevels levels = _game.GetLevels();
        const Json* levelsMember = FindJsonMember(options, "levels");
        if (levelsMember != nullptr) {
            if (FindJsonMember(options, "level") != nullptr) {
                return InvalidParams("'level' and 'levels' cannot be given together");
            }
            const Optional<SeatLevels> read = ReadResetLevels(*levelsMember);
            if (!read) {
                return InvalidParams(Format("'levels' has to be an array of {} integers, each {}", kSeatCount, LevelRangeText().c_str()));
            }
            levels = *read;
        } else if (FindJsonMember(options, "level") != nullptr) {
            const auto number = GetJsonInt(options, "level");
            const Optional<ComputerLevel> read = number ? LevelOf(*number) : std::nullopt;
            if (!read) {
                return InvalidParams(Format("'level' has to be {}", LevelRangeText().c_str()));
            }
            levels = UniformLevels(*read);
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
        _game.SetLevels(levels);
        if (const MatchStatus started = _game.StartMatch(seed); !started) {
            return InvalidParams(Format("the match did not start: {}", EnumUtils<MatchError>::ToString(started.error())));
        }
        _lastSeed = seed;
        _game.GetLocalSession().RunComputers();  // StartMatch closed the seat choice: a reset is a controller entering the game
        return {};
    }

    uint64_t MahjongGameControl::GetStateVersion() const {
        return _game.GetLocalSession().Version();
    }

}  // namespace BECore::Mahjong
