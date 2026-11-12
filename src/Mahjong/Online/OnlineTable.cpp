#include "OnlineTable.h"

#include <BECore/Control/Json.h>
#include <Mahjong/MahjongStateJson.h>
#include <Mahjong/Online/MahjongSeatedGame.h>

namespace BECore::Mahjong {

    namespace {

        constexpr int kNoIndex = -1;
        constexpr int kChowTiles = 3;
        constexpr int kPungTiles = 3;
        constexpr int kKongTiles = 4;
        constexpr int kMaxCount = std::numeric_limits<int>::max();

        template <typename TEnum>
        struct NamedValue {
            eastl::string_view name;
            TEnum value;
        };

        constexpr NamedValue<Phase> kTurnPhases[] = {{"discard", Phase::Discard}, {"claim", Phase::Claim}, {"rob_kong", Phase::RobKong}, {"finished", Phase::Finished}};
        constexpr NamedValue<MeldKind> kMeldKinds[] = {{"chow", MeldKind::Chow}, {"pung", MeldKind::Pung}, {"kong", MeldKind::Kong}};
        constexpr NamedValue<ResultKind> kResultKinds[] = {{"win", ResultKind::Win}, {"drawn", ResultKind::Drawn}};
        constexpr NamedValue<WinSource> kWinSources[] = {{"discard", WinSource::Discard}, {"self_draw", WinSource::SelfDraw}, {"robbed_kong", WinSource::RobbedKong}};
        constexpr NamedValue<SessionPhase> kSessionPhases[] = {{"playing", SessionPhase::Playing}, {"hand_over", SessionPhase::HandOver}, {"match_over", SessionPhase::MatchOver}};

        eastl::string_view ViewOf(const eastl::string& text) {
            return eastl::string_view(text.data(), text.size());
        }

        /// One reading of one view into one table. The path of the member being read is kept in a list, so a failure names it
        /// (`seats[2].melds[0].kind`) and a success builds no text.
        class ViewReader {
        public:
            ViewReader(const Json& view, const Json& actions, OnlineTable& table) : _view(view), _actions(actions), _t(table) {}

            [[nodiscard]] std::expected<void, OnlineViewError> Read() {
                if (!_view.is_object()) {
                    Fail("", "has to be an object");
                } else if (ReadFormat()) {
                    ReadTable();
                }
                if (_failed) {
                    return std::unexpected(std::move(_error));
                }
                return {};
            }

        private:
            struct PathPart {
                eastl::string_view key;
                int index = kNoIndex;
            };

            /// The member being read, for the length of a scope.
            class Scope {
            public:
                Scope(ViewReader& reader, eastl::string_view key, int index = kNoIndex) : _reader(reader) {
                    _reader._path.push_back(PathPart{key, index});
                }
                ~Scope() {
                    _reader._path.pop_back();
                }
                Scope(const Scope&) = delete;
                Scope& operator=(const Scope&) = delete;

            private:
                ViewReader& _reader;
            };

            // ----- errors -----

            bool Fail(eastl::string_view key, eastl::string_view what, OnlineViewErrorKind kind = OnlineViewErrorKind::Malformed) {
                if (_failed) {
                    return false;
                }
                _failed = true;
                _error.kind = kind;
                eastl::string& text = _error.text;
                text.clear();
                for (const PathPart& part : _path) {
                    if (!text.empty()) {
                        text.push_back('.');
                    }
                    text.append(part.key.data(), part.key.size());
                    if (part.index != kNoIndex) {
                        text.append(Format("[{}]", part.index));
                    }
                }
                if (!key.empty()) {
                    if (!text.empty()) {
                        text.push_back('.');
                    }
                    text.append(key.data(), key.size());
                }
                if (text.empty()) {
                    text = "view";
                }
                text.append(": ");
                text.append(what.data(), what.size());
                return false;
            }

            // ----- members -----

