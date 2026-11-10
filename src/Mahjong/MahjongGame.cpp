#include "MahjongGame.h"

#include <BECore/GameManager/CoreManager.h>
#include <Games/GameManager.h>
#include <Generated/MahjongGame.gen.hpp>
#include <random>

namespace BECore::Mahjong {

    MahjongGame::MahjongGame() : _store(CoreManager::GetFileSystem()) {
        _recorder.SetNext(&_log);
        _session.SetLogSink(&_recorder);
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

    void MahjongGame::Start() {
        _store.Open(MatchSaveDirectory());
        ApplySavedPrefs();
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
        FlushSave();
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
        const MatchStatus started = _session.StartMatch(_settings, seed, _seats, _level);
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
        _hintsOn = values->hintsOn;
        _seats = values->seats;
        _settings.handsPerMatch = values->handsPerMatch;
        _level = values->level;
        _animationSpeed = values->speed;
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
        const MatchLogEvent& start = events.front();
        if (const ReplayStatus resumed = _session.ResumeMatch(events, start.seats, start.levels); !resumed) {
            LOG_WARNING(Format("MahjongGame: the saved match is not used: event {} was refused ({})", resumed.error().index, EnumUtils<ReplayProblem>::ToString(resumed.error().problem)));
            return false;
        }
        _savedCounter = _recorder.Counter();  // the slot already holds exactly this
        // The panel shows the resumed match's own choices when it opens again (the prefs may be newer than the match)
        _seats = start.seats;
        // One level until the game keeps one per seat: the lowest computer seat's, the first seat's when nobody is a computer
        const bool anyComputer = std::ranges::any_of(start.seats, [](SeatKind kind) { return kind == SeatKind::Computer; });
        _level = anyComputer ? FirstComputerLevel(start.seats, start.levels) : start.levels[0];
        if (start.settings.handsPerMatch == kFullMatchHands || start.settings.handsPerMatch == kShortMatchHands) {
            _settings.handsPerMatch = start.settings.handsPerMatch;
        }
        return true;
    }

    void MahjongGame::FlushSave() {
        if (!_store.Open(MatchSaveDirectory())) {
            return;
        }
        if (const uint64_t counter = _recorder.Counter(); counter != _savedCounter) {
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
        const MahjongPrefsSave prefs = MakePrefs(_language.ToStringView(), _hintsOn, _seats, handsAreAChoice ? hands : _savedPrefs.hands, _level, _animationSpeed);
        if (!(prefs == _savedPrefs)) {
            _savedPrefs = prefs;
            _store.SavePrefs(prefs);
        }
    }

}  // namespace BECore::Mahjong
