#include "MatchLogJson.h"

#include <Mahjong/MahjongGameControl.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kEventStart = "start";
        constexpr eastl::string_view kEventHand = "hand";
        constexpr eastl::string_view kEventAction = "action";
        constexpr eastl::string_view kEventSettlement = "settlement";
        constexpr eastl::string_view kEventEnd = "end";
        constexpr eastl::string_view kByPerson = "person";
        constexpr eastl::string_view kByComputer = "computer";
        constexpr eastl::string_view kKindWin = "win";
        constexpr eastl::string_view kKindDrawn = "drawn";

        eastl::string_view EventName(MatchLogKind kind) {
            switch (kind) {
                case MatchLogKind::Start:
                    return kEventStart;
                case MatchLogKind::Hand:
                    return kEventHand;
                case MatchLogKind::Action:
                    return kEventAction;
                case MatchLogKind::Settlement:
                    return kEventSettlement;
                case MatchLogKind::End:
                    return kEventEnd;
            }
            return kEventStart;
        }

        Json WindsToJson(const SeatWinds& winds) {
            Json list = Json::array();
            for (const Wind wind : winds) {
                list.push_back(MakeJsonString(EnumUtils<Wind>::ToString(wind)));
            }
            return list;
        }

        Json SeatsInOrderToJson(const eastl::array<Seat, kSeatCount>& seats) {
            Json list = Json::array();
            for (const Seat seat : seats) {
                list.push_back(seat);
            }
            return list;
        }

        Json SettingsToJson(const MahjongSettings& settings) {
            Json json = Json::object();
            json["handsPerMatch"] = settings.handsPerMatch;
            json["flowers"] = settings.flowers;
            json["replayDrawnHand"] = settings.replayDrawnHand;
            json["limit"] = settings.limit;
            json["startingScore"] = settings.startingScore;
            return json;
        }

        // ----- reading: every read is checked, the error names the member -----

        using Failure = std::unexpected<eastl::string>;

        Failure Fail(eastl::string_view member, eastl::string_view what) {
            return std::unexpected(Format("{}: {}", member, what));
        }

        eastl::string_view EaView(const eastl::string& text) {
            return eastl::string_view(text.c_str(), text.size());
        }

        /// An integer member within [low, high].
        std::expected<int64_t, eastl::string> ReadInt(const Json& object, eastl::string_view key, int64_t low, int64_t high) {
            const std::optional<int64_t> value = GetJsonInt(object, key);
            if (!value) {
                return Fail(key, "has to be an integer");
            }
            if (*value < low || *value > high) {
                return Fail(key, Format("is out of range ({}..{})", low, high).c_str());
            }
            return *value;
        }

        std::expected<int, eastl::string> ReadIntMember(const Json& object, eastl::string_view key) {
            const auto value = ReadInt(object, key, std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
            if (!value) {
                return std::unexpected(value.error());
            }
            return static_cast<int>(*value);
        }

        std::expected<bool, eastl::string> ReadBool(const Json& object, eastl::string_view key) {
            const std::optional<bool> value = GetJsonBool(object, key);
            if (!value) {
                return Fail(key, "has to be true or false");
            }
            return *value;
        }

        std::expected<eastl::string, eastl::string> ReadString(const Json& object, eastl::string_view key) {
            const std::optional<eastl::string> value = GetJsonString(object, key);
            if (!value) {
                return Fail(key, "has to be a string");
            }
            return *value;
        }

        std::expected<Seat, eastl::string> ReadSeat(const Json& object, eastl::string_view key) {
            const auto value = ReadInt(object, key, 0, kSeatCount - 1);
            if (!value) {
                return std::unexpected(value.error());
            }
            return static_cast<Seat>(*value);
        }

        /// A seat, or null for no seat (`kNoSeat`).
        std::expected<Seat, eastl::string> ReadSeatOrNull(const Json& object, eastl::string_view key) {
            const Json* member = FindJsonMember(object, key);
            if (member != nullptr && member->is_null()) {
                return kNoSeat;
            }
            return ReadSeat(object, key);
        }

        std::expected<Wind, eastl::string> ReadWindText(const eastl::string& text, eastl::string_view member) {
            const std::optional<Wind> wind = EnumUtils<Wind>::Cast(EaView(text));
            if (!wind) {
                return Fail(member, Format("is not a wind: \"{}\"", text).c_str());
            }
            return *wind;
        }

        std::expected<Wind, eastl::string> ReadWind(const Json& object, eastl::string_view key) {
            const auto text = ReadString(object, key);
            if (!text) {
                return std::unexpected(text.error());
            }
            return ReadWindText(*text, key);
        }

        /// The array member `key`, or the error.
        std::expected<const Json*, eastl::string> ReadArray(const Json& object, eastl::string_view key, size_t size) {
            const Json* member = FindJsonMember(object, key);
            if (member == nullptr || !member->is_array()) {
                return Fail(key, "has to be an array");
            }
            if (member->size() != size) {
                return Fail(key, Format("has to have {} entries", size).c_str());
            }
            return member;
        }

        /// A tile from the string at `item`, `member` names it in the error.
        std::expected<Tile, eastl::string> ReadTileItem(const Json& item, eastl::string_view member) {
            if (!item.is_string()) {
                return Fail(member, "has to hold tiles as strings");
            }
            const std::string& text = item.get_ref<const std::string&>();
            const std::optional<Tile> tile = TryParse(text);
            if (!tile) {
                return Fail(member, Format("is not a tile: \"{}\"", text).c_str());
            }
            return *tile;
        }

        std::expected<eastl::vector<Tile>, eastl::string> ReadTiles(const Json& object, eastl::string_view key) {
            const Json* member = FindJsonMember(object, key);
            if (member == nullptr || !member->is_array()) {
                return Fail(key, "has to be an array of tiles");
            }
            eastl::vector<Tile> tiles;
            for (const Json& item : *member) {
                const auto tile = ReadTileItem(item, key);
                if (!tile) {
                    return std::unexpected(tile.error());
                }
                tiles.push_back(*tile);
            }
            return tiles;
        }

        /// An array item as an integer, nullopt for anything else; a number above INT64_MAX is refused like `GetJsonInt` does.
        std::optional<int64_t> ReadIntItem(const Json& item) {
            if (!item.is_number_integer()) {
                return std::nullopt;
            }
            if (item.is_number_unsigned()) {
                const uint64_t value = item.get<uint64_t>();
                if (value > static_cast<uint64_t>(std::numeric_limits<int64_t>::max())) {
                    return std::nullopt;
                }
                return static_cast<int64_t>(value);
            }
            return item.get<int64_t>();
        }

        std::expected<SeatScores, eastl::string> ReadSeatScores(const Json& object, eastl::string_view key) {
            const auto list = ReadArray(object, key, kSeatCount);
            if (!list) {
                return std::unexpected(list.error());
            }
            SeatScores scores{};
            size_t index = 0;
            for (const Json& item : **list) {
                const std::optional<int64_t> value = ReadIntItem(item);
                if (!value || *value < std::numeric_limits<int>::min() || *value > std::numeric_limits<int>::max()) {
                    return Fail(key, "has to hold integers");
                }
                scores[index++] = static_cast<int>(*value);
            }
            return scores;
        }

        std::expected<MahjongSettings, eastl::string> ReadSettings(const Json& object) {
            const Json* member = FindJsonMember(object, "settings");
            if (member == nullptr || !member->is_object()) {
                return Fail("settings", "has to be an object");
            }
            MahjongSettings settings;
            const auto hands = ReadIntMember(*member, "handsPerMatch");
            const auto flowers = ReadBool(*member, "flowers");
            const auto replay = ReadBool(*member, "replayDrawnHand");
            const auto limit = ReadIntMember(*member, "limit");
            const auto starting = ReadIntMember(*member, "startingScore");
            if (!hands) {
                return Fail("settings", hands.error().c_str());
            }
            if (!flowers) {
                return Fail("settings", flowers.error().c_str());
            }
            if (!replay) {
                return Fail("settings", replay.error().c_str());
            }
            if (!limit) {
                return Fail("settings", limit.error().c_str());
            }
            if (!starting) {
                return Fail("settings", starting.error().c_str());
            }
            settings.handsPerMatch = *hands;
            settings.flowers = *flowers;
            settings.replayDrawnHand = *replay;
            settings.limit = *limit;
            settings.startingScore = *starting;
            return settings;
        }

        std::expected<MatchLogEvent, eastl::string> StartFromJson(const Json& json) {
            MatchLogEvent event;
            event.kind = MatchLogKind::Start;
            const auto version = ReadIntMember(json, "version");
            if (!version) {
                return std::unexpected(version.error());
            }
            if (*version != kMatchLogFormatVersion) {
                return Fail("version", Format("is not a version this reader knows: {}", *version).c_str());
            }
            const auto seed = ReadInt(json, "seed", 0, std::numeric_limits<uint32_t>::max());
            if (!seed) {
                return std::unexpected(seed.error());
            }
            event.seed = static_cast<uint32_t>(*seed);
            const auto settings = ReadSettings(json);
            if (!settings) {
                return std::unexpected(settings.error());
            }
            event.settings = *settings;
            const auto letters = ReadString(json, "seats");
            if (!letters) {
                return std::unexpected(letters.error());
            }
            const std::optional<SeatSetup> seats = ReadSeats(*letters);
            if (!seats) {
                return Fail("seats", Format("has to be four letters, P for a person and C for a computer: \"{}\"", *letters).c_str());
            }
            event.seats = *seats;
            return event;
        }

        std::expected<MatchLogEvent, eastl::string> HandFromJson(const Json& json) {
            MatchLogEvent event;
            event.kind = MatchLogKind::Hand;
            const auto number = ReadInt(json, "number", 0, std::numeric_limits<int>::max());
            const auto east = ReadSeat(json, "east");
            const auto round = ReadWind(json, "roundWind");
            if (!number) {
                return std::unexpected(number.error());
            }
            if (!east) {
                return std::unexpected(east.error());
            }
            if (!round) {
                return std::unexpected(round.error());
            }
            event.number = static_cast<int>(*number);
            event.east = *east;
            event.roundWind = *round;

            const auto winds = ReadArray(json, "winds", kSeatCount);
            if (!winds) {
                return std::unexpected(winds.error());
            }
            size_t index = 0;
            for (const Json& item : **winds) {
                if (!item.is_string()) {
                    return Fail("winds", "has to hold wind names as strings");
                }
                const auto wind = ReadWindText(eastl::string(item.get_ref<const std::string&>().c_str()), "winds");
                if (!wind) {
                    return std::unexpected(wind.error());
                }
                event.winds[index++] = *wind;
            }

            const auto deal = ReadArray(json, "deal", kSeatCount);
            if (!deal) {
                return std::unexpected(deal.error());
            }
            index = 0;
            for (const Json& entry : **deal) {
                const auto concealed = ReadTiles(entry, "concealed");
                if (!concealed) {
                    return Fail(Format("deal[{}]", index).c_str(), concealed.error().c_str());
                }
                const auto bonus = ReadTiles(entry, "bonus");
                if (!bonus) {
                    return Fail(Format("deal[{}]", index).c_str(), bonus.error().c_str());
                }
                event.concealed[index] = *concealed;
                event.bonus[index] = *bonus;
                ++index;
            }
            return event;
        }

        std::expected<MatchLogEvent, eastl::string> ActionFromEventJson(const Json& json) {
            MatchLogEvent event;
            event.kind = MatchLogKind::Action;
            const auto seat = ReadSeat(json, "seat");
            if (!seat) {
                return std::unexpected(seat.error());
            }
            event.seat = *seat;
            const auto by = ReadString(json, "by");
            if (!by) {
                return std::unexpected(by.error());
            }
            if (EaView(*by) == kByPerson) {
                event.by = SeatKind::Person;
            } else if (EaView(*by) == kByComputer) {
                event.by = SeatKind::Computer;
            } else {
                return Fail("by", "has to be \"person\" or \"computer\"");
            }
            const Json* action = FindJsonMember(json, "action");
            if (action == nullptr || !action->is_object()) {
                return Fail("action", "has to be an object");
            }
            const std::expected<ControlAction, ControlError> parsed = ActionFromJson(*action);
            if (!parsed) {
                return Fail("action", parsed.error().message.c_str());
            }
            if (parsed->nextHand) {
                return Fail("action", "next_hand is no action of a seat");
            }
            event.action = parsed->hand;
            return event;
        }

        std::expected<MatchLogEvent, eastl::string> SettlementFromJson(const Json& json) {
            MatchLogEvent event;
            event.kind = MatchLogKind::Settlement;
            HandSettlement& settlement = event.settlement;
            const auto kind = ReadString(json, "kind");
            if (!kind) {
                return std::unexpected(kind.error());
            }
            if (EaView(*kind) == kKindWin) {
                settlement.kind = ResultKind::Win;
            } else if (EaView(*kind) == kKindDrawn) {
                settlement.kind = ResultKind::Drawn;
            } else {
                return Fail("kind", "has to be \"win\" or \"drawn\"");
            }
            const auto winner = ReadSeatOrNull(json, "winner");
            const auto east = ReadSeat(json, "east");
            const auto round = ReadWind(json, "roundWind");
            const auto scores = ReadSeatScores(json, "scores");
            const auto deltas = ReadSeatScores(json, "deltas");
            if (!winner) {
                return std::unexpected(winner.error());
            }
            if (!east) {
                return std::unexpected(east.error());
            }
            if (!round) {
                return std::unexpected(round.error());
            }
            if (!scores) {
                return std::unexpected(scores.error());
            }
            if (!deltas) {
                return std::unexpected(deltas.error());
            }
            settlement.winner = *winner;
            settlement.east = *east;
            settlement.roundWind = *round;
            settlement.scores = *scores;
            settlement.deltas = *deltas;
            return event;
        }

        std::expected<MatchLogEvent, eastl::string> EndFromJson(const Json& json) {
            MatchLogEvent event;
            event.kind = MatchLogKind::End;
            const auto scores = ReadSeatScores(json, "scores");
            if (!scores) {
                return std::unexpected(scores.error());
            }
            event.scores = *scores;
            const auto standings = ReadArray(json, "standings", kSeatCount);
            if (!standings) {
                return std::unexpected(standings.error());
            }
            size_t index = 0;
            for (const Json& item : **standings) {
                const std::optional<int64_t> value = ReadIntItem(item);
                if (!value || *value < 0 || *value >= kSeatCount) {
                    return Fail("standings", "has to hold seats 0..3");
                }
                event.standings[index++] = static_cast<Seat>(*value);
            }
            return event;
        }

    }  // namespace

    Json EventToJson(const MatchLogEvent& event) {
        Json json = Json::object();
        json["event"] = MakeJsonString(EventName(event.kind));
        switch (event.kind) {
            case MatchLogKind::Start:
                json["version"] = kMatchLogFormatVersion;
                json["seed"] = event.seed;
                json["settings"] = SettingsToJson(event.settings);
                json["seats"] = MakeJsonString(SeatsToLetters(event.seats));
                break;
            case MatchLogKind::Hand: {
                json["number"] = event.number;
                json["east"] = event.east;
                json["roundWind"] = MakeJsonString(EnumUtils<Wind>::ToString(event.roundWind));
                json["winds"] = WindsToJson(event.winds);
                Json deal = Json::array();
                for (size_t seat = 0; seat < kSeatCount; ++seat) {
                    Json entry = Json::object();
                    entry["concealed"] = TilesToJson(event.concealed[seat]);
                    entry["bonus"] = TilesToJson(event.bonus[seat]);
                    deal.push_back(std::move(entry));
                }
                json["deal"] = std::move(deal);
                break;
            }
            case MatchLogKind::Action:
                json["seat"] = event.seat;
                json["by"] = MakeJsonString(event.by == SeatKind::Person ? kByPerson : kByComputer);
                json["action"] = ActionToJson(event.action);
                break;
            case MatchLogKind::Settlement: {
                const HandSettlement& settlement = event.settlement;
                json["kind"] = MakeJsonString(settlement.kind == ResultKind::Win ? kKindWin : kKindDrawn);
                json["winner"] = SeatOrNull(settlement.winner);
                json["east"] = settlement.east;
                json["roundWind"] = MakeJsonString(EnumUtils<Wind>::ToString(settlement.roundWind));
                json["scores"] = SeatScoresToJson(settlement.scores);
                json["deltas"] = SeatScoresToJson(settlement.deltas);
                Json seats = Json::array();
                for (const ScoreBreakdown& breakdown : settlement.breakdowns) {
                    Json entry = Json::object();
                    entry["breakdown"] = BreakdownToJson(breakdown);
                    seats.push_back(std::move(entry));
                }
                json["seats"] = std::move(seats);
                break;
            }
            case MatchLogKind::End:
                json["scores"] = SeatScoresToJson(event.scores);
                json["standings"] = SeatsInOrderToJson(event.standings);
                break;
        }
        return json;
    }

    std::expected<MatchLogEvent, eastl::string> EventFromJson(const Json& json) {
        if (!json.is_object()) {
            return std::unexpected(eastl::string("the line has to be a JSON object"));
        }
        const auto name = ReadString(json, "event");
        if (!name) {
            return std::unexpected(name.error());
        }
        const eastl::string_view kind = EaView(*name);
        if (kind == kEventStart) {
            return StartFromJson(json);
        }
        if (kind == kEventHand) {
            return HandFromJson(json);
        }
        if (kind == kEventAction) {
            return ActionFromEventJson(json);
        }
        if (kind == kEventSettlement) {
            return SettlementFromJson(json);
        }
        if (kind == kEventEnd) {
            return EndFromJson(json);
        }
        return Fail("event", Format("is not a known event: \"{}\"", *name).c_str());
    }

    std::expected<eastl::vector<MatchLogEvent>, eastl::string> ParseMatchLog(std::string_view lines) {
        eastl::vector<MatchLogEvent> events;
        size_t lineNumber = 0;
        size_t begin = 0;
        while (begin <= lines.size()) {
            size_t end = lines.find('\n', begin);
            if (end == std::string_view::npos) {
                end = lines.size();
            }
            ++lineNumber;
            const std::string_view line = lines.substr(begin, end - begin);
            begin = end + 1;
            if (line.find_first_not_of(" \t\r") == std::string_view::npos) {
                continue;
            }
            const std::expected<Json, ControlError> parsed = ParseJson(eastl::string_view(line.data(), line.size()));
            if (!parsed) {
                return std::unexpected(Format("line {}: not JSON: {}", lineNumber, parsed.error().message));
            }
            std::expected<MatchLogEvent, eastl::string> event = EventFromJson(*parsed);
            if (!event) {
                return std::unexpected(Format("line {}: {}", lineNumber, event.error()));
            }
            events.push_back(std::move(*event));
        }
        return events;
    }

}  // namespace BECore::Mahjong
