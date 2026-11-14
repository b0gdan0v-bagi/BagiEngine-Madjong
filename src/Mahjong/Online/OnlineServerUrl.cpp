#include "OnlineServerUrl.h"

#include <BECore/GameManager/CoreManager.h>
#include <Mahjong/Platform.h>
#include <cstdlib>

namespace BECore::Mahjong {

    namespace {

        constexpr StringView kWebServerAttribute = "webServer";
        constexpr StringView kPlainScheme = "ws://";
        constexpr StringView kSecureScheme = "wss://";

        bool HasServerScheme(StringView address) {
            return address.starts_with(kPlainScheme) || address.starts_with(kSecureScheme);
        }

    }  // namespace

    OnlineServerConfig ReadOnlineServerConfig(const XmlNode& root) {
        OnlineServerConfig config;
        if (!root) {
            return config;
        }
        const auto address = root.ParseAttribute<StringView>(kWebServerAttribute);
        if (address && HasServerScheme(*address)) {
            config.webServer.assign(address->data(), address->size());
        }
        return config;
    }

    String OnlineServerUrl(bool browser, const char* fromEnvironment, const OnlineServerConfig& config) {
        if (browser) {
            return config.webServer;
        }
        if (fromEnvironment != nullptr && fromEnvironment[0] != '\0') {
            return String(fromEnvironment);
        }
        return String(kDefaultOnlineServerUrl.data(), kDefaultOnlineServerUrl.size());
    }

    String OnlineServerUrl() {
        OnlineServerConfig config;
        if (kBrowserBuild) {
            config = ReadOnlineServerConfig(CoreManager::GetConfigManager().GetConfig(kOnlineConfigName));
        }
        return OnlineServerUrl(kBrowserBuild, std::getenv(kOnlineServerVariable.data()), config);
    }

}  // namespace BECore::Mahjong
