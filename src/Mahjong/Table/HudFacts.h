#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>

namespace BECore::Mahjong {

    /**
     * @brief What the HUD's data (`config/MahjongHudValues.xml`) reads of the table: the level `view` of its context
     *
     * Plain fields only, so a path into it is fully fixed. It is made by `MakeHudFacts` from public data alone (the phases and the move
     * log), so a value that reads it can never peek at a hidden tile. A field is added here with its test, and the element it shows is
     * named in the file; the component holds no rule of its own about it.
     */
    struct HudFacts {
        BE_CLASS(HudFacts)

        /// The move line has a text: a room's line, or in a hand being played the last public move
        BE_REFLECT_FIELD bool hasLastMove = false;
        /// The match is over: the final standings are shown
        BE_REFLECT_FIELD bool matchOver = false;

        [[nodiscard]] bool operator==(const HudFacts&) const = default;
    };

    /// The facts of the table @p match / @p session shows; @p roomLineUp is a room's line (the connection or a word of the client) that fills the move line before any move
    [[nodiscard]] HudFacts MakeHudFacts(const Match& match, const TableSession& session, bool roomLineUp);

}  // namespace BECore::Mahjong
