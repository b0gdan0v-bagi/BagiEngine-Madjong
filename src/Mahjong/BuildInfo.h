#pragma once

#include <Mahjong/Game/TableSession.h>
#include <string_view>

namespace BECore::Mahjong {

    /// The version of the game for testers, written into every match log. Bumped by hand when the owner publishes a build.
    inline constexpr std::string_view kGameVersion = "0.1.0";

    /// The id of the build: the short git hash that `cmake --preset` read at configure time (`CMakeLists.txt`), `unknown` when git said
    /// nothing (a source archive). **It is the id of the last configure, not of the last build:** a commit made after it shows only after
    /// the next `cmake --preset`. Only `BuildInfo.cpp` is compiled with it, so a new id recompiles nothing else.
    [[nodiscard]] std::string_view BuildId();

    /// The origin a match of this build says it was played from: `Web` for the browser build (`kBrowserBuild`, passed by the caller so
    /// that a test drives both), else `Desktop`, with `BuildId()` and `kGameVersion`, and the `nickname` the caller passes (empty: none;
    /// the caller passes one only while the tester's consent is `Granted`).
    [[nodiscard]] MatchLogOrigin MakeMatchOrigin(bool browser, eastl::string_view nickname = {});

}  // namespace BECore::Mahjong
