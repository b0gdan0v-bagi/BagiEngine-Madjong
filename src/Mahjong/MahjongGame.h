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
#include <Mahjong/UploadPrefs.h>

namespace BECore::Mahjong {

    /// The language the game speaks until `SetLanguage` says otherwise (the default language of `config/UIStrings.xml`).
    inline constexpr StringView kDefaultLanguage = "en";

    /**
     * @brief The `IGame` of mahjong: it owns the `Match` and the `TableSession` over it, which every front end drives.
     *
     * Components reach it through `GetActive()` and change the match only through `GetSession()`. The seed of a match is chosen here
     * (`std::random_device`), never in the rules, which only see it: `StartMatch(seed)` is the seam of a test and of the control API.
     * A match begun through the session is logged, and saved when a person begins it (`StartMatch`; the match `Start()` deals behind
     * `Setup` is not, so it cannot overwrite the saved one); **one started through the non-const `GetMatch()` / `GetLocalMatch()` is
     * neither**. `GetSettings()`, `GetSeats()`, `GetLevels()`, the language, the hints mode and the animation speed are kept across
     * `Reset()` and `StartMatch`. Contracts of each part: `src/Mahjong/CLAUDE.md`.
     */
    class MahjongGame : public IGame {
        BE_CLASS(MahjongGame)
    public:
        MahjongGame();
        ~MahjongGame() override;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;

        /// The adapter of the control API (`MahjongGameControl.h`).
        IControllableGame* AsControllable() override;

        /// Applies the saved choices of `Setup` and opens the panel over a match: the saved one when it replays (`CanContinue()`), else a
        /// random one that is not saved (the panel's `Start` begins the one that is).
        void Start() override;
        /// Writes what changed (`FlushSave()`).
        void Stop() override;
        void Reset() override;

        /// Writes the match in progress and the choices of `Setup` when they differ from what was written last. Cheap when nothing
        /// changed, so a frame hook can call it (one write for a burst of computer steps). `src/Mahjong/Save/CLAUDE.md`.
        void FlushSave();

        /// Whether `Start()` put a saved match on the table behind `Setup`. Cleared by `ContinueMatch()`, any new match and the panel closing.
        bool CanContinue() const {
            return _canContinue;
        }
        /// Closes `Setup` (and the rules) and plays the saved match on. Ignored, false, when `CanContinue()` is not true.
        bool ContinueMatch();

        /// Whether what the game saves is there at the next visit: false with saving off for any reason (no folder, a browser that does
        /// not remember). Read at `Start()`.
        [[nodiscard]] bool SavesAreRemembered() const;

        /// The match the table shows: the display copy of the room's table while `IsOnlineTable()`, else the local one.
        Match& GetMatch() {
            return _onlineTable ? _displayMatch : _match;
        }
        const Match& GetMatch() const {
            return _onlineTable ? _displayMatch : _match;
        }

        /// The session over the match the table shows. While `IsOnlineTable()` it is the display session (remote mode: nothing is played
        /// through it, `Game/TableSession.h`).
        TableSession& GetSession() {
            return _onlineTable ? _displaySession : _session;
        }
        const TableSession& GetSession() const {
            return _onlineTable ? _displaySession : _session;
        }

        /// This machine's own match and session, whatever the table shows: what the save, the match log, the upload and the control API see.
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

        /// Whether the table shows a room's game (a table in the `Game` stage, or frozen in `Reconnecting`). It is the state of the last
        /// `SyncOnlineTable()`, so it moves at one place of the frame. While it holds the control API answers `no_active_model`; when it
        /// ends `Setup` opens over the local game as it was.
        bool IsOnlineTable() const {
            return _onlineTable;
        }
        /// Brings the display pair up to date with the client (`Online/OnlineMirror.h`); called right after `GetOnline().Update()`, works
        /// only when `OnlineClient::Version()` moved.
        void SyncOnlineTable();
        /// Whether a room's table has just ended with a word of the client to say: the online panel is open and shows it. True once.
        bool TakeRoomEndNotice() {
            const bool due = _roomEndNotice;
            _roomEndNotice = false;
            return due;
        }

