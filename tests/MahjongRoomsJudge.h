#pragma once

#include <BECore/Control/Json.h>
#include <set>
#include <vector>

namespace BECore::Mahjong::Tests {

    /**
     * @brief The judge of the Mahjong rooms: what a client may know, decided from the text it received.
     *
     * Mahjong tiles have no ids (two copies of "4p" look alike), so the judge cannot look for a tile of another hand by its name. It
     * checks the **schema** instead: only the known keys at the top of a message, in the state, in each `seats[i]`, in a meld and in
     * `result`; the only list of concealed tiles in a playing hand is `view.hand`, it is the seat's own (`view.seat` is the seat of the
     * client) and has the size that `seats[seat].concealedCount` says; the melds of the other seats are all open; there is no `hands`,
     * no wall and no concealed list of another seat; `result` (all four hands) only when the phase is not `playing`; the messages of
     * the other types carry no tile list at all. What the schema cannot see (a neighbour's tiles in the place of the own ones) is held
     * by the equal-views test of `MahjongRoomsTest.cpp`.
     *
     * `tests/rooms_smoke.py` holds the same rule in Python: change both together.
     */

    namespace RoomsJudgeDetail {

        using Offences = std::vector<std::string>;
        using KeySet = std::set<std::string>;

        constexpr int kJudgedSeatCount = 4;

        inline void Offend(Offences& offences, const std::string& where, const std::string& what) {
            offences.push_back(where + ": " + what);
        }

        /// The object has every key of @p required and no key beyond @p required and @p optional.
        inline bool CheckKeys(const Json& value, const std::string& where, const KeySet& required, const KeySet& optional, Offences& offences) {
            if (!value.is_object()) {
                Offend(offences, where, "is not an object");
                return false;
            }
            bool good = true;
            for (const auto& item : value.items()) {
                if (!required.contains(item.key()) && !optional.contains(item.key())) {
                    Offend(offences, where, "has the key '" + item.key() + "' that the schema does not know");
                    good = false;
                }
            }
            for (const std::string& key : required) {
                if (!value.contains(key)) {
                    Offend(offences, where, "lacks the key '" + key + "'");
                    good = false;
                }
            }
            return good;
        }

        inline const Json* Member(const Json& object, const char* key) {
            if (!object.is_object()) {
                return nullptr;
            }
            const auto found = object.find(key);
            return found == object.end() ? nullptr : &*found;
        }

        inline bool IsListOf(const Json& value, bool (*isItem)(const Json&)) {
            if (!value.is_array()) {
                return false;
            }
            for (const Json& item : value) {
                if (!isItem(item)) {
                    return false;
                }
            }
            return true;
        }

        inline bool IsString(const Json& value) {
            return value.is_string();
        }

        inline bool IsBool(const Json& value) {
            return value.is_boolean();
        }

        inline void CheckList(const Json& object, const char* key, bool (*isItem)(const Json&), const char* what, const std::string& where, Offences& offences) {
            const Json* list = Member(object, key);
            if (list != nullptr && !IsListOf(*list, isItem)) {
                Offend(offences, where + "." + key, std::string("is not a list of ") + what);
            }
        }

        inline void CheckNumber(const Json& object, const char* key, const std::string& where, Offences& offences) {
            const Json* number = Member(object, key);
            if (number != nullptr && !number->is_number_integer()) {
                Offend(offences, where + "." + key, "is not an integer");
            }
        }

        inline void CheckMeld(const Json& meld, const std::string& where, bool mustBeOpen, Offences& offences) {
            if (!CheckKeys(meld, where, {"kind", "tiles", "concealed", "from", "fromDiscard", "claimed"}, {}, offences)) {
                return;
            }
            CheckList(meld, "tiles", IsString, "tiles", where, offences);
            const Json* concealed = Member(meld, "concealed");
            if (concealed == nullptr || !concealed->is_boolean()) {
                Offend(offences, where + ".concealed", "is not a boolean");
            } else if (mustBeOpen && concealed->get<bool>()) {
                Offend(offences, where, "is a concealed meld of another seat");
            }
        }

        inline void CheckMelds(const Json& owner, const std::string& where, bool mustBeOpen, Offences& offences) {
            const Json* melds = Member(owner, "melds");
            if (melds == nullptr || !melds->is_array()) {
                Offend(offences, where + ".melds", "is not a list");
                return;
            }
            size_t index = 0;
            for (const Json& meld : *melds) {
                CheckMeld(meld, where + ".melds[" + std::to_string(index++) + "]", mustBeOpen, offences);
            }
        }

