#include "ArenaTester.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerPlayer.h>

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr uint32_t kFnvBasis = 2166136261U;
        constexpr uint32_t kFnvPrime = 16777619U;
        constexpr int kByteBits = 8;
        constexpr int kBytesPerNumber = 4;
        constexpr uint32_t kByteMask = 0xFFU;

        void MixByte(uint32_t& hash, uint32_t byte) {
            hash ^= byte;
            hash *= kFnvPrime;
        }

        void MixNumber(uint32_t& hash, uint32_t number) {
            for (int byte = 0; byte < kBytesPerNumber; ++byte) {
                MixByte(hash, (number >> (byte * kByteBits)) & kByteMask);
            }
        }

        /// An `Action` event of the log, numbered as `Analyzer::LogPosition` numbers it.
        struct ActionRef {
            int hand = 0;
            int index = 0;
            Seat seat = kNoSeat;
            SeatKind by = SeatKind::Computer;
        };

        eastl::vector<ActionRef> ActionRefs(const eastl::vector<MatchLogEvent>& events) {
            eastl::vector<ActionRef> refs;
            int hand = 1;
            int index = 0;
            for (const MatchLogEvent& event : events) {
                if (event.kind == MatchLogKind::Settlement) {
                    ++hand;
                    index = 0;
                } else if (event.kind == MatchLogKind::Action) {
                    refs.push_back({hand, index++, event.seat, event.by});
                }
            }
            return refs;
        }

        /// The (hand, seat) pairs in which a person acts, in log order.
        eastl::vector<eastl::pair<int, Seat>> PersonTurns(const eastl::vector<ActionRef>& refs) {
            eastl::vector<eastl::pair<int, Seat>> turns;
            for (const ActionRef& ref : refs) {
                if (ref.by != SeatKind::Person) {
                    continue;
                }
                const eastl::pair<int, Seat> turn(ref.hand, ref.seat);
                if (eastl::find(turns.begin(), turns.end(), turn) == turns.end()) {
                    turns.push_back(turn);
                }
            }
            return turns;
        }

        const ActionRef* FindRef(const eastl::vector<ActionRef>& refs, int hand, int index) {
            for (const ActionRef& ref : refs) {
                if (ref.hand == hand && ref.index == index) {
                    return &ref;
                }
            }
            return nullptr;
        }

        bool IsBefore(const TesterDifference& a, const TesterDifference& b) {
            return a.hand != b.hand ? a.hand < b.hand : a.actionIndex < b.actionIndex;
        }

    }  // namespace

    TesterPhase TesterPhaseOf(Phase phase) noexcept {
        return phase == Phase::Discard ? TesterPhase::DiscardTurn : TesterPhase::ClaimWindow;
    }

    TesterChoice AnalyzeTesterDecision(const TableView& view, const HandAction& logged, const ArenaPlayer& bot, uint32_t seed) {
        ASSERT(!bot.random);
        TesterChoice choice;
        choice.phase = TesterPhaseOf(view.phase);
        if (view.legal.size() < 2 || bot.random) {
            choice.forced = true;
            return choice;
        }
        ComputerPlayer player(seed, bot.profile);
        choice.chosen = player.Choose(view);
        choice.equal = choice.chosen == logged;
        return choice;
    }

    uint32_t DecisionSeed(std::string_view logName, int hand, int actionIndex) {
        uint32_t hash = kFnvBasis;
        for (const char letter : logName) {
            MixByte(hash, static_cast<uint8_t>(letter));
        }
        MixNumber(hash, static_cast<uint32_t>(hand));
        MixNumber(hash, static_cast<uint32_t>(actionIndex));
        return hash;
    }

    eastl::string ValidateTesterBots(const eastl::vector<ArenaPlayer>& bots) {
        if (bots.empty()) {
            return "no bot to compare the people with";
        }
        for (const ArenaPlayer& bot : bots) {
            if (bot.random) {
                return Format("{} is no bot to compare with: the random seats do not look at the table", bot.name.c_str());
            }
        }
        return {};
    }

    std::expected<TesterMatch, eastl::string> AnalyzeTesterMatch(std::string_view logName, const eastl::vector<MatchLogEvent>& events, const eastl::vector<ArenaPlayer>& bots) {
        const eastl::vector<ActionRef> refs = ActionRefs(events);
        TesterMatch match;
        match.bots.resize(bots.size());
        for (const auto& [hand, seat] : PersonTurns(refs)) {
            const std::expected<eastl::vector<Analyzer::LogPosition>, eastl::string> positions = Analyzer::PositionsOfSeat(events, hand, seat);
            if (!positions.has_value()) {
                return std::unexpected(Format("{}: {}", logName, positions.error().c_str()));
            }
            for (const Analyzer::LogPosition& position : *positions) {
                const ActionRef* ref = FindRef(refs, position.hand, position.actionIndex);
                if (ref == nullptr || ref->by != SeatKind::Person) {
                    continue;
                }
                const uint32_t seed = DecisionSeed(logName, position.hand, position.actionIndex);
                for (size_t bot = 0; bot < bots.size(); ++bot) {
                    const TesterChoice choice = AnalyzeTesterDecision(position.view, position.logged, bots[bot], seed);
                    if (choice.forced) {
                        continue;
                    }
                    PhaseCount& count = match.bots[bot][static_cast<size_t>(choice.phase)];
                    ++count.decisions;
                    count.equal += choice.equal ? 1 : 0;
                    if (bot == 0 && !choice.equal) {
                        match.differences.push_back({eastl::string(logName.data(), logName.size()), position.hand, position.actionIndex, position.seat, position.logged, choice.chosen});
                    }
                }
            }
        }
        eastl::sort(match.differences.begin(), match.differences.end(), IsBefore);
        return match;
    }

    TesterResult MergeTesterMatches(const eastl::vector<ArenaPlayer>& bots, const eastl::vector<TesterMatch>& matches) {
        TesterResult result;
        result.matches = static_cast<int>(matches.size());
        for (size_t bot = 0; bot < bots.size(); ++bot) {
            BotAgreement agreement;
            agreement.player = bots[bot];
            eastl::array<eastl::vector<double>, kTesterRowCount> shares;
            for (const TesterMatch& match : matches) {
                if (bot >= match.bots.size()) {
                    continue;
                }
                PhaseCount both;
                for (size_t phase = 0; phase < kTesterPhaseCount; ++phase) {
                    const PhaseCount& count = match.bots[bot][phase];
                    both.decisions += count.decisions;
                    both.equal += count.equal;
                    agreement.decisions[phase] += count.decisions;
                    agreement.equal[phase] += count.equal;
                    if (count.decisions > 0) {
                        shares[phase].push_back(kPercent * static_cast<double>(count.equal) / static_cast<double>(count.decisions));
                    }
                }
                agreement.decisions[kTesterBothRow] += both.decisions;
                agreement.equal[kTesterBothRow] += both.equal;
                if (both.decisions > 0) {
                    shares[kTesterBothRow].push_back(kPercent * static_cast<double>(both.equal) / static_cast<double>(both.decisions));
                }
            }
            for (size_t row = 0; row < kTesterRowCount; ++row) {
                agreement.share[row] = MeanCi(shares[row]);
            }
            result.bots.push_back(eastl::move(agreement));
        }
        for (const TesterMatch& match : matches) {
            result.differences.insert(result.differences.end(), match.differences.begin(), match.differences.end());
        }
        return result;
    }

}  // namespace BECore::Mahjong::Arena