        /// The writer of the match log (`MatchLogWriter.h`); `Path()` is the file of the current (or last) match.
        const MatchLogWriter& GetMatchLog() const {
            return _log;
        }

        /// The recorder of the match for the tester upload (`MatchUpload.h`). Off until a sink and a consent of `Granted` are given: with
        /// the defaults nothing is buffered or sent.
        MatchUploader& GetUpload() {
            return _upload;
        }
        const MatchUploader& GetUpload() const {
            return _upload;
        }

        /// The tester's choice about sending the matches (`UploadPrefs.h`): a plain member; the game itself does nothing with it.
        UploadPrefs& GetUploadPrefs() {
            return _uploadPrefs;
        }
        const UploadPrefs& GetUploadPrefs() const {
            return _uploadPrefs;
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

        /// The level of each seat in the next match (`kDefaultLevel`). All four are kept, also those of person seats, so a seat that turns
        /// computer again keeps its level.
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

        /// Begins a match through the session with `GetSettings()`, `GetSeats()`, `GetLevels()` and `seed` (one seed is one match). Nothing
        /// changes for settings that are not playable (`BadSettings`). A match that began closes the setup panel.
        MatchStatus StartMatch(uint32_t seed);

        /// `StartMatch` with a seed from `std::random_device`; a refusal is logged and returned.
        MatchStatus StartRandomMatch();

        /// `StartMatch` from a position (`Match::Start(settings, seed, state)`): the first hand is the fixed deal of `state`. The match is
        /// saved like any a person begins, and **never uploaded**: a built position is nobody's game. Nothing changes on a refusal
        /// (`BadSettings`, `BadDeal`). The control API's `set_state`.
        MatchStatus StartFixedMatch(uint32_t seed, const FixedStart& state);

        /// Puts the table where `events` (a match log from its `Start`) leave it (`TableSession::ResumeMatch`): the seats and levels of
        /// the first `Start` become the next match's, the setup and the rules close. Saved like any match a person begins, **never
        /// uploaded**, and the log echoes into the match log file and the save. The caller checks the log replays first; a refusal
        /// (the `ReplayError` of the session) changes nothing. The control API's `set_state`.
        std::expected<void, ReplayError> StartFromLog(const Vector<MatchLogEvent>& events);

        /// Whether the panel `Setup` is open. Every new match and every control action close it. While it is open the table is not played.
        bool IsSetupOpen() const {
            return _setupOpen;
        }
        void OpenSetup() {
            _setupOpen = true;
        }
        void CloseSetup() {
            _setupOpen = false;
            _optionsOpen = false;
            _canContinue = false;  // whoever closes the panel has moved on from the offer
        }

        /// Whether the page `Options` is open over the table. `CloseSetup()` closes it too.
        bool IsOptionsOpen() const {
            return _optionsOpen;
        }
        void OpenOptions() {
            _optionsOpen = true;
        }
        void CloseOptions() {
            _optionsOpen = false;
        }

        /// Whether the rules screen (`Table/RulesText.h`) is open. A new match and every control action close it.
        bool IsRulesOpen() const {
            return _rulesOpen;
        }
        void OpenRules() {
            _rulesOpen = true;
        }
        void CloseRules() {
            _rulesOpen = false;
        }

        /// Whether the online panel (`Online/OnlineScreens.h`) is open over `Setup`. `CloseSetup()` does not close it, so closing the panel
        /// gives the player `Setup` and its `Continue` offer back; `Start()` and `Stop()` close it.
        bool IsOnlineOpen() const {
            return _onlineOpen;
        }
        void OpenOnline() {
            _onlineOpen = true;
        }
        void CloseOnline() {
            _onlineOpen = false;
        }

        /// Whether a modal panel covers the table: no click is taken, no computer steps, no hint is shown.
        bool IsTablePaused() const {
            return _setupOpen || _optionsOpen || _rulesOpen || _onlineOpen;
        }

        /// The client of the online play, made on first use. It lives as long as the game, so the screens and the table of a room read the
        /// same one.
        OnlineClient& GetOnline();
        const OnlineClient* FindOnline() const {
            return _online.get();
        }
        /// The seam for a test transport. False, and nothing changes, once the client exists.
        bool SetOnlineSettings(RoomClientSettings settings);

        /// The name the player chose (empty: none) and the session to offer `Return` for (`IsValid()` false: none): the one of the `online`
        /// slot until the client's own moves, then the client's. **The token is a secret**: it is read here to be handed to
        /// `OnlineClient::Resume` and nowhere else.
        const OnlineValues& StoredOnline() const {
            return _storedOnline;
        }
        /// Remembers the player's name; saved by `FlushSave()` when it differs.
        void SetOnlineName(StringView name);

        /// The language of the words, an id of `config/UIStrings.xml`.
        PoolString GetLanguage() const {
            return _language;
        }
        /// Picks the language and applies it to the string table. False, and nothing changes, for a language the table does not have.
        bool SetLanguage(PoolString language);

        /// The player's own choice of hints (`Table/Coach.h`), no rule: the control API does not know it. A change drops the note and the
        /// tally of the coach (`GetCoach()` is enabled exactly while the mode is Coach).
        HintsMode GetHintsMode() const {
            return _hintsMode;
        }
        void SetHintsMode(HintsMode mode);
        /// Hints and Coach (every hint rule is the same in both).
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
        /// The mode at the table on screen: the player's choice outside a room; in a room `Off` unless the host allowed hints
        /// (`OnlineTable::hintsAllowed`). What decides whether a hint is on screen asks `HintsActive()` / `CoachActive()`, not the three above.
        HintsMode EffectiveHintsMode() const {
            return _onlineTable && !_online->Table().hintsAllowed ? HintsMode::Off : _hintsMode;
        }
        bool HintsActive() const {
            return EffectiveHintsMode() != HintsMode::Off;
        }
        bool CoachActive() const {
            return EffectiveHintsMode() == HintsMode::Coach;
        }
        /// The coach's ledger for the table on screen: the local game's, or, while `IsOnlineTable()`, the room's own (fed by
        /// `MahjongTableComponent`); the two never mix. The room's starts afresh when a table starts or ends or the room's setting changes.
        const CoachLedger& GetCoach() const {
            return _onlineTable ? _roomCoach : _coach;
        }
        CoachLedger& GetCoach() {
            return _onlineTable ? _roomCoach : _coach;
        }
        /// The keys of the ledger of `GetCoach()` for the hand on screen. In a room the display session's `StartCount()` moves with every
        /// hand, so the match is a count of the room's tables and the hand adds that start count.
        MoveContext CoachContext() const;

        /// The pace of the table (`Table/AnimationSpeed.h`), saved with the other choices. No rule: the control API neither knows it nor
        /// waits for it.
        AnimationSpeed GetAnimationSpeed() const {
            return _animationSpeed;
        }
        void SetAnimationSpeed(AnimationSpeed speed) {
            _animationSpeed = speed;
        }

        /// The active game, or nullptr when no game is active or the active game is not Mahjong.
        static MahjongGame* GetActive();

    private:
        /// An empty table, or one without the language, is left alone.
        void ApplyLanguage() const;

        /// `StartMatch` with `record`: whether the match is kept for the save.
        MatchStatus BeginMatch(uint32_t seed, bool record);
        MatchStatus BeginRandomMatch(bool record);
        /// None while the storage is not remembered: nothing is saved or offered.
        const std::filesystem::path& SaveDirectory() const;
        void ApplySavedPrefs();
        void ApplySavedOnline();
        /// Marks the slot to be written.
        void FollowOnlineSession();
        /// The panels over the table close; the offer of `Continue` stays.
        void EnterOnlineTable();
        /// The room's ledger is enabled exactly while `CoachActive()`, dropped with @p restart.
        void SyncRoomCoach(bool restart);
        /// The display pair is forgotten; with @p openSetup `Setup` opens over the local game.
        void LeaveOnlineTable(bool openSetup);
        /// False (one warning unless nothing is saved or it is finished) when the saved match cannot be put on the table.
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
        UploadPrefs _uploadPrefs;
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
        UniquePtr<OnlineClient> _online;
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
