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

    MahjongPrefsSave MakePrefs(eastl::string_view language, bool hintsOn, const SeatSetup& seats, int handsPerMatch, ComputerLevel level) {
        MahjongPrefsSave prefs;
        prefs.language.assign(language.data(), language.size());
        prefs.hintsOn = hintsOn;
        prefs.seats = SeatsToLetters(seats);
        prefs.hands = handsPerMatch;
        const std::string_view name = LevelName(level);
        prefs.level.assign(name.data(), name.size());
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
        const std::optional<ComputerLevel> level = TryParseLevel(std::string_view(prefs.level.c_str(), prefs.level.size()));
        if (!level) {
            return std::unexpected(Format("the level '{}' is not {}", prefs.level, LevelNamesText()));
        }
        values.level = *level;
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
