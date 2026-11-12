#include "OnlineServerUrl.h"

#include <BECore/GameManager/CoreManager.h>
#include <Mahjong/Platform.h>
#include <cstdlib>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kWebServerAttribute = "webServer";
        constexpr eastl::string_view kPlainScheme = "ws://";
        constexpr eastl::string_view kSecureScheme = "wss://";

        bool HasServerScheme(eastl::string_view address) {
            return address.starts_with(kPlainScheme) || address.starts_with(kSecureScheme);
        }

    }  // namespace

    OnlineServerConfig ReadOnlineServerConfig(const XmlNode& root) {
        OnlineServerConfig config;
        if (!root) {
            return config;
        }
        const auto address = root.ParseAttribute<eastl::string_view>(kWebServerAttribute);
        if (address && HasServerScheme(*address)) {
            config.webServer.assign(address->data(), address->size());
        }
        return config;
    }

    eastl::string OnlineServerUrl(bool browser, const char* fromEnvironment, const OnlineServerConfig& config) {
        if (browser) {
            return config.webServer;
        }
        if (fromEnvironment != nullptr && fromEnvironment[0] != '\0') {
            return eastl::string(fromEnvironment);
        }
        return eastl::string(kDefaultOnlineServerUrl.data(), kDefaultOnlineServerUrl.size());
    }

    eastl::string OnlineServerUrl() {
        OnlineServerConfig config;
        if (kBrowserBuild) {
            config = ReadOnlineServerConfig(CoreManager::GetConfigManager().GetConfig(kOnlineConfigName));
        }
        return OnlineServerUrl(kBrowserBuild, std::getenv(kOnlineServerVariable.data()), config);
    }

}  // namespace BECore::Mahjong
