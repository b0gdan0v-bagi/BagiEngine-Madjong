#include <Common/EngineEnvironment.h>
#include <Mahjong/MatchLogJson.h>
#include <Mahjong/WebUploadSink.h>
#include <gtest/gtest.h>
#include <map>
#include <string>
#include <vector>

/**
 * The sink that posts the pieces of a match to the page's server (`WebUploadSink.h`): the order, the answers and what each one
 * does, the bounds. A fake poster answers from a script and a fake clock is a number the test moves.
 */
namespace BECore::Mahjong::Tests {

    using namespace BECore::Tests;

    namespace {

        constexpr uint32_t kSeed = 41;
        constexpr int kStatusOk = 200;
        constexpr int kStatusBadRequest = 400;
        constexpr int kStatusUnauthorized = 401;
        constexpr int kStatusNotFound = 404;
        constexpr int kStatusConflict = 409;
        constexpr int kStatusTooLarge = 413;
        constexpr int kStatusTooMany = 429;
        constexpr int kStatusUnavailable = 503;
        constexpr double kStart = 1000.0;
        constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};

        /// What the page would answer to the post in flight; the test sets it, `Poll` hands it out once
        class FakePoster final : public IWebPoster {
        public:
            struct Post {
                std::string path;
                std::string body;
            };

            [[nodiscard]] bool Available() const override {
                return available;
            }

            std::expected<WebPostId, eastl::string> Start(eastl::string_view path, eastl::string_view body) override {
                if (refuseStart) {
                    return std::unexpected(eastl::string("refused"));
                }
                posts.push_back({std::string(path.data(), path.size()), std::string(body.data(), body.size())});
                pending = static_cast<WebPostId>(posts.size());
                answered = false;
                return pending;
            }

            WebPostResult Poll(WebPostId id) override {
                ++polls;
                EXPECT_EQ(id, pending) << "only the last post is waited for";
                if (!answered) {
                    return {WebPostStatus::Pending, 0};
                }
                return answer;
            }

            /// The server answers the post in flight with `status`
            void Answer(int status) {
                answer = {WebPostStatus::Done, status};
                answered = true;
            }

            void AnswerFailed() {
                answer = {WebPostStatus::Failed, 0};
                answered = true;
            }

            bool available = true;
            bool refuseStart = false;
            std::vector<Post> posts;
            WebPostId pending = 0;
            bool answered = false;
            WebPostResult answer;
            int polls = 0;
        };

        UploadPiece Piece(const char* id, uint32_t index, const char* lines = "{\"event\":\"x\"}\n", UploadPieceEnd end = UploadPieceEnd::Open) {
            UploadPiece piece;
            piece.matchId = id;
            piece.index = index;
            piece.lines = lines;
            piece.end = end;
            return piece;
        }

        struct SinkFixture {
            FakePoster poster;
            WebUploadSink sink;

            SinkFixture() {
                sink.SetPoster(&poster);
            }

            /// The answer to the post in flight, read by a pump at `now`
            void AnswerAndPump(int status, double now) {
                poster.Answer(status);
                sink.Pump(now);
            }
        };