            /// The member @p key of @p object, nullptr (with the failure set) when it is missing.
            const Json* Need(const Json& object, eastl::string_view key) {
                const Json* member = FindJsonMember(object, key);
                if (member == nullptr) {
                    Fail(key, "is missing");
                }
                return member;
            }

            bool GetInt(const Json& object, eastl::string_view key, int64_t low, int64_t high, int64_t& out) {
                if (Need(object, key) == nullptr) {
                    return false;
                }
                const std::optional<int64_t> number = GetJsonInt(object, key);
                if (!number) {
                    return Fail(key, "has to be an integer");
                }
                if (*number < low || *number > high) {
                    return Fail(key, Format("is {}, out of the range {}..{}", *number, low, high).c_str());
                }
                out = *number;
                return true;
            }

            bool GetCount(const Json& object, eastl::string_view key, int& out) {
                int64_t number = 0;
                if (!GetInt(object, key, 0, kMaxCount, number)) {
                    return false;
                }
                out = static_cast<int>(number);
                return true;
            }

            bool GetScore(const Json& object, eastl::string_view key, int& out) {
                int64_t number = 0;
                if (!GetInt(object, key, std::numeric_limits<int>::min(), kMaxCount, number)) {
                    return false;
                }
                out = static_cast<int>(number);
                return true;
            }

            bool GetBool(const Json& object, eastl::string_view key, bool& out) {
                if (Need(object, key) == nullptr) {
                    return false;
                }
                const std::optional<bool> flag = GetJsonBool(object, key);
                if (!flag) {
                    return Fail(key, "has to be true or false");
                }
                out = *flag;
                return true;
            }

            /// A seat 0..3; with @p nullable a JSON null is `kNoSeat`.
            bool GetSeat(const Json& object, eastl::string_view key, Seat& out, bool nullable) {
                const Json* member = Need(object, key);
                if (member == nullptr) {
                    return false;
                }
                if (nullable && member->is_null()) {
                    out = kNoSeat;
                    return true;
                }
                int64_t number = 0;
                if (!GetInt(object, key, 0, kSeatCount - 1, number)) {
                    return false;
                }
                out = static_cast<Seat>(number);
                return true;
            }

            bool GetString(const Json& object, eastl::string_view key, eastl::string& out) {
                if (Need(object, key) == nullptr) {
                    return false;
                }
                const std::optional<eastl::string> text = GetJsonString(object, key);
                if (!text) {
                    return Fail(key, "has to be a string");
                }
                out = *text;
                return true;
            }

            template <typename TEnum, size_t N>
            bool GetNamed(const Json& object, eastl::string_view key, const NamedValue<TEnum> (&names)[N], TEnum& out) {
                if (!GetString(object, key, _word)) {
                    return false;
                }
                for (const NamedValue<TEnum>& entry : names) {
                    if (entry.name == ViewOf(_word)) {
                        out = entry.value;
                        return true;
                    }
                }
                return Fail(key, Format("'{}' is not a known name", _word.c_str()).c_str());
            }

            bool GetWind(const Json& object, eastl::string_view key, Wind& out) {
                if (!GetString(object, key, _word)) {
                    return false;
                }
                for (const Wind wind : {Wind::East, Wind::South, Wind::West, Wind::North}) {
                    if (EnumUtils<Wind>::ToString(wind) == ViewOf(_word)) {
                        out = wind;
                        return true;
                    }
                }
                return Fail(key, Format("'{}' is not a wind", _word.c_str()).c_str());
            }

            /// A tile of the notation in a JSON string.
            bool ReadTileText(const Json& item, eastl::string_view key, Tile& out) {
                if (!item.is_string()) {
                    return Fail(key, "has to be a string with a tile");
                }
                const std::string& text = item.get_ref<const std::string&>();
                const std::optional<Tile> tile = TryParse(std::string_view(text));
                if (!tile) {
                    return Fail(key, Format("'{}' is not a tile", text.c_str()).c_str());
                }
                out = *tile;
                return true;
            }

