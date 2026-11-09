#pragma once

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /**
     * @brief The last move of a hand in words, for the line of the HUD: "Seat 2 (South) discarded 5 Dots", "Seat 3 (West) claimed 4 Bamboo
     *        for a pung", "Seat 1 (East) declared a concealed kong".
     *
     * `move` is an entry of `HandModel::Moves()` and `seatWind` the wind of `move.seat` in that hand. One key per sentence
     * (`move.discard`, `move.discardCalling`, `move.chow`, `move.pung`, `move.kong`, `move.concealedKong`, `move.upgradeKong`) with the
     * placeholders `{seat}` and `{tile}`, in the current language of `strings`. Public data only: the sentence of a concealed kong names no
     * tile (the move has none). Empty for a move of no seat.
     */
    [[nodiscard]] eastl::string MoveText(const UIStrings& strings, const HandMove& move, Wind seatWind);

}  // namespace BECore::Mahjong
