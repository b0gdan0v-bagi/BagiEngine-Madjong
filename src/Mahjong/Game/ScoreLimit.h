#pragma once

#include <Mahjong/Game/ScoreReading.h>

namespace BECore::Mahjong {

    /**
     * @brief The limit hand the winner holds, if any (`input.won` is true); nothing for a hand that is no win.
     *
     * A limit hand is read on the tiles the winner holds (concealed ones and those of the melds) for honours only, terminals only, all
     * green, one suit; on `readings` (any of them) for the hands made of sets: concealed pungs of one suit, big three dragons, big four
     * winds, four kongs, all green; on the concealed tiles for seven pairs (no melds) and thirteen orphans; on the circumstances of the
     * win for the last tile as 1 dot and the loose tile as 5 dots. The readings of the table are in `CLAUDE.md` ("Scoring").
     * A hand that is two limit hands is named by the more specific one: seven pairs of honours is `SevenPairsHonours`, not `HonoursOnly`;
     * otherwise the first in the order of `ScoreKind`. They score the same, so only the name differs.
     */
    [[nodiscard]] std::optional<ScoreKind> FindLimitHand(const eastl::vector<Reading>& readings, const ScoreInput& input);

}  // namespace BECore::Mahjong
