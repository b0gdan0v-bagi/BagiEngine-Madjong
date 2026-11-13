#include "MahjongGame.h"

#include <BECore/GameManager/CoreManager.h>
#include <Games/GameManager.h>
#include <Generated/MahjongGame.gen.hpp>
#include <Mahjong/BuildInfo.h>
#include <Mahjong/Platform.h>
#include <random>

namespace BECore::Mahjong {

    namespace {

        constexpr int kMatchIdWords = 4;  // 4 x 32 bits

        /// A random id of 128 bits as 32 hex digits, from `std::random_device` alone: nothing of the seed, the time or the machine.
        eastl::string RandomMatchId() {
            std::random_device device;
            eastl::string id;
            for (int word = 0; word < kMatchIdWords; ++word) {
                id.append(Format("{:08x}", static_cast<uint32_t>(device())));
            }
            return id;
        }

    }  // namespace

    MahjongGame::MahjongGame() : _store(CoreManager::GetFileSystem()) {
        _recorder.SetNext(&_upload);
        _upload.SetNext(&_log);
        _upload.SetMatchIdSource(RandomMatchId);  // used only for a match that is recorded: with no sink or consent it is never called
        _session.SetLogSink(&_recorder);
        _session.SetLogOrigin(MakeMatchOrigin(kBrowserBuild));  // every front end is stamped: the session writes it into each Start
        _session.SetMoveObserver(&_coach);
    }

    MahjongGame::~MahjongGame() {
        _session.SetMoveObserver(nullptr);
    }

    void MahjongGame::SetHintsMode(HintsMode mode) {
        _hintsMode = mode;
        _coach.SetEnabled(mode == HintsMode::Coach);
        SyncRoomCoach(false);
    }

    void MahjongGame::SyncRoomCoach(bool restart) {
        if (restart) {
            _roomCoach.SetEnabled(false);  // a changed value drops the note and the tallies
        }
        _roomCoach.SetEnabled(_onlineTable && CoachActive());
    }

    MoveContext MahjongGame::CoachContext() const {
        if (!_onlineTable) {
            return _session.Context();
        }
        return MoveContext{_roomTables, _displayMatch.HandsPlayed(), static_cast<size_t>(_displaySession.StartCount()), 1};
    }

    MahjongGame* MahjongGame::GetActive() {
        auto* active = GameManager::GetInstance().GetActive();
        return active ? active->Cast<MahjongGame>() : nullptr;
    }

    PoolString MahjongGame::GetName() const {
        return "Mahjong"_intern;
    }

    PoolString MahjongGame::GetSceneName() const {
        return "MahjongScene"_intern;
    }

    IControllableGame* MahjongGame::AsControllable() {
        return &_control;
    }

    bool MahjongGame::SetLanguage(PoolString language) {
        if (!CoreManager::GetUIStrings().HasLanguage(language)) {
            return false;
        }
        _language = language;
        ApplyLanguage();
        return true;
    }

    void MahjongGame::ApplyLanguage() const {
        UIStrings& strings = CoreManager::GetUIStrings();
        if (strings.HasLanguage(_language)) {
            strings.SetLanguage(_language);
        }
    }

    const std::filesystem::path& MahjongGame::SaveDirectory() const {
        static const std::filesystem::path none;  // by reference: a path by value would allocate in every `FlushSave()`
        return _storage == SaveStorageState::NotRemembered ? none : MatchSaveDirectory();
    }

    bool MahjongGame::SavesAreRemembered() const {
        return _store.IsOn() && SaveStorageRemembers(_storage);
    }

    void MahjongGame::Start() {
        LeaveOnlineTable(false);
        _syncedClient = kNotSynced;     // a client that is still in a room shows its table again at the next sync
        _storage = MatchSaveStorage();  // the page has settled it before the start; read once
        _store.Open(SaveDirectory());
        ApplySavedPrefs();
        ApplySavedOnline();
        CloseOnline();
        // The saved match is the table behind the panel; else a match is dealt at once so that the table is never empty, and it is not
        // recorded: only the match that the person begins may take the place of the saved one
        const bool resumed = ResumeSavedMatch();
        if (!resumed) {
            BeginRandomMatch(false);
        }
        OpenSetup();
        _canContinue = resumed;
    }

    void MahjongGame::Stop() {
        CloseOnline();
        FlushSave();
        _upload.Abandon();  // a match given up by the game closing tells the store so
    }

    void MahjongGame::Reset() {
        StartRandomMatch();
    }

