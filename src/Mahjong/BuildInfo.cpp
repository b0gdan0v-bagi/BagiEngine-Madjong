#include "BuildInfo.h"

// The define comes from CMakeLists.txt for this file only; without it (another build system) the id is unknown.
#ifndef MAHJONG_BUILD_ID
#define MAHJONG_BUILD_ID unknown
#endif

#define MAHJONG_STRINGIZE_VALUE(value) #value
#define MAHJONG_STRINGIZE(value) MAHJONG_STRINGIZE_VALUE(value)

namespace BECore::Mahjong {

    std::string_view BuildId() {
        return MAHJONG_STRINGIZE(MAHJONG_BUILD_ID);
    }

    MatchLogOrigin MakeMatchOrigin(bool browser) {
        MatchLogOrigin origin;
        origin.platform = browser ? MatchPlatform::Web : MatchPlatform::Desktop;
        origin.build.assign(BuildId().data(), BuildId().size());
        origin.gameVersion.assign(kGameVersion.data(), kGameVersion.size());
        return origin;
    }

}  // namespace BECore::Mahjong
