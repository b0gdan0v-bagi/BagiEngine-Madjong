#pragma once

#include <Mahjong/Analyzer/AnalyzerPosition.h>
#include <Mahjong/Arena/ArenaLogs.h>
#include <Mahjong/Arena/ArenaPlayer.h>
#include <Mahjong/Arena/ArenaStats.h>

namespace BECore::Mahjong::Arena {

    /// The own turn (a discard, a win, a kong) and the answer to a discard or to a kong (a robbed-kong window included).
    CORE_ENUM(TesterPhase, uint8_t, DiscardTurn, ClaimWindow)

    inline constexpr size_t kTesterPhaseCount = EnumUtils<TesterPhase>::Count();
    /// Each phase, and both together (the last).
    inline constexpr size_t kTesterRowCount = kTesterPhaseCount + 1;
    inline constexpr size_t kTesterBothRow = kTesterPhaseCount;

    [[nodiscard]] TesterPhase TesterPhaseOf(Phase phase) noexcept;

    struct TesterChoice {
        TesterPhase phase = TesterPhase::DiscardTurn;
        /// One legal action: nothing was decided, nothing is counted.
        bool forced = false;
        HandAction chosen;
        /// The same `HandAction` as the logged one, `declareCalling` included.
        bool equal = false;
    };

    /**
     * @brief The choice of `bot` on `view`, set against what the person did (`logged`).
     *
     * **Only the `TableView` goes into a bot:** no model, no deal, no other seat's tiles; `logged` is to compare, not to look at.
     * `bot` is a computer player, not `random`.
     */
    [[nodiscard]] TesterChoice AnalyzeTesterDecision(const TableView& view, const HandAction& logged, const ArenaPlayer& bot, uint32_t seed);

    /// The rng seed of the decision at `hand` (1-based) and `actionIndex` (0-based) of the log `logName`: FNV-1a over the name and the
    /// two numbers (four bytes each, least significant first), so one log gives one answer on every platform; nothing of the machine,
    /// the order or the other bots is in it.
    [[nodiscard]] uint32_t DecisionSeed(StringView logName, int hand, int actionIndex);

    /// Empty when every player of `bots` can be asked about a view; `random` is no bot here, and `bots` must not be empty.
    [[nodiscard]] String ValidateTesterBots(const Vector<ArenaPlayer>& bots);

    struct PhaseCount {
        int64_t decisions = 0;
        int64_t equal = 0;

        bool operator==(const PhaseCount&) const = default;
    };
    using PhaseCounts = Array<PhaseCount, kTesterPhaseCount>;

    /// The numbers are those of `Analyzer::PositionFromLog` (hand 1-based, action index 0-based), so `--log <log> --hand K --action I`
    /// of the analyzer opens it.
    struct TesterDifference {
        String log;
        int hand = 0;
        int actionIndex = 0;
        Seat seat = kNoSeat;
        HandAction logged;
        HandAction chosen;
    };

    struct TesterMatch {
        Vector<PhaseCounts> bots;
        /// Against the first bot only, in log order.
        Vector<TesterDifference> differences;
    };

    /**
     * @brief Every decision of a person in the replayed match `events` (called `logName`), judged by each of `bots`.
     *
     * A decision is an `Action` event with `by` `Person`, its view `Analyzer::PositionsOfSeat`. A forced decision is not counted. The
     * rng of a choice is `DecisionSeed(logName, hand, actionIndex)`, the same for every bot. A log with no person acts gives zero
     * decisions and no error. `bots` has been through `ValidateTesterBots`.
     */
    [[nodiscard]] std::expected<TesterMatch, String> AnalyzeTesterMatch(StringView logName, const Vector<MatchLogEvent>& events, const Vector<ArenaPlayer>& bots);

    struct BotAgreement {
        ArenaPlayer player;
        Array<int64_t, kTesterRowCount> decisions{};
        Array<int64_t, kTesterRowCount> equal{};
        /// The **match** is the unit (a match with no decision in the row is left out): the decisions of one match are not independent,
        /// so they are not the sample.
        Array<MeanInterval, kTesterRowCount> share{};
    };

    struct TesterResult {
        /// The first is the reference of `differences`.
        Vector<BotAgreement> bots;
        /// Printing may cap them; the count is always `size()`.
        Vector<TesterDifference> differences;
        int matches = 0;
    };

    /// `matches` are in name order.
    [[nodiscard]] TesterResult MergeTesterMatches(const Vector<ArenaPlayer>& bots, const Vector<TesterMatch>& matches);

}  // namespace BECore::Mahjong::Arena
