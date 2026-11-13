#include "MatchSaveStore.h"

#include <Mahjong/Platform.h>
#include <cstdlib>

namespace BECore::Mahjong {

    namespace {

        constexpr const char* kDefaultDirectory = "saves";
        constexpr const char* kDirectoryVariable = "BAGI_MAHJONG_SAVE_DIR";

        std::filesystem::path& Directory() {
            static std::filesystem::path directory = DefaultMatchSaveDirectory(kBrowserBuild, std::getenv(kDirectoryVariable));
            return directory;
        }

        std::optional<SaveStorageState>& StorageOverride() {
            static std::optional<SaveStorageState> state;
            return state;
        }

        SaveProblem ProblemOf(const SaveError& error) {
            if (error.kind == SaveErrorKind::NotFound) {
                return SaveProblem{SaveProblemKind::Missing, error.detail};
            }
            eastl::string detail = error.detail;
            if (detail.empty()) {
                detail = EnumUtils<SaveErrorKind>::ToString(error.kind);
            }
            return SaveProblem{SaveProblemKind::Broken, eastl::move(detail)};
        }

        SaveProblem SavingIsOff() {
            return SaveProblem{SaveProblemKind::Missing, "saving is off"};
        }

    }  // namespace

    std::filesystem::path DefaultMatchSaveDirectory(bool browser, const char* fromEnvironment) {
        if (browser) {
            return std::filesystem::path(kWebSaveDirectory.begin(), kWebSaveDirectory.end());
        }
        if (fromEnvironment != nullptr) {
            return fromEnvironment;
        }
        return kDefaultDirectory;
    }

    const std::filesystem::path& MatchSaveDirectory() {
        return Directory();
    }

    void SetMatchSaveDirectory(std::filesystem::path directory) {
        Directory() = std::move(directory);
    }

    SaveStorageState MatchSaveStorage() {
        const std::optional<SaveStorageState>& forced = StorageOverride();
        return forced ? *forced : GetSaveStorageState();
    }

    void SetMatchSaveStorage(std::optional<SaveStorageState> state) {
        StorageOverride() = state;
    }

    bool MatchSaveStore::Open(const std::filesystem::path& directory) {
        if (_asked && directory == _directory) {
            return _ready;
        }
        _asked = true;
        _directory = directory;
        _ready = false;
        if (directory.empty()) {
            return false;
        }
        if (!_service.Initialize(directory)) {
            LOG_WARNING(Format("MatchSaveStore: the folder '{}' could not be made, nothing is saved", directory.string()));
            return false;
        }
        _ready = true;
        return true;
    }

    template <typename State>
    bool MatchSaveStore::Write(eastl::string_view slot, const State& state) {
        if (const auto saved = _service.Save(slot, kMahjongSaveSchema, state); !saved) {
            LOG_WARNING(Format("MatchSaveStore: the slot '{}' could not be written: {}", slot, saved.error().detail));
            return false;
        }
        return true;
    }

    bool MatchSaveStore::SaveMatch(const eastl::vector<MatchLogEvent>& events) {
        BE_PROFILE_SCOPE("MatchSave::SaveMatch");
        if (!IsOn()) {
            return false;
        }
        MahjongMatchSave state;
        state.log = MatchSaveText(events);
        return Write(kMatchSlot, state);
    }

    std::expected<eastl::vector<MatchLogEvent>, SaveProblem> MatchSaveStore::LoadMatch() const {
        BE_PROFILE_SCOPE("MatchSave::LoadMatch");
        if (!IsOn()) {
            return std::unexpected(SavingIsOff());
        }
        MahjongMatchSave state;  // a failed load may fill it partly, so it is only read after a success
        if (const auto loaded = _service.Load(kMatchSlot, kMahjongSaveSchema, state); !loaded) {
            return std::unexpected(ProblemOf(loaded.error()));
        }
        auto events = ParseMatchSaveText(std::string_view(state.log.c_str(), state.log.size()));
        if (!events) {
            return std::unexpected(SaveProblem{SaveProblemKind::Broken, eastl::move(events.error())});
        }
        return eastl::move(*events);
    }

    void MatchSaveStore::ClearMatch() {
        if (IsOn()) {
            _service.Delete(kMatchSlot);
        }
    }

    bool MatchSaveStore::SavePrefs(const MahjongPrefsSave& prefs) {
        return IsOn() && Write(kPrefsSlot, prefs);
    }

    std::expected<MahjongPrefsSave, SaveProblem> MatchSaveStore::LoadPrefs() const {
        if (!IsOn()) {
            return std::unexpected(SavingIsOff());
        }
        MahjongPrefsSave state;
        if (const auto loaded = _service.Load(kPrefsSlot, kMahjongSaveSchema, state); !loaded) {
            return std::unexpected(ProblemOf(loaded.error()));
        }
        return state;
    }

    bool MatchSaveStore::SaveOnline(const MahjongOnlineSave& save) {
        return IsOn() && Write(kOnlineSlot, save);
    }

    std::expected<OnlineValues, SaveProblem> MatchSaveStore::LoadOnline() const {
        if (!IsOn()) {
            return std::unexpected(SavingIsOff());
        }
        MahjongOnlineSave state;
        if (const auto loaded = _service.Load(kOnlineSlot, kMahjongSaveSchema, state); !loaded) {
            return std::unexpected(ProblemOf(loaded.error()));
        }
        std::expected<OnlineValues, eastl::string> values = ReadOnline(state);
        if (!values) {
            return std::unexpected(SaveProblem{SaveProblemKind::Broken, eastl::move(values.error())});
        }
        return eastl::move(*values);
    }

    void MatchSaveStore::ClearOnline() {
        if (IsOn()) {
            _service.Delete(kOnlineSlot);
        }
    }

}  // namespace BECore::Mahjong
