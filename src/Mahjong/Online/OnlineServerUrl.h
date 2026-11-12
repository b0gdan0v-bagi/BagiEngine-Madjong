#pragma once

namespace BECore::Mahjong {

    /// The address of the room server when `BAGI_MAHJONG_SERVER` is not set on a desktop: the default port of `BagiMahjongServer` on this machine.
    inline constexpr eastl::string_view kDefaultOnlineServerUrl = "ws://localhost:8080";

    /// The name of the environment variable that gives a desktop build another server.
    inline constexpr eastl::string_view kOnlineServerVariable = "BAGI_MAHJONG_SERVER";

    /// The name of the config (`config/MahjongOnlineConfig.xml`) that holds the address for the browser build.
    inline constexpr eastl::string_view kOnlineConfigName = "MahjongOnlineConfig";

    /// What `MahjongOnlineConfig.xml` says.
    struct OnlineServerConfig {
        /// The `wss://` (or `ws://`) address of the hosted server for the browser build; empty: not set.
        eastl::string webServer;
    };

    /// Reads the `webServer` attribute of the root of the config. The address is kept only when it starts with `ws://` or `wss://`, else it reads as not set; an invalid
    /// @p root gives the empty config. Pure.
    [[nodiscard]] OnlineServerConfig ReadOnlineServerConfig(const XmlNode& root);

    /// The address of the room server. The browser gives `config.webServer` (possibly empty: there is no server to offer, the environment is ignored); a desktop gives
    /// @p fromEnvironment (the value of `BAGI_MAHJONG_SERVER`, or null) when it is not empty, else `kDefaultOnlineServerUrl` (the config is ignored). The player never
    /// types it. Pure, so a test can drive both builds.
    [[nodiscard]] eastl::string OnlineServerUrl(bool browser, const char* fromEnvironment, const OnlineServerConfig& config);

    /// `OnlineServerUrl` for this build, this process's environment and the loaded `MahjongOnlineConfig`. Empty exactly when there is no server to offer (the browser
    /// without an address): the `Online` button is then hidden.
    [[nodiscard]] eastl::string OnlineServerUrl();

}  // namespace BECore::Mahjong
