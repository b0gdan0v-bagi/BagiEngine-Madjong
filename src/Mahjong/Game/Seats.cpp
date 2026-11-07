#include "Seats.h"

namespace BECore::Mahjong {

    SeatWinds MakeSeatWinds(Seat east) noexcept {
        SeatWinds winds{};
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            winds[seat] = static_cast<Wind>(Offset(east, seat));
        }
        return winds;
    }

    Seat DecideEast(const SeatRoller& roll) {
        eastl::vector<Seat> running;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            running.push_back(seat);
        }
        while (running.size() > 1) {
            int best = 0;
            eastl::vector<Seat> leaders;
            for (const Seat seat : running) {
                const int total = roll(seat);
                if (total > best) {
                    best = total;
                    leaders.clear();
                }
                if (total == best) {
                    leaders.push_back(seat);
                }
            }
            running = eastl::move(leaders);
        }
        return running.front();
    }

    SeatWinds RollSeatWinds(MahjongRng& rng) {
        return MakeSeatWinds(DecideEast([&rng](Seat) { return rng.Roll2d6(); }));
    }

}  // namespace BECore::Mahjong
