#pragma once

#include <BECore/Reflection/TypeTraits.h>
#include <BECore/Save/SaveGame.h>
#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/Table/AnimationSpeed.h>

namespace BECore::Mahjong {

    /// The id of the game in a save file (`SaveSchema::game`)
    inline constexpr eastl::string_view kMahjongSaveGame = "Mahjong";

    /// The version of the state schema that is written now
    inline constexpr uint32_t kMahjongSaveVersion = 1;

    /// The oldest version that is still read
    inline constexpr uint32_t kMahjongSaveMinVersion = 1;

    /// What the save game service gets for both slots
    inline constexpr SaveSchema kMahjongSaveSchema{kMahjongSaveGame, kMahjongSaveVersion, kMahjongSaveMinVersion};

    /// The slot of the match in progress and the slot of the choices of `Setup`
    inline constexpr eastl::string_view kMatchSlot = "match";
    inline constexpr eastl::string_view kPrefsSlot = "settings";

    /// The most lines a saved match may have: a sanity cap on what is parsed (a 16-hand match of four players is a few thousand events)
    inline constexpr size_t kMaxSavedEvents = 50000;

    /**
     * @brief The match in progress as it is saved: the text of `MatchSaveText`, one JSON line per event.
     *
     * A log and not the table: the resume replays the actions on a fresh match (`TableSession::ResumeMatch`), and the settlements in
     * the text are what the replay checks itself against.
     */
    struct MahjongMatchSave {
        BE_CLASS(MahjongMatchSave)

        /// The `Start`, `Action` and `Settlement` events of the match, one line each (`MatchSaveText`)
        BE_REFLECT_FIELD eastl::string log;

        bool operator==(const MahjongMatchSave& other) const {
            return log == other.log;
        }
    };

    /**
     * @brief The seven choices of the `Setup` panel and the `Options` page, saved so that the game opens on them. No rule that a control `reset` alone can set
     * (`flowers`, `limit`, `replayDrawnHand`, `startingScore`) is here: the match slot has the settings of its match.
     *
     * Adding a field needs no version (an old file keeps the default); changing what a field means does (`kMahjongSaveVersion`).
     */
    struct MahjongPrefsSave {
        BE_CLASS(MahjongPrefsSave)

        /// The id of the language of the string table ("en", "ru"); the game checks it against the table
        BE_REFLECT_FIELD eastl::string language = "en";
        /// The hints switch
        BE_REFLECT_FIELD bool hintsOn = true;
        /// Who plays each seat, four letters as in the control `reset`: P a person, C a computer
        BE_REFLECT_FIELD eastl::string seats = "PCCC";
        /// 16 or 8
        BE_REFLECT_FIELD int32_t hands = kFullMatchHands;
        /// The level of the computer players, its number as text `1` .. `10` (`LevelText`); a copy of an older build holds `easy` or
        /// `normal` (`TryParseLevelName`)
        BE_REFLECT_FIELD eastl::string level = "5";
        /// The level of each seat, four numbers `1` .. `10` separated by commas ("5,2,6,10", seat 0 first; a person's seat too). Empty: all
        /// four seats are at `level`, which is what an older copy and a table of one level hold (`MakePrefs` leaves it empty then). When it
        /// is not empty it wins; `level` then holds the level of the lowest computer seat.
        BE_REFLECT_FIELD eastl::string levels;
        /// The speed of the table (`SpeedText`: `slow` | `normal` | `fast`); a copy written before the setting holds the default
        BE_REFLECT_FIELD eastl::string speed = "normal";

        bool operator==(const MahjongPrefsSave& other) const {
            return language == other.language && hintsOn == other.hintsOn && seats == other.seats && hands == other.hands && level == other.level && levels == other.levels && speed == other.speed;
        }
    };

