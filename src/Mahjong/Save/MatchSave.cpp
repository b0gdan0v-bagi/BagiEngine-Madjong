#include "MatchSave.h"

#include <Generated/MatchSave.gen.hpp>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MatchLogJson.h>

namespace BECore::Mahjong {

    namespace {

        constexpr const char* kSettlementBreakdownsKey = "seats";

        /// The line number (from 1) of the `index`th event of a text: the lines that are not blank, as `ParseMatchLog` counts them.
        size_t LineOfEvent(std::string_view text, size_t index) {
            size_t lineNumber = 0;
            size_t seen = 0;
            size_t begin = 0;
            while (begin <= text.size()) {
                size_t end = text.find('\n', begin);
                if (end == std::string_view::npos) {
                    end = text.size();
                }
                ++lineNumber;
                if (text.substr(begin, end - begin).find_first_not_of(" \t\r") != std::string_view::npos) {
                    if (seen == index) {
                        return lineNumber;
                    }
                    ++seen;
                }
                begin = end + 1;
            }
            return lineNumber;
        }

        size_t CountLines(std::string_view text) {
            size_t count = 0;
            size_t begin = 0;
            while (begin < text.size()) {
                size_t end = text.find('\n', begin);
                if (end == std::string_view::npos) {
                    end = text.size();
                }
                if (text.substr(begin, end - begin).find_first_not_of(" \t\r") != std::string_view::npos) {
                    ++count;
                }
                begin = end + 1;
            }
            return count;
        }

        constexpr char kLevelsSeparator = ',';

        /// The four levels of a `levels` text ("5,2,6,10"), or nothing when it is not exactly four numbers 1..10.
        std::optional<SeatLevels> ParseLevelsText(std::string_view text) {
            SeatLevels levels = UniformLevels(kDefaultLevel);
            for (size_t seat = 0; seat < levels.size(); ++seat) {
                const bool last = seat + 1 == levels.size();
                const size_t end = text.find(kLevelsSeparator);
                if ((end == std::string_view::npos) != last) {
                    return std::nullopt;
                }
                const std::optional<ComputerLevel> level = TryParseLevel(text.substr(0, end));
                if (!level) {
                    return std::nullopt;
                }
                levels[seat] = *level;
                text = last ? std::string_view() : text.substr(end + 1);
            }
            return levels;
        }

    }  // namespace

    eastl::string MatchSaveText(const eastl::vector<MatchLogEvent>& events) {
        eastl::string text;
        for (const MatchLogEvent& event : events) {
            if (event.kind == MatchLogKind::Hand || event.kind == MatchLogKind::End) {
                continue;
            }
            Json json = EventToJson(event);
            if (event.kind == MatchLogKind::Settlement) {
                json.erase(kSettlementBreakdownsKey);
            }
            const std::string line = json.dump();
            text.append(line.data(), line.size());
            text.push_back('\n');
        }
        return text;
    }

    std::expected<eastl::vector<MatchLogEvent>, eastl::string> ParseMatchSaveText(std::string_view text) {
        if (const size_t lines = CountLines(text); lines > kMaxSavedEvents) {
            return std::unexpected(Format("the saved match has {} lines, more than the {} allowed", lines, kMaxSavedEvents));
        }
        std::expected<eastl::vector<MatchLogEvent>, eastl::string> events = ParseMatchLog(text);
        if (!events) {
            return events;
        }
        if (events->empty()) {
            return std::unexpected(eastl::string("the saved match is empty"));
        }
        for (size_t index = 0; index < events->size(); ++index) {
            const MatchLogKind kind = (*events)[index].kind;
            if (index == 0 && kind != MatchLogKind::Start) {
                return std::unexpected(Format("line {}: the saved match has to begin with a start", LineOfEvent(text, index)));
            }
            if (kind == MatchLogKind::Hand || kind == MatchLogKind::End) {
                return std::unexpected(Format("line {}: a saved match holds no {} event", LineOfEvent(text, index), EnumUtils<MatchLogKind>::ToString(kind)));
            }
        }
        return events;
    }

    MahjongPrefsSave MakePrefs(eastl::string_view language, bool hintsOn, const SeatSetup& seats, int handsPerMatch, ComputerLevel level, AnimationSpeed speed) {
        return MakePrefs(language, hintsOn, seats, handsPerMatch, UniformLevels(level), speed);
    }

