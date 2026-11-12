#include "OnlineWords.h"

#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    eastl::string_view OnlineErrorKey(eastl::string_view code) {
        for (const OnlineCodeWord& word : kKnownOnlineCodes) {
            if (word.code == code) {
                return word.key;
            }
        }
        return kOnlineUnknownKey;
    }

    bool IsSilentOnlineCode(eastl::string_view code) {
        return code == RoomErrors::kStaleAction;
    }

    eastl::string OnlineNoticeText(const UIStrings& strings, eastl::string_view key, eastl::string_view arg) {
        return Tr(strings, key, {{"code", arg}});
    }

    eastl::string OnlineSeatText(const UIStrings& strings, BECore::SeatKind kind, eastl::string_view name, bool mine, bool connected) {
        switch (kind) {
            case BECore::SeatKind::Empty:
                return Tr(strings, "online.seat.empty");
            case BECore::SeatKind::Computer:
                return Tr(strings, "online.seat.computer");
            case BECore::SeatKind::Human:
                break;
        }
        if (mine) {
            return Tr(strings, "online.seat.you", {{"name", name}});
        }
        if (!connected) {
            return Tr(strings, "online.seat.away", {{"name", name}});
        }
        return eastl::string(name.data(), name.size());
    }

}  // namespace BECore::Mahjong
