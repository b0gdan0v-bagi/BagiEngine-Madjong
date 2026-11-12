#include "MahjongSeatedGame.h"

#include <BECore/Control/Json.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MahjongStateJson.h>

namespace BECore::Mahjong {

    namespace {

        constexpr uint64_t kMixIncrement = 0x9E3779B97F4A7C15ULL;
        constexpr uint64_t kMixFirstMultiplier = 0xBF58476D1CE4E5B9ULL;
        constexpr uint64_t kMixSecondMultiplier = 0x94D049BB133111EBULL;
        constexpr int kMixFirstShift = 30;
        constexpr int kMixSecondShift = 27;
        constexpr int kMixLastShift = 31;
        constexpr int kHalfSeedBits = 32;

        std::unexpected<ControlError> Fail(eastl::string_view code, eastl::string message) {
            return std::unexpected(MakeControlError(code, std::move(message)));
        }

        std::unexpected<ControlError> InvalidSeat(int seat) {
            return Fail(SeatedGameErrors::kInvalidSeat, Format("seat {} is out of range", seat));
        }

        /// The 64-bit seed of the room as the 32-bit seed of a match.
        uint32_t FoldSeed(uint64_t seed) noexcept {
            return static_cast<uint32_t>((seed ^ (seed >> kHalfSeedBits)) & std::numeric_limits<uint32_t>::max());
        }

        /// What an answer may say of an applied action to every seat: the action as the table shows it. A concealed kong names no
        /// tile (the others only count it) and a pass says nothing (it would tell that the seat could claim): `nullopt`.
        std::optional<Json> PublicActionToJson(const HandAction& action) {
            switch (action.kind) {
                case ActionKind::Pass:
                    return std::nullopt;
                case ActionKind::ConcealedKong: {
                    Json json = ActionToJson(action);
                    json.erase("tile");
                    return json;
                }
                case ActionKind::Discard:
                case ActionKind::DeclareWin:
                case ActionKind::UpgradeKong:
                case ActionKind::Claim:
                    return ActionToJson(action);
            }
            return std::nullopt;
        }

    }  // namespace

    uint32_t MixSeed(uint32_t seed, uint64_t version) noexcept {
        // splitmix64 over both numbers: the same on every platform
        uint64_t z = (static_cast<uint64_t>(seed) << kHalfSeedBits) ^ version;
        z += kMixIncrement;
        z = (z ^ (z >> kMixFirstShift)) * kMixFirstMultiplier;
        z = (z ^ (z >> kMixSecondShift)) * kMixSecondMultiplier;
        z ^= z >> kMixLastShift;
        return FoldSeed(z);
    }

    SeatedGameInfo MahjongSeatedGame::Info() {
        // Every member sees these options of the host in the lobby, before the start
        return SeatedGameInfo{eastl::string(kSeatedGameName), kSeatCount, {"hands", "level", eastl::string(kHintsAllowedKey)}};
    }

    int MahjongSeatedGame::GetSeatCount() const {
        return kSeatCount;
    }

    MahjongSeatedGame::SeatMoves MahjongSeatedGame::MovesOf(Seat seat) const {
        SeatMoves moves;
        if (!IsSeat(seat)) {
            return moves;
        }
        switch (PhaseOfMatch(_match)) {
            case SessionPhase::Playing:
                moves.hand = _match.Hand().LegalActions(seat);
                break;
            case SessionPhase::HandOver:
                moves.nextHand = !_ready[static_cast<size_t>(seat)];
                break;
            case SessionPhase::MatchOver:
                break;
        }
        return moves;
    }

    bool MahjongSeatedGame::AllReadyBut(Seat seat) const {
        for (Seat other = 0; other < kSeatCount; ++other) {
            if (other != seat && !_ready[static_cast<size_t>(other)]) {
                return false;
            }
        }
        return true;
    }

    std::expected<void, ControlError> MahjongSeatedGame::Start(uint64_t seed, const Json& options) {
        int handsPerMatch = kFullMatchHands;
        if (FindJsonMember(options, "hands") != nullptr) {
            const auto hands = GetJsonInt(options, "hands");
            if (!hands || (*hands != kShortMatchHands && *hands != kFullMatchHands)) {
                return Fail(ControlErrors::kInvalidParams, Format("'hands' has to be {} or {}", kShortMatchHands, kFullMatchHands));
            }
            handsPerMatch = static_cast<int>(*hands);
        }
        ComputerLevel level = kDefaultLevel;
        if (FindJsonMember(options, "level") != nullptr) {
            const auto number = GetJsonInt(options, "level");
            const std::optional<ComputerLevel> read = number ? LevelOf(*number) : std::nullopt;
            if (!read) {
                return Fail(ControlErrors::kInvalidParams, Format("'level' has to be {}", LevelRangeText().c_str()));
            }
            level = *read;
        }
        bool hintsAllowed = false;
        if (FindJsonMember(options, kHintsAllowedKey) != nullptr) {
            const std::optional<bool> flag = GetJsonBool(options, kHintsAllowedKey);
            if (!flag) {
                return Fail(ControlErrors::kInvalidParams, Format("'{}' has to be true or false", kHintsAllowedKey.data()));
            }
            hintsAllowed = *flag;
        }

        // Every option is read and checked: from here on a failure of the match leaves the previous one in place.
        MahjongSettings settings;
        settings.handsPerMatch = handsPerMatch;
        Match match;
        const uint32_t matchSeed = FoldSeed(seed);
        if (const MatchStatus started = match.Start(settings, matchSeed); !started) {
            return Fail(ControlErrors::kInvalidParams, Format("the match did not start: {}", EnumUtils<MatchError>::ToString(started.error())));
        }
        _match = std::move(match);
        _level = level;
        _hintsAllowed = hintsAllowed;
        _matchSeed = matchSeed;
        _ready = {};
        ++_version;
        return {};
    }