            bool GetTile(const Json& object, eastl::string_view key, Tile& out, bool nullable) {
                const Json* member = Need(object, key);
                if (member == nullptr) {
                    return false;
                }
                if (nullable && member->is_null()) {
                    out = Tile{};
                    return true;
                }
                return ReadTileText(*member, key, out);
            }

            /// The member @p key as an array; nullptr (failure set) when it is missing or is not one.
            const Json* GetArray(const Json& object, eastl::string_view key) {
                const Json* member = Need(object, key);
                if (member != nullptr && !member->is_array()) {
                    Fail(key, "has to be a list");
                    return nullptr;
                }
                return member;
            }

            /// Calls @p read(item) for each element of the list @p key, with the path `key[i]` while it reads.
            template <typename TRead>
            bool ForEach(const Json& object, eastl::string_view key, TRead&& read) {
                const Json* list = GetArray(object, key);
                if (list == nullptr) {
                    return false;
                }
                int index = 0;
                for (const Json& item : *list) {
                    Scope scope(*this, key, index++);
                    if (!read(item)) {
                        return false;
                    }
                }
                return true;
            }

            /// Calls @p read(member) for the object @p key, with the path `key` while it reads.
            template <typename TRead>
            bool InObject(const Json& object, eastl::string_view key, TRead&& read) {
                const Json* member = Need(object, key);
                if (member == nullptr) {
                    return false;
                }
                if (!member->is_object()) {
                    return Fail(key, "has to be an object");
                }
                Scope scope(*this, key);
                return read(*member);
            }

            bool GetTiles(const Json& object, eastl::string_view key, eastl::vector<Tile>& out) {
                out.clear();
                return ForEach(object, key, [&](const Json& item) {
                    Tile tile;
                    if (!ReadTileText(item, "", tile)) {
                        return false;
                    }
                    out.push_back(tile);
                    return true;
                });
            }

            /// Four numbers, one per seat.
            bool GetSeatScores(const Json& object, eastl::string_view key, SeatScores& out) {
                const Json* list = GetArray(object, key);
                if (list == nullptr) {
                    return false;
                }
                if (list->size() != static_cast<size_t>(kSeatCount)) {
                    return Fail(key, "has to be a list of four numbers");
                }
                size_t index = 0;
                for (const Json& item : *list) {
                    if (!item.is_number_integer() || item.get<int64_t>() < std::numeric_limits<int>::min() || item.get<int64_t>() > kMaxCount) {
                        Scope scope(*this, key, static_cast<int>(index));
                        return Fail("", "has to be an integer");
                    }
                    out[index++] = static_cast<int>(item.get<int64_t>());
                }
                return true;
            }

            // ----- the pieces -----

            bool ReadFormat() {
                const Json* member = FindJsonMember(_view, "format");
                if (member == nullptr) {
                    return Fail("format", "is missing", OnlineViewErrorKind::Format);
                }
                const std::optional<int64_t> number = GetJsonInt(_view, "format");
                if (!number) {
                    return Fail("format", "has to be an integer", OnlineViewErrorKind::Format);
                }
                if (*number != kOnlineViewFormat) {
                    return Fail("format", Format("is {}, this client reads {}", *number, kOnlineViewFormat).c_str(), OnlineViewErrorKind::Format);
                }
                _t.format = kOnlineViewFormat;
                return true;
            }

