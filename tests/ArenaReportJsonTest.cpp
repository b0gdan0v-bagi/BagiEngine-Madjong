#include <Common/Printers.h>
#include <Mahjong/Arena/ArenaReport.h>
#include <Mahjong/Arena/ArenaReportJson.h>
#include <gtest/gtest.h>

/// About 11 s in Debug: a few seeds of Normal, a weaker Normal and random seats against Normal, each played once or twice.
namespace BECore::Mahjong::Arena {

    namespace {

        constexpr uint32_t kFirstSeed = 4000;
        constexpr int kFewSeeds = 4;
        constexpr int kOneSeed = 1;
        constexpr int kThreadSeeds = 3;
        constexpr int kThreadedJobs = 3;
        constexpr double kScale = 1000000.0;
        constexpr StringView kSignificantWord = "significant";
        constexpr StringView kIntervalMark = "+-";
        constexpr size_t kOthersLabelTokens = 3;
        constexpr size_t kNameTokens = 1;
        constexpr size_t kMainCells = 6;
        constexpr size_t kEndsCells = 4;
        constexpr size_t kPairedCells = 2;
        /// Rows per focus player in the main table and in "how the hands end": its own row and its others'.
        constexpr size_t kRowsPerFocus = 2;

        ArenaPlayer Player(StringView spec) {
            const std::expected<ArenaPlayer, String> player = ParseArenaPlayer(spec);
            EXPECT_TRUE(player.has_value()) << ToStd(spec);
            return player.value_or(ArenaPlayer{});
        }

        ArenaConfig ConfigOf(std::initializer_list<StringView> focus, int count) {
            ArenaConfig config;
            config.first = kFirstSeed;
            config.count = count;
            for (const StringView spec : focus) {
                config.focus.push_back(Player(spec));
            }
            config.others = Player("normal");
            return config;
        }

        ArenaResult Play(const ArenaConfig& config) {
            std::expected<ArenaResult, String> result = RunArena(config);
            EXPECT_TRUE(result.has_value()) << (result.has_value() ? "" : result.error().c_str());
            return result.has_value() ? eastl::move(*result) : ArenaResult{};
        }

        Json Parsed(const ArenaResult& result) {
            const std::expected<Json, ControlError> parsed = ParseJson(FormatArenaReportJson(result));
            EXPECT_TRUE(parsed.has_value());
            return parsed.has_value() ? *parsed : Json::object();
        }

        /// One cell of a text row: the number as printed, its `+- half` and the word `significant` after it.
        struct TextCell {
            String value;
            String half;
            bool hasHalf = false;
            bool significant = false;
        };

        /// The parts of `text` between `separator`s that are not empty (so the runs of spaces of a table are one gap).
        Vector<String> Split(const String& text, char separator) {
            Vector<String> parts;
            String current;
            for (const char letter : text) {
                if (letter == separator) {
                    if (!current.empty()) {
                        parts.push_back(current);
                    }
                    current.clear();
                } else {
                    current.push_back(letter);
                }
            }
            if (!current.empty()) {
                parts.push_back(current);
            }
            return parts;
        }

        Vector<String> Tokens(const String& line) {
            return Split(line, ' ');
        }

        Vector<TextCell> CellsOf(const String& line, size_t labelTokens) {
            const Vector<String> tokens = Tokens(line);
            Vector<TextCell> cells;
            size_t at = labelTokens;
            while (at < tokens.size()) {
                TextCell cell;
                cell.value = tokens[at++];
                if (at + 1 < tokens.size() && tokens[at] == kIntervalMark) {
                    cell.half = tokens[at + 1];
                    cell.hasHalf = true;
                    at += 2;
                }
                if (at < tokens.size() && tokens[at] == kSignificantWord) {
                    cell.significant = true;
                    ++at;
                }
                cells.push_back(cell);
            }
            return cells;
        }

        /// The report has no empty line that a table needs, so the empty ones are left out.
        Vector<String> LinesOf(const String& text) {
            return Split(text, '\n');
        }

        bool StartsWith(const String& text, StringView prefix) {
            return StringView(text).substr(0, prefix.size()) == prefix;
        }

