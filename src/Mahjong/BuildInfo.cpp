#include "BuildInfo.h"

// Defined by CMakeLists.txt for this file only, so a new id recompiles nothing else.
#ifndef MAHJONG_BUILD_ID
#define MAHJONG_BUILD_ID "unknown"
#endif

namespace BECore::Mahjong {

    StringView BuildId() {
        return MAHJONG_BUILD_ID;
    }

    MatchLogOrigin MakeMatchOrigin(bool browser, StringView nickname) {
        MatchLogOrigin origin;
        origin.platform = browser ? MatchPlatform::Web : MatchPlatform::Desktop;
        origin.build.assign(BuildId().data(), BuildId().size());
        origin.gameVersion.assign(kGameVersion.data(), kGameVersion.size());
        origin.nickname.assign(nickname.data(), nickname.size());
        return origin;
    }

}  // namespace BECore::Mahjong
