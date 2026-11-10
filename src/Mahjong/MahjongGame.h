#pragma once

#include <Games/IGame.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MatchLogWriter.h>
#include <Mahjong/Save/MatchSave.h>
#include <Mahjong/Save/MatchSaveStore.h>

namespace BECore::Mahjong {

    /// The language the game speaks until `SetLanguage` says otherwise (the default language of `config/UIStrings.xml`).
    inline constexpr eastl::string_view kDefaultLanguage = "en";

    /**
     * @brief Concrete IGame for mahjong: it owns the match (`Game/Match.h`), the rules of classic Chinese mahjong for four seats, and
     *        the `TableSession` over it (`Game/TableSession.h`), which every front end drives.
     *
     * Activates "MahjongScene" when started. Auto-registered into AbstractFactory<IGame> via the BE_CLASS reflection machinery, no
     * hand-written registrar. The rules are plain C++ under Game/; the scene glue in Components/ reaches the match through
     * GetActive()->GetMatch() and plays it through GetActive()->GetSession().
     *
     * `Start()` and `Reset()` begin a new match with a seed from `std::random_device`: the randomness is chosen here, outside the rules,
     * which only ever see the seed (`StartMatch(seed)` is how a test or a control API replays a match). A match is begun with
     * `GetSettings()` (the defaults, `MahjongSettings{}`, until `SetSettings`) and the seat setup `GetSeats()` (`kDefaultSeatSetup`:
     * seat 0 a person, the others computers) and the level of the computers `GetLevel()` (`kDefaultLevel`, Normal); all three are kept
     * across `Reset()`. `AsControllable()` hands out the control adapter, which drives the match through the session. The seat choice of
     * a new game is the panel `Setup` of the scene (`IsSetupOpen`, with the page `Options` over it, `IsOptionsOpen`), the pages of the
     * rules are `IsRulesOpen`. The language of the words (`GetLanguage`, `kDefaultLanguage`) is kept for the session and applied to
     * `CoreManager::GetUIStrings()` at every start. `HintsOn()` is the switch of the hints (default on, kept for the session, changed
     * on the page `Options`). Every match begun through the session is written down by the `MatchLogWriter` the game owns
     * (`GetMatchLog()`); a match started through the non-const `GetMatch()` is not.
     *
     * **Saving** (`Save/MatchSave.h`, `Save/MatchSaveStore.h`): the session's sink is a `MatchRecorder` that keeps the events of the
     * match begun by `StartMatch` (and passes every one on to the match log writer), and `FlushSave()` writes the `match` slot (and the
     * `settings` slot, the choices of `Setup`) when something changed; `MahjongHudComponent` calls it once per frame (a `SceneUpdateEvent`) and `Stop()` calls it, so a burst
     * of computer steps is one write. `Start()` reads the prefs and offers a saved match that replays (`CanContinue()`, taken by
     * `ContinueMatch()`); the background match it deals otherwise is not recorded, so it cannot overwrite the saved one.
     */
    class MahjongGame : public IGame {
        BE_CLASS(MahjongGame)
    public:
        MahjongGame();
        ~MahjongGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;

        /// The adapter of the external control API (`MahjongGameControl.h`): a client plays the person seats of the session.
        IControllableGame* AsControllable() override;

        /// Applies the saved choices of `Setup` and opens the panel over a match: the saved one when there is one that replays
        /// (`CanContinue()`), else a new random one that is not saved (the panel's `Start` begins the one that is).
        void Start() override;
        /// Writes what changed (`FlushSave()`).
        void Stop() override;
        void Reset() override;

        /// Writes the match in progress and the choices of `Setup` to the save folder (`MatchSaveDirectory()`) when they differ from what
        /// was written last; the end of a match deletes the match slot. Cheap when nothing changed, so a frame hook can call it.
        void FlushSave();

        /// Whether `Start()` found a saved match and put it on the table behind `Setup`. Cleared by `ContinueMatch()`, by any new match
        /// and by the panel closing.
        bool CanContinue() const {
            return _canContinue;
        }
        /// Closes `Setup` (and the rules) and plays the saved match on. Ignored, false, when `CanContinue()` is not true.
        bool ContinueMatch();

        /// The match: started by `Start()` / `Reset()` / `StartMatch`, `NotStarted` before the game is first started.
        Match& GetMatch() {
            return _match;
        }
        const Match& GetMatch() const {
            return _match;
        }

        /// The session over the match: seat setup, computer players, viewer, state version. Drive the match through it.
        TableSession& GetSession() {
            return _session;
        }
        const TableSession& GetSession() const {
            return _session;
        }

        /// The writer of the match log (`MatchLogWriter.h`): every match begun through the session is written to a file of its own;
        /// `Path()` is the file of the current (or last) match.
        const MatchLogWriter& GetMatchLog() const {
            return _log;
        }

        /// The settings of the next match; a match in progress keeps the ones it was started with.
        const MahjongSettings& GetSettings() const {
            return _settings;
        }
        void SetSettings(const MahjongSettings& settings) {
            _settings = settings;
        }
        /// Who plays each seat in the next match.
        const SeatSetup& GetSeats() const {
            return _seats;
        }
        void SetSeats(const SeatSetup& seats) {
            _seats = seats;
        }

