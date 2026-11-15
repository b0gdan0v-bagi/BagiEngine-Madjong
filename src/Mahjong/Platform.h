#pragma once

namespace BECore::Mahjong {

    /// True in the browser build, where the working directory is gone on reload: saves go to the engine's persistent folder, and the
    /// match log, which has no such folder, stays off.
#ifdef PLATFORM_WEB
    inline constexpr bool kBrowserBuild = true;
#else
    inline constexpr bool kBrowserBuild = false;
#endif

}  // namespace BECore::Mahjong