            bool ReadMeld(const Json& item, Meld& meld) {
                if (!item.is_object()) {
                    return Fail("", "has to be an object");
                }
                if (!GetNamed(item, "kind", kMeldKinds, meld.kind)) {
                    return false;
                }
                const Json* tiles = GetArray(item, "tiles");
                if (tiles == nullptr) {
                    return false;
                }
                const size_t expected = static_cast<size_t>(meld.kind == MeldKind::Kong ? kKongTiles : (meld.kind == MeldKind::Chow ? kChowTiles : kPungTiles));
                if (tiles->size() != expected) {
                    return Fail("tiles", Format("has {} tiles, a {} has {}", tiles->size(), EnumUtils<MeldKind>::ToString(meld.kind), expected).c_str());
                }
                {
                    int index = 0;
                    for (const Json& tile : *tiles) {
                        Scope scope(*this, "tiles", index);
                        Tile read;
                        if (!ReadTileText(tile, "", read)) {
                            return false;
                        }
                        if (index++ == 0) {
                            meld.first = read;
                        }
                    }
                }
                return GetBool(item, "concealed", meld.concealed) && GetSeat(item, "from", meld.from, true) && GetBool(item, "fromDiscard", meld.fromDiscard) &&
                       GetTile(item, "claimed", meld.claimed, true);
            }

            bool ReadMelds(const Json& object, eastl::string_view key, eastl::vector<Meld>& out) {
                out.clear();
                return ForEach(object, key, [&](const Json& item) {
                    Meld meld;
                    if (!ReadMeld(item, meld)) {
                        return false;
                    }
                    out.push_back(meld);
                    return true;
                });
            }

            bool ReadSeat(const Json& item, OnlineSeat& seat) {
                if (!item.is_object()) {
                    return Fail("", "has to be an object");
                }
                return GetWind(item, "wind", seat.wind) && GetScore(item, "score", seat.score) && GetCount(item, "concealedCount", seat.concealedCount) && ReadMelds(item, "melds", seat.melds) &&
                       GetCount(item, "concealedKongs", seat.concealedKongs) && GetTiles(item, "bonus", seat.bonus) && GetCount(item, "discardCount", seat.discardCount) &&
                       GetBool(item, "calling", seat.calling);
            }

            bool ReadTurn(const Json& turn) {
                return GetNamed(turn, "phase", kTurnPhases, _t.turn.phase) && GetSeat(turn, "current", _t.turn.current, true) && GetTile(turn, "pendingTile", _t.turn.pendingTile, true) &&
                       GetBool(turn, "yourTurn", _t.turn.yourTurn);
            }

            bool ReadSeats() {
                const Json* list = GetArray(_view, "seats");
                if (list == nullptr) {
                    return false;
                }
                if (list->size() != static_cast<size_t>(kSeatCount)) {
                    return Fail("seats", "has to be a list of four");
                }
                int index = 0;
                for (const Json& item : *list) {
                    Scope scope(*this, "seats", index);
                    if (!ReadSeat(item, _t.seats[static_cast<size_t>(index)])) {
                        return false;
                    }
                    ++index;
                }
                return true;
            }

            bool ReadDiscards() {
                _t.discards.clear();
                return ForEach(_view, "discards", [&](const Json& item) {
                    DiscardEntry entry;
                    if (!item.is_object()) {
                        return Fail("", "has to be an object");
                    }
                    if (!GetTile(item, "tile", entry.tile, false) || !GetSeat(item, "seat", entry.seat, false)) {
                        return false;
                    }
                    _t.discards.push_back(entry);
                    return true;
                });
            }

            bool ReadReady() {
                const Json* list = GetArray(_view, "ready");
                if (list == nullptr) {
                    return false;
                }
                if (list->size() != static_cast<size_t>(kSeatCount)) {
                    return Fail("ready", "has to be a list of four");
                }
                size_t index = 0;
                for (const Json& item : *list) {
                    if (!item.is_boolean()) {
                        Scope scope(*this, "ready", static_cast<int>(index));
                        return Fail("", "has to be true or false");
                    }
                    _t.ready[index++] = item.get<bool>();
                }
                return true;
            }

            bool ReadActions() {
                _t.actions.clear();
                if (!_actions.is_array()) {
                    Scope scope(*this, "actions");
                    return Fail("", "has to be a list");
                }
                int index = 0;
                for (const Json& item : _actions) {
                    const std::expected<ControlAction, ControlError> action = ActionFromJson(item);
                    if (!action) {
                        Scope scope(*this, "actions", index);
                        return Fail("", action.error().message.c_str());
                    }
                    _t.actions.push_back(*action);
                    ++index;
                }
                return true;
            }

