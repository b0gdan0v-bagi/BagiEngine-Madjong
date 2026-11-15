#include "MahjongSetState.h"

#include <Mahjong/MahjongGame.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MatchLogJson.h>

namespace BECore::Mahjong {

    namespace {

        constexpr StringView kKeyDeal = "deal";
        constexpr StringView kKeyLog = "log";
        constexpr StringView kKeySeed = "seed";
        constexpr StringView kKeySeats = "seats";
        constexpr StringView kKeyLevel = "level";
        constexpr StringView kKeyLevels = "levels";
        constexpr StringView kKeySettings = "settings";
        constexpr StringView kKeyScores = "scores";
        constexpr StringView kKeyHandsPlayed = "handsPlayed";

        std::unexpected<ControlError> InvalidParams(String message) {
            return std::unexpected(MakeControlError(ControlErrors::kInvalidParams, std::move(message)));
        }

        bool Has(const Json& object, StringView key) {
            return FindJsonMember(object, key) != nullptr;
        }

        /// An array item as an integer, nullopt for anything else (a number above INT64_MAX included, as `GetJsonInt` refuses it).
        Optional<int64_t> IntOf(const Json& item) {
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

        std::unexpected<ControlError> NotAnInteger(StringView path, int64_t low, int64_t high) {
            return InvalidParams(Format("'{}' has to be an integer from {} to {}", path, low, high));
        }

        /// The member `key` of `object` as an integer in `low..high`; `path` names it in the error.
        std::expected<int64_t, ControlError> ReadInt(const Json& object, StringView key, StringView path, int64_t low, int64_t high) {
            const Optional<int64_t> value = GetJsonInt(object, key);
            if (!value || *value < low || *value > high) {
                return NotAnInteger(path, low, high);
            }
            return *value;
        }

        std::expected<uint32_t, ControlError> ReadSeed(const Json& state, uint32_t fallback) {
            if (!Has(state, kKeySeed)) {
                return fallback;
            }
            const auto seed = GetJsonInt(state, kKeySeed);
            if (!seed || *seed < 0 || *seed > static_cast<int64_t>(std::numeric_limits<uint32_t>::max())) {
                return InvalidParams("'seed' has to be an integer from 0 to 4294967295");
            }
            return static_cast<uint32_t>(*seed);
        }

        std::expected<void, ControlError> ReadSeatsAndLevels(const Json& state, SeatSetup& seats, SeatLevels& levels) {
            if (Has(state, kKeySeats)) {
                const auto letters = GetJsonString(state, kKeySeats);
                const Optional<SeatSetup> read = letters ? ReadSeats(*letters) : std::nullopt;
                if (!read) {
                    return InvalidParams("'seats' has to be four letters, P for a person and C for a computer, such as \"PCCC\"");
                }
                seats = *read;
            }
            if (Has(state, kKeyLevels)) {
                if (Has(state, kKeyLevel)) {
                    return InvalidParams("'level' and 'levels' cannot be given together");
                }
                const Optional<SeatLevels> read = ReadResetLevels(*FindJsonMember(state, kKeyLevels));
                if (!read) {
                    return InvalidParams(Format("'levels' has to be an array of {} integers, each {}", kSeatCount, LevelRangeText().c_str()));
                }
                levels = *read;
            } else if (Has(state, kKeyLevel)) {
                const auto number = GetJsonInt(state, kKeyLevel);
                const Optional<ComputerLevel> read = number ? LevelOf(*number) : std::nullopt;
                if (!read) {
                    return InvalidParams(Format("'level' has to be {}", LevelRangeText().c_str()));
                }
                levels = UniformLevels(*read);
            }
            return {};
        }

        /// `settings` of the call over the game's: every member optional, the ones left out keep the game's.
        std::expected<MahjongSettings, ControlError> ReadSettings(const Json& state, MahjongSettings settings) {
            const Json* member = FindJsonMember(state, kKeySettings);
            if (member == nullptr) {
                return settings;
            }
            if (!member->is_object()) {
                return InvalidParams("'settings' has to be an object");
            }
            if (Has(*member, "handsPerMatch")) {
                const auto hands = GetJsonInt(*member, "handsPerMatch");
                if (!hands || (*hands != kShortMatchHands && *hands != kFullMatchHands)) {
                    return InvalidParams(Format("'settings.handsPerMatch' has to be {} or {}", kShortMatchHands, kFullMatchHands));
                }
                settings.handsPerMatch = static_cast<int>(*hands);
            }
            if (Has(*member, "flowers")) {
                const auto flowers = GetJsonBool(*member, "flowers");
                if (!flowers) {
                    return InvalidParams("'settings.flowers' has to be true or false");
                }
                settings.flowers = *flowers;
            }
            if (Has(*member, "replayDrawnHand")) {
                const auto replay = GetJsonBool(*member, "replayDrawnHand");
                if (!replay) {
                    return InvalidParams("'settings.replayDrawnHand' has to be true or false");
                }
                settings.replayDrawnHand = *replay;
            }
            if (Has(*member, "limit")) {
                const auto limit = ReadInt(*member, "limit", "settings.limit", 0, kMaxSetStateLimit);
                if (!limit) {
                    return std::unexpected(limit.error());
                }
                settings.limit = static_cast<int>(*limit);
            }
            if (Has(*member, "startingScore")) {
                const auto starting = ReadInt(*member, "startingScore", "settings.startingScore", -kMaxSetStateScore, kMaxSetStateScore);
                if (!starting) {
                    return std::unexpected(starting.error());
                }
                settings.startingScore = static_cast<int>(*starting);
            }
            if (!IsPlayable(settings)) {
                return InvalidParams("the settings of the game cannot be played");
            }
            return settings;
        }

        /// `scores` (four integers), or `startingScore` for every seat when left out.
        std::expected<SeatScores, ControlError> ReadScores(const Json& state, int startingScore) {
            SeatScores scores{};
            const Json* member = FindJsonMember(state, kKeyScores);
            if (member == nullptr) {
                scores.fill(startingScore);
                return scores;
            }
            if (!member->is_array() || member->size() != kSeatCount) {
                return InvalidParams(Format("'scores' has to be an array of {} integers", kSeatCount));
            }
            size_t seat = 0;
            for (const Json& item : *member) {
                const Optional<int64_t> value = IntOf(item);
                if (!value || *value < -kMaxSetStateScore || *value > kMaxSetStateScore) {
                    return InvalidParams(Format("'scores' has to hold integers from {} to {}", -kMaxSetStateScore, kMaxSetStateScore));
                }
                scores[seat++] = static_cast<int>(*value);
            }
            return scores;
        }

    }  // namespace

