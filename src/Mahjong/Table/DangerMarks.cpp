#include "DangerMarks.h"

#include <Mahjong/Game/DiscardDanger.h>

namespace BECore::Mahjong {

    namespace {

        DangerMark MarkOf(Danger danger) {
            switch (danger) {
                case Danger::Safe:
                    return DangerMark::Safe;
                case Danger::Medium:
                    return DangerMark::Risky;
                case Danger::High:
                    return DangerMark::Dangerous;
                case Danger::Low:
                    break;
            }
            return DangerMark::None;
        }

    }  // namespace

    eastl::array<DangerMark, kTileIdCount> DangerMarks(const TableView& view) {
        eastl::array<DangerMark, kTileIdCount> marks{};
        if (!IsSeat(view.seat) || view.phase != Phase::Discard || view.currentSeat != view.seat || view.legal.empty() || !HasDeclaredOpponent(view)) {
            return marks;
        }
        const DangerTable dangers = DiscardDangers(view);
        const TileCounts held = CountTiles(view.own.concealed);
        for (int id = 0; id < kKindCount; ++id) {
            if (held[id] > 0) {
                marks[id] = MarkOf(dangers[id]);
            }
        }
        return marks;
    }

    Color DangerMarkColor(DangerMark mark) noexcept {
        switch (mark) {
            case DangerMark::Safe:
                return kSafeMark;
            case DangerMark::Risky:
                return kRiskyMark;
            case DangerMark::Dangerous:
            case DangerMark::None:
                break;
        }
        return kDangerMark;
    }

    eastl::string_view DangerHoverKey(DangerMark mark) noexcept {
        switch (mark) {
            case DangerMark::Safe:
                return "hints.hover.safe";
            case DangerMark::Risky:
                return "hints.hover.risky";
            case DangerMark::Dangerous:
                return "hints.hover.dangerous";
            case DangerMark::None:
                break;
        }
        return {};
    }

}  // namespace BECore::Mahjong
