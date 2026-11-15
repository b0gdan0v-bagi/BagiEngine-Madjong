#pragma once

#include <BECore/Control/Json.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/FixedWall.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/Game/TableSession.h>

namespace BECore::Mahjong {

    class MahjongGame;

    /// The most a `settings.limit` of a `set_state` may name, and the largest magnitude of a score or `startingScore`: far above any
    /// play, small enough that no payment of a hand can overflow an `int`.
    inline constexpr int kMaxSetStateLimit = 1'000'000;
    inline constexpr int kMaxSetStateScore = 100'000'000;

    /// What a `set_state` that leaves a member out takes: the game's own choices and the seed of the last `reset`.
    struct SetStateDefaults {
        SeatSetup seats = kDefaultSeatSetup;
        SeatLevels levels = UniformLevels(kDefaultLevel);
        MahjongSettings settings;
        uint32_t seed = 0;
    };

    /// A `set_state` with a fixed deal, read and checked as far as the text goes: everything the match needs to start.
    struct SetStateDeal {
        FixedStart start;
        uint32_t seed = 0;
        SeatSetup seats = kDefaultSeatSetup;
        SeatLevels levels = UniformLevels(kDefaultLevel);
        MahjongSettings settings;
    };

    /// A `set_state` with the prefix of a match log: the events to put the match where they leave it, the first `Start` already
    /// carrying the seats and levels of the call.
    struct SetStateLog {
        Vector<MatchLogEvent> events;
    };

    using SetStateRequest = Variant<SetStateDeal, SetStateLog>;

    /**
     * @brief Reads the `params` of a `set_state` (`src/Mahjong/CLAUDE.md`, "Control API"), one of two forms. A pure function of the
     *        JSON and the defaults; it touches no game.
     *
     * - `{"deal": {...}, "seed"?, "seats"?, "level"? | "levels"?, "settings"?, "scores"?, "handsPlayed"?}`: a match from a fixed deal.
     *   `deal` is the object `FixedDealFromJson` reads, so a `state` copied out of a match log is a valid `set_state`.
     * - `{"log": "<text of a .jsonl>", "until"?, "seats"?, "level"? | "levels"?}`: the first `until` events of a match log (all when
     *   absent; 0 or more than there are is refused), parsed by `ParseMatchLog`.
     *
     * Client data: only the checked helpers of `BECore/Control/Json.h`. Anything malformed or impossible (a deal the strict wall
     * refuses, a `handsPlayed` outside the match, a log that does not parse, a key of the other form, `deal` with `log`, neither,
     * `level` with `levels`) is `invalid_params` whose text names the member.
     */
    [[nodiscard]] std::expected<SetStateRequest, ControlError> ParseSetState(const Json& state, const SetStateDefaults& defaults);

    /**
     * @brief Starts the match of `request` on `game`: the position or the log is tried on a scratch `Match` first, and only then the
     *        game's seats, levels (and settings, for a deal) are set (kept for the next match, as after `reset`), the match begins
     *        (`MahjongGame::StartFixedMatch`, `StartFromLog`) and the computers run to the first person. A refusal changes nothing.
     */
    std::expected<void, ControlError> StartSetState(MahjongGame& game, const SetStateRequest& request);

}  // namespace BECore::Mahjong
