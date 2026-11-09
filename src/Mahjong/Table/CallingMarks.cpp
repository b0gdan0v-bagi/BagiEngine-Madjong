#include "CallingMarks.h"

namespace BECore::Mahjong {

    eastl::array<bool, kTileIdCount> KeepsWaitingDiscards(const TableView& view) {
        eastl::array<bool, kTileIdCount> keeps{};
        if (!IsSeat(view.seat) || !view.own.callingDeclared || view.phase != Phase::Discard || view.currentSeat != view.seat || view.legal.empty()) {
            return keeps;
        }
        TileCounts counts = CountTiles(view.own.concealed);
        const int meldCount = static_cast<int>(view.own.melds.size());
        const TileCounts meldTiles = CountMeldTiles(view.own.melds);
        for (int id = 0; id < kKindCount; ++id) {
            if (counts[id] == 0) {
                continue;
            }
            --counts[id];
            keeps[id] = IsWaiting(counts, meldCount, meldTiles);
            ++counts[id];
        }
        return keeps;
    }

}  // namespace BECore::Mahjong