        inline void CheckSeatEntry(const Json& entry, int index, int viewer, Offences& offences) {
            const std::string where = "view.seats[" + std::to_string(index) + "]";
            if (!CheckKeys(entry, where, {"wind", "score", "concealedCount", "melds", "concealedKongs", "bonus", "discardCount", "calling"}, {}, offences)) {
                return;
            }
            CheckMelds(entry, where, index != viewer, offences);
            CheckList(entry, "bonus", IsString, "tiles", where, offences);
            for (const char* key : {"score", "concealedCount", "concealedKongs", "discardCount"}) {
                CheckNumber(entry, key, where, offences);
            }
        }

        inline void CheckResult(const Json& result, Offences& offences) {
            const std::string where = "view.result";
            if (!CheckKeys(result, where, {"kind", "winner", "winningTile", "source", "from", "loose", "lastLiveTile", "seats", "scores", "deltas"}, {}, offences)) {
                return;
            }
            const Json* seats = Member(result, "seats");
            if (seats == nullptr || !seats->is_array() || seats->size() != kJudgedSeatCount) {
                Offend(offences, where + ".seats", "is not a list of four");
                return;
            }
            int index = 0;
            for (const Json& entry : *seats) {
                const std::string at = where + ".seats[" + std::to_string(index++) + "]";
                if (!CheckKeys(entry, at, {"concealed", "melds", "bonus", "breakdown"}, {}, offences)) {
                    continue;
                }
                CheckList(entry, "concealed", IsString, "tiles", at, offences);
                CheckList(entry, "bonus", IsString, "tiles", at, offences);
                CheckMelds(entry, at, false, offences);
                CheckKeys(*Member(entry, "breakdown"), at + ".breakdown", {"won", "lines", "points", "doubles", "raw", "score", "limitHand"}, {}, offences);
            }
        }

        inline void CheckState(const Json& state, int seat, Offences& offences) {
            const std::string where = "view";
            if (!CheckKeys(state, where, {"phase", "seat", "seatWind", "roundWind", "east", "hand", "turn", "seats", "discards", "wall", "handsPlayed", "handsTotal", "version", "ready"}, {"result"},
                           offences)) {
                return;
            }
            const Json& phase = state["phase"];
            const bool playing = phase.is_string() && phase.get<std::string>() == "playing";
            const bool known = playing || (phase.is_string() && (phase.get<std::string>() == "hand_over" || phase.get<std::string>() == "match_over"));
            if (!known) {
                Offend(offences, "view.phase", "is not playing, hand_over or match_over");
            }
            if (playing && state.contains("result")) {
                Offend(offences, "view.result", "is sent while the hand is played (it shows all four hands)");
            }
            if (!state["seat"].is_number_integer() || state["seat"].get<int>() != seat) {
                Offend(offences, "view.seat", "is not the seat of the client");
            }
            for (const char* key : {"wall", "east", "handsPlayed", "handsTotal", "version"}) {
                CheckNumber(state, key, where, offences);
            }
            if (!state["wall"].is_number_integer()) {
                Offend(offences, "view.wall", "is not a count");
            }
            CheckList(state, "hand", IsString, "tiles", where, offences);
            CheckKeys(state["turn"], "view.turn", {"phase", "current", "pendingTile", "yourTurn"}, {}, offences);
            const Json& ready = state["ready"];
            if (!IsListOf(ready, IsBool) || ready.size() != kJudgedSeatCount) {
                Offend(offences, "view.ready", "is not four booleans");
            }
            const Json& discards = state["discards"];
            if (!discards.is_array()) {
                Offend(offences, "view.discards", "is not a list");
            } else {
                for (const Json& discard : discards) {
                    CheckKeys(discard, "view.discards[]", {"tile", "seat"}, {}, offences);
                }
            }

            const Json& seats = state["seats"];
            if (!seats.is_array() || seats.size() != kJudgedSeatCount) {
                Offend(offences, "view.seats", "is not a list of four");
                return;
            }
            for (int index = 0; index < kJudgedSeatCount; ++index) {
                CheckSeatEntry(seats[static_cast<size_t>(index)], index, seat, offences);
            }
            // the one concealed list of the playing hand is the seat's own, and it is as long as the seat's own count says
            const Json* own = seat >= 0 && seat < kJudgedSeatCount ? Member(seats[static_cast<size_t>(seat)], "concealedCount") : nullptr;
            if (own == nullptr || !own->is_number_integer() || !state["hand"].is_array() || state["hand"].size() != own->get<size_t>()) {
                Offend(offences, "view.hand", "does not have the size that the own concealedCount says");
            }
            if (const Json* result = Member(state, "result")) {
                CheckResult(*result, offences);
            }
        }

