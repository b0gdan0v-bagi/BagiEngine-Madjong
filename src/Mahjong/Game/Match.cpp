#include "Match.h"

#include <EASTL/algorithm.h>
#include <EASTL/sort.h>

namespace BECore::Mahjong {

    MatchProgress NextProgress(MatchProgress progress, ResultKind kind, Seat winner, const MahjongSettings& settings) noexcept {
        if (kind == ResultKind::Drawn && settings.replayDrawnHand) {
            return progress;
        }
        const bool eastKeeps = kind == ResultKind::Win && winner == progress.east;
        if (!eastKeeps || kEastRepeatCountsAsHand) {
            ++progress.handsPlayed;
        }
        if (!eastKeeps) {
            progress.east = NextSeat(progress.east);
        }
        return progress;
    }

    Wind RoundWindOf(int handsPlayed, const MahjongSettings& settings) noexcept {
        const int handsPerRound = settings.handsPerMatch / kRoundsPerMatch;
        if (handsPerRound <= 0 || handsPlayed <= 0) {
            return Wind::East;
        }
        return static_cast<Wind>(eastl::min(handsPlayed / handsPerRound, kRoundsPerMatch - 1));
    }

    bool IsMatchOver(int handsPlayed, const MahjongSettings& settings) noexcept {
        return handsPlayed >= settings.handsPerMatch;
    }

    bool IsPlayable(const MahjongSettings& settings) noexcept {
        return settings.handsPerMatch > 0 && settings.handsPerMatch % kRoundsPerMatch == 0;
    }

    MatchStatus Match::DealHand(const MatchProgress& progress, HandModel& hand) {
        Wall wall = MakeWall(_rng, _settings, progress.east);
        const HandStatus begun = hand.Begin(_settings, eastl::move(wall), MakeSeatWinds(progress.east), RoundWindOf(progress.handsPlayed, _settings));
        if (!begun.has_value()) {
            return std::unexpected(MatchError::HandRefused);
        }
        return {};
    }

    MatchStatus Match::Start(const MahjongSettings& settings, uint32_t seed) {
        if (!IsPlayable(settings)) {
            return std::unexpected(MatchError::BadSettings);
        }
        Match next;
        next._settings = settings;
        next._rng.Seed(seed);
        next._progress = MatchProgress{0, EastOf(RollSeatWinds(next._rng))};
        next._scores.fill(settings.startingScore);
        if (const MatchStatus dealt = next.DealHand(next._progress, next._hand); !dealt.has_value()) {
            return dealt;
        }
        next._phase = MatchPhase::InHand;
        *this = eastl::move(next);
        return {};
    }

    std::expected<HandSettlement, MatchError> Match::EndHand() {
        if (_phase != MatchPhase::InHand) {
            return std::unexpected(MatchError::NotInHand);
        }
        if (_hand.GetPhase() != Phase::Finished) {
            return std::unexpected(MatchError::HandNotFinished);
        }
        HandSettlement settlement = SettleHand(_hand);
        if (const MatchStatus applied = ApplySettlement(settlement); !applied.has_value()) {
            return std::unexpected(applied.error());
        }
        return settlement;
    }

    MatchStatus Match::ApplySettlement(const HandSettlement& settlement) {
        if (_phase != MatchPhase::InHand) {
            return std::unexpected(MatchError::NotInHand);
        }
        const MatchProgress next = NextProgress(_progress, settlement.kind, settlement.winner, _settings);
        const bool over = IsMatchOver(next.handsPlayed, _settings);
        HandModel nextHand;
        if (!over) {
            // Before anything changes, so a refusal leaves the match as it was.
            if (const MatchStatus dealt = DealHand(next, nextHand); !dealt.has_value()) {
                return dealt;
            }
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            _scores[seat] += settlement.deltas[seat];
        }
        _history.push_back(settlement);
        _progress = next;
        if (over) {
            _phase = MatchPhase::Finished;
        } else {
            _hand = eastl::move(nextHand);
        }
        return {};
    }

    eastl::array<Seat, kSeatCount> Match::Standings() const {
        eastl::array<Seat, kSeatCount> seats{};
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            seats[seat] = seat;
        }
        // A total order (the seat breaks a tie), so the unstable sort gives one answer.
        eastl::sort(seats.begin(), seats.end(), [this](Seat a, Seat b) { return _scores[a] != _scores[b] ? _scores[a] > _scores[b] : a < b; });
        return seats;
    }

}  // namespace BECore::Mahjong