        /// The `count` lines that follow the line `first`.
        Vector<String> LinesFrom(const Vector<String>& lines, size_t first, size_t count) {
            Vector<String> rows;
            EXPECT_LE(first + count, lines.size());
            for (size_t index = first; index < first + count && index < lines.size(); ++index) {
                rows.push_back(lines[index]);
            }
            return rows;
        }

        /// The rows of the main table: they follow its header, the first line that starts with `player`.
        Vector<String> MainRows(const Vector<String>& lines, size_t count) {
            for (size_t index = 0; index < lines.size(); ++index) {
                if (StartsWith(lines[index], "player")) {
                    return LinesFrom(lines, index + 1, count);
                }
            }
            ADD_FAILURE() << "no main table";
            return {};
        }

        /// The rows of the table whose title is the line `title`: a header line sits between.
        Vector<String> RowsUnder(const Vector<String>& lines, const String& title, size_t count) {
            for (size_t index = 0; index < lines.size(); ++index) {
                if (lines[index] == title) {
                    return LinesFrom(lines, index + 2, count);
                }
            }
            ADD_FAILURE() << "no table titled " << title.c_str();
            return {};
        }

        /// A number written with the text's two decimals; a negative zero is a zero.
        String Written(double value) {
            const String text = Format("{:.2f}", value);
            return text == "-0.00" ? String("0.00") : text;
        }

        String WrittenText(const String& cell) {
            const String unsignedCell = StartsWith(cell, "+") ? String(cell.c_str() + 1) : cell;
            return unsignedCell == "-0.00" ? String("0.00") : unsignedCell;
        }

        String TextOf(const Json& json, const char* key) {
            return String(json.at(key).get<std::string>().c_str());  //-NoStd: the JSON library's string
        }

        double Number(const Json& json, const char* key) {
            return json.at(key).get<double>();
        }

        void ExpectPlain(const TextCell& cell, const Json& json, const char* key, const String& where) {
            EXPECT_EQ(WrittenText(cell.value), Written(Number(json, key))) << where.c_str() << " " << key;
            EXPECT_FALSE(cell.hasHalf) << where.c_str() << " " << key;
        }

        void ExpectInterval(const TextCell& cell, const Json& json, const char* key, const String& where, bool paired) {
            const Json& interval = json.at(key);
            EXPECT_EQ(WrittenText(cell.value), Written(Number(interval, "mean"))) << where.c_str() << " " << key;
            EXPECT_EQ(cell.hasHalf, interval.at("hasInterval").get<bool>()) << where.c_str() << " " << key;
            if (cell.hasHalf) {
                EXPECT_EQ(cell.half, Written(Number(interval, "half"))) << where.c_str() << " " << key;
            } else {
                EXPECT_EQ(Number(interval, "half"), 0.0) << where.c_str() << " " << key;
            }
            EXPECT_EQ(cell.significant, paired && interval.at("significant").get<bool>()) << where.c_str() << " " << key;
            EXPECT_EQ(interval.contains("significant"), paired) << where.c_str() << " " << key;
        }

        /// Every float of the document is finite, already rounded to the document's precision and no negative zero.
        void ExpectCleanNumbers(const Json& json) {
            if (json.is_number_float()) {
                const double value = json.get<double>();
                EXPECT_TRUE(std::isfinite(value));
                EXPECT_EQ(std::round(value * kScale) / kScale, value);
                EXPECT_FALSE(value == 0.0 && std::signbit(value));
            } else if (json.is_object() || json.is_array()) {
                for (const Json& member : json) {
                    ExpectCleanNumbers(member);
                }
            }
        }

        void ExpectMainRow(const Vector<TextCell>& cells, const Json& json, const String& where) {
            ASSERT_EQ(cells.size(), kMainCells) << where.c_str();
            EXPECT_EQ(cells[0].value, Format("{}", json.at("hands").get<int64_t>())) << where.c_str();
            ExpectInterval(cells[1], json, "winPercent", where, false);
            ExpectInterval(cells[2], json, "pointsPerHand", where, false);
            ExpectPlain(cells[3], json, "drawnPercent", where);
            ExpectPlain(cells[4], json, "declaredPercent", where);
            ExpectPlain(cells[5], json, "avgWinScore", where);
        }

