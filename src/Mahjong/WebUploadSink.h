#pragma once

#include <CoreSDL/SDLWebPost.h>
#include <Mahjong/MatchUpload.h>

namespace BECore::Mahjong {

    /// The post call of the page behind a seam, so a test drives every answer. The engine's own is `EngineWebPoster`.
    class IWebPoster {
    public:
        virtual ~IWebPoster() = default;
        /// Whether a post can be made at all (a browser page with `window.bagiWebPost`)
        [[nodiscard]] virtual bool Available() const = 0;
        /// Starts a POST of `body` to `path` and returns at once; the error names the refusal
        [[nodiscard]] virtual std::expected<WebPostId, eastl::string> Start(eastl::string_view path, eastl::string_view body) = 0;
        /// `Pending` until the answer arrives, then `Done` (with the HTTP status) or `Failed`, once
        [[nodiscard]] virtual WebPostResult Poll(WebPostId id) = 0;
    };

    /// `WebPostStart` / `WebPostPoll` / `WebPostIsAvailable` of the engine (content type empty: `application/x-ndjson`)
    class EngineWebPoster final : public IWebPoster {
    public:
        [[nodiscard]] bool Available() const override;
        [[nodiscard]] std::expected<WebPostId, eastl::string> Start(eastl::string_view path, eastl::string_view body) override;
        [[nodiscard]] WebPostResult Poll(WebPostId id) override;
    };

    /// Whether the sink still posts, and why not: `Unauthorized` (401: the password is refused) and `NoStore` (404: no store behind the
    /// service) end the posting for the session
    CORE_ENUM(WebUploadState, uint8_t, Active, Unauthorized, NoStore)

    /// What the sink did since it was made, for the tests and the log
    struct WebUploadCounters {
        uint32_t sent = 0;            ///< pieces the server answered 200
        uint32_t retried = 0;         ///< posts that are made again later (429, 5xx, no answer)
        uint32_t droppedMatches = 0;  ///< matches given up (refused for good, too big, or pushed out of the queue)
        uint32_t droppedPieces = 0;   ///< the pieces of those matches that were queued or arrived later
        uint32_t flushed = 0;         ///< pieces posted by `Flush` whose answer nobody reads
    };

    /**
     * @brief The sink of the match upload that posts the pieces to the page's own server: `POST /matches/<matchId>/<index>` with the
     * `lines` as the body (an empty `lines`, the marker of a match given up, is an empty body, which the server stores).
     *
     * Plain C++ over `IWebPoster`; the clock is a parameter (seconds, any origin) so a test needs no real time. Not thread safe (the
     * browser's main thread).
     * - **In order, one at a time:** the server wants piece 0 to start a match, so a piece is posted only when the one before it was
     *   answered. A piece stays at the front of the queue until its post is answered.
     * - **Answers:** 2xx the piece is done. 408, 429, 5xx and a failed post (network) post the same piece again after a back-off that
     *   starts at `kFirstBackoffSeconds` and doubles to `kMaxBackoffSeconds` (the same bytes sent again are a 200 for the server). 401 and
     *   404 disable the sink for the session: the queue is dropped and nothing is posted or queued any more. Every other answer
     *   (400, 409, 413 ...) is one that retrying cannot fix: that match is dropped, its queued pieces and the ones that still arrive
     *   too, and the next match goes on. A piece over `kWebPostMaxBodyBytes` or with a path the page refuses is dropped the same way,
     *   without a post.
     * - **Bounds:** at most `kMaxQueuedPieces` pieces and `kMaxQueuedBytes` of lines wait (a tester who is offline for hours must not
     *   grow memory without end); beyond that the oldest match waiting is dropped whole and counted.
     * - **A poster that is not `Available()`** (a desktop build, a page without the script) queues nothing and posts nothing.
     * - **A steady frame allocates nothing:** an empty queue with nothing in flight is a return at the top of `Pump`; the queue and the
     *   path are members that keep their capacity.
     *
     * `Flush` is for the game that is stopping: it posts every piece waiting now, in order, without waiting for answers or the back-off,
     * and empties the queue. The engine's `keepalive` lets a post outlive a closing tab only up to 60000 bytes of body, so a bigger piece
     * may not arrive then; and the posts of one flush may reach the server in any order, which is a risk only for a match whose first
     * pieces are still waiting.
     */
    class WebUploadSink final : public IMatchUploadSink {
    public:
        static constexpr size_t kMaxQueuedPieces = 64;
        static constexpr size_t kMaxQueuedBytes = 4 * 1024 * 1024;
        static constexpr double kFirstBackoffSeconds = 60.0;
        static constexpr double kMaxBackoffSeconds = 600.0;

        /// The page's post call; not owned, null for none (nothing is queued)
        void SetPoster(IWebPoster* poster);

        void Send(const UploadPiece& piece) override;

        /// Once a frame: reads the answer of the post in flight and starts the next post when it is due
        void Pump(double nowSeconds);

        /// The game is stopping: posts what is queued now, ignoring the back-off and not waiting for answers
        void Flush(double nowSeconds);

        [[nodiscard]] WebUploadState State() const noexcept {
            return _state;
        }
        [[nodiscard]] const WebUploadCounters& Counters() const noexcept {
            return _counters;
        }
        [[nodiscard]] size_t QueuedPieces() const noexcept {
            return _queue.size();
        }
        /// Whether a post is waiting for its answer
        [[nodiscard]] bool IsPosting() const noexcept {
            return _inFlight != 0;
        }

    private:
        void StartFront(double nowSeconds);
        void OnAnswer(const WebPostResult& result, double nowSeconds);
        void RetryLater(double nowSeconds);
        /// By value: the caller often names the id of a queued piece, which the removal moves
        void DropMatch(eastl::string matchId);
        void Disable(WebUploadState reason);
        void EnforceBounds();
        void BuildPath(const UploadPiece& piece);
        [[nodiscard]] size_t QueuedBytes() const;

        IWebPoster* _poster = nullptr;
        eastl::vector<UploadPiece> _queue;
        WebPostId _inFlight = 0;
        double _retryAt = 0.0;
        double _backoff = kFirstBackoffSeconds;
        WebUploadState _state = WebUploadState::Active;
        WebUploadCounters _counters;
        /// The match dropped last: what still arrives of it is dropped (only one match produces pieces at a time)
        eastl::string _droppedMatchId;
        eastl::string _path;
    };

}  // namespace BECore::Mahjong
