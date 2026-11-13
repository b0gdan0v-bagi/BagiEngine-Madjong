#pragma once

#include <Games/IGame.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MatchLogWriter.h>
#include <Mahjong/MatchUpload.h>
#include <Mahjong/Online/OnlineClient.h>
#include <Mahjong/Online/OnlineMirror.h>
#include <Mahjong/Save/MatchSave.h>
#include <Mahjong/Save/MatchSaveStore.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/Coach.h>

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
     * seat 0 a person, the others computers) and the level of each seat `GetLevels()` (`kDefaultLevel`, Normal, all four); all three are kept
     * across `Reset()`. `AsControllable()` hands out the control adapter, which drives the match through the session. The seat choice of
     * a new game is the panel `Setup` of the scene (`IsSetupOpen`), the page `Options` is `IsOptionsOpen`, the pages of the
     * rules are `IsRulesOpen`. The language of the words (`GetLanguage`, `kDefaultLanguage`) is kept for the session and applied to
     * `CoreManager::GetUIStrings()` at every start. `GetHintsMode()` is the switch of the hints (`Off | Hints | Coach`, default Hints, kept for the
     * session, changed on the page `Options`). Every match begun through the session is written down by the `MatchLogWriter` the game owns
     * (`GetMatchLog()`); a match started through the non-const `GetMatch()` / `GetLocalMatch()` is not.
     *
     * **Saving** (`Save/MatchSave.h`, `Save/MatchSaveStore.h`): the session's sink is a `MatchRecorder` that keeps the events of the
     * match begun by `StartMatch` (and passes every one on to the match log writer), and `FlushSave()` writes the `match` slot (and the
     * `settings` slot, the choices of `Setup`) when something changed; `MahjongHudComponent` calls it once per frame (a `SceneUpdateEvent`) and `Stop()` calls it, so a burst
     * of computer steps is one write. `Start()` reads the prefs and offers a saved match that replays (`CanContinue()`, taken by
     * `ContinueMatch()`); the background match it deals otherwise is not recorded, so it cannot overwrite the saved one. A browser whose
     * storage is not remembered (`NotRemembered`: a private window) is saving off: nothing is written or read, and `Continue` is not offered.
     */
    class MahjongGame : public IGame {
        BE_CLASS(MahjongGame)
    public:
        MahjongGame();
        ~MahjongGame() override;

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

        /// Whether what the game saves is there at the next visit: saving is on and the storage keeps it (false with saving off for any
        /// reason: no folder, a folder that cannot be made, a browser that does not remember). The storage state is read at `Start()`.
        [[nodiscard]] bool SavesAreRemembered() const;

        /// The match the table shows: the display copy of the room's table while `IsOnlineTable()`, else the local one (`GetLocalMatch()`).
        /// Started by `Start()` / `Reset()` / `StartMatch`, `NotStarted` before the game is first started.
        Match& GetMatch() {
            return _onlineTable ? _displayMatch : _match;
        }
        const Match& GetMatch() const {
            return _onlineTable ? _displayMatch : _match;
        }

        /// The session over the match the table shows: seat setup, computer players, viewer, state version. Drive the match through it.
        /// While `IsOnlineTable()` it is the display session (remote mode: nothing is played through it, `Game/TableSession.h`).
        TableSession& GetSession() {
            return _onlineTable ? _displaySession : _session;
        }
        const TableSession& GetSession() const {
            return _onlineTable ? _displaySession : _session;
        }

        /// The match and the session of this machine's own game, whatever the table shows: what the save, the match log, the upload and the
        /// control API see. Unchanged while a room's table is shown.
        Match& GetLocalMatch() {
            return _match;
        }
        const Match& GetLocalMatch() const {
            return _match;
        }
        TableSession& GetLocalSession() {
            return _session;
        }
        const TableSession& GetLocalSession() const {
            return _session;
        }

        /// Whether the table shows a room's game: the client has a table in the `Game` stage, or frozen in `Reconnecting`. The state of the last
        /// `SyncOnlineTable()`, so it moves at one place of the frame. While it holds `Setup` and the online panel are closed and the control
        /// API answers `no_active_model`; when it ends (`Leave`, the room gone, a view of another format) `Setup` opens over the local game as it was.
        bool IsOnlineTable() const {
            return _onlineTable;
        }
        /// Brings the display pair up to date with the client (`Online/OnlineMirror.h`). `MahjongOnlineComponent` calls it right after
        /// `GetOnline().Update()`; it does work only when `OnlineClient::Version()` moved.
        void SyncOnlineTable();
        /// Whether the table of a room has just ended with a word of the client to say (the room is gone, another device took the seat, a view of
        /// another format): `Setup` and the online panel are open, and the panel shows that notice instead of clearing it. True once.
        bool TakeRoomEndNotice() {
            const bool due = _roomEndNotice;
            _roomEndNotice = false;
            return due;
        }

        /// The writer of the match log (`MatchLogWriter.h`): every match begun through the session is written to a file of its own;
        /// `Path()` is the file of the current (or last) match.
        const MatchLogWriter& GetMatchLog() const {
            return _log;
        }

        /// The recorder of the match for the tester upload (`MatchUpload.h`), in the session's sink chain between the save recorder and the
        /// log writer. Off until a sink, a match id source (set here) and a consent of `Granted` are given: with the defaults nothing is
        /// buffered or sent. The glue that has the real sink and the consent UI sets them through this.
        MatchUploader& GetUpload() {
            return _upload;
        }
        const MatchUploader& GetUpload() const {
            return _upload;
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

        /// How well each seat plays in the next match (`Game/ComputerLevel.h`), one level per seat; default Normal. All four are kept, also
        /// those of seats a person plays: only computer seats use theirs, and a seat that turns computer again keeps its level.
        const SeatLevels& GetLevels() const {
            return _levels;
        }
        void SetLevels(const SeatLevels& levels) {
            _levels = levels;
        }
        /// All four seats at `level`.
        void SetLevel(ComputerLevel level) {
            _levels = UniformLevels(level);
        }

        /// Begins a new match through the session with `GetSettings()`, `GetSeats()`, `GetLevels()` and `seed` (one seed is one match, the first hand
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
            _optionsOpen = false;  // a start or a control action leaves every page
            _canContinue = false;  // whoever closes the panel has moved on from the offer
        }

        /// Whether the page `Options` (language, hints, speed) is open over the table (the HUD button opens it). `CloseSetup()` closes it
        /// too, so every start, `Continue` and every accepted control action do.
        bool IsOptionsOpen() const {
            return _optionsOpen;
        }
        void OpenOptions() {
            _optionsOpen = true;
        }
        void CloseOptions() {
            _optionsOpen = false;
        }

        /// Whether the rules screen (the modal pages `Rules` of the scene, `Table/RulesText.h`) is open. The button `Rules` of `Options` opens it; a
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

        /// Whether the online panel (the pages of `Online/OnlineScreens.h`) is open over `Setup`. It is a flag of its own: `CloseSetup()` (every
        /// start and control action) does not close it, so closing the panel gives the player `Setup` and its `Continue` offer back; `Start()` and
        /// `Stop()` close it.
        bool IsOnlineOpen() const {
            return _onlineOpen;
        }
        void OpenOnline() {
            _onlineOpen = true;
        }
        void CloseOnline() {
            _onlineOpen = false;
        }

        /// Whether a modal panel (`Setup`, the page `Options`, the rules or the online panel) covers the table: no click is taken, no computer
        /// steps, no hint is shown.
        bool IsTablePaused() const {
            return _setupOpen || _optionsOpen || _rulesOpen || _onlineOpen;
        }

        /// The client of the online play, made on first use with `SetOnlineSettings` (the defaults: the real transport). It lives as long as
        /// the game: the screens and the table of a room read it, and `Update()` is called by the component once per frame.
        OnlineClient& GetOnline();
        const OnlineClient* FindOnline() const {
            return _online.get();
        }
        /// The settings of the client (a test gives it a transport of its own). False, and nothing changes, once the client exists.
        bool SetOnlineSettings(RoomClientSettings settings);

        /// What `Start()` read of the `online` slot, and what the game keeps since: the name the player chose (empty: none) and the session to
        /// offer `Return` for (`IsValid()` false: none). The session is the one of the slot until the client's own moves (`SessionRevision()`),
        /// then the client's, so it follows `Leave` and a room that is gone. **The token is a secret**: it is read here to be handed to
        /// `OnlineClient::Resume` and nowhere else.
        const OnlineValues& StoredOnline() const {
            return _storedOnline;
        }
        /// Remembers the player's name; saved by `FlushSave()` when it differs.
        void SetOnlineName(eastl::string_view name);

        /// The language of the words, an id of `config/UIStrings.xml`. Kept across `Reset()` and `StartMatch`.
        PoolString GetLanguage() const {
            return _language;
        }
        /// Picks the language and applies it to the string table. False, and nothing changes, for a language the table does not have.
        bool SetLanguage(PoolString language);

        /// What the hints of the table do (`Table/Coach.h`): nothing, the hints (`Table/Hints.h`: the tiles the hand waits for, the discards
        /// that declare, the advice), or the hints and the coach's notes. Hints by default, kept across `Reset()`, `StartMatch` and `New
        /// game`, takes effect at once. It is no rule, so it is not in the settings, and the control API does not know it. A change drops
        /// the note and the tally of the coach (`GetCoach()` is enabled exactly while the mode is Coach).
        HintsMode GetHintsMode() const {
            return _hintsMode;
        }
        void SetHintsMode(HintsMode mode);
        /// The hints are on: Hints and Coach (so every hint rule is the same in both).
        bool HintsOn() const {
            return _hintsMode != HintsMode::Off;
        }
        /// Hints or Off, as `SetHintsMode`.
        void SetHintsOn(bool on) {
            SetHintsMode(on ? HintsMode::Hints : HintsMode::Off);
        }
        bool CoachOn() const {
            return _hintsMode == HintsMode::Coach;
        }
        /// The mode that applies at the table on screen: the player's own choice outside a room; in a room `Off` unless the room allows hints
        /// (`OnlineTable::hintsAllowed`, the host's decision), else the player's choice. The three above stay the player's own choice (the Options
        /// page, the saved choices); everything that decides whether a hint is on screen asks `HintsActive()` / `CoachActive()`.
        HintsMode EffectiveHintsMode() const {
            return _onlineTable && !_online->Table().hintsAllowed ? HintsMode::Off : _hintsMode;
        }
        bool HintsActive() const {
            return EffectiveHintsMode() != HintsMode::Off;
        }
        bool CoachActive() const {
            return EffectiveHintsMode() == HintsMode::Coach;
        }
        /// The note and the tally of the coach for the table on screen: the local game's (fed by the session while the mode is Coach and one person
        /// sits at the table), or, while `IsOnlineTable()`, the room's own ledger, fed by `MahjongTableComponent` with the player's accepted actions
        /// and enabled exactly while `CoachActive()`; the two never mix. It starts afresh when a room's table starts or ends or the room's
        /// setting changes.
        const CoachLedger& GetCoach() const {
            return _onlineTable ? _roomCoach : _coach;
        }
        CoachLedger& GetCoach() {
            return _onlineTable ? _roomCoach : _coach;
        }
        /// The keys of the ledger of `GetCoach()` for the hand on screen (`HandKeyOf(context)`; `startCount` is the match). Locally the session's
        /// own `Context()`. In a room the display session's `StartCount()` moves with every hand, so the match is a count of the game (one per
        /// table of a room) and the hand adds that start count.
        MoveContext CoachContext() const;

        /// The speed of the table (`Table/AnimationSpeed.h`): the pause of the computers, the flights and the marks of a claim, at once.
        /// Normal by default, kept across `Reset()`, `StartMatch` and `New game`, saved with the other choices. It is no rule, so it is not
        /// in the settings, and the control API neither knows it nor waits for it.
        AnimationSpeed GetAnimationSpeed() const {
            return _animationSpeed;
        }
        void SetAnimationSpeed(AnimationSpeed speed) {
            _animationSpeed = speed;
        }

        /// The active game, or nullptr when no game is active or the active game is not Mahjong.
        static MahjongGame* GetActive();

    private:
        /// Puts the stored language into the string table; an empty table (or one without the language) is left alone.
        void ApplyLanguage() const;

        /// `StartMatch` with `record` saying whether the match is kept for the save.
        MatchStatus BeginMatch(uint32_t seed, bool record);
        MatchStatus BeginRandomMatch(bool record);
        /// The folder the store opens: `MatchSaveDirectory()`, or none while the storage is not remembered (nothing is saved or offered).
        const std::filesystem::path& SaveDirectory() const;
        /// The saved choices of `Setup` into the game; one warning for a slot that cannot be used.
        void ApplySavedPrefs();
        /// The `online` slot into `_storedOnline`; one warning for a slot that cannot be used.
        void ApplySavedOnline();
        /// The client's session into `_storedOnline` once its revision moved; marks the slot to be written.
        void FollowOnlineSession();
        /// The table of a room goes live: the panels over the table close (the offer of `Continue` stays).
        void EnterOnlineTable();
        /// The room's ledger follows `CoachActive()`: enabled exactly then, dropped with @p restart (a table starts or ends).
        void SyncRoomCoach(bool restart);
        /// The table of a room is gone: the display pair is forgotten, and, with @p openSetup, `Setup` opens over the local game.
        void LeaveOnlineTable(bool openSetup);
        /// The saved match onto the table; false (one warning unless nothing is saved or it is finished) when it cannot be.
        bool ResumeSavedMatch();

        Match _match;
        MahjongSettings _settings;
        SeatSetup _seats = kDefaultSeatSetup;
        SeatLevels _levels = UniformLevels(kDefaultLevel);
        bool _setupOpen = false;
        bool _rulesOpen = false;
        bool _optionsOpen = false;
        bool _onlineOpen = false;
        HintsMode _hintsMode = kDefaultHintsMode;
        AnimationSpeed _animationSpeed = kDefaultAnimationSpeed;
        PoolString _language = PoolString::Intern(kDefaultLanguage);
        MatchLogWriter _log;      // before _session: the session reports to it, so it goes last
        MatchUploader _upload;    // forwards to _log
        MatchRecorder _recorder;  // the session's sink; forwards to _upload
        MatchSaveStore _store;
        SaveStorageState _storage = SaveStorageState::NotApplicable;  // `MatchSaveStorage()` at the last `Start()`
        uint64_t _savedCounter = 0;                                   // the recorder's counter at the last write of the match slot
        MahjongPrefsSave _savedPrefs;                                 // what the prefs slot holds (the defaults while there is none)
        bool _canContinue = false;
        RoomClientSettings _onlineSettings;
        OnlineValues _storedOnline;
        uint32_t _seenSessionRevision = 0;  // of the client, when `_storedOnline.session` last took its session
        bool _onlineDirty = false;          // the name or the session moved since the slot was written
        TableSession _session{_match};      // after _match: it keeps a reference
        CoachLedger _coach;                 // after _session, which holds a pointer to it (the destructor lets it go)
        CoachLedger _roomCoach;             // the ledger of a room's table: nobody observes the display session, the table component feeds it
        uint64_t _roomTables = 0;           // the tables of a room shown so far: the match of the room's ledger
        MahjongGameControl _control{*this};
        eastl::unique_ptr<OnlineClient> _online;
        // The table of a room as a display copy (`Online/OnlineMirror.h`); the local pair above never sees it
        Match _displayMatch;
        TableSession _displaySession{_displayMatch};  // after _displayMatch: it keeps a reference
        MoveTracker _tracker;
        DisplayState _displayState;  // members, so a view keeps the capacity of the last
        TableKey _tableKey;
        static constexpr uint64_t kNotSynced = std::numeric_limits<uint64_t>::max();
        uint64_t _syncedClient = kNotSynced;  // `OnlineClient::Version()` of the last `SyncOnlineTable()`
        bool _onlineTable = false;
        uint32_t _noticeAtTable = 0;  // `OnlineClient::Notice().serial` when the table went live: a later one is a word for the player
        bool _roomEndNotice = false;
    };

}  // namespace BECore::Mahjong
