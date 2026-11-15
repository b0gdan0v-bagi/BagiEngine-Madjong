#pragma once

#include <Mahjong/MatchUpload.h>

namespace BECore::Mahjong {

    /// The file of the tester's choice, in the save folder of the page. Its name has a dot, so it is no `SaveGameService` slot: the
    /// game's own saves never list it.
    inline constexpr StringView kUploadPrefsFile = "upload.txt";

    /// The consent (`Unasked` until the screen was answered) and an optional nickname (empty: none). The nickname goes into a log only
    /// while the consent is `Granted`; keeping it here is not sending it.
    struct UploadPrefs {
        UploadConsent consent = UploadConsent::Unasked;
        String nickname;

        [[nodiscard]] bool operator==(const UploadPrefs& other) const = default;
    };

    /**
     * @brief The prefs from the text of the file (`consent=unasked|granted|declined`, `nickname=<text>`). **Never an error to the
     * player:** damaged text (also over `kMaxUploadPrefsBytes`) reads as the defaults, and a nickname that `TextEntry` of kind `Name`
     * would refuse reads as empty.
     */
    [[nodiscard]] UploadPrefs ParseUploadPrefs(StringView text);

    /// The text of the file for `prefs`; `ParseUploadPrefs` of it gives `prefs` back (a nickname `TextEntry` refuses is left out).
    [[nodiscard]] String FormatUploadPrefs(const UploadPrefs& prefs);

    /// The most bytes of the file that are read.
    inline constexpr size_t kMaxUploadPrefsBytes = 1024;

    /// The defaults when the folder is empty (nowhere to read), the file is missing or cannot be read.
    [[nodiscard]] UploadPrefs LoadUploadPrefs(const std::filesystem::path& directory);

    /// Writes through a temp name and a rename, so a failed write leaves the old file, then asks the page to flush the folder. False
    /// and nothing done for an empty folder, true when the file is in place.
    bool StoreUploadPrefs(const std::filesystem::path& directory, const UploadPrefs& prefs);

    /// The engine's persistent folder in a browser, empty (nowhere) elsewhere.
    [[nodiscard]] std::filesystem::path UploadPrefsFolder();

}  // namespace BECore::Mahjong