        /// How well the computer seats play in the next match (`Game/ComputerLevel.h`), one level for the whole table; default Normal.
        ComputerLevel GetLevel() const {
            return _level;
        }
        void SetLevel(ComputerLevel level) {
            _level = level;
        }

        /// Begins a new match through the session with `GetSettings()`, `GetSeats()`, `GetLevel()` and `seed` (one seed is one match, the first hand
        /// included). `BadSettings`, and nothing changes, for settings that are not playable. A match that began closes the setup panel.
        MatchStatus StartMatch(uint32_t seed);

        /// `StartMatch` with a seed from `std::random_device`; a refusal (it cannot happen with the default settings) is logged and
        /// returned.
        MatchStatus StartRandomMatch();

        /// Whether the panel that asks which seats are people is open (`MahjongHudComponent` shows it). `Start()` opens it over the match
        /// that it begins; every new match (`StartMatch`, a control `reset`) and a control action close it; the HUD button opens it.
        /// While it is open the table is not played: no click is taken and no computer steps.
        bool IsSetupOpen() const {
            return _setupOpen;
        }
        void OpenSetup() {
            _setupOpen = true;
        }
        void CloseSetup() {
            _setupOpen = false;
            _optionsOpen = false;  // the page lives over the panel
            _canContinue = false;  // whoever closes the panel has moved on from the offer
        }

        /// Whether the page `Options` (language, hints, level of the computers) is open over `Setup`. Meaningful only with `Setup` open:
        /// `Setup` stays open under it, so `Continue` and the pending seats and hands are not lost. `CloseSetup()` closes it, so every
        /// start, `Continue` and every accepted control action do too.
        bool IsOptionsOpen() const {
            return _optionsOpen;
        }
        void OpenOptions() {
            _optionsOpen = true;
        }
        void CloseOptions() {
            _optionsOpen = false;
        }

        /// Whether the rules screen (the modal pages `Rules` of the scene, `Table/RulesText.h`) is open. The HUD button `Rules` opens it; a
        /// new match (`StartMatch`, a control `reset`) and every control action close it. While it is open the table is not played, like
        /// under `Setup`.
        bool IsRulesOpen() const {
            return _rulesOpen;
        }
        void OpenRules() {
            _rulesOpen = true;
        }
        void CloseRules() {
            _rulesOpen = false;
        }

        /// Whether a modal panel (`Setup`, its page `Options` or the rules) covers the table: no click is taken, no computer steps, no hint is shown.
        bool IsTablePaused() const {
            return _setupOpen || _optionsOpen || _rulesOpen;
        }

        /// The language of the words, an id of `config/UIStrings.xml`. Kept across `Reset()` and `StartMatch`.
        PoolString GetLanguage() const {
            return _language;
        }
        /// Picks the language and applies it to the string table. False, and nothing changes, for a language the table does not have.
        bool SetLanguage(PoolString language);

        /// Whether the table shows hints (`Table/Hints.h`: the tiles the hand waits for, the discards that declare, the advice). On by
        /// default, kept across `Reset()`, `StartMatch` and `New game`, takes effect at once. It is no rule, so it is not in the settings,
        /// and the control API does not know it.
        bool HintsOn() const {
            return _hintsOn;
        }
        void SetHintsOn(bool on) {
            _hintsOn = on;
        }

        /// The active game, or nullptr when no game is active or the active game is not Mahjong.
        static MahjongGame* GetActive();

    private:
        /// Puts the stored language into the string table; an empty table (or one without the language) is left alone.
        void ApplyLanguage() const;

        /// `StartMatch` with `record` saying whether the match is kept for the save.
        MatchStatus BeginMatch(uint32_t seed, bool record);
        MatchStatus BeginRandomMatch(bool record);
        /// The saved choices of `Setup` into the game; one warning for a slot that cannot be used.
        void ApplySavedPrefs();
        /// The saved match onto the table; false (one warning unless nothing is saved or it is finished) when it cannot be.
        bool ResumeSavedMatch();

        Match _match;
        MahjongSettings _settings;
        SeatSetup _seats = kDefaultSeatSetup;
        ComputerLevel _level = kDefaultLevel;
        bool _setupOpen = false;
        bool _rulesOpen = false;
        bool _optionsOpen = false;
        bool _hintsOn = true;
        PoolString _language = PoolString::Intern(kDefaultLanguage);
        MatchLogWriter _log;      // before _session: the session reports to it, so it goes last
        MatchRecorder _recorder;  // the session's sink; forwards to _log
        MatchSaveStore _store;
        uint64_t _savedCounter = 0;    // the recorder's counter at the last write of the match slot
        MahjongPrefsSave _savedPrefs;  // what the prefs slot holds (the defaults while there is none)
        bool _canContinue = false;
        TableSession _session{_match};  // after _match: it keeps a reference
        MahjongGameControl _control{*this};
    };

}  // namespace BECore::Mahjong