        inline void CheckView(const Json& root, int seat, Offences& offences) {
            if (!CheckKeys(root, "message", {"v", "type", "room", "seat", "version", "finished", "view", "actions"}, {}, offences)) {
                return;
            }
            if (!root["seat"].is_number_integer() || root["seat"].get<int>() != seat) {
                Offend(offences, "message.seat", "is not the seat of the client");
            }
            const Json& actions = root["actions"];
            if (!actions.is_array()) {
                Offend(offences, "message.actions", "is not a list");
            } else {
                for (const Json& action : actions) {
                    CheckKeys(action, "message.actions[]", {"type"}, {"tile", "declareCalling", "claim", "first"}, offences);
                }
            }
            const Json& view = root["view"];
            if (view.is_object() && view.empty()) {
                return;  // the game has not started
            }
            CheckState(view, seat, offences);
        }

        inline void CheckLobby(const Json& root, Offences& offences) {
            if (!CheckKeys(root, "lobby", {"v", "type", "room", "game", "phase", "host", "seats"}, {}, offences)) {
                return;
            }
            if (!root["seats"].is_array()) {
                Offend(offences, "lobby.seats", "is not a list");
                return;
            }
            for (const Json& seat : root["seats"]) {
                CheckKeys(seat, "lobby.seats[]", {"seat", "kind"}, {"name", "connected"}, offences);
            }
        }

    }  // namespace RoomsJudgeDetail

    /// What is wrong with one text that a client received; empty when nothing. @p seat is the seat the client holds (from its `joined`),
    /// -1 before it has one. A text that is not a JSON object is an offence.
    inline std::vector<std::string> JudgeRoomMessage(std::string_view text, int seat) {
        using namespace RoomsJudgeDetail;
        Offences offences;
        const auto parsed = ParseJson(eastl::string_view(text.data(), text.size()));
        if (!parsed || !parsed->is_object()) {
            offences.push_back("message: is not a JSON object");
            return offences;
        }
        const Json& root = *parsed;
        const Json* type = Member(root, "type");
        if (type == nullptr || !type->is_string()) {
            offences.push_back("message: has no type");
            return offences;
        }
        const std::string name = type->get<std::string>();
        if (name == "view") {
            CheckView(root, seat, offences);
        } else if (name == "lobby") {
            CheckLobby(root, offences);
        } else if (name == "joined") {
            CheckKeys(root, "joined", {"v", "type", "room", "player", "token", "seat", "host"}, {}, offences);
        } else if (name == "error") {
            CheckKeys(root, "error", {"v", "type", "code", "message"}, {"ref"}, offences);
        } else if (name == "left") {
            CheckKeys(root, "left", {"v", "type", "reason"}, {}, offences);
        } else if (name == "pong") {
            CheckKeys(root, "pong", {"v", "type"}, {}, offences);
        } else {
            offences.push_back("message: has the type '" + name + "' that the protocol does not send");
        }
        return offences;
    }

    /// The verdicts over a whole run: how many texts were judged, how many frames were not text, and every offence found.
    class RoomsJudgeLog {
    public:
        /// Judges @p text for the client of @p seat; the offences are kept and returned.
        std::vector<std::string> Text(std::string_view text, int seat) {
            ++_texts;
            std::vector<std::string> offences = JudgeRoomMessage(text, seat);
            _offences.insert(_offences.end(), offences.begin(), offences.end());
            return offences;
        }

        /// A frame that is not text (binary) was received: the protocol has none.
        void NonTextFrame() {
            ++_nonText;
        }

        [[nodiscard]] size_t Texts() const {
            return _texts;
        }

        [[nodiscard]] size_t NonText() const {
            return _nonText;
        }

        [[nodiscard]] const std::vector<std::string>& Offences() const {
            return _offences;
        }

    private:
        size_t _texts = 0;
        size_t _nonText = 0;
        std::vector<std::string> _offences;
    };

}  // namespace BECore::Mahjong::Tests
