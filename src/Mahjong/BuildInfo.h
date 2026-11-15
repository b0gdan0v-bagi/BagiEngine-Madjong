#pragma once

#include <Mahjong/Game/TableSession.h>
#include <string_view>

namespace BECore::Mahjong {

    /// Written into every match log; bumped by hand when the owner publishes a build.
    inline constexpr StringView kGameVersion = "0.1.0";

    /// The short git hash that `cmake --preset` read at configure time, `unknown` without git. **It is the id of the last configure,
    /// not of the last build:** a later commit shows only after the next `cmake --preset`.
    [[nodiscard]] StringView BuildId();

    /// The origin stamped into a match log. `browser` is passed (`kBrowserBuild`) so that a test drives both. The caller passes a
    /// `nickname` only while the tester's consent is `Granted`.
    [[nodiscard]] MatchLogOrigin MakeMatchOrigin(bool browser, StringView nickname = {});

}  // namespace BECore::Mahjong