        void ExpectOthersRow(const Vector<TextCell>& cells, const Json& json, const String& where) {
            ASSERT_EQ(cells.size(), kMainCells) << where.c_str();
            EXPECT_EQ(cells[0].value, Format("{}", json.at("hands").get<int64_t>())) << where.c_str();
            ExpectPlain(cells[1], json, "winPercent", where);
            ExpectPlain(cells[2], json, "pointsPerHand", where);
            ExpectPlain(cells[3], json, "drawnPercent", where);
            ExpectPlain(cells[4], json, "declaredPercent", where);
            ExpectPlain(cells[5], json, "avgWinScore", where);
        }

        void ExpectEndsRow(const Vector<TextCell>& cells, const Json& json, const String& where) {
            ASSERT_EQ(cells.size(), kEndsCells) << where.c_str();
            ExpectInterval(cells[0], json, "gaveWinPercent", where, false);
            ExpectPlain(cells[1], json, "selfDrawPercentOfWins", where);
            ExpectInterval(cells[2], json, "standingScore", where, false);
            ExpectPlain(cells[3], json, "pointsPerLostHand", where);
        }

        void ExpectEndsOthersRow(const Vector<TextCell>& cells, const Json& json, const String& where) {
            ASSERT_EQ(cells.size(), kEndsCells) << where.c_str();
            ExpectPlain(cells[0], json, "gaveWinPercent", where);
            ExpectPlain(cells[1], json, "selfDrawPercentOfWins", where);
            ExpectPlain(cells[2], json, "standingScore", where);
            ExpectPlain(cells[3], json, "pointsPerLostHand", where);
        }

        /// The two paired tables of the text against the `paired` members of the focus players after the first.
        void ExpectPairedTables(const Vector<String>& lines, const Json& focusList) {
            const size_t rows = focusList.size() - 1;
            const String baseline = TextOf(focusList.at(0), "name");
            const String pointsTitle = Format("paired difference against {} (the same seeds)", baseline);
            const String endsTitle = Format("paired difference against {} (the same seeds), how the hands end", baseline);
            const Vector<String> pointsRows = RowsUnder(lines, pointsTitle, rows);
            const Vector<String> endsRows = RowsUnder(lines, endsTitle, rows);
            ASSERT_EQ(pointsRows.size(), rows);
            ASSERT_EQ(endsRows.size(), rows);
            for (size_t index = 0; index < rows; ++index) {
                const Json& paired = focusList.at(index + 1).at("paired");
                const String where = Format("paired {}", index + 1);
                EXPECT_EQ(TextOf(paired, "against"), baseline);
                const Vector<TextCell> points = CellsOf(pointsRows[index], kNameTokens);
                ASSERT_EQ(points.size(), kPairedCells) << where.c_str();
                ExpectInterval(points[0], paired, "pointsPerHand", where, true);
                ExpectInterval(points[1], paired, "winPercent", where, true);
                const Vector<TextCell> ends = CellsOf(endsRows[index], kNameTokens);
                ASSERT_EQ(ends.size(), kPairedCells) << where.c_str();
                ExpectInterval(ends[0], paired, "gaveWinPercent", where, true);
                ExpectInterval(ends[1], paired, "standingScore", where, true);
            }
        }

        void ExpectTextAndDocumentAgree(const ArenaResult& result) {
            const Json json = Parsed(result);
            const Json& focusList = json.at("focus");
            ASSERT_EQ(focusList.size(), result.focus.size());
            const Vector<String> lines = LinesOf(FormatArenaReport(result));
            const size_t rowCount = focusList.size() * kRowsPerFocus;
            const Vector<String> main = MainRows(lines, rowCount);
            const Vector<String> ends = RowsUnder(lines, "how the hands end", rowCount);
            ASSERT_EQ(main.size(), rowCount);
            ASSERT_EQ(ends.size(), rowCount);
            for (size_t index = 0; index < focusList.size(); ++index) {
                const Json& focus = focusList.at(index);
                const String where = Format("focus {}", index);
                const Vector<String> tokens = Tokens(main[index * kRowsPerFocus]);
                ASSERT_FALSE(tokens.empty());
                EXPECT_EQ(tokens[0], TextOf(focus, "name"));
                ExpectMainRow(CellsOf(main[index * kRowsPerFocus], kNameTokens), focus, where);
                ExpectOthersRow(CellsOf(main[index * kRowsPerFocus + 1], kOthersLabelTokens), focus.at("others"), where);
                ExpectEndsRow(CellsOf(ends[index * kRowsPerFocus], kNameTokens), focus.at("ends"), where);
                ExpectEndsOthersRow(CellsOf(ends[index * kRowsPerFocus + 1], kOthersLabelTokens), focus.at("ends").at("others"), where);
            }
            ExpectCleanNumbers(json);
        }

    }  // namespace