    bool MahjongGame::ContinueMatch() {
        if (!_canContinue) {
            return false;
        }
        CloseSetup();  // clears the offer
        CloseRules();
        return true;
    }

    MatchStatus MahjongGame::StartMatch(uint32_t seed) {
        return BeginMatch(seed, true);
    }

    MatchStatus MahjongGame::StartRandomMatch() {
        return BeginRandomMatch(true);
    }

    MatchStatus MahjongGame::BeginMatch(uint32_t seed, bool record) {
        ApplyLanguage();
        _recorder.SetRecording(record);
        _upload.SetRecording(record);  // the match dealt behind the panel is nobody's game
        const MatchStatus started = _session.StartMatch(_settings, seed, _seats, _levels);
        if (started.has_value()) {
            CloseSetup();
            CloseRules();
        }
        return started;
    }

    MatchStatus MahjongGame::BeginRandomMatch(bool record) {
        std::random_device device;
        const MatchStatus started = BeginMatch(device(), record);
        if (!started.has_value()) {
            LOG_ERROR(Format("MahjongGame: the match did not start: {}", EnumUtils<MatchError>::ToString(started.error())));
        }
        return started;
    }

    void MahjongGame::ApplySavedPrefs() {
        const std::expected<MahjongPrefsSave, SaveProblem> loaded = _store.LoadPrefs();
        if (!loaded) {
            if (loaded.error().kind == SaveProblemKind::Broken) {
                LOG_WARNING(Format("MahjongGame: the saved settings are not used: {}", loaded.error().detail));
            }
            return;
        }
        _savedPrefs = *loaded;  // what the slot holds, so nothing is written until a choice changes
        const std::expected<PrefsValues, eastl::string> values = ReadPrefs(*loaded);
        if (!values) {
            LOG_WARNING(Format("MahjongGame: the saved settings are not used: {}", values.error()));
            return;
        }
        SetLanguage(PoolString::Intern(eastl::string_view(values->language.c_str(), values->language.size())));  // a language the table lacks stays out
        SetHintsMode(values->hintsMode);
        _seats = values->seats;
        _settings.handsPerMatch = values->handsPerMatch;
        _levels = values->levels;
        _animationSpeed = values->speed;
    }

    void MahjongGame::SyncOnlineTable() {
        if (!_online || _online->Version() == _syncedClient) {
            return;
        }
        _syncedClient = _online->Version();
        const OnlineStage stage = _online->Stage();
        const bool live = _online->HasTable() && (stage == OnlineStage::Game || stage == OnlineStage::Reconnecting);
        if (!live) {
            if (_onlineTable) {
                LeaveOnlineTable(true);
            }
            return;
        }
        const OnlineTable& table = _online->Table();
        MakeDisplayState(table, _online->Lobby(), _displayState);
        _displayState.hand.moves = _tracker.Follow(table);
        const TableKey key = KeyOf(table);
        const bool newTable = !_onlineTable || IsAnotherTable(_tableKey, key);
        _tableKey = key;
        ShowOnlineTable(_displayMatch, _displaySession, _displayState, newTable);
        if (!_onlineTable) {
            _onlineTable = true;
            EnterOnlineTable();
        }
        SyncRoomCoach(false);  // the room's setting may have moved with this view
    }

    void MahjongGame::EnterOnlineTable() {
        _noticeAtTable = _online->Notice().serial;
        ++_roomTables;
        SyncRoomCoach(true);
        const bool offer = _canContinue;
        CloseSetup();
        _canContinue = offer;  // the saved match is still there for when the room is left
        CloseRules();
        CloseOnline();
    }

    void MahjongGame::LeaveOnlineTable(bool openSetup) {
        _tableKey = TableKey{};
        _tracker.Reset();
        if (!_onlineTable) {
            return;
        }
        _onlineTable = false;
        SyncRoomCoach(true);
        if (openSetup) {
            OpenSetup();
            // A room that ended with a word of the client (not one the player asked for: `Leave` makes none) says it on the online panel
            const OnlineNotice& notice = _online->Notice();
            if (notice.serial != _noticeAtTable && !notice.key.empty()) {
                OpenOnline();
                _roomEndNotice = true;
            }
        }
    }

    OnlineClient& MahjongGame::GetOnline() {
        if (!_online) {
            _online = eastl::make_unique<OnlineClient>(_onlineSettings);
        }
        return *_online;
    }

    bool MahjongGame::SetOnlineSettings(RoomClientSettings settings) {
        if (_online) {
            return false;
        }
        _onlineSettings = eastl::move(settings);
        return true;
    }

