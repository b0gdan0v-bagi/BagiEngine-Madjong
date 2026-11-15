#pragma once

namespace BECore::Mahjong {

    /// The desktop's address when the environment gives none: the default port of `BagiMahjongServer` on this machine.
    inline constexpr StringView kDefaultOnlineServerUrl = "ws://localhost:8080";

    /// Gives a desktop build another server.
    inline constexpr StringView kOnlineServerVariable = "BAGI_MAHJONG_SERVER";

    /// The config (`config/MahjongOnlineConfig.xml`) that holds the address for the browser build.
    inline constexpr StringView kOnlineConfigName = "MahjongOnlineConfig";

    struct OnlineServerConfig {
        /// Empty: not set.
        String webServer;
    };

    /// Reads the `webServer` attribute of the root. Kept only when it starts with `ws://` or `wss://`, else not set; an invalid @p root gives
    /// the empty config. Pure.
    [[nodiscard]] OnlineServerConfig ReadOnlineServerConfig(const XmlNode& root);

    /// The browser gives `config.webServer` (possibly empty: no server to offer, the environment is ignored); a desktop gives
    /// @p fromEnvironment (`BAGI_MAHJONG_SERVER`, or null) when not empty, else `kDefaultOnlineServerUrl` (the config is ignored). Pure, so a
    /// test can drive both builds.
    [[nodiscard]] String OnlineServerUrl(bool browser, const char* fromEnvironment, const OnlineServerConfig& config);

    /// For this build, this process's environment and the loaded config. Empty exactly when there is no server to offer (a browser
    /// without an address): the `Online` button is then hidden.
    [[nodiscard]] String OnlineServerUrl();

}  // namespace BECore::Mahjong
