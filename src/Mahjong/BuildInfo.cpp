#include "BuildInfo.h"

// The define (a quoted string) comes from CMakeLists.txt for this file only; without it (another build system) the id is unknown.
#ifndef MAHJONG_BUILD_ID
#define MAHJONG_BUILD_ID "unknown"
#endif

namespace BECore::Mahjong {

    std::string_view BuildId() {
        return MAHJONG_BUILD_ID;
    }

    MatchLogOrigin MakeMatchOrigin(bool browser) {
        MatchLogOrigin origin;
        origin.platform = browser ? MatchPlatform::Web : MatchPlatform::Desktop;
        origin.build.assign(BuildId().data(), BuildId().size());
        origin.gameVersion.assign(kGameVersion.data(), kGameVersion.size());
        return origin;
    }

}  // namespace BECore::Mahjong