    MahjongPrefsSave MakePrefs(eastl::string_view language, bool hintsOn, const SeatSetup& seats, int handsPerMatch, const SeatLevels& levels, AnimationSpeed speed) {
        MahjongPrefsSave prefs;
        prefs.language.assign(language.data(), language.size());
        prefs.hintsOn = hintsOn;
        prefs.seats = SeatsToLetters(seats);
        prefs.hands = handsPerMatch;
        const std::string_view text = LevelText(FirstComputerLevel(seats, levels));
        prefs.level.assign(text.data(), text.size());
        if (levels != UniformLevels(levels[0])) {
            for (size_t seat = 0; seat < levels.size(); ++seat) {
                if (seat > 0) {
                    prefs.levels.push_back(kLevelsSeparator);
                }
                const std::string_view number = LevelText(levels[seat]);
                prefs.levels.append(number.data(), number.size());
            }
        } else if (std::ranges::none_of(seats, [](SeatKind kind) { return kind == SeatKind::Computer; })) {
            // `level` is the default when nobody is a computer, so it cannot carry the one level of the table
            const std::string_view number = LevelText(levels[0]);
            prefs.level.assign(number.data(), number.size());
        }
        const std::string_view speedText = SpeedText(speed);
        prefs.speed.assign(speedText.data(), speedText.size());
        return prefs;
    }

    std::expected<PrefsValues, eastl::string> ReadPrefs(const MahjongPrefsSave& prefs) {
        PrefsValues values;
        if (prefs.language.empty()) {
            return std::unexpected(eastl::string("the language is empty"));
        }
        values.language = prefs.language;
        values.hintsOn = prefs.hintsOn;
        const std::optional<SeatSetup> seats = ReadSeats(prefs.seats);
        if (!seats) {
            return std::unexpected(Format("the seats '{}' are not four letters P or C", prefs.seats));
        }
        values.seats = *seats;
        if (prefs.hands != kFullMatchHands && prefs.hands != kShortMatchHands) {
            return std::unexpected(Format("the hands {} are not {} or {}", prefs.hands, kFullMatchHands, kShortMatchHands));
        }
        values.handsPerMatch = prefs.hands;
        if (!prefs.levels.empty()) {  // one level per seat: it wins over `level`
            const std::optional<SeatLevels> levels = ParseLevelsText(std::string_view(prefs.levels.c_str(), prefs.levels.size()));
            if (!levels) {
                return std::unexpected(Format("the levels '{}' are not four numbers {}..{} separated by commas", prefs.levels, kMinLevel, kMaxLevel));
            }
            values.levels = *levels;
        } else {
            // A copy written before the scale of numbers holds a name (`easy` | `normal`): read as the level of that name
            const std::string_view levelText(prefs.level.c_str(), prefs.level.size());
            std::optional<ComputerLevel> level = TryParseLevel(levelText);
            if (!level) {
                level = TryParseLevelName(levelText);
            }
            if (!level) {
                return std::unexpected(Format("the level '{}' is not {}", prefs.level, LevelRangeText()));
            }
            values.levels = UniformLevels(*level);
        }
        const std::optional<AnimationSpeed> speed = TryParseSpeed(std::string_view(prefs.speed.c_str(), prefs.speed.size()));
        if (!speed) {
            return std::unexpected(Format("the speed '{}' is not slow, normal or fast", prefs.speed));
        }
        values.speed = *speed;
        return values;
    }

    void MatchRecorder::OnEvent(const MatchLogEvent& event) {
        if (_next != nullptr) {
            _next->OnEvent(event);
        }
        switch (event.kind) {
            case MatchLogKind::Start:
                _recording = _wantRecording;
                _finished = false;
                _events.clear();
                ++_counter;
                if (_recording) {
                    _events.push_back(event);
                }
                break;
            case MatchLogKind::Action:
            case MatchLogKind::Settlement:
                if (_recording) {
                    _events.push_back(event);
                    ++_counter;
                }
                break;
            case MatchLogKind::End:
                _finished = true;
                ++_counter;
                break;
            case MatchLogKind::Hand:
                break;
        }
    }

}  // namespace BECore::Mahjong
