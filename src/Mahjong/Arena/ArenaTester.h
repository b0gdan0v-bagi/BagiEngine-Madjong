#pragma once

#include <Mahjong/Analyzer/AnalyzerPosition.h>
#include <Mahjong/Arena/ArenaLogs.h>
#include <Mahjong/Arena/ArenaPlayer.h>
#include <Mahjong/Arena/ArenaStats.h>

namespace BECore::Mahjong::Arena {

    /// The two kinds of decision of a person: the own turn (a discard, a win, a kong) and the answer to a discard or to a kong (a
    /// claim window, a robbed-kong window included).
    CORE_ENUM(TesterPhase, uint8_t, DiscardTurn, ClaimWindow)

    inline constexpr size_t kTesterPhaseCount = EnumUtils<TesterPhase>::Count();
    /// The rows of an agreement: each phase, and both together (the last).
    inline constexpr size_t kTesterRowCount = kTesterPhaseCount + 1;
    inline constexpr size_t kTesterBothRow = kTesterPhaseCount;

    [[nodiscard]] TesterPhase TesterPhaseOf(Phase phase) noexcept;

    /// What a bot made of one decision of a person.
    struct TesterChoice {
        TesterPhase phase = TesterPhase::DiscardTurn;
        /// The view has one legal action: nothing was decided, nothing is counted.
        bool forced = false;
        HandAction chosen;
        /// `chosen` is the logged action (the same `HandAction`, `declareCalling` included).
        bool equal = false;
    };

    /**
     * @brief The choice of `bot` on `view`, set against what the person did (`logged`).
     *
     * **Only the `TableView` goes into a bot:** the parameters are the view, the logged action (to compare, not to look at), the bot and
     * a number for its tie-breaking rng; there is no model, no deal and no other seat's tiles, so a person's decision can be judged by
     * nothing a person could not see. `bot` is a computer player (a level, knobs, a search), not `random`.
     */
    [[nodiscard]] TesterChoice AnalyzeTesterDecision(const TableView& view, const HandAction& logged, const ArenaPlayer& bot, uint32_t seed);

    /// The rng seed of the decision at `hand` (1-based) and `actionIndex` (0-based) of the log `logName`: FNV-1a over the bytes of the
    /// name and of the two numbers (four bytes each, least significant first) from a constant start, so one log gives one answer on
    /// every platform and nothing of the machine, the order or the other bots is in it.
    [[nodiscard]] uint32_t DecisionSeed(std::string_view logName, int hand, int actionIndex);

    /// Empty when every player of `bots` can be asked about a view; else the text that says why not (`random` is no bot here) and
    /// when `bots` is empty.
    [[nodiscard]] eastl::string ValidateTesterBots(const eastl::vector<ArenaPlayer>& bots);

    /// The decisions that were counted and the ones that agreed, per phase.
    struct PhaseCount {
        int64_t decisions = 0;
        int64_t equal = 0;

        bool operator==(const PhaseCount&) const = default;
    };
    using PhaseCounts = eastl::array<PhaseCount, kTesterPhaseCount>;

    /// A decision where the person's logged action is not what the reference bot chooses. The numbers are those of
    /// `Analyzer::PositionFromLog` (hand 1-based, action index 0-based), so `--log <log> --hand K --action I` of the analyzer opens it.
    struct TesterDifference {
        eastl::string log;
        int hand = 0;
        int actionIndex = 0;
        Seat seat = kNoSeat;
        HandAction logged;
        HandAction chosen;
    };

    /// One match against all the bots.
    struct TesterMatch {
        /// One entry per bot, in the order of the bots.
        eastl::vector<PhaseCounts> bots;
        /// Against the first bot, in log order (hand, then action).
        eastl::vector<TesterDifference> differences;
    };

    /**
     * @brief Every decision of a person in the replayed match `events` (called `logName`), judged by each of `bots`.
     *
     * A decision is an `Action` event with `by` `Person`; its view is `Analyzer::PositionsOfSeat` of that seat and hand. A decision
     * with one legal action is forced and not counted. The tie-breaking rng of a choice is `DecisionSeed(logName, hand, actionIndex)`,
     * the same for every bot. A log with no person acts gives zero decisions and no error. The error text names the log and what the
     * position could not be built from. `bots` has been through `ValidateTesterBots`.
     */
    [[nodiscard]] std::expected<TesterMatch, eastl::string> AnalyzeTesterMatch(std::string_view logName, const eastl::vector<MatchLogEvent>& events, const eastl::vector<ArenaPlayer>& bots);

    /// The agreement of one bot with the people: the counts, and the share of equal choices in percent with its interval.
    struct BotAgreement {
        ArenaPlayer player;
        eastl::array<int64_t, kTesterRowCount> decisions{};
        eastl::array<int64_t, kTesterRowCount> equal{};
        /// The **match** is the unit: the mean over the matches of their own share (a match with no decision in the row is left out),
        /// with the 95 % interval of `MeanCi`. The decisions of one match are not independent, so they are not the sample.
        eastl::array<MeanInterval, kTesterRowCount> share{};
    };

    struct TesterResult {
        /// In the order of the bots; the first is the reference of `differences`.
        eastl::vector<BotAgreement> bots;
        /// Every difference, in the order of the matches and then of the log. Printing may cap them; the count is always `size()`.
        eastl::vector<TesterDifference> differences;
        /// The matches that were analysed.
        int matches = 0;
    };

    /// The agreement of `bots` over `matches` (the results of `AnalyzeTesterMatch`, in name order). The same matches give the same result.
    [[nodiscard]] TesterResult MergeTesterMatches(const eastl::vector<ArenaPlayer>& bots, const eastl::vector<TesterMatch>& matches);

}  // namespace BECore::Mahjong::Arena
