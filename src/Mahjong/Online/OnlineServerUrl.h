#pragma once

namespace BECore::Mahjong {

    /// The address of the room server when `BAGI_MAHJONG_SERVER` is not set: the default port of `BagiMahjongServer` on this machine. The address of a hosted server is the
    /// owner's to set (here, or in the variable for a build); the browser's `wss://` one is not set yet.
    inline constexpr eastl::string_view kDefaultOnlineServerUrl = "ws://localhost:8080";

    /// The name of the environment variable that gives a desktop build another server.
    inline constexpr eastl::string_view kOnlineServerVariable = "BAGI_MAHJONG_SERVER";

    /// The address of the room server: @p fromEnvironment (the value of `BAGI_MAHJONG_SERVER`, or null) when it is not empty and the build is not the browser, else
    /// `kDefaultOnlineServerUrl`. The player never types it. Pure, so a test can drive both builds.
    [[nodiscard]] eastl::string OnlineServerUrl(bool browser, const char* fromEnvironment);

    /// `OnlineServerUrl` for this build and this process's environment.
    [[nodiscard]] eastl::string OnlineServerUrl();

}  // namespace BECore::Mahjong
