#pragma once

#include <BECore/Save/SaveStorage.h>
#include <Mahjong/Save/MatchSave.h>

namespace BECore::Mahjong {

    /// `kWebSaveDirectory` in the browser build whatever the environment says, else `fromEnvironment` (the value of
    /// `BAGI_MAHJONG_SAVE_DIR` or null; empty switches saving off), else `saves`. Pure, so a test can drive both builds.
    [[nodiscard]] std::filesystem::path DefaultMatchSaveDirectory(bool browser, const char* fromEnvironment);

    /// Defaults to `DefaultMatchSaveDirectory`; an empty path switches saving off. Process-wide: `MahjongGame` reads it when it opens its store.
    [[nodiscard]] const std::filesystem::path& MatchSaveDirectory();
    void SetMatchSaveDirectory(std::filesystem::path directory);

    /// False only for `NotRemembered` (a browser whose storage lives in memory, e.g. a private window). Pure.
    [[nodiscard]] constexpr bool SaveStorageRemembers(SaveStorageState state) noexcept {
        return state != SaveStorageState::NotRemembered;
    }

    /// The engine's `GetSaveStorageState()` unless a test plays one with `SetMatchSaveStorage` (nullopt restores the engine's answer).
    [[nodiscard]] SaveStorageState MatchSaveStorage();
    void SetMatchSaveStorage(Optional<SaveStorageState> state);

    /// `Missing` is the usual case (nothing saved, or saving is off); `Broken` is a file that is there and cannot be used.
    CORE_ENUM(SaveProblemKind, uint8_t, Missing, Broken)

    struct SaveProblem {
        SaveProblemKind kind = SaveProblemKind::Missing;
        String detail;
    };

    /**
     * @brief The three slots of `MatchSave.h` over a `SaveGameService`; the game owns the store.
     *
     * With saving off every call does nothing and answers "nothing there". It never fails the game: a failed write is one warning
     * and leaves the old file, a failed read is a `SaveProblem`. Not thread safe (the main thread).
     */
    class MatchSaveStore {
    public:
        explicit MatchSaveStore(FileSystem& fileSystem) : _service(fileSystem) {}

        /// Asking again for the directory that is open, or that has failed, does nothing, so it is cheap to call every frame. False, and
        /// one warning, when the folder cannot be made; an empty path leaves saving off and is not an error.
        bool Open(const std::filesystem::path& directory);

        [[nodiscard]] bool IsOn() const noexcept {
            return _ready;
        }

        /// False, and one warning, when it was not written.
        bool SaveMatch(const Vector<MatchLogEvent>& events);
        /// Parsed only (`ParseMatchSaveText`); whether they replay is for `ReplayMatchLog`.
        [[nodiscard]] std::expected<Vector<MatchLogEvent>, SaveProblem> LoadMatch() const;
        void ClearMatch();

        bool SavePrefs(const MahjongPrefsSave& prefs);
        [[nodiscard]] std::expected<MahjongPrefsSave, SaveProblem> LoadPrefs() const;

        /// The session is **a secret** (`MahjongOnlineSave`); `LoadOnline` is checked by `ReadOnline`, and its `Broken` detail never holds the token.
        bool SaveOnline(const MahjongOnlineSave& save);
        [[nodiscard]] std::expected<OnlineValues, SaveProblem> LoadOnline() const;
        void ClearOnline();

    private:
        template <typename State>
        bool Write(StringView slot, const State& state);

        SaveGameService _service;
        std::filesystem::path _directory;  // also when opening failed
        bool _asked = false;
        bool _ready = false;
    };

}  // namespace BECore::Mahjong
