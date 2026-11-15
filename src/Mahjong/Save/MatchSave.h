#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Save/SaveGame.h>
#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/Table/AnimationSpeed.h>
#include <Mahjong/Table/Coach.h>
#include <RoomClient/RoomClient.h>

namespace BECore::Mahjong {

    /// The id of the game in a save file (`SaveSchema::game`): part of the file format, never renamed
    inline constexpr StringView kMahjongSaveGame = "Mahjong";

    /// Bumped when a field changes meaning; a new field needs none
    inline constexpr uint32_t kMahjongSaveVersion = 1;

    /// The oldest version still read
    inline constexpr uint32_t kMahjongSaveMinVersion = 1;

    /// What the save game service gets for every slot
    inline constexpr SaveSchema kMahjongSaveSchema{kMahjongSaveGame, kMahjongSaveVersion, kMahjongSaveMinVersion};

    /// The slot names are the file names of saves already on a player's disk
    inline constexpr StringView kMatchSlot = "match";
    inline constexpr StringView kPrefsSlot = "settings";
    inline constexpr StringView kOnlineSlot = "online";

    /// A sanity cap on what is parsed, far above a 16-hand match
    inline constexpr size_t kMaxSavedEvents = 50000;

    /**
     * @brief The match in progress as the text of `MatchSaveText`: a log, not the table (`src/Mahjong/Save/CLAUDE.md`, "What is stored").
     */
    struct MahjongMatchSave {
        BE_CLASS(MahjongMatchSave)

        BE_REFLECT_FIELD String log;

        bool operator==(const MahjongMatchSave& other) const {
            return log == other.log;
        }
    };

    /// The theme the game starts in (the default of `config/UIThemes.xml`). Here and not in `MahjongGame.h` because the defaults of the prefs below need it.
    inline constexpr StringView kDefaultTheme = "classic";

    /**
     * @brief The choices of the `Setup` panel and the `Options` page. Rules only a control `reset` can set are not here: the match slot
     * has the settings of its match.
     *
     * Adding a field needs no version (an old file keeps the default); changing what a field means does (`kMahjongSaveVersion`).
     */
    struct MahjongPrefsSave {
        BE_CLASS(MahjongPrefsSave)

        /// Checked by the game against the string table
        BE_REFLECT_FIELD String language = "en";
        BE_REFLECT_FIELD bool hintsOn = true;
        /// `HintsMode::Coach`; implies `hintsOn`; a file without it reads false
        BE_REFLECT_FIELD bool coach = false;
        /// Four letters as in the control `reset`: P a person, C a computer
        BE_REFLECT_FIELD String seats = "PCCC";
        BE_REFLECT_FIELD int32_t hands = kFullMatchHands;
        /// `LevelText`; an older file holds `easy` or `normal` (`TryParseLevelName`)
        BE_REFLECT_FIELD String level = "5";
        /// Four numbers, seat 0 first ("5,2,6,10"). Empty means all four seats are at `level` (an older file); when set it wins and
        /// `level` is the lowest computer seat's.
        BE_REFLECT_FIELD String levels;
        /// `SpeedText`; a file without it holds the default
        BE_REFLECT_FIELD String speed = "normal";
        /// An id of `config/UIThemes.xml`; a file without it holds the default. An id the table lacks is left to the game, like `language`
        BE_REFLECT_FIELD String theme = "classic";

        bool operator==(const MahjongPrefsSave& other) const {
            return language == other.language && hintsOn == other.hintsOn && coach == other.coach && seats == other.seats && hands == other.hands && level == other.level && levels == other.levels &&
                   speed == other.speed && theme == other.theme;
        }
    };

    /**
     * @brief The player's name and the session of the room they may come back to (`src/Mahjong/Online/OnlineClient.h`).
     *
     * **The session holds the token, a secret**: the slot is a plain file like the others (`src/Mahjong/Save/CLAUDE.md`) and the token
     * goes nowhere else, never into a log line or a message. Adding a field needs no version.
     */
    struct MahjongOnlineSave {
        BE_CLASS(MahjongOnlineSave)

        /// UTF-8; empty when none was typed
        BE_REFLECT_FIELD String name;
        /// The JSON text of `SessionToJson`; empty when no room is kept
        BE_REFLECT_FIELD String session;

