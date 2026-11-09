#include "MatchLog.h"

namespace BECore::Mahjong {

    namespace {

        bool SameSettings(const MahjongSettings& a, const MahjongSettings& b) noexcept {
            return a.handsPerMatch == b.handsPerMatch && a.flowers == b.flowers && a.replayDrawnHand == b.replayDrawnHand && a.limit == b.limit && a.startingScore == b.startingScore;
        }

        /// What replays of a settlement: the outcome and the money, not the breakdowns.
        bool SameSettlement(const HandSettlement& a, const HandSettlement& b) noexcept {
            return a.kind == b.kind && a.winner == b.winner && a.east == b.east && a.roundWind == b.roundWind && a.scores == b.scores && a.deltas == b.deltas;
        }

        /// The `Hand` event of the hand `match` has dealt, as a front end writes it; a replay compares it with the event of the log.
        MatchLogEvent DealtHandOf(const Match& match, const MatchLogEvent& like) {
            MatchLogEvent dealt = like;
            dealt.number = match.HandsPlayed();
            dealt.east = match.East();
            dealt.roundWind = match.RoundWind();
            dealt.winds = match.Winds();
            for (Seat seat = 0; seat < kSeatCount; ++seat) {
                dealt.concealed[static_cast<size_t>(seat)] = match.Hand().GetHand(seat).concealed;
                dealt.bonus[static_cast<size_t>(seat)] = match.Hand().GetHand(seat).bonus;
            }
            return dealt;
        }

        ReplayError At(size_t index, ReplayProblem problem) noexcept {
            return ReplayError{index, problem};
        }

    }  // namespace

    bool MatchLogEvent::operator==(const MatchLogEvent& other) const {
        if (kind != other.kind) {
            return false;
        }
        switch (kind) {
            case MatchLogKind::Start:
                return seed == other.seed && SameSettings(settings, other.settings) && seats == other.seats;
            case MatchLogKind::Hand:
                return number == other.number && east == other.east && roundWind == other.roundWind && winds == other.winds && concealed == other.concealed && bonus == other.bonus;
            case MatchLogKind::Action:
                return seat == other.seat && by == other.by && action == other.action;
            case MatchLogKind::Settlement:
                return SameSettlement(settlement, other.settlement);
            case MatchLogKind::End:
                return scores == other.scores && standings == other.standings;
        }
        return false;
    }

    ReplayStatus ReplayMatchLog(const eastl::vector<MatchLogEvent>& events, Match& match) {
        if (events.empty() || events.front().kind != MatchLogKind::Start) {
            return std::unexpected(At(0, ReplayProblem::NoStart));
        }
        for (size_t index = 0; index < events.size(); ++index) {
            const MatchLogEvent& event = events[index];
            switch (event.kind) {
                case MatchLogKind::Start:
                    if (!match.Start(event.settings, event.seed)) {
                        return std::unexpected(At(index, ReplayProblem::BadSettings));
                    }
                    break;
                case MatchLogKind::Hand:
                    if (match.GetPhase() != MatchPhase::InHand) {
                        return std::unexpected(At(index, ReplayProblem::NoStart));
                    }
                    if (!(event == DealtHandOf(match, event))) {
                        return std::unexpected(At(index, ReplayProblem::HandMismatch));
                    }
                    break;
                case MatchLogKind::Action:
                    if (match.GetPhase() != MatchPhase::InHand || !match.Hand().Apply(event.seat, event.action)) {
                        return std::unexpected(At(index, ReplayProblem::ActionRefused));
                    }
                    break;
                case MatchLogKind::Settlement: {
                    const std::expected<HandSettlement, MatchError> settled = match.EndHand();
                    if (!settled) {
                        return std::unexpected(At(index, ReplayProblem::SettlementRefused));
                    }
                    if (!SameSettlement(*settled, event.settlement)) {
                        return std::unexpected(At(index, ReplayProblem::SettlementMismatch));
                    }
                    break;
                }
                case MatchLogKind::End:
                    if (match.GetPhase() != MatchPhase::Finished || match.Scores() != event.scores || match.Standings() != event.standings) {
                        return std::unexpected(At(index, ReplayProblem::EndMismatch));
                    }
                    break;
            }
        }
        return {};
    }

}  // namespace BECore::Mahjong