    std::expected<SetStateDeal, ControlError> ParseSetState(const Json& state, const SetStateDefaults& defaults) {
        if (!state.is_object()) {
            return InvalidParams("set_state takes an object with 'deal', a position to start a match from");
        }
        if (Has(state, kKeyLog)) {
            return InvalidParams(Has(state, kKeyDeal) ? "'deal' and 'log' cannot be given together" : "'log' is not supported");
        }
        const Json* dealMember = FindJsonMember(state, kKeyDeal);
        if (dealMember == nullptr) {
            return InvalidParams("set_state needs 'deal' (a position) or 'log' (a match log)");
        }

        SetStateDeal request;
        const auto seed = ReadSeed(state, defaults.seed);
        if (!seed) {
            return std::unexpected(seed.error());
        }
        request.seed = *seed;
        request.seats = defaults.seats;
        request.levels = defaults.levels;
        if (const auto read = ReadSeatsAndLevels(state, request.seats, request.levels); !read) {
            return std::unexpected(read.error());
        }
        const auto settings = ReadSettings(state, defaults.settings);
        if (!settings) {
            return std::unexpected(settings.error());
        }
        request.settings = *settings;

        if (Has(state, kKeyHandsPlayed)) {
            const auto played = ReadInt(state, kKeyHandsPlayed, kKeyHandsPlayed, 0, request.settings.handsPerMatch - 1);
            if (!played) {
                return std::unexpected(played.error());
            }
            request.start.handsPlayed = static_cast<int>(*played);
        }
        const auto scores = ReadScores(state, request.settings.startingScore);
        if (!scores) {
            return std::unexpected(scores.error());
        }
        request.start.scores = *scores;

        auto deal = FixedDealFromJson(*dealMember);
        if (!deal) {
            return InvalidParams(Format("'deal': {}", deal.error()));
        }
        request.start.deal = std::move(*deal);
        if (const auto wall = TryBuildFixedWall(request.start.deal, request.settings.flowers); !wall) {
            return InvalidParams(Format("the deal cannot be played: {}", wall.error()));
        }
        return request;
    }

    std::expected<void, ControlError> StartSetState(MahjongGame& game, const SetStateDeal& request) {
        Match scratch;
        if (const MatchStatus tried = scratch.Start(request.settings, request.seed, request.start); !tried) {
            return InvalidParams(Format("the position cannot be played: {}", EnumUtils<MatchError>::ToString(tried.error())));
        }
        const MahjongSettings oldSettings = game.GetSettings();
        const SeatSetup oldSeats = game.GetSeats();
        const SeatLevels oldLevels = game.GetLevels();
        game.SetSettings(request.settings);
        game.SetSeats(request.seats);
        game.SetLevels(request.levels);
        if (const MatchStatus started = game.StartFixedMatch(request.seed, request.start); !started) {
            game.SetSettings(oldSettings);  // cannot happen after the scratch match took it, but a refusal must change nothing
            game.SetSeats(oldSeats);
            game.SetLevels(oldLevels);
            return InvalidParams(Format("the match did not start: {}", EnumUtils<MatchError>::ToString(started.error())));
        }
        game.GetLocalSession().RunComputers();
        return {};
    }

}  // namespace BECore::Mahjong