            bool ReadResultSeat(const Json& item, OnlineResultSeat& seat) {
                if (!item.is_object()) {
                    return Fail("", "has to be an object");
                }
                return GetTiles(item, "concealed", seat.concealed) && ReadMelds(item, "melds", seat.melds) && GetTiles(item, "bonus", seat.bonus);
            }

            bool ReadResult(const Json& json, OnlineResult& result) {
                if (!GetNamed(json, "kind", kResultKinds, result.kind) || !GetSeat(json, "winner", result.winner, true) || !GetTile(json, "winningTile", result.winningTile, true)) {
                    return false;
                }
                const Json* source = Need(json, "source");
                if (source == nullptr) {
                    return false;
                }
                result.source = WinSource::SelfDraw;
                if (!source->is_null() && !GetNamed(json, "source", kWinSources, result.source)) {
                    return false;
                }
                if (!GetSeat(json, "from", result.from, true) || !GetBool(json, "loose", result.loose) || !GetBool(json, "lastLiveTile", result.lastLiveTile)) {
                    return false;
                }
                const Json* list = GetArray(json, "seats");
                if (list == nullptr) {
                    return false;
                }
                if (list->size() != static_cast<size_t>(kSeatCount)) {
                    return Fail("seats", "has to be a list of four");
                }
                int index = 0;
                for (const Json& item : *list) {
                    Scope scope(*this, "seats", index);
                    if (!ReadResultSeat(item, result.seats[static_cast<size_t>(index)])) {
                        return false;
                    }
                    ++index;
                }
                return GetSeatScores(json, "scores", result.scores) && GetSeatScores(json, "deltas", result.deltas);
            }

            void ReadTable() {
                int64_t number = 0;
                const bool good = GetNamed(_view, "phase", kSessionPhases, _t.phase) && GetSeat(_view, "seat", _t.seat, false) && GetWind(_view, "seatWind", _t.seatWind) &&
                                  GetWind(_view, "roundWind", _t.roundWind) && GetSeat(_view, "east", _t.east, false) && GetTiles(_view, "hand", _t.hand) &&
                                  InObject(_view, "turn", [&](const Json& turn) { return ReadTurn(turn); }) && ReadSeats() && ReadDiscards() && GetCount(_view, "wall", _t.wall) &&
                                  GetCount(_view, "handsPlayed", _t.handsPlayed) && GetCount(_view, "handsTotal", _t.handsTotal) &&
                                  GetInt(_view, "version", 0, std::numeric_limits<int64_t>::max(), number) && ReadReady() && ReadActions();
                if (!good) {
                    return;
                }
                _t.version = static_cast<uint64_t>(number);
                if (_t.phase == SessionPhase::Playing) {
                    _t.result.reset();
                    return;
                }
                if (!_t.result) {
                    _t.result.emplace();
                }
                InObject(_view, "result", [&](const Json& json) { return ReadResult(json, *_t.result); });
            }

            const Json& _view;
            const Json& _actions;
            OnlineTable& _t;
            eastl::vector<PathPart> _path;
            eastl::string _word;
            OnlineViewError _error;
            bool _failed = false;
        };

    }  // namespace

    std::expected<void, OnlineViewError> ReadOnlineView(const Json& view, const Json& actions, OnlineTable& out, OnlineTable& scratch) {
        std::expected<void, OnlineViewError> read = ViewReader(view, actions, scratch).Read();
        if (read) {
            std::swap(out, scratch);
        }
        return read;
    }

    std::expected<void, OnlineViewError> ReadOnlineView(const Json& view, const Json& actions, OnlineTable& out) {
        OnlineTable scratch;
        return ReadOnlineView(view, actions, out, scratch);
    }

}  // namespace BECore::Mahjong
