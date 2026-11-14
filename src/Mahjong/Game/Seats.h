#pragma once

#include <EASTL/array.h>
#include <Mahjong/Game/MahjongRng.h>

namespace BECore::Mahjong {

    /// A seat is an index 0..3 around the table in counter-clockwise order (the order of play).
    using Seat = int;
    inline constexpr int kSeatCount = 4;

    /// The numbering is the same as the winds of Tile.
    CORE_ENUM(Wind, uint8_t, East, South, West, North)

    /// No seat (the winner of a drawn hand, the East of winds that name none).
    inline constexpr Seat kNoSeat = -1;

    [[nodiscard]] constexpr bool IsSeat(Seat seat) noexcept {
        return seat >= 0 && seat < kSeatCount;
    }

    [[nodiscard]] constexpr Seat NextSeat(Seat seat) noexcept {
        return (seat + 1) % kSeatCount;
    }

    /// The counter-clockwise distance from `from` to `to`: 1..3 for two different seats, 0 for the same seat.
    [[nodiscard]] constexpr int Offset(Seat from, Seat to) noexcept {
        return (to - from + kSeatCount) % kSeatCount;
    }

    using SeatWinds = Array<Wind, kSeatCount>;

    /// The seat winds when `east` is the dealer: South is `NextSeat(east)`, then West, then North.
    [[nodiscard]] SeatWinds MakeSeatWinds(Seat east) noexcept;

    /// The seat that holds East in `winds`; `kNoSeat` unless exactly one seat does.
    [[nodiscard]] Seat EastOf(const SeatWinds& winds) noexcept;

    /// The dice of a seat in the draw for East: returns the total of 2d6 for that seat. A seam, so that a test can script the rolls.
    using SeatRoller = eastl::function<int(Seat)>;

    /**
     * @brief Decides who is East. Each seat in the running rolls (in seat order); the highest total wins; a tie among the highest rolls
     *        again among the tied seats only (the others are out).
     */
    [[nodiscard]] Seat DecideEast(const SeatRoller& roll);

    /// The draw for East with real dice: each seat rolls 2d6 from `rng`, then the winds follow counter-clockwise from East.
    [[nodiscard]] SeatWinds RollSeatWinds(MahjongRng& rng);

}  // namespace BECore::Mahjong