        bool operator==(const MahjongOnlineSave& other) const {
            return name == other.name && session == other.session;
        }
    };

    /// `MahjongOnlineSave`, checked: an empty name and an invalid session mean none.
    struct OnlineValues {
        String name;
        RoomSession session;
    };

    /// `MahjongPrefsSave`, checked.
    struct PrefsValues {
        String language;
        HintsMode hintsMode = kDefaultHintsMode;
        SeatSetup seats = kDefaultSeatSetup;
        int handsPerMatch = kFullMatchHands;
        SeatLevels levels = UniformLevels(kDefaultLevel);
        AnimationSpeed speed = kDefaultAnimationSpeed;
        /// Not checked against the table of themes: that is the game's
        String theme = String(kDefaultTheme);
    };

    /**
     * @brief The `Start`, `Action` and `Settlement` events as `EventToJson` lines (`MatchLogJson.h`). `Hand` and `End` are skipped (the
     * replay deals and ends again), as are the breakdowns of a settlement.
     */
    [[nodiscard]] String MatchSaveText(const Vector<MatchLogEvent>& events);

    /**
     * @brief The events of a saved text, or the line number and what is wrong. Nothing is replayed here; `ReplayMatchLog` decides
     * whether the match comes out.
     */
    [[nodiscard]] std::expected<Vector<MatchLogEvent>, String> ParseMatchSaveText(StringView text);

    /// `coach` implies `hintsOn`.
    [[nodiscard]] MahjongPrefsSave MakePrefs(StringView language, bool hintsOn, const SeatSetup& seats, int handsPerMatch, const SeatLevels& levels, AnimationSpeed speed = kDefaultAnimationSpeed,
                                             bool coach = false, StringView theme = kDefaultTheme);
    /// A table of one level.
    [[nodiscard]] MahjongPrefsSave MakePrefs(StringView language, bool hintsOn, const SeatSetup& seats, int handsPerMatch, ComputerLevel level, AnimationSpeed speed = kDefaultAnimationSpeed,
                                             bool coach = false, StringView theme = kDefaultTheme);

    /// The choices of a prefs state (`coach` wins over `hintsOn`), or the first thing wrong; `level` is not read when `levels` is there.
    [[nodiscard]] std::expected<PrefsValues, String> ReadPrefs(const MahjongPrefsSave& prefs);

    /// An invalid @p session keeps no room.
    [[nodiscard]] MahjongOnlineSave MakeOnlineSave(StringView name, const RoomSession& session);

    /// The values, or the first thing wrong (a name `SanitizePlayerName` refuses, a session `SessionFromJson` refuses). The error never
    /// repeats the token.
    [[nodiscard]] std::expected<OnlineValues, String> ReadOnline(const MahjongOnlineSave& save);

    /**
     * @brief The sink that keeps the events of the current match for the save.
     *
     * Every event is first passed on to the next sink, recorded or not. `SetRecording` applies to the matches that begin after it (a
     * `Start` copies it). `Counter()` grows with every kept event, `Start` and `End`, so a writer that remembers the last counter it
     * saved knows when something is new.
     *
     * Not thread safe (the thread that drives the table).
     */
    class MatchRecorder final : public IMatchLogSink {
    public:
        void OnEvent(const MatchLogEvent& event) override;

        /// Where every event goes first; not owned, may be null
        void SetNext(IMatchLogSink* next) noexcept {
            _next = next;
        }

        void SetRecording(bool recording) noexcept {
            _wantRecording = recording;
        }
        /// The flag as it was at the `Start` of the match in progress
        [[nodiscard]] bool IsRecording() const noexcept {
            return _recording;
        }

        /// Empty for a match that is not kept
        [[nodiscard]] const Vector<MatchLogEvent>& Events() const noexcept {
            return _events;
        }
        [[nodiscard]] uint64_t Counter() const noexcept {
            return _counter;
        }
        /// True from an `End` to the next `Start`
        [[nodiscard]] bool Finished() const noexcept {
            return _finished;
        }

    private:
        IMatchLogSink* _next = nullptr;
        Vector<MatchLogEvent> _events;
        uint64_t _counter = 0;
        bool _wantRecording = true;
        bool _recording = false;
        bool _finished = false;
    };

}  // namespace BECore::Mahjong
