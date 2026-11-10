#include "HandTestSupport.h"

#include <BECore/Control/Json.h>
#include <Common/EngineEnvironment.h>
#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MatchLogJson.h>
#include <gtest/gtest.h>
#include <set>

/**
 * The JSON form of the match log (`MatchLogJson.h`): every event converts to an object and back, the actions are the control API's, and a
 * malformed line is an error that names the member and the line. The events come from matches played by four computers on a
 * `TableSession` with an in-memory `MatchLog`.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr uint32_t kSeed = 41;
        constexpr int kSeedsToPlay = 3;
        constexpr int kHandLimitPerMatch = 200;
        constexpr size_t kEastTiles = 14;
        constexpr size_t kOtherTiles = 13;
        constexpr int kBadSeat = 7;
        constexpr const char* kBadTile = "9z";

        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

        class MatchLogJsonTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }
        };

        /// The log of a match of four computers (8 hands) on `seed`.
        eastl::vector<MatchLogEvent> PlayedLog(uint32_t seed) {
            Match match;
            TableSession session(match);
            MatchLog log;
            session.SetLogSink(&log);
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            EXPECT_TRUE(session.StartMatch(settings, seed, kAllComputers).has_value());
            for (int hands = 0; session.GetPhase() != SessionPhase::MatchOver && hands < kHandLimitPerMatch; ++hands) {
                session.RunComputers();
                if (session.GetPhase() == SessionPhase::HandOver) {
                    EXPECT_TRUE(session.NextHand().has_value());
                }
            }
            return log.Events();
        }

        /// The first event of `kind`.
        const MatchLogEvent& First(const eastl::vector<MatchLogEvent>& events, MatchLogKind kind) {
            for (const MatchLogEvent& event : events) {
                if (event.kind == kind) {
                    return event;
                }
            }
            ADD_FAILURE() << "no event of kind " << Name(kind);
            return events.front();
        }

        Json Member(const Json& object, const char* key) {
            const Json* found = FindJsonMember(object, key);
            EXPECT_NE(found, nullptr) << "missing member '" << key << "'";
            return found ? *found : Json();
        }

        /// The error text of an event read from `json` ("" when it is accepted).
        eastl::string ErrorOf(const Json& json) {
            const std::expected<MatchLogEvent, eastl::string> event = EventFromJson(json);
            return event ? "" : event.error();
        }

        /// `json` with the member `key` replaced by `value`.
        Json With(Json json, const char* key, Json value) {
            json[key] = std::move(value);
            return json;
        }

        /// Whether `event` with the member `key` changed to `value` is refused with an error that names `key`.
        ::testing::AssertionResult ChangedMemberIsRefused(const MatchLogEvent& event, const char* key, Json value) {
            const eastl::string error = ErrorOf(With(EventToJson(event), key, std::move(value)));
            if (error.empty()) {
                return ::testing::AssertionFailure() << "'" << key << "' changed and still accepted";
            }
            if (error.find(key) == eastl::string::npos) {
                return ::testing::AssertionFailure() << "the error does not name '" << key << "': " << error.c_str();
            }
            return ::testing::AssertionSuccess();
        }

        std::string TextOf(const eastl::vector<MatchLogEvent>& events) {
            std::string text;
            for (const MatchLogEvent& event : events) {
                text += EventToJson(event).dump();
                text += '\n';
            }
            return text;
        }

    }  // namespace

    TEST_F(MatchLogJsonTest, EveryEventOfAPlayedMatchSurvivesTheRoundTrip) {
        std::set<MatchLogKind> kinds;
        for (uint32_t seed = kSeed; seed < kSeed + kSeedsToPlay; ++seed) {
            const eastl::vector<MatchLogEvent> events = PlayedLog(seed);
            ASSERT_GT(events.size(), 3u);
            for (size_t index = 0; index < events.size(); ++index) {
                const MatchLogEvent& event = events[index];
                kinds.insert(event.kind);
                const std::expected<MatchLogEvent, eastl::string> back = EventFromJson(EventToJson(event));
                ASSERT_TRUE(back.has_value()) << "seed " << seed << " event " << index << ": " << back.error().c_str();
                EXPECT_EQ(*back, event) << "seed " << seed << " event " << index;

                // Through the text of a line too.
                const auto parsed = ParseJson(DumpJson(EventToJson(event)));
                ASSERT_TRUE(parsed.has_value());
                const std::expected<MatchLogEvent, eastl::string> viaText = EventFromJson(*parsed);
                ASSERT_TRUE(viaText.has_value()) << viaText.error().c_str();
                EXPECT_EQ(*viaText, event);
            }
        }
        EXPECT_EQ(kinds.size(), 5u) << "all five kinds of event were seen";
    }

    TEST_F(MatchLogJsonTest, ThePersonAndTheComputerBothRoundTrip) {
        MatchLogEvent event;
        event.kind = MatchLogKind::Action;
        event.seat = 2;
        event.action = HandAction::MakeDiscard(T("5p"), true);
        for (const SeatKind by : {SeatKind::Person, SeatKind::Computer}) {
            event.by = by;
            const Json json = EventToJson(event);
            EXPECT_EQ(Member(json, "by"), by == SeatKind::Person ? "person" : "computer");
            const auto back = EventFromJson(json);
            ASSERT_TRUE(back.has_value()) << back.error().c_str();
            EXPECT_EQ(*back, event);
        }
    }

    TEST_F(MatchLogJsonTest, TheActionIsTheControlApisActionJson) {
        std::set<ActionKind> seen;
        for (uint32_t seed = kSeed; seed < kSeed + kSeedsToPlay; ++seed) {
            for (const MatchLogEvent& event : PlayedLog(seed)) {
                if (event.kind != MatchLogKind::Action) {
                    continue;
                }
                seen.insert(event.action.kind);
                const Json json = EventToJson(event);
                EXPECT_EQ(Member(json, "action"), ActionToJson(event.action));
                EXPECT_EQ(Member(json, "seat"), event.seat);
            }
        }
        EXPECT_TRUE(seen.count(ActionKind::Discard) > 0);
        EXPECT_TRUE(seen.count(ActionKind::Claim) > 0);
        EXPECT_TRUE(seen.count(ActionKind::Pass) > 0) << "computers pass claim windows";
    }

    TEST_F(MatchLogJsonTest, TheStartLineHasTheDocumentedMembers) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeed);
        const Json json = EventToJson(events.front());
        EXPECT_EQ(Member(json, "event"), "start");
        EXPECT_EQ(Member(json, "version"), kMatchLogFormatVersion);
        EXPECT_EQ(Member(json, "seed"), kSeed);
        EXPECT_EQ(Member(json, "seats"), "CCCC");
        EXPECT_EQ(Member(json, "levels"), Json::array({5, 5, 5, 5}));
        const Json settings = Member(json, "settings");
        EXPECT_EQ(Member(settings, "handsPerMatch"), kShortMatchHands);
        EXPECT_EQ(Member(settings, "flowers"), false);
        EXPECT_EQ(Member(settings, "replayDrawnHand"), true);
        EXPECT_EQ(Member(settings, "limit"), kDefaultLimit);
        EXPECT_EQ(Member(settings, "startingScore"), kExpectedStartingScore);
    }

    TEST_F(MatchLogJsonTest, TheLevelsOfTheStartRoundTripForEveryLevel) {
        MatchLogEvent start = PlayedLog(kSeed).front();
        ASSERT_EQ(start.kind, MatchLogKind::Start);
        for (const ComputerLevel level : kAllLevels) {
            start.levels = UniformLevels(level);
            const Json json = EventToJson(start);
            EXPECT_EQ(Member(json, "levels"), Json::array({LevelNumber(level), LevelNumber(level), LevelNumber(level), LevelNumber(level)})) << "numbers, never names";
            EXPECT_EQ(FindJsonMember(json, "level"), nullptr) << "version 4 has no single level";
            const auto back = EventFromJson(json);
            ASSERT_TRUE(back.has_value()) << back.error().c_str();
            EXPECT_EQ(back->levels, start.levels);
            EXPECT_EQ(*back, start);
        }
        // Four different levels, each at its own seat
        start.levels = {ComputerLevel{1}, ComputerLevel{3}, ComputerLevel{5}, ComputerLevel{10}};
        const Json json = EventToJson(start);
        EXPECT_EQ(Member(json, "levels"), Json::array({1, 3, 5, 10}));
        const auto back = EventFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(back->levels, start.levels);
        EXPECT_EQ(*back, start);

        // A match played at levels logs them, also through the text of the line.
        Match match;
        TableSession session(match);
        MatchLog log;
        session.SetLogSink(&log);
        const SeatLevels mixed{ComputerLevel::Easy, ComputerLevel{2}, ComputerLevel{7}, ComputerLevel::Normal};
        ASSERT_TRUE(session.StartMatch(MahjongSettings{}, kSeed, kAllComputers, mixed).has_value());
        const auto parsed = ParseMatchLog(TextOf(log.Events()).c_str());
        ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
        EXPECT_EQ(parsed->front().levels, mixed);
    }

    TEST_F(MatchLogJsonTest, AVersionOneStartHasNoLevelAndReadsAsNormal) {
        const MatchLogEvent start = PlayedLog(kSeed).front();
        Json json = EventToJson(start);
        json["version"] = kMatchLogOldestVersion;
        json.erase("levels");
        const auto back = EventFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(back->levels, UniformLevels(ComputerLevel::Normal));
        EXPECT_EQ(*back, start);

        // A version 1 line does not look at a `level` it should not have.
        json["level"] = "hard";
        const auto ignored = EventFromJson(json);
        ASSERT_TRUE(ignored.has_value()) << ignored.error().c_str();
        EXPECT_EQ(ignored->levels, UniformLevels(ComputerLevel::Normal));
    }

    TEST_F(MatchLogJsonTest, AVersionFourStartNeedsFourLevelsOneToTen) {
        const MatchLogEvent start = PlayedLog(kSeed).front();
        Json without = EventToJson(start);
        without.erase("levels");
        const eastl::string missing = ErrorOf(without);
        EXPECT_NE(missing.find("levels"), eastl::string::npos) << missing.c_str();

        // A version 4 line that has only the old `level` is refused: it names `levels`
        Json onlyLevel = without;
        onlyLevel["level"] = 3;
        const eastl::string old = ErrorOf(onlyLevel);
        EXPECT_NE(old.find("levels"), eastl::string::npos) << old.c_str();

        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array({5, 5, 5})));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array({5, 5, 5, 5, 5})));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array()));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array({5, 5, 5, 0})));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array({11, 5, 5, 5})));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array({5, -1, 5, 5})));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array({5, 5, "5", 5})));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array({5, 5, "normal", 5})));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array({5, 2.5, 5, 5})));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", Json::array({5, nullptr, 5, 5})));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", 5));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", "5,5,5,5"));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "levels", nullptr));
        // The versions around the known ones are refused.
        EXPECT_TRUE(ChangedMemberIsRefused(start, "version", kMatchLogFormatVersion + 1));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "version", kMatchLogOldestVersion - 1));
        EXPECT_EQ(kMatchLogFormatVersion, kMatchLogSeatLevelsVersion);
    }

    TEST_F(MatchLogJsonTest, AVersionThreeStartKeepsItsOneLevelAsFourEqualOnes) {
        // A line as the game wrote it before the level of each seat
        const std::string line =
            R"({"event":"start","version":3,"seed":41,"settings":{"handsPerMatch":4,"flowers":false,"replayDrawnHand":true,"limit":500,"startingScore":2000},"seats":"PCCC","level":3})";
        const auto json = ParseJson(eastl::string_view(line.data(), line.size()));
        ASSERT_TRUE(json.has_value());
        const auto back = EventFromJson(*json);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(back->levels, UniformLevels(ComputerLevel{3}));
        EXPECT_EQ(back->seed, 41u);
        EXPECT_EQ(back->seats, (SeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer}));

        // Version 3 needs its number 1..10; `levels` is not looked at
        for (const Json& bad : {Json(0), Json(11), Json(-1), Json("5"), Json("normal"), Json(2.5), Json(nullptr)}) {
            const eastl::string error = ErrorOf(With(*json, "level", bad));
            EXPECT_NE(error.find("level"), eastl::string::npos) << bad.dump();
        }
        Json without = *json;
        without.erase("level");
        EXPECT_NE(ErrorOf(without).find("level"), eastl::string::npos);
    }

    TEST_F(MatchLogJsonTest, AVersionTwoStartNeedsALevelOfTheTwoNamesAndReadsThemAsOneAndFive) {
        const MatchLogEvent start = PlayedLog(kSeed).front();
        Json versionTwo = EventToJson(start);
        versionTwo.erase("levels");
        versionTwo["version"] = kMatchLogLevelVersion;
        for (const auto& [name, level] : {std::pair{"easy", ComputerLevel::Easy}, std::pair{"normal", ComputerLevel::Normal}}) {
            const auto back = EventFromJson(With(versionTwo, "level", name));
            ASSERT_TRUE(back.has_value()) << back.error().c_str();
            EXPECT_EQ(back->levels, UniformLevels(level)) << name;
        }
        Json without = versionTwo;
        without.erase("level");
        const eastl::string missing = ErrorOf(without);
        EXPECT_NE(missing.find("level"), eastl::string::npos) << missing.c_str();
        for (const char* bad : {"Easy", "Hard", "easy ", "expert", "", "1", "5"}) {
            EXPECT_FALSE(EventFromJson(With(versionTwo, "level", bad)).has_value()) << "'" << bad << "'";
        }
        EXPECT_FALSE(EventFromJson(With(versionTwo, "level", 1)).has_value()) << "version 2 wrote names, not numbers";
        // An unknown name is answered with the two names
        const eastl::string unknown = ErrorOf(With(versionTwo, "level", "expert"));
        EXPECT_NE(unknown.find("easy or normal"), eastl::string::npos) << unknown.c_str();
        EXPECT_EQ(unknown.find("hard"), eastl::string::npos) << unknown.c_str();
    }

    TEST_F(MatchLogJsonTest, AVersionTwoStartThatNamesHardReadsAsNormal) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeed);
        const MatchLogEvent& start = events.front();
        ASSERT_EQ(start.kind, MatchLogKind::Start);
        ASSERT_EQ(start.levels, UniformLevels(ComputerLevel::Normal));
        Json hard = With(EventToJson(start), "level", std::string(kMatchLogRetiredLevelName));
        hard.erase("levels");
        hard["version"] = kMatchLogLevelVersion;
        const auto back = EventFromJson(hard);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(back->levels, UniformLevels(ComputerLevel::Normal));
        EXPECT_EQ(*back, start);

        // The whole file, with its first line written as an old game wrote it, reads back as the same log and replays.
        std::string text = hard.dump();
        text += '\n';
        for (size_t index = 1; index < events.size(); ++index) {
            text += EventToJson(events[index]).dump();
            text += '\n';
        }
        const auto parsed = ParseMatchLog(text);
        ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
        EXPECT_EQ(*parsed, events);
        Match replayed;
        const ReplayStatus status = ReplayMatchLog(*parsed, replayed);
        ASSERT_TRUE(status.has_value()) << "stopped at event " << status.error().index;
        EXPECT_EQ(replayed.GetPhase(), MatchPhase::Finished);

        // Only that one name is read as Normal.
        for (const char* bad : {"HARD", "hard "}) {
            EXPECT_FALSE(EventFromJson(With(hard, "level", bad)).has_value()) << "'" << bad << "'";
        }
    }

    TEST_F(MatchLogJsonTest, TheHandLineListsFourteenTilesForEastAndThirteenForTheOthers) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeed);
        const MatchLogEvent& hand = First(events, MatchLogKind::Hand);
        const Json json = EventToJson(hand);
        EXPECT_EQ(Member(json, "event"), "hand");
        EXPECT_EQ(Member(json, "number"), 0);
        EXPECT_EQ(Member(json, "east"), hand.east);
        EXPECT_EQ(Member(json, "roundWind"), "East");
        EXPECT_EQ(Member(json, "winds").size(), 4u);
        const Json deal = Member(json, "deal");
        ASSERT_EQ(deal.size(), 4u);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const Json concealed = Member(deal[static_cast<size_t>(seat)], "concealed");
            EXPECT_EQ(concealed.size(), seat == hand.east ? kEastTiles : kOtherTiles) << "seat " << seat;
            EXPECT_TRUE(Member(deal[static_cast<size_t>(seat)], "bonus").is_array());
        }
    }

    TEST_F(MatchLogJsonTest, TheSettlementLineHoldsTheBreakdownsAndTheWinnersLines) {
        bool won = false;
        for (uint32_t seed = kSeed; seed < kSeed + kSeedsToPlay; ++seed) {
            for (const MatchLogEvent& event : PlayedLog(seed)) {
                if (event.kind != MatchLogKind::Settlement) {
                    continue;
                }
                const Json json = EventToJson(event);
                EXPECT_EQ(Member(json, "event"), "settlement");
                EXPECT_EQ(Member(json, "scores"), SeatScoresToJson(event.settlement.scores));
                EXPECT_EQ(Member(json, "deltas"), SeatScoresToJson(event.settlement.deltas));
                const Json seats = Member(json, "seats");
                ASSERT_EQ(seats.size(), 4u);
                for (size_t seat = 0; seat < seats.size(); ++seat) {
                    EXPECT_EQ(Member(seats[seat], "breakdown"), BreakdownToJson(event.settlement.breakdowns[seat]));
                }
                if (event.settlement.kind == ResultKind::Win) {
                    won = true;
                    EXPECT_EQ(Member(json, "kind"), "win");
                    EXPECT_EQ(Member(json, "winner"), event.settlement.winner);
                    const Json lines = Member(Member(seats[static_cast<size_t>(event.settlement.winner)], "breakdown"), "lines");
                    EXPECT_FALSE(lines.empty()) << "the winner's lines are in the log";
                } else {
                    EXPECT_EQ(Member(json, "kind"), "drawn");
                    EXPECT_TRUE(Member(json, "winner").is_null());
                }
            }
        }
        EXPECT_TRUE(won);
    }

    TEST_F(MatchLogJsonTest, ADrawnSettlementRoundTripsWithNoWinner) {
        MatchLogEvent event;
        event.kind = MatchLogKind::Settlement;
        event.settlement.kind = ResultKind::Drawn;
        event.settlement.winner = kNoSeat;
        event.settlement.east = 3;
        event.settlement.roundWind = Wind::South;
        event.settlement.scores = {10, 20, 30, 40};
        const auto back = EventFromJson(EventToJson(event));
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(*back, event);
        EXPECT_EQ(back->settlement.winner, kNoSeat);
    }

    TEST_F(MatchLogJsonTest, AWholeFileIsReadBackAndReplays) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeed);
        std::string text = TextOf(events);
        // Blank lines and a CRLF ending are skipped / tolerated.
        const size_t firstBreak = text.find('\n');
        text.insert(firstBreak + 1, "\n   \n");
        text.insert(firstBreak, "\r");
        const auto parsed = ParseMatchLog(text);
        ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
        EXPECT_EQ(*parsed, events);

        Match replayed;
        const ReplayStatus status = ReplayMatchLog(*parsed, replayed);
        ASSERT_TRUE(status.has_value()) << "stopped at event " << status.error().index;
        EXPECT_EQ(replayed.GetPhase(), MatchPhase::Finished);

        const auto none = ParseMatchLog("");
        ASSERT_TRUE(none.has_value());
        EXPECT_TRUE(none->empty());
    }

    TEST_F(MatchLogJsonTest, AMalformedLineIsAnErrorWithItsLineNumber) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeed);
        const std::string valid = EventToJson(events.front()).dump();
        struct Case {
            std::string line;
            const char* expected;  // a part of the error text
        };
        const std::vector<Case> cases = {
            {"[1, 2]", "JSON object"},        {"42", "JSON object"},
            {"{\"seed\": 1}", "event"},       {"{\"event\": \"nonsense\"}", "event"},
            {"{\"event\": 5}", "event"},      {"{\"event\": \"start\", ", "not JSON"},
            {"this is not JSON", "not JSON"}, {With(EventToJson(First(events, MatchLogKind::Action)), "seat", kBadSeat).dump(), "seat"},
        };
        for (const Case& test : cases) {
            const std::string text = valid + "\n\n" + test.line + "\n" + valid + "\n";
            const auto parsed = ParseMatchLog(text);
            ASSERT_FALSE(parsed.has_value()) << test.line;
            EXPECT_EQ(parsed.error().rfind("line 3: ", 0), 0u) << parsed.error().c_str();
            EXPECT_NE(parsed.error().find(test.expected), eastl::string::npos) << test.line << " -> " << parsed.error().c_str();
        }
        // The first and a late line are named too.
        EXPECT_EQ(ParseMatchLog("{").error().rfind("line 1: ", 0), 0u);
        EXPECT_EQ(ParseMatchLog(valid + "\n" + valid + "\n[]").error().rfind("line 3: ", 0), 0u);
    }

    TEST_F(MatchLogJsonTest, EveryWrongMemberIsRefusedAndNamed) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeed);
        const MatchLogEvent& start = events.front();
        EXPECT_TRUE(ChangedMemberIsRefused(start, "seats", "PCC"));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "seats", "PCCX"));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "seats", 4));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "seed", "7"));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "seed", -1));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "settings", Json::object()));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "version", kMatchLogFormatVersion + 1));
        {
            Json settings = EventToJson(start)["settings"];
            settings["limit"] = "many";
            const eastl::string error = ErrorOf(With(EventToJson(start), "settings", settings));
            EXPECT_NE(error.find("limit"), eastl::string::npos) << error.c_str();
        }

        const MatchLogEvent& hand = First(events, MatchLogKind::Hand);
        EXPECT_TRUE(ChangedMemberIsRefused(hand, "east", kBadSeat));
        EXPECT_TRUE(ChangedMemberIsRefused(hand, "roundWind", "Middle"));
        EXPECT_TRUE(ChangedMemberIsRefused(hand, "winds", Json::array()));
        EXPECT_TRUE(ChangedMemberIsRefused(hand, "winds", Json::array({"East", "South", "West", "Up"})));
        EXPECT_TRUE(ChangedMemberIsRefused(hand, "deal", Json::array()));
        {
            Json json = EventToJson(hand);
            json["deal"][1]["concealed"][0] = kBadTile;
            const eastl::string error = ErrorOf(json);
            EXPECT_NE(error.find("concealed"), eastl::string::npos) << error.c_str();
            EXPECT_NE(error.find(kBadTile), eastl::string::npos) << error.c_str();
            json["deal"][1]["concealed"][0] = 5;
            EXPECT_NE(ErrorOf(json).find("concealed"), eastl::string::npos);
        }

        const MatchLogEvent& action = First(events, MatchLogKind::Action);
        EXPECT_TRUE(ChangedMemberIsRefused(action, "seat", kBadSeat));
        EXPECT_TRUE(ChangedMemberIsRefused(action, "seat", "1"));
        EXPECT_TRUE(ChangedMemberIsRefused(action, "by", "robot"));
        EXPECT_TRUE(ChangedMemberIsRefused(action, "action", "discard"));
        EXPECT_TRUE(ChangedMemberIsRefused(action, "action", Json::object()));
        EXPECT_TRUE(ChangedMemberIsRefused(action, "action", NextHandActionToJson()));
        {
            Json json = EventToJson(action);
            json["action"] = ActionToJson(HandAction::MakeDiscard(T("5p")));
            json["action"]["tile"] = kBadTile;
            EXPECT_NE(ErrorOf(json).find("action"), eastl::string::npos);
        }

        const MatchLogEvent& settlement = First(events, MatchLogKind::Settlement);
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "scores", "many"));
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "scores", Json::array({1, 2, 3})));
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "deltas", Json::array({1, 2, 3, "x"})));
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "kind", "tie"));
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "winner", kBadSeat));
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "east", nullptr));
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "roundWind", 3));

        const MatchLogEvent& end = events.back();
        ASSERT_EQ(end.kind, MatchLogKind::End);
        EXPECT_TRUE(ChangedMemberIsRefused(end, "scores", "1,2,3,4"));
        EXPECT_TRUE(ChangedMemberIsRefused(end, "standings", Json::array({0, 1, 2, kBadSeat})));
        EXPECT_TRUE(ChangedMemberIsRefused(end, "standings", Json::array({0, 1})));

        // A number above INT64_MAX is refused, not wrapped into a negative one.
        constexpr uint64_t kAboveInt64 = std::numeric_limits<uint64_t>::max();
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "scores", Json::array({kAboveInt64, 0, 0, 0})));
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "deltas", Json::array({0, kAboveInt64, 0, 0})));
        EXPECT_TRUE(ChangedMemberIsRefused(end, "scores", Json::array({0, 0, kAboveInt64, 0})));
        EXPECT_TRUE(ChangedMemberIsRefused(end, "standings", Json::array({0, 1, 2, kAboveInt64})));

        // A member that is missing altogether is named too.
        for (const MatchLogEvent* event : {&start, &hand, &action, &settlement, &end}) {
            Json json = EventToJson(*event);
            for (const auto& item : json.items()) {
                if (item.key() == "event" || (event->kind == MatchLogKind::Settlement && item.key() == "seats")) {
                    continue;
                }
                Json without = json;
                without.erase(item.key());
                const eastl::string error = ErrorOf(without);
                EXPECT_FALSE(error.empty()) << "without '" << item.key() << "'";
                EXPECT_NE(error.find(item.key().c_str()), eastl::string::npos) << error.c_str();
            }
        }
    }

    TEST_F(MatchLogJsonTest, TheBreakdownsOfASettlementAreNotRequiredToReadItBack) {
        const eastl::vector<MatchLogEvent> events = PlayedLog(kSeed);
        const MatchLogEvent& settlement = First(events, MatchLogKind::Settlement);
        Json json = EventToJson(settlement);
        json.erase("seats");
        const auto back = EventFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(*back, settlement);
    }

    TEST_F(MatchLogJsonTest, TheSeatLettersRoundTrip) {
        for (int bits = 0; bits < 16; ++bits) {
            SeatSetup seats = kAllComputers;
            for (size_t seat = 0; seat < kSeatCount; ++seat) {
                seats[seat] = ((bits >> seat) & 1) != 0 ? SeatKind::Person : SeatKind::Computer;
            }
            const eastl::string letters = SeatsToLetters(seats);
            EXPECT_EQ(letters.size(), 4u);
            const std::optional<SeatSetup> back = ReadSeats(letters);
            ASSERT_TRUE(back.has_value());
            EXPECT_EQ(*back, seats);
        }
        EXPECT_FALSE(ReadSeats("PCC").has_value());
        EXPECT_FALSE(ReadSeats("PCCZ").has_value());
        EXPECT_EQ(SeatsToLetters(SeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer}), "PCCC");
    }

}  // namespace BECore::Mahjong::Tests
