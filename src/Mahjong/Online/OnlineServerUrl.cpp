#include "OnlineServerUrl.h"

#include <Mahjong/Platform.h>
#include <cstdlib>

namespace BECore::Mahjong {

    eastl::string OnlineServerUrl(bool browser, const char* fromEnvironment) {
        if (!browser && fromEnvironment != nullptr && fromEnvironment[0] != '\0') {
            return eastl::string(fromEnvironment);
        }
        return eastl::string(kDefaultOnlineServerUrl.data(), kDefaultOnlineServerUrl.size());
    }

    eastl::string OnlineServerUrl() {
        return OnlineServerUrl(kBrowserBuild, std::getenv(kOnlineServerVariable.data()));
    }

}  // namespace BECore::Mahjong