    void MahjongGame::SetOnlineName(eastl::string_view name) {
        if (_storedOnline.name != name) {
            _storedOnline.name.assign(name.data(), name.size());
            _onlineDirty = true;
        }
    }

    void MahjongGame::ApplySavedOnline() {
        const std::expected<OnlineValues, SaveProblem> loaded = _store.LoadOnline();
        if (!loaded) {
            if (loaded.error().kind == SaveProblemKind::Broken) {
                LOG_WARNING(Format("MahjongGame: the saved online play is not used: {}", loaded.error().detail));
            }
            return;
        }
        _storedOnline = *loaded;
        _onlineDirty = false;  // the slot holds exactly this
    }

    void MahjongGame::FollowOnlineSession() {
        if (!_online || _online->SessionRevision() == _seenSessionRevision) {
            return;
        }
        _seenSessionRevision = _online->SessionRevision();
        _storedOnline.session = _online->SessionToKeep();  // invalid when the client forgot it
        _onlineDirty = true;
    }

    bool MahjongGame::ResumeSavedMatch() {
        std::expected<eastl::vector<MatchLogEvent>, SaveProblem> loaded = _store.LoadMatch();
        if (!loaded) {
            if (loaded.error().kind == SaveProblemKind::Broken) {
                LOG_WARNING(Format("MahjongGame: the saved match is not used: {}", loaded.error().detail));
            }
            return false;
        }
        const eastl::vector<MatchLogEvent>& events = *loaded;
        Match scratch;  // the replay is checked here, and a finished match is not offered, before the session is touched
        if (const ReplayStatus replayed = ReplayMatchLog(events, scratch); !replayed) {
            LOG_WARNING(Format("MahjongGame: the saved match is not used: event {} does not replay ({})", replayed.error().index, EnumUtils<ReplayProblem>::ToString(replayed.error().problem)));
            return false;
        }
        if (scratch.GetPhase() == MatchPhase::Finished) {
            return false;
        }
        ApplyLanguage();
        _recorder.SetRecording(true);  // the match goes on being saved
        // Not uploaded: the replay below goes through the whole chain and would record the match again under a new id, and the part
        // played before cannot be rejoined (the match id is not in the save). The next `BeginMatch` records again.
        _upload.SetRecording(false);
        const MatchLogEvent& start = events.front();
        if (const ReplayStatus resumed = _session.ResumeMatch(events, start.seats, start.levels); !resumed) {
            LOG_WARNING(Format("MahjongGame: the saved match is not used: event {} was refused ({})", resumed.error().index, EnumUtils<ReplayProblem>::ToString(resumed.error().problem)));
            return false;
        }
        _savedCounter = _recorder.Counter();  // the slot already holds exactly this
        // The panel shows the resumed match's own choices when it opens again (the prefs may be newer than the match)
        _seats = start.seats;
        _levels = start.levels;
        if (start.settings.handsPerMatch == kFullMatchHands || start.settings.handsPerMatch == kShortMatchHands) {
            _settings.handsPerMatch = start.settings.handsPerMatch;
        }
        return true;
    }

    void MahjongGame::FlushSave() {
        FollowOnlineSession();  // also with saving off: `StoredOnline()` follows the client
        if (!_store.Open(SaveDirectory())) {
            return;
        }
        // The match of a room is no match of this machine: the slot keeps the local one for `Continue`
        if (const uint64_t counter = _recorder.Counter(); !_onlineTable && counter != _savedCounter) {
            _savedCounter = counter;
            if (_recorder.IsRecording()) {  // a match dealt behind the panel is not kept, and must not touch the slot
                if (_recorder.Finished()) {
                    _store.ClearMatch();
                } else {
                    _store.SaveMatch(_recorder.Events());
                }
            }
        }
        const int hands = _settings.handsPerMatch;
        const bool handsAreAChoice = hands == kFullMatchHands || hands == kShortMatchHands;
        const MahjongPrefsSave prefs = MakePrefs(_language.ToStringView(), HintsOn(), _seats, handsAreAChoice ? hands : _savedPrefs.hands, _levels, _animationSpeed, CoachOn());
        if (!(prefs == _savedPrefs)) {
            _savedPrefs = prefs;
            _store.SavePrefs(prefs);
        }
        if (_onlineDirty) {
            _onlineDirty = false;  // a failed write is one warning and is not retried until something changes
            if (_storedOnline.name.empty() && !_storedOnline.session.IsValid()) {
                _store.ClearOnline();
            } else {
                _store.SaveOnline(MakeOnlineSave(_storedOnline.name, _storedOnline.session));
            }
        }
    }

}  // namespace BECore::Mahjong
