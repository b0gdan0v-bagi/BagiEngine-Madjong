#pragma once

#include <Mahjong/MatchUpload.h>

namespace BECore::Mahjong {

    /// The file of the tester's choice, in the save folder of the page (`PersistentSaveDirectory()`). Its name has a dot, so it is no
    /// `SaveGameService` slot: the game's own saves and `List()` never see it.
    inline constexpr eastl::string_view kUploadPrefsFile = "upload.txt";

    /// What the tester chose about sending the matches: the consent (`Unasked` until the screen was answered) and an optional nickname
    /// (empty: none). The nickname goes into a log only while the consent is `Granted`; keeping it here is not sending it.
    struct UploadPrefs {
        UploadConsent consent = UploadConsent::Unasked;
        eastl::string nickname;

        [[nodiscard]] bool operator==(const UploadPrefs& other) const = default;
    };

    /**
     * @brief The prefs from the text of the file: lines `consent=unasked|granted|declined` and `nickname=<text>`. **Never an error to the
     * player:** text that is damaged (not UTF-8 text of these two keys, an unknown key or consent, a key twice, no consent, over
     * `kMaxUploadPrefsBytes`) reads as the defaults, and a nickname that `TextEntry` of kind `Name` would refuse reads as empty.
     */
    [[nodiscard]] UploadPrefs ParseUploadPrefs(eastl::string_view text);

    /// The text of the file for `prefs`; `ParseUploadPrefs` of it gives `prefs` back (a nickname `TextEntry` refuses is left out)
    [[nodiscard]] eastl::string FormatUploadPrefs(const UploadPrefs& prefs);

    /// The most bytes of the file that are read (a real one is under a hundred)
    inline constexpr size_t kMaxUploadPrefsBytes = 1024;

    /// The prefs in `directory/upload.txt`; the defaults when the folder is empty (nowhere to read), the file is missing or cannot be read
    [[nodiscard]] UploadPrefs LoadUploadPrefs(const std::filesystem::path& directory);

    /// Writes the prefs to `directory/upload.txt` through a temp name and a rename, so a failed write leaves the old file, then asks the
    /// page to flush the folder (`NotifySaveChanged`). False and nothing done for an empty folder, true when the file is in place.
    bool StoreUploadPrefs(const std::filesystem::path& directory, const UploadPrefs& prefs);

    /// The folder of the file: the engine's persistent folder in a browser, empty (nowhere) elsewhere
    [[nodiscard]] std::filesystem::path UploadPrefsFolder();

    /// Whether the choice survives a reload (`SaveStorageState::Persistent`); false means it is asked again at each visit
    [[nodiscard]] bool UploadChoiceIsRemembered();

}  // namespace BECore::Mahjong