    /// The choices of `MahjongPrefsSave`, checked and in the types the game uses.
    struct PrefsValues {
        eastl::string language;
        bool hintsOn = true;
        SeatSetup seats = kDefaultSeatSetup;
        int handsPerMatch = kFullMatchHands;
        SeatLevels levels = UniformLevels(kDefaultLevel);
        AnimationSpeed speed = kDefaultAnimationSpeed;
    };

    /**
     * @brief The text of a match for the slot: the `Start`, `Action` and `Settlement` events of `events`, one `EventToJson` line each
     * (the form of `MatchLogJson.h`, ending in a newline), with no `Hand` and `End` events (the replay deals again and ends the match)
     * and no `seats` (the breakdowns) in a settlement (reading does not need them). Events of another kind are skipped.
     */
    [[nodiscard]] eastl::string MatchSaveText(const eastl::vector<MatchLogEvent>& events);

    /**
     * @brief The events of a saved text, or the error: the line number and what is wrong (not JSON, a member missing or wrong), that the
     * first event is not a `Start`, that a `Hand` or `End` line is there, or that there are more than `kMaxSavedEvents` lines. Nothing is
     * replayed here; `ReplayMatchLog` decides whether the match comes out.
     */
    [[nodiscard]] std::expected<eastl::vector<MatchLogEvent>, eastl::string> ParseMatchSaveText(std::string_view text);

    /// The state of the prefs slot from the choices of the game.
    [[nodiscard]] MahjongPrefsSave MakePrefs(eastl::string_view language, bool hintsOn, const SeatSetup& seats, int handsPerMatch, const SeatLevels& levels,
                                             AnimationSpeed speed = kDefaultAnimationSpeed);
    /// The same for a table of one level.
    [[nodiscard]] MahjongPrefsSave MakePrefs(eastl::string_view language, bool hintsOn, const SeatSetup& seats, int handsPerMatch, ComputerLevel level, AnimationSpeed speed = kDefaultAnimationSpeed);

    /// The choices of a prefs state, or the first thing wrong: seats not four letters P / C, a `levels` text that is not four numbers `1` .. `10`, a level that is not `1` .. `10` (or the old names
    /// `easy` / `normal`; not read when `levels` is there), a speed that is not `slow` / `normal` / `fast`, hands other than 16 and 8, an empty language.
    [[nodiscard]] std::expected<PrefsValues, eastl::string> ReadPrefs(const MahjongPrefsSave& prefs);

    /**
     * @brief The sink of a session that keeps the match for the save: the `Start`, `Action` and `Settlement` events of the current match.
     *
     * Every event is first passed on to the next sink (`SetNext`, the match log writer), recorded or not. A `Start` clears the kept
     * events; the other events are kept only in a match that was begun while `IsRecording()` (set by `SetRecording`, copied by a
     * `Start`, so a change applies to the matches that begin after it). `Counter()` grows with every kept event and with every `Start`
     * and `End`, so a writer that remembers the last counter it saved knows when something is new, whatever the number of events
     * between two looks. `Finished()` is true from an `End` to the next `Start`.
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

        /// Whether the matches that begin from now on are kept
        void SetRecording(bool recording) noexcept {
            _wantRecording = recording;
        }
        /// Whether the match in progress is being kept (the flag as it was at its `Start`)
        [[nodiscard]] bool IsRecording() const noexcept {
            return _recording;
        }

        /// The `Start`, `Action` and `Settlement` events of the current match; empty for a match that is not kept
        [[nodiscard]] const eastl::vector<MatchLogEvent>& Events() const noexcept {
            return _events;
        }
        [[nodiscard]] uint64_t Counter() const noexcept {
            return _counter;
        }
        /// Whether the `End` of the match was seen
        [[nodiscard]] bool Finished() const noexcept {
            return _finished;
        }

    private:
        IMatchLogSink* _next = nullptr;
        eastl::vector<MatchLogEvent> _events;
        uint64_t _counter = 0;
        bool _wantRecording = true;
        bool _recording = false;
        bool _finished = false;
    };

}  // namespace BECore::Mahjong
