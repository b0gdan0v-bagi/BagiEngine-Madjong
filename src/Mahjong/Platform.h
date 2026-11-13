#pragma once

namespace BECore::Mahjong {

    /// True in the browser build (Emscripten, `PLATFORM_WEB`). There the working directory is the page's in-memory file system, which is
    /// gone on reload: the saves go to the engine's persistent folder instead (`DefaultMatchSaveDirectory`), and the match log, which
    /// has no such folder, stays off (`DefaultMatchLogDirectory` is empty).
#ifdef PLATFORM_WEB
    inline constexpr bool kBrowserBuild = true;
#else
    inline constexpr bool kBrowserBuild = false;
#endif

}  // namespace BECore::Mahjong
