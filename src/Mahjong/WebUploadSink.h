#pragma once

#include <CoreSDL/SDLWebPost.h>
#include <Mahjong/MatchUpload.h>

namespace BECore::Mahjong {

    /// The post call of the page behind a seam, so a test drives every answer.
    class IWebPoster {
    public:
        virtual ~IWebPoster() = default;
        /// Whether a post can be made at all (a browser page with `window.bagiWebPost`).
        [[nodiscard]] virtual bool Available() const = 0;
        /// Returns at once; the error names the refusal.
        [[nodiscard]] virtual std::expected<WebPostId, String> Start(StringView path, StringView body) = 0;
        /// `Pending` until the answer arrives, then `Done` (with the HTTP status) or `Failed`, once.
        [[nodiscard]] virtual WebPostResult Poll(WebPostId id) = 0;
    };

    /// The engine's `WebPostStart` / `WebPostPoll` (empty content type: `application/x-ndjson`).
    class EngineWebPoster final : public IWebPoster {
    public:
        [[nodiscard]] bool Available() const override;
        [[nodiscard]] std::expected<WebPostId, String> Start(StringView path, StringView body) override;
        [[nodiscard]] WebPostResult Poll(WebPostId id) override;
    };

    /// Whether the sink still posts; `Unauthorized` (401) and `NoStore` (404) end the posting for the session.
    CORE_ENUM(WebUploadState, uint8_t, Active, Unauthorized, NoStore)

    /// What the sink did since it was made, for the tests and the log.
    struct WebUploadCounters {
        uint32_t sent = 0;
        uint32_t retried = 0;
        uint32_t droppedMatches = 0;  ///< refused for good, too big, or pushed out of the queue
        uint32_t droppedPieces = 0;   ///< the pieces of those matches that were queued or arrived later
        uint32_t flushed = 0;         ///< posted by `Flush`, whose answer nobody reads
    };

    /**
     * @brief The sink of the match upload: posts each piece to the page's own server (`src/Mahjong/CLAUDE.md`, "Upload sink") as
     * `POST /matches/<matchId>/<index>` with the `lines` as the body. It only gets what `MatchUploader` records, which is nothing unless
     * the consent is `Granted` (`UploadPrefs`; `Unasked` is off); an empty body marks a match given up.
     *
     * Plain C++ over `IWebPoster`; the clock is a parameter (seconds, any origin) so a test needs no real time. Not thread safe (the
     * browser's main thread).
     * - **In order, one at a time:** the server wants piece 0 to start a match, so a piece stays at the front of the queue until its
     *   post is answered.
     * - **Answers:** a failed post and 408, 429, 5xx post the same piece again after a back-off from `kFirstBackoffSeconds` doubling to
     *   `kMaxBackoffSeconds` (the same bytes again are a 200 for the server). 401 and 404 disable the sink for the session and drop the
     *   queue. Every other refusal cannot be fixed by retrying: that match is dropped, what still arrives of it too, and the next match
     *   goes on. A piece over `kWebPostMaxBodyBytes` or with a path the page refuses is dropped the same way, without a post.
     * - **Bounds:** at most `kMaxQueuedPieces` pieces and `kMaxQueuedBytes` of lines wait, so a tester offline for hours does not grow
     *   memory without end; beyond that the oldest match waiting is dropped whole and counted.
     * - **A steady frame allocates nothing:** an empty queue with nothing in flight returns at the top of `Pump`; the queue and the path
     *   keep their capacity.
     *
     * `Flush` is for the game that is stopping: it posts every waiting piece without waiting for answers or the back-off; a piece the
     * post call refuses is lost and counted in `droppedPieces`. The engine's `keepalive` carries a body only up to 60000 bytes, so a
     * bigger piece may not outlive a closing tab; the posts of one flush may arrive in any order, a risk only for a match whose first
     * pieces are still waiting.
     */
    class WebUploadSink final : public IMatchUploadSink {
    public:
        static constexpr size_t kMaxQueuedPieces = 64;
        static constexpr size_t kMaxQueuedBytes = 4 * 1024 * 1024;
        static constexpr double kFirstBackoffSeconds = 60.0;
        static constexpr double kMaxBackoffSeconds = 600.0;

        /// The page's post call; not owned, null for none or one that is not `Available()` (nothing is queued).
        void SetPoster(IWebPoster* poster);

        void Send(const UploadPiece& piece) override;

        /// Once a frame.
        void Pump(double nowSeconds);

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
        [[nodiscard]] bool IsPosting() const noexcept {
            return _inFlight != 0;
        }

    private:
        void StartFront(double nowSeconds);
        void OnAnswer(const WebPostResult& result, double nowSeconds);
        void RetryLater(double nowSeconds);
        /// By value: the caller often names the id of a queued piece, which the removal moves
        void DropMatch(String matchId);
        void Disable(WebUploadState reason);
        void EnforceBounds();
        void BuildPath(const UploadPiece& piece);
        [[nodiscard]] size_t QueuedBytes() const;

        IWebPoster* _poster = nullptr;
        Vector<UploadPiece> _queue;
        WebPostId _inFlight = 0;
        double _retryAt = 0.0;
        double _backoff = kFirstBackoffSeconds;
        WebUploadState _state = WebUploadState::Active;
        WebUploadCounters _counters;
        /// The match dropped last: what still arrives of it is dropped (only one match produces pieces at a time)
        String _droppedMatchId;
        String _path;
    };

}  // namespace BECore::Mahjong
