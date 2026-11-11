#include <Common/EngineEnvironment.h>
#include <Common/TempPath.h>
#include <Mahjong/MatchLogJson.h>
#include <Mahjong/MatchLogWriter.h>
#include <Mahjong/MatchUpload.h>
#include <gtest/gtest.h>
#include <vector>

/**
 * The in-memory recording of a match (`MatchUpload.h`): off unless the consent is `Granted`, pieces after each hand, the pieces of a
 * match reassemble into the log the file writer makes, and what is given up is told. No network: a fake sink keeps the pieces.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;
    namespace fs = std::filesystem;

    namespace {

        constexpr uint32_t kSeed = 41;
        constexpr uint32_t kOtherSeed = 42;
        constexpr int kHandLimitPerMatch = 200;
        constexpr const char* kFolderStem = "bagi-mahjong-match-upload";
        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

        class FakeSink final : public IMatchUploadSink {
        public:
            void Send(const UploadPiece& piece) override {
                pieces.push_back(piece);
            }

            eastl::vector<UploadPiece> pieces;
        };

        MahjongSettings ShortMatch() {
            MahjongSettings settings;
            settings.handsPerMatch = kShortMatchHands;
            return settings;
        }

        std::string_view View(const eastl::string& text) {
            return std::string_view(text.c_str(), text.size());
        }

        std::string ReadFile(const fs::path& path) {
            std::ifstream file(path, std::ios::binary);
            std::stringstream text;
            text << file.rdbuf();
            return text.str();
        }

        /// A session of four computers whose events go to a recording uploader, and from it to an in-memory log.
        struct UploadTable {
            Match match;
            TableSession session{match};
            MatchUploader uploader;
            MatchLog log;
            FakeSink sink;
            int idsDrawn = 0;

            explicit UploadTable(UploadConsent consent = UploadConsent::Granted, bool remembered = true) {
                uploader.SetNext(&log);
                uploader.SetSink(&sink);
                uploader.SetMatchIdSource([this] { return eastl::string(Format("match-{}", ++idsDrawn)); });
                uploader.SetConsent(consent, remembered);
                session.SetLogSink(&uploader);
            }

            void Start(uint32_t seed = kSeed) {
                ASSERT_TRUE(session.StartMatch(ShortMatch(), seed, kAllComputers).has_value());
            }

            /// The computers play the hand to its end and the next one is dealt (or the match ends).
            void PlayHand() {
                session.RunComputers();
                ASSERT_EQ(session.GetPhase(), SessionPhase::HandOver);
                ASSERT_TRUE(session.NextHand().has_value());
            }

            void PlayMatch() {
                for (int hands = 0; session.GetPhase() != SessionPhase::MatchOver && hands < kHandLimitPerMatch; ++hands) {
                    session.RunComputers();
                    if (session.GetPhase() == SessionPhase::HandOver) {
                        ASSERT_TRUE(session.NextHand().has_value());
                    }
                }
                ASSERT_EQ(session.GetPhase(), SessionPhase::MatchOver);
            }
        };

        /// Parses and replays `text`: the log is whole as far as it goes. Returns the events.
        eastl::vector<MatchLogEvent> ExpectReplays(const eastl::string& text) {
            const auto events = ParseMatchLog(View(text));
            EXPECT_TRUE(events.has_value()) << (events.has_value() ? "" : events.error().c_str());
            if (!events.has_value()) {
                return {};
            }
            Match replayed;
            const ReplayStatus status = ReplayMatchLog(*events, replayed);
            EXPECT_TRUE(status.has_value()) << "event " << status.error().index;
            return *events;
        }

        UploadPiece Piece(const char* id, uint32_t index, UploadPieceEnd end = UploadPieceEnd::Open, const char* lines = "") {
            UploadPiece piece;
            piece.matchId = id;
            piece.index = index;
            piece.lines = lines;
            piece.end = end;
            return piece;
        }

        class MatchUploadTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }
        };

    }  // namespace

    TEST_F(MatchUploadTest, UnaskedAndDeclinedKeepNothingAndSendNothing) {
        for (const UploadConsent consent : {UploadConsent::Unasked, UploadConsent::Declined}) {
            UploadTable table(consent);
            ASSERT_NO_FATAL_FAILURE(table.Start());
            ASSERT_NO_FATAL_FAILURE(table.PlayMatch());
            EXPECT_TRUE(table.sink.pieces.empty()) << EnumUtils<UploadConsent>::ToString(consent).data();
            EXPECT_EQ(table.idsDrawn, 0) << "no id is drawn for a match that is not recorded";
            EXPECT_FALSE(table.uploader.IsRecording());
            table.uploader.Abandon();
            EXPECT_TRUE(table.sink.pieces.empty());
        }
    }

    TEST_F(MatchUploadTest, TheDefaultIsOff) {
        MatchUploader uploader;
        EXPECT_EQ(uploader.Consent(), UploadConsent::Unasked);
    }

    TEST_F(MatchUploadTest, WithoutASinkOrAnIdSourceNothingIsRecorded) {
        UploadTable noSink;
        noSink.uploader.SetSink(nullptr);
        ASSERT_NO_FATAL_FAILURE(noSink.Start());
        ASSERT_NO_FATAL_FAILURE(noSink.PlayMatch());
        EXPECT_EQ(noSink.idsDrawn, 0);
        EXPECT_FALSE(noSink.uploader.IsRecording());

        UploadTable noSource;
        noSource.uploader.SetMatchIdSource({});
        ASSERT_NO_FATAL_FAILURE(noSource.Start());
        ASSERT_NO_FATAL_FAILURE(noSource.PlayMatch());
        EXPECT_TRUE(noSource.sink.pieces.empty());
        EXPECT_FALSE(noSource.uploader.IsRecording());

        UploadTable emptyId;
        emptyId.uploader.SetMatchIdSource([] { return eastl::string(); });
        ASSERT_NO_FATAL_FAILURE(emptyId.Start());
        ASSERT_NO_FATAL_FAILURE(emptyId.PlayMatch());
        EXPECT_TRUE(emptyId.sink.pieces.empty()) << "a match with no id would collide in the store";
    }

    TEST_F(MatchUploadTest, TheEventsGoToTheNextSinkWhateverTheConsent) {
        UploadTable off(UploadConsent::Declined);
        ASSERT_NO_FATAL_FAILURE(off.Start());
        ASSERT_NO_FATAL_FAILURE(off.PlayMatch());
        UploadTable on(UploadConsent::Granted);
        ASSERT_NO_FATAL_FAILURE(on.Start());
        ASSERT_NO_FATAL_FAILURE(on.PlayMatch());
        EXPECT_FALSE(off.log.Events().empty());
        EXPECT_EQ(off.log.Events().size(), on.log.Events().size());
        EXPECT_TRUE(off.log.Events() == on.log.Events());
    }

    TEST_F(MatchUploadTest, AGrantedMatchComesInOnePieceAfterEachHandAndTheEnd) {
        UploadTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start());
        ASSERT_NO_FATAL_FAILURE(table.PlayMatch());

        const size_t hands = table.match.History().size();
        ASSERT_EQ(hands, static_cast<size_t>(kShortMatchHands));
        ASSERT_EQ(table.sink.pieces.size(), hands + 1);
        for (size_t at = 0; at < table.sink.pieces.size(); ++at) {
            const UploadPiece& piece = table.sink.pieces[at];
            EXPECT_EQ(piece.matchId, "match-1");
            EXPECT_EQ(piece.index, at);
            EXPECT_EQ(piece.end, at < hands ? UploadPieceEnd::Open : UploadPieceEnd::Ended) << "piece " << at;
            EXPECT_FALSE(piece.lines.empty());
            EXPECT_EQ(piece.lines.back(), '\n');
        }
        EXPECT_FALSE(table.uploader.IsRecording()) << "the match is closed after End";
        EXPECT_EQ(table.idsDrawn, 1);
    }

    TEST_F(MatchUploadTest, ThePiecesAssembleIntoTheLogThatReplaysAndIsTheFileOfTheWriter) {
        const fs::path folder = TestTempPath(kFolderStem);
        const fs::path before = MatchLogDirectory();
        SetMatchLogDirectory(folder);

        MatchLogWriter writer;
        UploadTable table;
        table.uploader.SetNext(&writer);
        ASSERT_NO_FATAL_FAILURE(table.Start());
        ASSERT_NO_FATAL_FAILURE(table.PlayMatch());
        const fs::path file = writer.Path();
        SetMatchLogDirectory(before);

        const auto assembled = AssemblePieces(table.sink.pieces);
        ASSERT_TRUE(assembled.has_value()) << assembled.error().c_str();
        const eastl::vector<MatchLogEvent> events = ExpectReplays(*assembled);
        EXPECT_EQ(events.back().kind, MatchLogKind::End);
        EXPECT_EQ(std::string(assembled->c_str(), assembled->size()), ReadFile(file));

        std::error_code error;
        fs::remove_all(folder, error);
    }

    TEST_F(MatchUploadTest, AMatchGivenUpByANewStartEndsWithAnAbandonedPieceAndStillReplays) {
        UploadTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start(kSeed));
        ASSERT_NO_FATAL_FAILURE(table.PlayHand());
        ASSERT_NO_FATAL_FAILURE(table.Start(kOtherSeed));  // the first match is given up in its second hand

        ASSERT_EQ(table.sink.pieces.size(), 2u);  // hand 1 (Open) and the rest of the match (Abandoned); the new match has sent nothing yet
        EXPECT_EQ(table.sink.pieces[0].end, UploadPieceEnd::Open);
        EXPECT_EQ(table.sink.pieces[1].end, UploadPieceEnd::Abandoned);
        EXPECT_EQ(table.sink.pieces[1].index, 1u);
        EXPECT_EQ(table.sink.pieces[1].matchId, "match-1");
        EXPECT_TRUE(table.uploader.IsRecording());
        ASSERT_NO_FATAL_FAILURE(table.PlayMatch());
        EXPECT_EQ(table.sink.pieces[2].matchId, "match-2") << "the new match has its own id";
        EXPECT_EQ(table.sink.pieces[2].index, 0u);
        const auto assembled = AssemblePieces({table.sink.pieces[0], table.sink.pieces[1]});
        ASSERT_TRUE(assembled.has_value()) << assembled.error().c_str();
        ExpectReplays(*assembled);
    }

    TEST_F(MatchUploadTest, TheAbandonedMatchAssemblesIntoALogThatReplays) {
        UploadTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start());
        ASSERT_NO_FATAL_FAILURE(table.PlayHand());
        table.session.RunComputers();  // part of the second hand is played, then the game stops
        table.uploader.Abandon();

        eastl::vector<UploadPiece> pieces = table.sink.pieces;
        ASSERT_FALSE(pieces.empty());
        EXPECT_EQ(pieces.back().end, UploadPieceEnd::Abandoned);
        const auto assembled = AssemblePieces(pieces);
        ASSERT_TRUE(assembled.has_value()) << assembled.error().c_str();
        const eastl::vector<MatchLogEvent> events = ExpectReplays(*assembled);
        EXPECT_FALSE(events.empty());
        EXPECT_NE(events.back().kind, MatchLogKind::End);

        const size_t sent = table.sink.pieces.size();
        table.uploader.Abandon();
        EXPECT_EQ(table.sink.pieces.size(), sent) << "a closed match is not abandoned twice";
        EXPECT_FALSE(table.uploader.IsRecording());
    }

    TEST_F(MatchUploadTest, AnEmptyRemainderIsStillSentAsAnEmptyAbandonedPiece) {
        UploadTable table;
        MatchLogEvent start;
        start.kind = MatchLogKind::Start;
        MatchLogEvent settlement;
        settlement.kind = MatchLogKind::Settlement;
        table.uploader.OnEvent(start);
        table.uploader.OnEvent(settlement);  // cuts a piece, so nothing is buffered
        table.uploader.Abandon();

        ASSERT_EQ(table.sink.pieces.size(), 2u);
        EXPECT_EQ(table.sink.pieces[0].end, UploadPieceEnd::Open);
        EXPECT_EQ(table.sink.pieces[1].end, UploadPieceEnd::Abandoned);
        EXPECT_TRUE(table.sink.pieces[1].lines.empty());
        EXPECT_EQ(table.sink.pieces[1].index, 1u);
    }

    TEST_F(MatchUploadTest, WithdrawingTheConsentInTheMiddleOfAMatchSendsNothingMore) {
        UploadTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start());
        ASSERT_NO_FATAL_FAILURE(table.PlayHand());
        ASSERT_EQ(table.sink.pieces.size(), 1u);

        table.uploader.SetConsent(UploadConsent::Declined, true);
        EXPECT_FALSE(table.uploader.IsRecording());
        ASSERT_NO_FATAL_FAILURE(table.PlayMatch());
        table.uploader.Abandon();
        table.uploader.SetConsent(UploadConsent::Granted, true);  // granting again does not bring the rest of that match back
        EXPECT_FALSE(table.uploader.IsRecording());
        ASSERT_NO_FATAL_FAILURE(table.Start(kOtherSeed));
        EXPECT_EQ(table.sink.pieces.size(), 1u) << "no more pieces and no Abandoned one";
        EXPECT_TRUE(table.uploader.IsRecording()) << "the next match is recorded again";
    }

    TEST_F(MatchUploadTest, GrantingTheConsentInTheMiddleOfAMatchRecordsOnlyTheNextOne) {
        UploadTable table(UploadConsent::Unasked);
        ASSERT_NO_FATAL_FAILURE(table.Start());
        ASSERT_NO_FATAL_FAILURE(table.PlayHand());
        table.uploader.SetConsent(UploadConsent::Granted, true);
        EXPECT_FALSE(table.uploader.IsRecording()) << "a log without its Start would not replay";
        ASSERT_NO_FATAL_FAILURE(table.PlayMatch());
        EXPECT_TRUE(table.sink.pieces.empty());

        ASSERT_NO_FATAL_FAILURE(table.Start(kOtherSeed));
        ASSERT_NO_FATAL_FAILURE(table.PlayMatch());
        ASSERT_FALSE(table.sink.pieces.empty());
        EXPECT_EQ(table.sink.pieces.front().index, 0u);
        const auto assembled = AssemblePieces(table.sink.pieces);
        ASSERT_TRUE(assembled.has_value()) << assembled.error().c_str();
        const eastl::vector<MatchLogEvent> events = ExpectReplays(*assembled);
        ASSERT_FALSE(events.empty());
        EXPECT_EQ(events.front().kind, MatchLogKind::Start);
        EXPECT_EQ(events.front().seed, kOtherSeed);
    }

    TEST_F(MatchUploadTest, TakingTheSinkAwayInTheMiddleOfAMatchDropsIt) {
        UploadTable table;
        ASSERT_NO_FATAL_FAILURE(table.Start());
        ASSERT_NO_FATAL_FAILURE(table.PlayHand());
        table.uploader.SetSink(nullptr);
        ASSERT_NO_FATAL_FAILURE(table.PlayMatch());
        table.uploader.Abandon();
        EXPECT_EQ(table.sink.pieces.size(), 1u);
    }

    TEST_F(MatchUploadTest, TheRememberedFlagIsOnEveryPiece) {
        for (const bool remembered : {false, true}) {
            UploadTable table(UploadConsent::Granted, remembered);
            ASSERT_NO_FATAL_FAILURE(table.Start());
            ASSERT_NO_FATAL_FAILURE(table.PlayHand());
            ASSERT_NO_FATAL_FAILURE(table.Start(kOtherSeed));
            table.uploader.Abandon();
            ASSERT_GE(table.sink.pieces.size(), 3u);
            for (const UploadPiece& piece : table.sink.pieces) {
                EXPECT_EQ(piece.consentRemembered, remembered);
            }
        }
    }

    TEST_F(MatchUploadTest, APieceHoldsExactlyTheFiveListedMembers) {
        // a structured binding compiles only when the number of members is exactly five: a new member (a name, an address, a time) fails here
        const UploadPiece piece;
        const auto& [matchId, index, lines, end, consentRemembered] = piece;
        EXPECT_TRUE(matchId.empty());
        EXPECT_EQ(index, 0u);
        EXPECT_TRUE(lines.empty());
        EXPECT_EQ(end, UploadPieceEnd::Open);
        EXPECT_FALSE(consentRemembered);
    }

    TEST_F(MatchUploadTest, AssemblingTakesThePiecesInIndexOrder) {
        const eastl::vector<UploadPiece> pieces{Piece("m", 2, UploadPieceEnd::Ended, "c\n"), Piece("m", 0, UploadPieceEnd::Open, "a\n"), Piece("m", 1, UploadPieceEnd::Open, "b\n")};
        const auto assembled = AssemblePieces(pieces);
        ASSERT_TRUE(assembled.has_value());
        EXPECT_EQ(*assembled, "a\nb\nc\n");
    }

    TEST_F(MatchUploadTest, AssemblingAMatchThatStopsOnAnOpenPieceIsAllowed) {
        const auto assembled = AssemblePieces({Piece("m", 0, UploadPieceEnd::Open, "a\n"), Piece("m", 1, UploadPieceEnd::Open, "b\n")});
        ASSERT_TRUE(assembled.has_value());
        EXPECT_EQ(*assembled, "a\nb\n");
    }

    TEST_F(MatchUploadTest, AssemblingRefusesWhatIsNotOneWholeMatch) {
        EXPECT_FALSE(AssemblePieces({}).has_value()) << "no pieces";
        EXPECT_FALSE(AssemblePieces({Piece("m", 0), Piece("other", 1)}).has_value()) << "two matches";
        EXPECT_FALSE(AssemblePieces({Piece("m", 0), Piece("m", 2)}).has_value()) << "a gap";
        EXPECT_FALSE(AssemblePieces({Piece("m", 1), Piece("m", 2)}).has_value()) << "the first is missing";
        EXPECT_FALSE(AssemblePieces({Piece("m", 0), Piece("m", 1), Piece("m", 1)}).has_value()) << "a repeat";
        EXPECT_FALSE(AssemblePieces({Piece("m", 0, UploadPieceEnd::Ended), Piece("m", 1)}).has_value()) << "a piece after Ended";
        EXPECT_FALSE(AssemblePieces({Piece("m", 0, UploadPieceEnd::Abandoned), Piece("m", 1)}).has_value()) << "a piece after Abandoned";
        const auto gap = AssemblePieces({Piece("m", 0), Piece("m", 2)});
        EXPECT_NE(std::string(gap.error().c_str()).find("1"), std::string::npos) << "the error names the missing piece";
    }

}  // namespace BECore::Mahjong::Tests
