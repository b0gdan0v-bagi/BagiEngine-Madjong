#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/Match.h>

namespace BECore::Mahjong {

    /**
     * @brief One computer seat: looks at its `TableView` and chooses an element of `view.legal`.
     *
     * `Choose` dispatches on the phase: `ChooseTurnAction` on the own turn, `ChooseClaimAction` in a claim window or a robbed-kong
     * window. The player owns a `MahjongRng` that only breaks ties, so one seed gives the same play on every platform. It never sees
     * a `HandModel` except through `Decide`, which makes the view first. It has a `ComputerLevel` (default Normal, the player described
     * in `CLAUDE.md`) and plays by `ProfileOf(level)`; the constructor that takes a `LevelProfile` plays by those numbers (tests and the
     * arena play a profile that is no level, e.g. a search seat) and reports `Level()` Normal.
     */
    class ComputerPlayer {
    public:
        explicit ComputerPlayer(uint32_t seed, ComputerLevel level = kDefaultLevel) : _rng(seed), _level(level), _profile(ProfileOf(level)) {}
        ComputerPlayer(uint32_t seed, const LevelProfile& profile) : _rng(seed), _profile(profile) {}

        [[nodiscard]] ComputerLevel Level() const noexcept {
            return _level;
        }

        /// An element of `view.legal`. The caller asks only a seat that can act: a view with no legal action asserts in debug and answers
        /// `Pass` (which the model then refuses).
        [[nodiscard]] HandAction Choose(const TableView& view);

        /// `Choose` on `MakeTableView(model, seat)`; nullopt when `model.LegalActions(seat)` is empty (nothing to do, or not a seat).
        [[nodiscard]] std::optional<HandAction> Decide(const HandModel& model, Seat seat);

    private:
        MahjongRng _rng;
        ComputerLevel _level = kDefaultLevel;
        LevelProfile _profile;
    };

    /// The rng seed of seat `seat` at a table seeded `tableSeed`: a fixed mixing, one roll of an `MahjongRng` seeded `tableSeed * 4 + seat`
    /// (never a draw from the machine), so the four seats play differently and one table seed is one table. A number that is no seat is
    /// mixed like any other.
    [[nodiscard]] uint32_t SeatSeed(uint32_t tableSeed, Seat seat);

    /// What `ComputerTable::Step` did: the action it applied (an element of the actor's `LegalActions`), or why the model refused it.
    using StepResult = std::expected<HandAction, HandError>;

    /**
     * @brief Four computer players at one table: they play a hand, or a whole match, with no person.
     *
     * Seat `i` is a `ComputerPlayer` seeded `SeatSeed(tableSeed, i)`. `Step` asks the next actor for an action and applies it through
     * `HandModel::Apply`; it never falls back to another action, so an action the model refuses is a defect and comes back as the
     * error. The table does not know a seat setup (which seats are people) yet; it drives whatever model it is given. Each seat has
     * a level of its own (`SeatLevels`, `LevelOf`); the seat seeds do not depend on any level, so one table seed gives the same
     * tie-breaks at every level, and a table of `UniformLevels(l)` plays exactly what the one-level constructor made.
     */
    class ComputerTable {
    public:
        /// Every seat plays at `level`.
        explicit ComputerTable(uint32_t seed, ComputerLevel level = kDefaultLevel);
        /// Seat `i` plays at `levels[i]`.
        ComputerTable(uint32_t seed, const SeatLevels& levels);
        /// Every seat plays by `profile` (and `LevelOf` answers Normal for each); the seat seeds are those of the level constructors.
        ComputerTable(uint32_t seed, const LevelProfile& profile);

        /// The level of the player of `seat`; the default level for a number that is no seat.
        [[nodiscard]] ComputerLevel LevelOf(Seat seat) const noexcept {
            return IsSeat(seat) ? _players[static_cast<size_t>(seat)].Level() : kDefaultLevel;
        }

        /// The seat to ask next: `CurrentSeat()` in `Phase::Discard`, the first of `PendingClaimSeats()` in a window, `kNoSeat` when
        /// the hand is `Finished`.
        [[nodiscard]] static Seat NextActor(const HandModel& model);

        /// One action of the next actor, applied. `WrongPhase` on a finished hand, `NotYourTurn` when the next actor has no legal action
        /// (cannot happen on a model that is not finished), otherwise the error of `Apply`.
        StepResult Step(HandModel& model);

        /// `Step` until the hand is finished; the first refusal ends it and is returned.
        HandStatus PlayHand(HandModel& model);

        /// Hands of `match` are played and ended (`Match::EndHand`) until the match is `Finished`; the first refusal of a hand ends it. A
        /// finished hand that `EndHand` refuses is a defect (asserts in debug) and is answered `WrongPhase`.
        HandStatus PlayMatch(Match& match);

    private:
        eastl::array<ComputerPlayer, kSeatCount> _players;
    };

}  // namespace BECore::Mahjong