        class WebUploadSinkTest : public ::testing::Test {
        protected:
            static void SetUpTestSuite() {
                EngineEnvironment::EnsureCoreServices();
            }
        };

    }  // namespace

    TEST_F(WebUploadSinkTest, PiecesAreOnePostAtATimeInOrderAtTheirPath) {
        SinkFixture fixture;
        fixture.sink.Send(Piece("abc", 0, "{\"a\":1}\n"));
        fixture.sink.Send(Piece("abc", 1, "{\"b\":2}\n"));
        fixture.sink.Send(Piece("abc", 2, ""));
        EXPECT_TRUE(fixture.poster.posts.empty()) << "Send only queues";

        fixture.sink.Pump(kStart);
        ASSERT_EQ(fixture.poster.posts.size(), 1U);
        EXPECT_EQ(fixture.poster.posts[0].path, "/matches/abc/0");
        EXPECT_EQ(fixture.poster.posts[0].body, "{\"a\":1}\n");
        fixture.sink.Pump(kStart + 1);
        EXPECT_EQ(fixture.poster.posts.size(), 1U) << "one in flight: the next waits for the answer";
        EXPECT_TRUE(fixture.sink.IsPosting());

        fixture.AnswerAndPump(kStatusOk, kStart + 2);
        ASSERT_EQ(fixture.poster.posts.size(), 2U);
        EXPECT_EQ(fixture.poster.posts[1].path, "/matches/abc/1");
        fixture.AnswerAndPump(kStatusOk, kStart + 3);
        ASSERT_EQ(fixture.poster.posts.size(), 3U);
        EXPECT_EQ(fixture.poster.posts[2].path, "/matches/abc/2");
        EXPECT_TRUE(fixture.poster.posts[2].body.empty()) << "the marker of a match given up is an empty body";
        fixture.AnswerAndPump(kStatusOk, kStart + 4);

        EXPECT_EQ(fixture.sink.Counters().sent, 3U);
        EXPECT_EQ(fixture.sink.QueuedPieces(), 0U);
        EXPECT_FALSE(fixture.sink.IsPosting());
        EXPECT_EQ(fixture.sink.State(), WebUploadState::Active);
    }

    TEST_F(WebUploadSinkTest, AnIdlePumpAsksNobody) {
        SinkFixture fixture;
        fixture.sink.Pump(kStart);
        EXPECT_EQ(fixture.poster.polls, 0);
        fixture.sink.Send(Piece("abc", 0));
        fixture.sink.Pump(kStart);
        fixture.AnswerAndPump(kStatusOk, kStart + 1);
        const int polls = fixture.poster.polls;
        const size_t posts = fixture.poster.posts.size();
        for (int frame = 0; frame < 100; ++frame) {
            fixture.sink.Pump(kStart + 2 + frame);
        }
        EXPECT_EQ(fixture.poster.polls, polls) << "an empty queue returns before any call";
        EXPECT_EQ(fixture.poster.posts.size(), posts);
    }

    TEST_F(WebUploadSinkTest, TransientAnswersRetryTheSamePieceAfterTheBackoffThatDoublesToTheCap) {
        for (const int status : {kStatusTooMany, kStatusUnavailable, 0}) {
            SinkFixture fixture;
            fixture.sink.Send(Piece("abc", 0));
            fixture.sink.Send(Piece("abc", 1));
            fixture.sink.Pump(kStart);

            double now = kStart;
            double delay = WebUploadSink::kFirstBackoffSeconds;
            for (int attempt = 0; attempt < 8; ++attempt) {
                ASSERT_EQ(fixture.poster.posts.size(), static_cast<size_t>(attempt + 1)) << status;
                if (status == 0) {
                    fixture.poster.AnswerFailed();
                } else {
                    fixture.poster.Answer(status);
                }
                fixture.sink.Pump(now);
                EXPECT_EQ(fixture.sink.Counters().retried, static_cast<uint32_t>(attempt + 1));
                fixture.sink.Pump(now + delay - 1);
                EXPECT_EQ(fixture.poster.posts.size(), static_cast<size_t>(attempt + 1)) << "not before the back-off";
                now += delay;
                fixture.sink.Pump(now);
                EXPECT_EQ(fixture.poster.posts.back().path, "/matches/abc/0") << "the same piece again";
                delay = std::min(delay * 2.0, WebUploadSink::kMaxBackoffSeconds);
            }
            EXPECT_DOUBLE_EQ(delay, WebUploadSink::kMaxBackoffSeconds);

            // A 200 resets the back-off and the queue goes on
            fixture.AnswerAndPump(kStatusOk, now);
            EXPECT_EQ(fixture.poster.posts.back().path, "/matches/abc/1");
            fixture.poster.AnswerFailed();
            fixture.sink.Pump(now);
            fixture.sink.Pump(now + WebUploadSink::kFirstBackoffSeconds);
            EXPECT_EQ(fixture.poster.posts.back().path, "/matches/abc/1");
            EXPECT_EQ(fixture.sink.State(), WebUploadState::Active);
            EXPECT_EQ(fixture.sink.Counters().droppedMatches, 0U);
        }
    }

    TEST_F(WebUploadSinkTest, ARefusedStartIsRetriedLater) {
        SinkFixture fixture;
        fixture.poster.refuseStart = true;
        fixture.sink.Send(Piece("abc", 0));
        fixture.sink.Pump(kStart);
        EXPECT_EQ(fixture.sink.Counters().retried, 1U);
        fixture.poster.refuseStart = false;
        fixture.sink.Pump(kStart + 1);
        EXPECT_TRUE(fixture.poster.posts.empty());
        fixture.sink.Pump(kStart + WebUploadSink::kFirstBackoffSeconds);
        EXPECT_EQ(fixture.poster.posts.size(), 1U);
    }

    TEST_F(WebUploadSinkTest, AnAnswerThatRetryingCannotFixDropsThatMatchOnly) {
        for (const int status : {kStatusBadRequest, kStatusConflict, kStatusTooLarge}) {
            SinkFixture fixture;
            fixture.sink.Send(Piece("one", 0));
            fixture.sink.Send(Piece("one", 1));
            fixture.sink.Send(Piece("two", 0));
            fixture.sink.Pump(kStart);
            fixture.AnswerAndPump(status, kStart + 1);

            EXPECT_EQ(fixture.sink.Counters().droppedMatches, 1U) << status;
            EXPECT_EQ(fixture.sink.Counters().droppedPieces, 2U);
            ASSERT_EQ(fixture.poster.posts.size(), 2U);
            EXPECT_EQ(fixture.poster.posts[1].path, "/matches/two/0") << "the next match goes on at once";

            fixture.sink.Send(Piece("one", 2));
            EXPECT_EQ(fixture.sink.QueuedPieces(), 1U) << "what still arrives of the dropped match is dropped";
            EXPECT_EQ(fixture.sink.Counters().droppedPieces, 3U);
            fixture.AnswerAndPump(kStatusOk, kStart + 2);
            EXPECT_EQ(fixture.sink.Counters().sent, 1U);
            EXPECT_EQ(fixture.sink.State(), WebUploadState::Active);
        }
    }

    TEST_F(WebUploadSinkTest, UnauthorizedAndNotFoundEndThePostingForTheSession) {
        const std::pair<int, WebUploadState> cases[] = {{kStatusUnauthorized, WebUploadState::Unauthorized}, {kStatusNotFound, WebUploadState::NoStore}};
        for (const auto& [status, state] : cases) {
            SinkFixture fixture;
            fixture.sink.Send(Piece("one", 0));
            fixture.sink.Send(Piece("one", 1));
            fixture.sink.Send(Piece("two", 0));
            fixture.sink.Pump(kStart);
            fixture.AnswerAndPump(status, kStart + 1);

            EXPECT_EQ(fixture.sink.State(), state);
            EXPECT_EQ(fixture.sink.QueuedPieces(), 0U);
            EXPECT_FALSE(fixture.sink.IsPosting());
            fixture.sink.Send(Piece("three", 0));
            fixture.sink.Pump(kStart + 1000);
            fixture.sink.Flush(kStart + 1000);
            EXPECT_EQ(fixture.sink.QueuedPieces(), 0U) << "a later Send queues nothing";
            EXPECT_EQ(fixture.poster.posts.size(), 1U) << "nothing more is posted";
        }
    }

    TEST_F(WebUploadSinkTest, AnOverLargePieceOrABadPathIsDroppedWithoutAPost) {
        SinkFixture fixture;
        fixture.sink.Send(Piece("one", 0));
        UploadPiece big = Piece("one", 1);
        big.lines.assign(kWebPostMaxBodyBytes + 1, 'x');
        fixture.sink.Send(big);
        EXPECT_EQ(fixture.sink.QueuedPieces(), 0U) << "the match is dropped whole, the pieces already queued too";
        EXPECT_EQ(fixture.sink.Counters().droppedMatches, 1U);
        EXPECT_EQ(fixture.sink.Counters().droppedPieces, 2U);

        fixture.sink.Send(Piece("has space", 0));
        EXPECT_EQ(fixture.sink.QueuedPieces(), 0U);
        EXPECT_EQ(fixture.sink.Counters().droppedMatches, 2U);

        fixture.sink.Send(Piece("two", 0));
        fixture.sink.Pump(kStart);
        EXPECT_EQ(fixture.poster.posts.size(), 1U);
        EXPECT_EQ(fixture.poster.posts[0].path, "/matches/two/0");

        UploadPiece exact = Piece("three", 0);
        exact.lines.assign(kWebPostMaxBodyBytes, 'x');
        fixture.sink.Send(exact);
        EXPECT_EQ(fixture.sink.QueuedPieces(), 2U) << "a piece at the limit is fine";
    }

    TEST_F(WebUploadSinkTest, TheQueueIsBoundedAndTheOldestMatchWaitingGoesFirst) {
        SinkFixture fixture;
        // Pieces count: 64 in all; the 65th pushes the oldest match out
        fixture.sink.Send(Piece("old", 0));
        fixture.sink.Send(Piece("old", 1));
        for (uint32_t index = 0; index < WebUploadSink::kMaxQueuedPieces - 2; ++index) {
            fixture.sink.Send(Piece("new", index));
        }
        EXPECT_EQ(fixture.sink.QueuedPieces(), WebUploadSink::kMaxQueuedPieces);
        EXPECT_EQ(fixture.sink.Counters().droppedMatches, 0U);
        fixture.sink.Send(Piece("new", WebUploadSink::kMaxQueuedPieces));
        EXPECT_EQ(fixture.sink.Counters().droppedMatches, 1U);
        EXPECT_EQ(fixture.sink.QueuedPieces(), WebUploadSink::kMaxQueuedPieces - 1);
        fixture.sink.Pump(kStart);
        EXPECT_EQ(fixture.poster.posts[0].path, "/matches/new/0");

        // Bytes: pieces of 1 MiB, four fit
        SinkFixture heavy;
        UploadPiece megabyte = Piece("m", 0);
        megabyte.lines.assign(1024 * 1024 - 1, 'x');
        megabyte.lines.push_back('\n');
        for (uint32_t index = 0; index < 4; ++index) {
            megabyte.index = index;
            megabyte.matchId = index < 2 ? "first" : "second";
            heavy.sink.Send(megabyte);
        }
        EXPECT_EQ(heavy.sink.QueuedPieces(), 4U);
        megabyte.matchId = "third";
        megabyte.index = 0;
        heavy.sink.Send(megabyte);
        EXPECT_EQ(heavy.sink.QueuedPieces(), 3U) << "the first match, 2 MiB, is pushed out for the 5th MiB";
        EXPECT_EQ(heavy.sink.Counters().droppedMatches, 1U);
        EXPECT_EQ(heavy.sink.Counters().droppedPieces, 2U);

        // A match alone that outgrows the bound is dropped itself
        SinkFixture alone;
        for (uint32_t index = 0; index < WebUploadSink::kMaxQueuedPieces + 1; ++index) {
            alone.sink.Send(Piece("only", index));
        }
        EXPECT_EQ(alone.sink.QueuedPieces(), 0U);
        EXPECT_EQ(alone.sink.Counters().droppedMatches, 1U);
        alone.sink.Send(Piece("only", WebUploadSink::kMaxQueuedPieces + 1));
        EXPECT_EQ(alone.sink.QueuedPieces(), 0U);
    }

    TEST_F(WebUploadSinkTest, DroppingTheMatchInFlightForgetsItsAnswer) {
        SinkFixture fixture;
        fixture.sink.Send(Piece("old", 0));
        fixture.sink.Pump(kStart);
        ASSERT_TRUE(fixture.sink.IsPosting());
        for (uint32_t index = 0; index < WebUploadSink::kMaxQueuedPieces; ++index) {
            fixture.sink.Send(Piece("new", index));
        }
        EXPECT_FALSE(fixture.sink.IsPosting());
        EXPECT_EQ(fixture.sink.Counters().droppedMatches, 1U);
        fixture.sink.Pump(kStart + 1);
        ASSERT_EQ(fixture.poster.posts.size(), 2U);
        EXPECT_EQ(fixture.poster.posts[1].path, "/matches/new/0");
    }

    TEST_F(WebUploadSinkTest, FlushPostsWhatIsQueuedIgnoringTheBackoffAndTheAnswers) {
        SinkFixture fixture;
        fixture.sink.Send(Piece("abc", 0));
        fixture.sink.Pump(kStart);
        fixture.AnswerAndPump(kStatusUnavailable, kStart + 1);
        EXPECT_EQ(fixture.poster.posts.size(), 1U);
        fixture.sink.Send(Piece("abc", 1));
        fixture.sink.Send(Piece("abc", 2, "", UploadPieceEnd::Abandoned));
        fixture.sink.Pump(kStart + 2);
        EXPECT_EQ(fixture.poster.posts.size(), 1U) << "the back-off holds";

        fixture.sink.Flush(kStart + 2);
        ASSERT_EQ(fixture.poster.posts.size(), 4U);
        EXPECT_EQ(fixture.poster.posts[1].path, "/matches/abc/0");
        EXPECT_EQ(fixture.poster.posts[2].path, "/matches/abc/1");
        EXPECT_EQ(fixture.poster.posts[3].path, "/matches/abc/2");
        EXPECT_EQ(fixture.sink.QueuedPieces(), 0U);
        EXPECT_EQ(fixture.sink.Counters().flushed, 3U);

        // A post in flight is not posted twice
        SinkFixture busy;
        busy.sink.Send(Piece("abc", 0));
        busy.sink.Send(Piece("abc", 1));
        busy.sink.Pump(kStart);
        busy.sink.Flush(kStart);
        ASSERT_EQ(busy.poster.posts.size(), 2U);
        EXPECT_EQ(busy.poster.posts[1].path, "/matches/abc/1");
    }

    TEST_F(WebUploadSinkTest, AnUnavailablePosterQueuesAndPostsNothing) {
        SinkFixture fixture;
        fixture.poster.available = false;
        fixture.sink.Send(Piece("abc", 0));
        EXPECT_EQ(fixture.sink.QueuedPieces(), 0U);
        fixture.sink.Pump(kStart);
        fixture.sink.Flush(kStart);
        EXPECT_TRUE(fixture.poster.posts.empty());

        WebUploadSink noPoster;
        noPoster.Send(Piece("abc", 0));
        noPoster.Pump(kStart);
        EXPECT_EQ(noPoster.QueuedPieces(), 0U);

        EngineWebPoster engine;
        EXPECT_FALSE(engine.Available()) << "a desktop build cannot post";
        WebUploadSink real;
        real.SetPoster(&engine);
        real.Send(Piece("abc", 0));
        EXPECT_EQ(real.QueuedPieces(), 0U);
    }

    TEST_F(WebUploadSinkTest, TakingThePosterAwayDropsTheQueue) {
        SinkFixture fixture;
        fixture.sink.Send(Piece("abc", 0));
        fixture.sink.Pump(kStart);
        fixture.sink.SetPoster(nullptr);
        EXPECT_EQ(fixture.sink.QueuedPieces(), 0U);
        EXPECT_FALSE(fixture.sink.IsPosting());
        fixture.sink.Pump(kStart + 1);
    }

    TEST_F(WebUploadSinkTest, ThePostsOfAMatchOfFourComputersAssembleIntoALogThatReplays) {
        SinkFixture fixture;
        Match match;
        TableSession session{match};
        MatchUploader uploader;
        MatchLog log;
        uploader.SetNext(&log);
        uploader.SetSink(&fixture.sink);
        uploader.SetMatchIdSource([] { return eastl::string("0123456789abcdef0123456789abcdef"); });
        uploader.SetConsent(UploadConsent::Granted, true);
        session.SetLogSink(&uploader);
        MahjongSettings settings;
        settings.handsPerMatch = kShortMatchHands;
        ASSERT_TRUE(session.StartMatch(settings, kSeed, kAllComputers).has_value());
        for (int guard = 0; session.GetPhase() != SessionPhase::MatchOver && guard < 200; ++guard) {
            session.RunComputers();
            if (session.GetPhase() == SessionPhase::HandOver) {
                ASSERT_TRUE(session.NextHand().has_value());
            }
        }
        ASSERT_EQ(session.GetPhase(), SessionPhase::MatchOver);

        // The server is unavailable once, then answers; a piece posted twice is the same bytes
        double now = kStart;
        std::map<std::string, std::string> byPath;
        size_t seen = 0;
        for (int frame = 0; frame < 100 && fixture.sink.QueuedPieces() > 0; ++frame) {
            fixture.sink.Pump(now);
            while (fixture.poster.posts.size() > seen) {
                const FakePoster::Post& post = fixture.poster.posts[seen++];
                const auto [at, inserted] = byPath.emplace(post.path, post.body);
                EXPECT_TRUE(inserted || at->second == post.body) << post.path;
            }
            if (fixture.sink.IsPosting()) {
                fixture.poster.Answer(frame == 2 ? kStatusUnavailable : kStatusOk);
            }
            now += WebUploadSink::kFirstBackoffSeconds;
        }
        EXPECT_EQ(fixture.sink.QueuedPieces(), 0U);
        EXPECT_EQ(fixture.sink.Counters().retried, 1U);
        ASSERT_EQ(byPath.size(), static_cast<size_t>(kShortMatchHands) + 1);

        eastl::vector<UploadPiece> pieces;
        for (const auto& [path, body] : byPath) {
            UploadPiece piece;
            piece.matchId = "0123456789abcdef0123456789abcdef";
            piece.index = static_cast<uint32_t>(std::stoul(path.substr(path.rfind('/') + 1)));
            piece.lines.assign(body.data(), body.size());
            pieces.push_back(piece);
        }
        const auto assembled = AssemblePieces(pieces);
        ASSERT_TRUE(assembled.has_value()) << assembled.error().c_str();
        const auto events = ParseMatchLog(std::string_view(assembled->c_str(), assembled->size()));
        ASSERT_TRUE(events.has_value()) << events.error().c_str();
        Match replayed;
        EXPECT_TRUE(ReplayMatchLog(*events, replayed).has_value());
        EXPECT_TRUE(*events == log.Events()) << "what was posted is what the session said";
    }

}  // namespace BECore::Mahjong::Tests
