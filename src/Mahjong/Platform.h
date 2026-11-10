#pragma once

namespace BECore::Mahjong {

    /// True in the browser build (Emscripten, `PLATFORM_WEB`). There the working directory is the page's in-memory file system, which is
    /// gone on reload, so the game writes no files: the default folders of the saves and of the match log are empty (see
    /// `DefaultMatchSaveDirectory`, `DefaultMatchLogDirectory`).
#ifdef PLATFORM_WEB
    inline constexpr bool kBrowserBuild = true;
#else
    inline constexpr bool kBrowserBuild = false;
#endif

}  // namespace BECore::Mahjong
