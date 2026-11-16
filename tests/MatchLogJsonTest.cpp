#include <BECore/Control/Json.h>
#include <Common/EngineEnvironment.h>
#include <Mahjong/Game/MatchLog.h>
#include <Mahjong/MahjongGameControl.h>
#include <Mahjong/MatchLogJson.h>
#include <gtest/gtest.h>
#include <set>
#include <tests/FixedWall.h>
#include <tests/HandTestSupport.h>

/** The JSON form of the match log (`MatchLogJson.h`); the events come from matches played by four computers on a `TableSession` with an in-memory `MatchLog`. */
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
        Vector<MatchLogEvent> PlayedLog(uint32_t seed) {
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
        const MatchLogEvent& First(const Vector<MatchLogEvent>& events, MatchLogKind kind) {
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
        String ErrorOf(const Json& json) {
            const std::expected<MatchLogEvent, String> event = EventFromJson(json);
            return event ? "" : event.error();
        }

        /// `json` with the member `key` replaced by `value`.
        Json With(Json json, const char* key, Json value) {
            json[key] = std::move(value);
            return json;
        }

        /// Whether `event` with the member `key` changed to `value` is refused with an error that names `key`.
        ::testing::AssertionResult ChangedMemberIsRefused(const MatchLogEvent& event, const char* key, Json value) {
            const String error = ErrorOf(With(EventToJson(event), key, std::move(value)));
            if (error.empty()) {
                return ::testing::AssertionFailure() << "'" << key << "' changed and still accepted";
            }
            if (error.find(key) == String::npos) {
                return ::testing::AssertionFailure() << "the error does not name '" << key << "': " << error.c_str();
            }
            return ::testing::AssertionSuccess();
        }

        String TextOf(const Vector<MatchLogEvent>& events) {
            String text;
            for (const MatchLogEvent& event : events) {
                AppendFormat(text, "{}\n", EventToJson(event).dump());
            }
            return text;
        }

    }  // namespace

    TEST_F(MatchLogJsonTest, EveryEventOfAPlayedMatchSurvivesTheRoundTrip) {
        Set<MatchLogKind> kinds;
        for (uint32_t seed = kSeed; seed < kSeed + kSeedsToPlay; ++seed) {
            const Vector<MatchLogEvent> events = PlayedLog(seed);
            ASSERT_GT(events.size(), 3u);
            for (size_t index = 0; index < events.size(); ++index) {
                const MatchLogEvent& event = events[index];
                kinds.insert(event.kind);
                const std::expected<MatchLogEvent, String> back = EventFromJson(EventToJson(event));
                ASSERT_TRUE(back.has_value()) << "seed " << seed << " event " << index << ": " << back.error().c_str();
                EXPECT_EQ(*back, event) << "seed " << seed << " event " << index;

                const auto parsed = ParseJson(DumpJson(EventToJson(event)));
                ASSERT_TRUE(parsed.has_value());
                const std::expected<MatchLogEvent, String> viaText = EventFromJson(*parsed);
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
        Set<ActionKind> seen;
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
        const Vector<MatchLogEvent> events = PlayedLog(kSeed);
        const Json json = EventToJson(events.front());
        EXPECT_EQ(Member(json, "event"), "start");
        EXPECT_EQ(Member(json, "version"), kMatchLogFormatVersion);
        EXPECT_EQ(Member(json, "seed"), kSeed);
        EXPECT_EQ(Member(json, "seats"), "CCCC");
        EXPECT_EQ(Member(json, "levels"), Json::array({5, 5, 5, 5}));
        EXPECT_EQ(Member(json, "platform"), "unknown") << "a session that was told no origin says so";
        EXPECT_EQ(Member(json, "build"), "");
        EXPECT_EQ(Member(json, "gameVersion"), "");
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

    TEST_F(MatchLogJsonTest, AStartNeedsFourLevelsOneToTen) {
        const MatchLogEvent start = PlayedLog(kSeed).front();
        Json without = EventToJson(start);
        without.erase("levels");
        const String missing = ErrorOf(without);
        EXPECT_NE(missing.find("levels"), String::npos) << missing.c_str();

        // A version 4 line that has only the old `level` is refused: it names `levels`
        Json onlyLevel = without;
        onlyLevel["level"] = 3;
        const String old = ErrorOf(onlyLevel);
        EXPECT_NE(old.find("levels"), String::npos) << old.c_str();

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
        EXPECT_TRUE(ChangedMemberIsRefused(start, "version", kMatchLogFormatVersion + 1));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "version", kMatchLogOldestVersion - 1));
        EXPECT_GT(kMatchLogFormatVersion, kMatchLogSeatLevelsVersion);
    }

    TEST_F(MatchLogJsonTest, AVersionFiveStartHoldsEverythingTheTestersLogsNeed) {
        // A match of a person and three computers at known levels from a known origin: the start line says seed, settings, who plays
        // what, the level of each seat and where the match was played; `by` of each action tells the person from the computer.
        Match match;
        TableSession session(match);
        MatchLog log;
        session.SetLogSink(&log);
        session.SetLogOrigin({MatchPlatform::Web, "a1b2c3d", "0.1.0"});
        const SeatLevels levels{ComputerLevel::Normal, ComputerLevel{2}, ComputerLevel{7}, ComputerLevel::Easy};
        ASSERT_TRUE(session.StartMatch(MahjongSettings{}, kSeed, kDefaultSeatSetup, levels).has_value());
        const Json json = EventToJson(log.Events().front());
        for (const char* key : {"event", "version", "seed", "settings", "seats", "levels", "platform", "build", "gameVersion"}) {
            EXPECT_NE(FindJsonMember(json, key), nullptr) << key;
        }
        EXPECT_EQ(Member(json, "version"), kMatchLogFormatVersion);
        EXPECT_EQ(FindJsonMember(json, "nickname"), nullptr) << "no nickname typed, no member";
        EXPECT_EQ(Member(json, "seats"), "PCCC");
        EXPECT_EQ(Member(json, "levels"), Json::array({5, 2, 7, 1}));
        EXPECT_EQ(Member(json, "platform"), "web");
        EXPECT_EQ(Member(json, "build"), "a1b2c3d");
        EXPECT_EQ(Member(json, "gameVersion"), "0.1.0");
    }

    TEST_F(MatchLogJsonTest, ActionsSayWhetherAPersonOrAComputerActed) {
        Match match;
        TableSession session(match);
        MatchLog log;
        session.SetLogSink(&log);
        ASSERT_TRUE(session.StartMatch(MahjongSettings{}, kSeed, kDefaultSeatSetup).has_value());
        const Seat person = session.Actor();
        ASSERT_TRUE(session.ActorIsPerson()) << "seat 0, a person, acts first";
        const auto legal = match.Hand().LegalActions(person);
        ASSERT_FALSE(legal.empty());
        ASSERT_TRUE(session.PerformPerson(legal.front()).has_value());
        session.RunComputers();
        Set<String> seen;
        for (const MatchLogEvent& event : log.Events()) {
            if (event.kind == MatchLogKind::Action) {
                const Json json = EventToJson(event);
                seen.insert(FromStdString(Member(json, "by").get<std::string>()));  //-NoStd: nlohmann reads a string as std::string
                EXPECT_EQ(Member(json, "by"), event.seat == person ? "person" : "computer");
            }
        }
        EXPECT_EQ(seen, (Set<String>{"person", "computer"}));
    }

    TEST_F(MatchLogJsonTest, TheOriginRoundTripsForEveryPlatformAndEmptyStrings) {
        MatchLogEvent start = PlayedLog(kSeed).front();
        for (const MatchPlatform platform : {MatchPlatform::Unknown, MatchPlatform::Desktop, MatchPlatform::Web}) {
            for (const MatchLogOrigin& origin : {MatchLogOrigin{platform, "", ""}, MatchLogOrigin{platform, "9f8e7d6", "0.1.0"}}) {
                start.origin = origin;
                const Json json = EventToJson(start);
                const auto back = EventFromJson(json);
                ASSERT_TRUE(back.has_value()) << back.error().c_str();
                EXPECT_EQ(back->origin, origin);
                EXPECT_EQ(*back, start);
                const auto parsed = ParseMatchLog(TextOf({start}).c_str());
                ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
                EXPECT_EQ(parsed->front().origin, origin);
            }
        }
        MatchLogEvent other = start;
        other.origin.build = "different";
        EXPECT_FALSE(other == start);
        other = start;
        other.origin.platform = MatchPlatform::Desktop;
        EXPECT_FALSE(other == start);
        other = start;
        other.origin.gameVersion = "9.9.9";
        EXPECT_FALSE(other == start);
    }

    TEST_F(MatchLogJsonTest, AVersionFourStartReadsWithAnUnknownOrigin) {
        MatchLogEvent start = PlayedLog(kSeed).front();
        start.origin = {MatchPlatform::Web, "abc", "1.0"};
        Json json = EventToJson(start);
        json["version"] = 4;
        json.erase("platform");
        json.erase("build");
        json.erase("gameVersion");
        const auto back = EventFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(back->origin, MatchLogOrigin{});
        EXPECT_EQ(back->levels, start.levels);

        // A version 4 line does not look at origin members it should not have.
        json["platform"] = "mainframe";
        const auto ignored = EventFromJson(json);
        ASSERT_TRUE(ignored.has_value()) << ignored.error().c_str();
        EXPECT_EQ(ignored->origin, MatchLogOrigin{});
    }

    TEST_F(MatchLogJsonTest, AVersionFiveStartNeedsItsThreeOriginMembers) {
        MatchLogEvent start = PlayedLog(kSeed).front();
        start.origin = {MatchPlatform::Desktop, "abc", "1.0"};
        for (const char* key : {"platform", "build", "gameVersion"}) {
            Json without = EventToJson(start);
            without.erase(key);
            const String missing = ErrorOf(without);
            EXPECT_NE(missing.find(key), String::npos) << key << ": " << missing.c_str();
            EXPECT_TRUE(ChangedMemberIsRefused(start, key, 5));
            EXPECT_TRUE(ChangedMemberIsRefused(start, key, nullptr));
            EXPECT_TRUE(ChangedMemberIsRefused(start, key, Json::array()));
        }
        EXPECT_TRUE(ChangedMemberIsRefused(start, "platform", "mainframe"));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "platform", "Desktop")) << "the names are lower case";
        EXPECT_TRUE(ChangedMemberIsRefused(start, "platform", ""));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "version", 0));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "version", kMatchLogFormatVersion + 1));
        EXPECT_EQ(kMatchLogFormatVersion, 7);
        EXPECT_EQ(kMatchLogNicknameVersion, 6);
        EXPECT_EQ(kMatchLogStateVersion, 7);
    }

    TEST_F(MatchLogJsonTest, TheNicknameRoundTripsAndIsWrittenOnlyWhenThereIsOne) {
        MatchLogEvent start = PlayedLog(kSeed).front();
        EXPECT_EQ(FindJsonMember(EventToJson(start), "nickname"), nullptr);
        for (const char* nickname : {"Anna", "Anna Lee 7", "\xD0\x90\xD0\xBD\xD0\xBD\xD0\xB0 \xD0\x9F"}) {
            start.origin.nickname = nickname;
            const Json json = EventToJson(start);
            EXPECT_EQ(Member(json, "nickname"), nickname);
            const auto back = EventFromJson(json);
            ASSERT_TRUE(back.has_value()) << back.error().c_str();
            EXPECT_EQ(back->origin.nickname, nickname);
            EXPECT_EQ(*back, start);
            const auto parsed = ParseMatchLog(TextOf({start}).c_str());
            ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
            EXPECT_EQ(parsed->front().origin.nickname, nickname);
        }
        MatchLogEvent other = start;
        other.origin.nickname = "Boris";
        EXPECT_FALSE(other == start);
        other.origin.nickname.clear();
        EXPECT_FALSE(other == start);
        EXPECT_TRUE(EventFromJson(EventToJson(other)).has_value());
        EXPECT_TRUE(*EventFromJson(EventToJson(other)) == other);
    }

    TEST_F(MatchLogJsonTest, TheNicknameIsCheckedAndAnErrorNamesIt) {
        MatchLogEvent start = PlayedLog(kSeed).front();
        start.origin.nickname = "Anna";
        const std::string longest(kMaxMatchLogNicknameBytes, 'a');  //-NoStd: the Json input is a std::string
        EXPECT_EQ(kMaxMatchLogNicknameBytes, 24u);
        EXPECT_TRUE(EventFromJson(With(EventToJson(start), "nickname", longest)).has_value());
        EXPECT_TRUE(ChangedMemberIsRefused(start, "nickname", longest + "a")) << "25 bytes";
        EXPECT_TRUE(ChangedMemberIsRefused(start, "nickname", 5));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "nickname", nullptr));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "nickname", Json::array()));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "nickname", "An\nna"));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "nickname", std::string("An\0na", 5)));  //-NoStd: the Json input, with an embedded NUL
        EXPECT_TRUE(ChangedMemberIsRefused(start, "nickname", "An\x7F"));
        EXPECT_TRUE(ChangedMemberIsRefused(start, "nickname", "\t"));
    }

    TEST_F(MatchLogJsonTest, ABeforeVersionSixStartReadsWithNoNicknameAndIgnoresOne) {
        MatchLogEvent start = PlayedLog(kSeed).front();
        start.origin = {MatchPlatform::Web, "abc", "1.0", ""};
        for (const int version : {5, 4}) {
            Json json = EventToJson(start);
            json["version"] = version;
            if (version == 4) {
                json.erase("platform");
                json.erase("build");
                json.erase("gameVersion");
            }
            const auto plain = EventFromJson(json);
            ASSERT_TRUE(plain.has_value()) << plain.error().c_str();
            EXPECT_TRUE(plain->origin.nickname.empty());
            // A nickname the version has no place for is not looked at, even one a version 6 reader would refuse.
            for (const Json& stray : {Json("Anna"), Json(5), Json(std::string(kMaxMatchLogNicknameBytes + 1, 'x'))}) {  //-NoStd: the Json input
                const auto ignored = EventFromJson(With(json, "nickname", stray));
                ASSERT_TRUE(ignored.has_value()) << "version " << version << ": " << ignored.error().c_str();
                EXPECT_TRUE(ignored->origin.nickname.empty());
                EXPECT_EQ(ignored->origin, plain->origin);
            }
        }
    }

    TEST_F(MatchLogJsonTest, AWholeLogWithANicknameInItsStartParsesAndReplays) {
        Vector<MatchLogEvent> events = PlayedLog(kSeed);
        events.front().origin = {MatchPlatform::Web, "a1b2c3d", "0.1.0", "Anna"};
        const auto parsed = ParseMatchLog(TextOf(events).c_str());
        ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
        EXPECT_EQ(*parsed, events);
        EXPECT_EQ(parsed->front().origin.nickname, "Anna");
        Match replayed;
        const ReplayStatus status = ReplayMatchLog(*parsed, replayed);
        ASSERT_TRUE(status.has_value()) << "stopped at event " << status.error().index;
        EXPECT_EQ(replayed.GetPhase(), MatchPhase::Finished);
    }

    TEST_F(MatchLogJsonTest, AVersionThreeStartKeepsItsOneLevelAsFourEqualOnes) {
        // A line of version 3, which has one `level` for all seats and no `levels`
        const String line =
            R"({"event":"start","version":3,"seed":41,"settings":{"handsPerMatch":4,"flowers":false,"replayDrawnHand":true,"limit":500,"startingScore":2000},"seats":"PCCC","level":3})";
        const auto json = ParseJson(line);
        ASSERT_TRUE(json.has_value());
        const auto back = EventFromJson(*json);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(back->levels, UniformLevels(ComputerLevel{3}));
        EXPECT_EQ(back->seed, 41u);
        EXPECT_EQ(back->seats, (SeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer}));

        // Version 3 needs its number 1..10; `levels` is not looked at
        for (const Json& bad : {Json(0), Json(11), Json(-1), Json("5"), Json("normal"), Json(2.5), Json(nullptr)}) {
            const String error = ErrorOf(With(*json, "level", bad));
            EXPECT_NE(error.find("level"), String::npos) << bad.dump();
        }
        Json without = *json;
        without.erase("level");
        EXPECT_NE(ErrorOf(without).find("level"), String::npos);
    }

    TEST_F(MatchLogJsonTest, AVersionTwoStartNeedsALevelOfTheTwoNamesAndReadsThemAsOneAndFive) {
        const MatchLogEvent start = PlayedLog(kSeed).front();
        Json versionTwo = EventToJson(start);
        versionTwo.erase("levels");
        versionTwo["version"] = kMatchLogLevelVersion;
        for (const auto& [name, level] : {Pair<const char*, ComputerLevel>{"easy", ComputerLevel::Easy}, Pair<const char*, ComputerLevel>{"normal", ComputerLevel::Normal}}) {
            const auto back = EventFromJson(With(versionTwo, "level", name));
            ASSERT_TRUE(back.has_value()) << back.error().c_str();
            EXPECT_EQ(back->levels, UniformLevels(level)) << name;
        }
        Json without = versionTwo;
        without.erase("level");
        const String missing = ErrorOf(without);
        EXPECT_NE(missing.find("level"), String::npos) << missing.c_str();
        for (const char* bad : {"Easy", "Hard", "easy ", "expert", "", "1", "5"}) {
            EXPECT_FALSE(EventFromJson(With(versionTwo, "level", bad)).has_value()) << "'" << bad << "'";
        }
        EXPECT_FALSE(EventFromJson(With(versionTwo, "level", 1)).has_value()) << "version 2 wrote names, not numbers";
        // An unknown name is answered with the two names
        const String unknown = ErrorOf(With(versionTwo, "level", "expert"));
        EXPECT_NE(unknown.find("easy or normal"), String::npos) << unknown.c_str();
        EXPECT_EQ(unknown.find("hard"), String::npos) << unknown.c_str();
    }

    TEST_F(MatchLogJsonTest, AVersionTwoStartThatNamesHardReadsAsNormal) {
        const Vector<MatchLogEvent> events = PlayedLog(kSeed);
        const MatchLogEvent& start = events.front();
        ASSERT_EQ(start.kind, MatchLogKind::Start);
        ASSERT_EQ(start.levels, UniformLevels(ComputerLevel::Normal));
        Json hard = With(EventToJson(start), "level", ToStdString(kMatchLogRetiredLevelName));
        hard.erase("levels");
        hard["version"] = kMatchLogLevelVersion;
        const auto back = EventFromJson(hard);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(back->levels, UniformLevels(ComputerLevel::Normal));
        EXPECT_EQ(*back, start);

        // The whole file, with its first line written as an old game wrote it, reads back as the same log and replays.
        String text = FromStdString(hard.dump());
        text += '\n';
        for (size_t index = 1; index < events.size(); ++index) {
            AppendFormat(text, "{}\n", EventToJson(events[index]).dump());
        }
        const auto parsed = ParseMatchLog(text);
        ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
        EXPECT_EQ(*parsed, events);
        Match replayed;
        const ReplayStatus status = ReplayMatchLog(*parsed, replayed);
        ASSERT_TRUE(status.has_value()) << "stopped at event " << status.error().index;
        EXPECT_EQ(replayed.GetPhase(), MatchPhase::Finished);

        for (const char* bad : {"HARD", "hard "}) {
            EXPECT_FALSE(EventFromJson(With(hard, "level", bad)).has_value()) << "'" << bad << "'";
        }
    }

    TEST_F(MatchLogJsonTest, TheHandLineListsFourteenTilesForEastAndThirteenForTheOthers) {
        const Vector<MatchLogEvent> events = PlayedLog(kSeed);
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
        const Vector<MatchLogEvent> events = PlayedLog(kSeed);
        String text = TextOf(events);
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
        const Vector<MatchLogEvent> events = PlayedLog(kSeed);
        const String valid = FromStdString(EventToJson(events.front()).dump());
        struct Case {
            String line;
            const char* expected;  // a part of the error text
        };
        const Vector<Case> cases = {
            {"[1, 2]", "JSON object"},        {"42", "JSON object"},
            {"{\"seed\": 1}", "event"},       {"{\"event\": \"nonsense\"}", "event"},
            {"{\"event\": 5}", "event"},      {"{\"event\": \"start\", ", "not JSON"},
            {"this is not JSON", "not JSON"}, {FromStdString(With(EventToJson(First(events, MatchLogKind::Action)), "seat", kBadSeat).dump()), "seat"},
        };
        for (const Case& test : cases) {
            const String text = Format("{}\n\n{}\n{}\n", valid, test.line, valid);
            const auto parsed = ParseMatchLog(text);
            ASSERT_FALSE(parsed.has_value()) << test.line;
            EXPECT_EQ(parsed.error().rfind("line 3: ", 0), 0u) << parsed.error().c_str();
            EXPECT_NE(parsed.error().find(test.expected), String::npos) << test.line << " -> " << parsed.error().c_str();
        }
        EXPECT_EQ(ParseMatchLog("{").error().rfind("line 1: ", 0), 0u);
        EXPECT_EQ(ParseMatchLog(Format("{}\n{}\n[]", valid, valid)).error().rfind("line 3: ", 0), 0u);
    }

    TEST_F(MatchLogJsonTest, EveryWrongMemberIsRefusedAndNamed) {
        const Vector<MatchLogEvent> events = PlayedLog(kSeed);
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
            const String error = ErrorOf(With(EventToJson(start), "settings", settings));
            EXPECT_NE(error.find("limit"), String::npos) << error.c_str();
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
            const String error = ErrorOf(json);
            EXPECT_NE(error.find("concealed"), String::npos) << error.c_str();
            EXPECT_NE(error.find(kBadTile), String::npos) << error.c_str();
            json["deal"][1]["concealed"][0] = 5;
            EXPECT_NE(ErrorOf(json).find("concealed"), String::npos);
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
            EXPECT_NE(ErrorOf(json).find("action"), String::npos);
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

        constexpr uint64_t kAboveInt64 = std::numeric_limits<uint64_t>::max();
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "scores", Json::array({kAboveInt64, 0, 0, 0})));
        EXPECT_TRUE(ChangedMemberIsRefused(settlement, "deltas", Json::array({0, kAboveInt64, 0, 0})));
        EXPECT_TRUE(ChangedMemberIsRefused(end, "scores", Json::array({0, 0, kAboveInt64, 0})));
        EXPECT_TRUE(ChangedMemberIsRefused(end, "standings", Json::array({0, 1, 2, kAboveInt64})));

        for (const MatchLogEvent* event : {&start, &hand, &action, &settlement, &end}) {
            Json json = EventToJson(*event);
            for (const auto& item : json.items()) {
                if (item.key() == "event" || (event->kind == MatchLogKind::Settlement && item.key() == "seats")) {
                    continue;
                }
                Json without = json;
                without.erase(item.key());
                const String error = ErrorOf(without);
                EXPECT_FALSE(error.empty()) << "without '" << item.key() << "'";
                EXPECT_NE(error.find(item.key().c_str()), String::npos) << error.c_str();
            }
        }
    }

    TEST_F(MatchLogJsonTest, TheBreakdownsOfASettlementAreNotRequiredToReadItBack) {
        const Vector<MatchLogEvent> events = PlayedLog(kSeed);
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
            const String letters = SeatsToLetters(seats);
            EXPECT_EQ(letters.size(), 4u);
            const Optional<SeatSetup> back = ReadSeats(letters);
            ASSERT_TRUE(back.has_value());
            EXPECT_EQ(*back, seats);
        }
        EXPECT_FALSE(ReadSeats("PCC").has_value());
        EXPECT_FALSE(ReadSeats("PCCZ").has_value());
        EXPECT_EQ(SeatsToLetters(SeatSetup{SeatKind::Person, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer}), "PCCC");
    }

    // ----- version 7: the position a match began from -----

    namespace {

        /// The Start event of a match begun from `state` (`TableSession` logs it), the first event of its log.
        MatchLogEvent StartFrom(const FixedStart& state, bool flowers = false) {
            Match match;
            TableSession session(match);
            MatchLog log;
            session.SetLogSink(&log);
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            settings.flowers = flowers;
            EXPECT_TRUE(session.StartMatch(settings, kSeed, kDefaultSeatSetup, UniformLevels(kDefaultLevel), state).has_value());
            return log.Events().front();
        }

        /// Whether `state` with `change` made on its JSON is refused with an error that names `member`.
        template <typename Change>
        ::testing::AssertionResult BrokenStateIsRefused(const MatchLogEvent& start, const char* member, Change change) {
            Json json = EventToJson(start);
            change(json["state"]);
            const String error = ErrorOf(json);
            if (error.empty()) {
                return ::testing::AssertionFailure() << member << ": accepted";
            }
            if (error.find(member) == String::npos) {
                return ::testing::AssertionFailure() << "the error does not name '" << member << "': " << error.c_str();
            }
            return ::testing::AssertionSuccess();
        }

    }  // namespace

    TEST_F(MatchLogJsonTest, AStartFromAPositionRoundTripsWithAndWithoutLiveAfterDeal) {
        FixedStart state = MakeSamplePosition(2, 5);
        for (const Optional<int> liveAfterDeal : {Optional<int>(), Optional<int>(40)}) {
            state.deal.liveAfterDeal = liveAfterDeal;
            const MatchLogEvent start = StartFrom(state);
            ASSERT_TRUE(start.state.has_value());
            const Json json = EventToJson(start);
            EXPECT_EQ(Member(json, "version"), 7);
            const Json written = Member(json, "state");
            EXPECT_EQ(Member(written, "handsPlayed"), 5);
            EXPECT_EQ(Member(written, "scores"), Json::array({250, 120, -40, 900}));
            const Json deal = Member(written, "deal");
            EXPECT_EQ(Member(deal, "east"), 2);
            EXPECT_EQ(Member(deal, "hands")[1][4], "5m");
            EXPECT_EQ(Member(deal, "hands")[3].size(), 13u);
            EXPECT_EQ(Member(deal, "live"), Json::array({"Gd", "Gd", "9m"}));
            EXPECT_EQ(Member(deal, "loose"), Json::array({"4s", "5s"}));
            EXPECT_EQ(FindJsonMember(deal, "liveAfterDeal") != nullptr, liveAfterDeal.has_value());

            const auto back = EventFromJson(json);
            ASSERT_TRUE(back.has_value()) << back.error().c_str();
            EXPECT_EQ(*back, start);
            ASSERT_TRUE(back->state.has_value());
            EXPECT_TRUE(*back->state == state);
        }
    }

    TEST_F(MatchLogJsonTest, AStartFromAPositionWithFlowersKeepsTheBonusTiles) {
        FixedWallSpec spec;
        spec.hands[0] = "1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 4s";
        spec.hands[1] = "1m 2m 3m 4m 5m 6m 7m 8m 9m Ew Sw Ww Nw";
        spec.hands[2] = "1p 2p 3p 4p 5p 6p 7p 8p 9p Rd Wd Gd Ew";
        spec.hands[3] = "5s 6s 7s 8s 9s 1m 2m 3m 4m 5m 6m 7m 8m";
        spec.live = "F1 F2 T1";
        spec.loose = "T2";
        FixedStart state;
        state.deal = MakeFixedDeal(spec, 1);
        const MatchLogEvent start = StartFrom(state, true);
        const Json json = EventToJson(start);
        EXPECT_EQ(Member(Member(Member(json, "state"), "deal"), "live"), Json::array({"F1", "F2", "T1"}));
        const auto back = EventFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(*back, start);
    }

    TEST_F(MatchLogJsonTest, AStartWithoutAPositionIsAVersionSixLineApartFromTheVersion) {
        const MatchLogEvent start = PlayedLog(kSeed).front();
        Json json = EventToJson(start);
        EXPECT_EQ(FindJsonMember(json, "state"), nullptr);
        EXPECT_EQ(Member(json, "version"), 7);
        json["version"] = 6;
        const auto back = EventFromJson(json);
        ASSERT_TRUE(back.has_value()) << back.error().c_str();
        EXPECT_EQ(*back, start) << "version 6 and 7 read the same line the same way";
        constexpr size_t kVersionSixMembers = 9;
        for (const char* key : {"event", "version", "seed", "settings", "seats", "levels", "platform", "build", "gameVersion"}) {
            EXPECT_NE(FindJsonMember(json, key), nullptr) << key;
        }
        EXPECT_EQ(json.size(), kVersionSixMembers) << "no member besides those of version 6";
    }

    TEST_F(MatchLogJsonTest, AStateInAnOlderLineIsNotLookedAt) {
        const MatchLogEvent withState = StartFrom(MakeSamplePosition(1, 3));
        for (const int version : {6, 5, 4}) {
            Json json = EventToJson(withState);
            json["version"] = version;
            if (version == 4) {
                for (const char* member : {"platform", "build", "gameVersion"}) {
                    json.erase(member);
                }
            }
            const auto back = EventFromJson(json);
            ASSERT_TRUE(back.has_value()) << "version " << version << ": " << back.error().c_str();
            EXPECT_FALSE(back->state.has_value()) << "version " << version;
            // Even a state no reader of version 7 would take is not looked at.
            json["state"] = "garbage";
            ASSERT_TRUE(EventFromJson(json).has_value()) << "version " << version;
        }
        Json current = EventToJson(withState);
        current["state"] = "garbage";
        EXPECT_NE(ErrorOf(current).find("state"), String::npos);
    }

    TEST_F(MatchLogJsonTest, AMalformedStateIsRefusedAndNamed) {
        const MatchLogEvent start = StartFrom(MakeSamplePosition(1, 3));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal.hands[2]", [](Json& state) { state["deal"]["hands"][2].erase(state["deal"]["hands"][2].begin()); })) << "a missing tile";
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal.hands[1]", [](Json& state) { state["deal"]["hands"][1][3] = "9z"; })) << "a bad tile";
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal.hands[0]", [](Json& state) { state["deal"]["hands"][0][0] = 7; })) << "a tile that is no text";
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal.hands", [](Json& state) { state["deal"]["hands"].erase(state["deal"]["hands"].begin()); })) << "three hands";
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal.east", [](Json& state) { state["deal"]["east"] = 5; }));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal.east", [](Json& state) { state["deal"]["east"] = "1"; }));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal.live", [](Json& state) { state["deal"]["live"] = Json::array({"1p", "zz"}); }));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal.liveAfterDeal", [](Json& state) { state["deal"]["liveAfterDeal"] = "many"; }));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal", [](Json& state) { state["deal"] = 3; }));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.deal", [](Json& state) { state.erase("deal"); }));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.scores", [](Json& state) { state["scores"] = Json::array({1, 2, 3}); })) << "scores of three numbers";
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.scores", [](Json& state) { state["scores"] = Json::array({1, 2, 3, "x"}); }));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.scores", [](Json& state) { state.erase("scores"); }));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.handsPlayed", [](Json& state) { state["handsPlayed"] = "3"; }));
        EXPECT_TRUE(BrokenStateIsRefused(start, "state.handsPlayed", [](Json& state) { state.erase("handsPlayed"); }));
        Json notObject = EventToJson(start);
        notObject["state"] = Json::array();
        EXPECT_NE(ErrorOf(notObject).find("state"), String::npos);
    }

    TEST_F(MatchLogJsonTest, ATileListIsAnArrayOfNotationsOrOneText) {
        Json deal = FixedDealToJson(MakeSamplePosition(1).deal);
        const auto reference = FixedDealFromJson(deal);
        ASSERT_TRUE(reference.has_value()) << reference.error().c_str();

        Json texts = deal;
        for (size_t seat = 0; seat < kSeatCount; ++seat) {
            String text;
            for (const Tile tile : reference->hands[seat]) {
                AppendFormat(text, "{}{}", text.empty() ? "" : " ", ToString(tile));
            }
            texts["hands"][seat] = ToStd(text);
        }
        texts["live"] = "Gd Gd 9m";
        texts.erase("loose");
        const auto fromTexts = FixedDealFromJson(texts);
        ASSERT_TRUE(fromTexts.has_value()) << fromTexts.error().c_str();
        FixedDeal want = *reference;
        want.loose.clear();
        EXPECT_TRUE(*fromTexts == want) << "texts of tiles read as the arrays do; a list left out is empty";

        texts["hands"][0] = "1p 2p 3p 4p 5p 6p 7p 8p 9p 1s 2s 3s 9z";
        const auto bad = FixedDealFromJson(texts);
        ASSERT_FALSE(bad.has_value());
        EXPECT_EQ(bad.error().rfind("hands[0]: ", 0), 0u) << bad.error().c_str();
        EXPECT_FALSE(FixedDealFromJson(Json::array()).has_value());
    }

    TEST_F(MatchLogJsonTest, AWholeLogOfAMatchFromAPositionReadsBackAndReplaysEqual) {
        Match match;
        TableSession session(match);
        MatchLog log;
        session.SetLogSink(&log);
        MahjongSettings settings;
        settings.handsPerMatch = kShortMatchHands;
        ASSERT_TRUE(session.StartMatch(settings, kSeed, kAllComputers, UniformLevels(kDefaultLevel), MakeSamplePosition(3, 5)).has_value());
        for (int hands = 0; session.GetPhase() != SessionPhase::MatchOver && hands < kHandLimitPerMatch; ++hands) {
            session.RunComputers();
            if (session.GetPhase() == SessionPhase::HandOver) {
                ASSERT_TRUE(session.NextHand().has_value());
            }
        }
        ASSERT_EQ(session.GetPhase(), SessionPhase::MatchOver);

        const auto parsed = ParseMatchLog(TextOf(log.Events()));
        ASSERT_TRUE(parsed.has_value()) << parsed.error().c_str();
        EXPECT_EQ(*parsed, log.Events());
        Match replayed;
        MatchLog echo;
        const ReplayStatus status = ReplayMatchLog(*parsed, replayed, &echo);
        ASSERT_TRUE(status.has_value()) << "stopped at event " << status.error().index << ": " << Name(status.error().problem);
        EXPECT_EQ(echo.Events(), log.Events());
        EXPECT_EQ(replayed.Scores(), match.Scores());
        EXPECT_EQ(replayed.Standings(), match.Standings());
    }

}  // namespace BECore::Mahjong::Tests
