#pragma once

#include <Mahjong/Save/MatchSave.h>

namespace BECore::Mahjong {

    /// The default save folder: empty (saving off) in the browser build, else `fromEnvironment` when it is not null (an empty value
    /// switches saving off), else `saves`. `fromEnvironment` is the value of `BAGI_MAHJONG_SAVE_DIR` or null. Pure, so a test can drive
    /// both builds.
    [[nodiscard]] std::filesystem::path DefaultMatchSaveDirectory(bool browser, const char* fromEnvironment);

    /// The folder the saves are written to, relative to the working directory unless absolute. The default is
    /// `DefaultMatchSaveDirectory` (`saves`, or `BAGI_MAHJONG_SAVE_DIR`; none in the browser). An empty path switches saving off.
    /// Process-wide: `MahjongGame` reads it when it opens its store.
    [[nodiscard]] const std::filesystem::path& MatchSaveDirectory();
    void SetMatchSaveDirectory(std::filesystem::path directory);

    /// Why a slot could not be read: `Missing` is the usual case (nothing saved yet, or saving is off), `Broken` is a file that is there
    /// and cannot be used (damaged, of another game or version, or a state whose text is wrong); `detail` says what.
    CORE_ENUM(SaveProblemKind, uint8_t, Missing, Broken)

    struct SaveProblem {
        SaveProblemKind kind = SaveProblemKind::Missing;
        eastl::string detail;
    };

    /**
     * @brief The two slots of Mahjong (`match` and `settings`, `MatchSave.h`) over a `SaveGameService`.
     *
     * `Open(directory)` makes the folder and mounts it; an empty directory means saving is off, and then every call does nothing and
     * answers "nothing there" (`Missing`, false). The store never throws and never fails the game: a folder that cannot be made, a
     * write that fails or a file that cannot be read is one warning (a write) or a `SaveProblem` (a read) and the game goes on.
     * Not thread safe (the main thread).
     */
    class MatchSaveStore {
    public:
        explicit MatchSaveStore(FileSystem& fileSystem) : _service(fileSystem) {}

        /// Opens (makes and mounts) `directory`. Asking again for the directory that is open, or that has failed, does nothing, so it is
        /// cheap to call every frame. False, and one warning, when the folder cannot be made; an empty path is not an error and leaves
        /// saving off.
        bool Open(const std::filesystem::path& directory);

        /// Whether saves are written and read: a directory was opened.
        [[nodiscard]] bool IsOn() const noexcept {
            return _ready;
        }

        /// The `Start`, `Action` and `Settlement` events of a match into the `match` slot. False, and one warning, when it was not written.
        bool SaveMatch(const eastl::vector<MatchLogEvent>& events);
        /// The events of the `match` slot, parsed (`ParseMatchSaveText`; whether they replay is for `ReplayMatchLog` to say).
        [[nodiscard]] std::expected<eastl::vector<MatchLogEvent>, SaveProblem> LoadMatch() const;
        /// Removes the `match` slot (nothing when there is none).
        void ClearMatch();

        bool SavePrefs(const MahjongPrefsSave& prefs);
        [[nodiscard]] std::expected<MahjongPrefsSave, SaveProblem> LoadPrefs() const;

    private:
        template <typename State>
        bool Write(eastl::string_view slot, const State& state);

        SaveGameService _service;
        std::filesystem::path _directory;  // the directory last asked for, also when it failed
        bool _asked = false;
        bool _ready = false;  // the directory is not empty and was opened
    };

}  // namespace BECore::Mahjong