    TEST(ArenaReportJsonTest, EveryCellOfTheTextReportIsInTheDocument) {
        const ArenaResult result = Play(ConfigOf({"normal", "normal:skipClaimPercent=20", "random"}, kFewSeeds));
        ExpectTextAndDocumentAgree(result);
        const Json json = Parsed(result);
        EXPECT_EQ(TextOf(json, "format"), "bagi-arena-report");
        EXPECT_EQ(json.at("version").get<int>(), kArenaReportJsonVersion);
        EXPECT_EQ(json.at("first").get<uint32_t>(), kFirstSeed);
        EXPECT_EQ(json.at("seeds").get<int>(), kFewSeeds);
        EXPECT_EQ(json.at("handsPerFocus").get<int>(), kFewSeeds * static_cast<int>(kSeatCount));
        EXPECT_EQ(TextOf(json, "others"), "normal");
        const Json& focusList = json.at("focus");
        ASSERT_EQ(focusList.size(), size_t{3});
        EXPECT_EQ(TextOf(focusList.at(1), "name"), "normal:skipClaimPercent=20");
        EXPECT_EQ(TextOf(focusList.at(2), "name"), "random");
        EXPECT_TRUE(focusList.at(0).at("paired").is_null());
        ExpectPairedTables(LinesOf(FormatArenaReport(result)), focusList);
    }

    TEST(ArenaReportJsonTest, OneFocusPlayerHasNoPairedRows) {
        const ArenaResult result = Play(ConfigOf({"normal"}, kFewSeeds));
        const Json json = Parsed(result);
        ASSERT_EQ(json.at("focus").size(), size_t{1});
        EXPECT_TRUE(json.at("focus").at(0).at("paired").is_null());
        ExpectTextAndDocumentAgree(result);
    }

    TEST(ArenaReportJsonTest, OneSeedHasNoIntervalAndNoNanOrInf) {
        const ArenaResult result = Play(ConfigOf({"normal", "random"}, kOneSeed));
        const String text = FormatArenaReportJson(result);
        for (const StringView word : {"nan", "inf", "NaN", "Infinity"}) {
            EXPECT_EQ(StringView(text).find(word), StringView::npos) << ToStd(word);
        }
        const Json json = Parsed(result);
        const Json& first = json.at("focus").at(0);
        EXPECT_FALSE(first.at("winPercent").at("hasInterval").get<bool>());
        EXPECT_EQ(Number(first.at("winPercent"), "half"), 0.0);
        const Json& paired = json.at("focus").at(1).at("paired");
        for (const char* key : {"pointsPerHand", "winPercent", "gaveWinPercent", "standingScore"}) {
            EXPECT_FALSE(paired.at(key).at("hasInterval").get<bool>()) << key;
            EXPECT_EQ(Number(paired.at(key), "half"), 0.0) << key;
            EXPECT_FALSE(paired.at(key).at("significant").get<bool>()) << key;
        }
        ExpectTextAndDocumentAgree(result);
    }

    TEST(ArenaReportJsonTest, TheBytesDoNotDependOnTheCallOrTheThreads) {
        ArenaConfig config = ConfigOf({"normal", "easy"}, kThreadSeeds);
        config.jobs = 1;
        const ArenaResult single = Play(config);
        EXPECT_EQ(FormatArenaReportJson(single), FormatArenaReportJson(single));
        EXPECT_FALSE(FormatArenaReportJson(single).empty());
        config.jobs = kThreadedJobs;
        const ArenaResult threaded = Play(config);
        EXPECT_EQ(FormatArenaReportJson(threaded), FormatArenaReportJson(single));
    }

    TEST(ArenaReportJsonTest, TimingIsNotInTheDocument) {
        ArenaConfig config = ConfigOf({"normal"}, kOneSeed);
        const String plain = FormatArenaReportJson(Play(config));
        config.timing = true;
        EXPECT_EQ(FormatArenaReportJson(Play(config)), plain);
    }

}  // namespace BECore::Mahjong::Arena