    std::expected<Json, ControlError> MahjongSeatedGame::GetSeatView(int seat) const {
        if (!IsSeat(seat)) {
            return InvalidSeat(seat);
        }
        if (_match.GetPhase() == MatchPhase::NotStarted) {
            return Json::object();
        }
        StateJsonContext context{_match};
        context.phase = PhaseOfMatch(_match);
        context.version = _version;
        context.viewSeat = seat;
        Json view = TableStateToJson(context, MakeTableView(_match.Hand(), seat));
        Json ready = Json::array();
        for (const bool flag : _ready) {
            ready.push_back(flag);
        }
        view["ready"] = std::move(ready);
        view["format"] = kOnlineViewFormat;
        view[std::string(kHintsAllowedKey.data(), kHintsAllowedKey.size())] = _hintsAllowed;
        return view;
    }

    std::expected<Json, ControlError> MahjongSeatedGame::GetSeatActions(int seat) const {
        if (!IsSeat(seat)) {
            return InvalidSeat(seat);
        }
        const SeatMoves moves = MovesOf(seat);
        Json actions = Json::array();
        for (const HandAction& action : moves.hand) {
            actions.push_back(ActionToJson(action));
        }
        if (moves.nextHand) {
            actions.push_back(NextHandActionToJson());
        }
        return actions;
    }

    std::expected<Json, ControlError> MahjongSeatedGame::PerformSeatAction(int seat, const Json& action) {
        if (!IsSeat(seat)) {
            return InvalidSeat(seat);
        }
        const std::expected<ControlAction, ControlError> parsed = ActionFromJson(action);
        if (!parsed) {
            return std::unexpected(parsed.error());
        }
        const SeatMoves moves = MovesOf(seat);
        if (moves.Empty()) {
            return Fail(SeatedGameErrors::kNotYourTurn, Format("it is not the turn of seat {}", seat));
        }

        Json outcome = Json::object();
        if (parsed->nextHand) {
            if (!moves.nextHand) {
                return Fail(GameControlErrors::kIllegalAction, "next_hand is possible only when the hand is over");
            }
            if (AllReadyBut(seat)) {
                const std::expected<HandSettlement, MatchError> settled = _match.EndHand();
                if (!settled) {
                    return Fail(GameControlErrors::kIllegalAction, Format("the hand cannot be settled: {}", EnumUtils<MatchError>::ToString(settled.error())));
                }
                _ready = {};
                outcome["settlement"] = SettlementToJson(*settled);
            } else {
                _ready[static_cast<size_t>(seat)] = true;
            }
        } else {
            if (eastl::find(moves.hand.begin(), moves.hand.end(), parsed->hand) == moves.hand.end()) {
                return Fail(GameControlErrors::kIllegalAction, "the action is not among the legal actions now (see the actions of the seat)");
            }
            if (const HandStatus done = _match.Hand().Apply(seat, parsed->hand); !done) {
                return Fail(GameControlErrors::kIllegalAction, Format("the action was refused: {}", EnumUtils<HandError>::ToString(done.error())));
            }
        }
        ++_version;
        if (parsed->nextHand) {
            outcome["applied"] = NextHandActionToJson();
        } else if (std::optional<Json> shown = PublicActionToJson(parsed->hand)) {
            outcome["applied"] = std::move(*shown);
        }
        outcome["phase"] = MakeJsonString(PhaseName(PhaseOfMatch(_match)));
        outcome["version"] = _version;
        return outcome;
    }

    std::optional<Json> MahjongSeatedGame::ChooseComputerAction(int seat) const {
        const SeatMoves moves = MovesOf(seat);
        if (moves.Empty()) {
            return std::nullopt;
        }
        if (moves.nextHand) {
            return NextHandActionToJson();
        }
        ComputerPlayer player(SeatSeed(MixSeed(_matchSeed, _version), seat), _level);
        const std::optional<HandAction> chosen = player.Decide(_match.Hand(), seat);
        if (!chosen) {
            return std::nullopt;
        }
        return ActionToJson(*chosen);
    }

    bool MahjongSeatedGame::IsFinished() const {
        return _match.GetPhase() == MatchPhase::Finished;
    }

    uint64_t MahjongSeatedGame::GetStateVersion() const {
        return _version;
    }

}  // namespace BECore::Mahjong
