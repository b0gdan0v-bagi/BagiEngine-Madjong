#pragma once

#include <Mahjong/Game/ScoreReading.h>

namespace BECore::Mahjong {

    /**
     * @brief The limit hand the winner holds, if any; nothing for a hand that is no win or whose tiles make no winning shape (a
     *        complete reading, seven pairs or thirteen orphans).
     *
     * Each hand is read on what it is about: all the tiles the winner holds, `readings` (any of them) for the hands made of sets,
     * the concealed tiles for seven pairs and thirteen orphans, the circumstances of the win for the last tile as 1 dot and the loose
     * tile as 5 dots. The owner's readings: `Game/CLAUDE.md` ("Scoring readings decided by the owner").
     * A hand that is two limit hands is named by the more specific one (seven pairs of honours is `SevenPairsHonours`, not
     * `HonoursOnly`), otherwise the first in the order of `ScoreKind`. They score the same, so only the name differs.
     */
    [[nodiscard]] Optional<ScoreKind> FindLimitHand(const Vector<Reading>& readings, const ScoreInput& input);

}  // namespace BECore::Mahjong
