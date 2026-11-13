#include "WebUploadSink.h"

namespace BECore::Mahjong {

    namespace {

        constexpr int kStatusOkFirst = 200;
        constexpr int kStatusOkLast = 299;
        constexpr int kStatusRequestTimeout = 408;
        constexpr int kStatusUnauthorized = 401;
        constexpr int kStatusNotFound = 404;
        constexpr int kStatusTooManyRequests = 429;
        constexpr int kStatusServerErrorFirst = 500;

    }  // namespace

    bool EngineWebPoster::Available() const {
        return WebPostIsAvailable();
    }

    std::expected<WebPostId, eastl::string> EngineWebPoster::Start(eastl::string_view path, eastl::string_view body) {
        return WebPostStart(path, body, {});
    }

    WebPostResult EngineWebPoster::Poll(WebPostId id) {
        return WebPostPoll(id);
    }

    void WebUploadSink::SetPoster(IWebPoster* poster) {
        if (poster == _poster) {
            return;
        }
        _poster = poster;
        // An id belongs to the poster that gave it, and without a poster nothing waits to be sent
        _inFlight = 0;
        if (_poster == nullptr) {
            _counters.droppedPieces += static_cast<uint32_t>(_queue.size());
            _queue.clear();
        }
    }

    void WebUploadSink::Send(const UploadPiece& piece) {
        if (_state != WebUploadState::Active || _poster == nullptr || !_poster->Available()) {
            return;
        }
        if (!_droppedMatchId.empty() && piece.matchId == _droppedMatchId) {
            ++_counters.droppedPieces;
            return;
        }
        BuildPath(piece);
        if (piece.lines.size() > kWebPostMaxBodyBytes || !IsWebPostPathValid(_path)) {
            LOG_WARNING(Format("WebUploadSink: piece {} of match {} cannot be posted (too large or a bad path): the match is dropped", piece.index, piece.matchId));
            ++_counters.droppedPieces;
            DropMatch(piece.matchId);
            return;
        }
        _queue.push_back(piece);
        EnforceBounds();
    }

    void WebUploadSink::Pump(double nowSeconds) {
        if (_state != WebUploadState::Active || _queue.empty() || _poster == nullptr) {
            return;
        }
        if (_inFlight != 0) {
            const WebPostResult result = _poster->Poll(_inFlight);
            if (result.status == WebPostStatus::Pending) {
                return;
            }
            _inFlight = 0;
            OnAnswer(result, nowSeconds);
        }
        // Each pass posts the front piece, drops a match, or sets a retry time in the future, so the loop ends
        while (_state == WebUploadState::Active && _inFlight == 0 && !_queue.empty() && nowSeconds >= _retryAt) {
            StartFront(nowSeconds);
        }
    }

    void WebUploadSink::Flush([[maybe_unused]] double nowSeconds) {
        if (_state != WebUploadState::Active || _poster == nullptr) {
            return;
        }
        for (size_t position = _inFlight != 0 ? 1 : 0; position < _queue.size(); ++position) {
            BuildPath(_queue[position]);
            if (!_poster->Start(_path, _queue[position].lines).has_value()) {
                _counters.droppedPieces += static_cast<uint32_t>(_queue.size() - position);  // refused: these are lost, and counted
                break;
            }
            ++_counters.flushed;
        }
        _queue.clear();
        _inFlight = 0;
    }

    void WebUploadSink::StartFront(double nowSeconds) {
        const UploadPiece& piece = _queue.front();
        BuildPath(piece);
        const std::expected<WebPostId, eastl::string> started = _poster->Start(_path, piece.lines);
        if (!started.has_value()) {
            LOG_WARNING(Format("WebUploadSink: the post of piece {} was refused ({}): again later", piece.index, started.error()));
            RetryLater(nowSeconds);
            return;
        }
        _inFlight = *started;
    }

    void WebUploadSink::OnAnswer(const WebPostResult& result, double nowSeconds) {
        if (result.status != WebPostStatus::Done) {
            RetryLater(nowSeconds);
            return;
        }
        const int status = result.httpStatus;
        if (status >= kStatusOkFirst && status <= kStatusOkLast) {
            ++_counters.sent;
            _backoff = kFirstBackoffSeconds;
            _queue.erase(_queue.begin());
        } else if (status == kStatusUnauthorized) {
            Disable(WebUploadState::Unauthorized);
        } else if (status == kStatusNotFound) {
            Disable(WebUploadState::NoStore);
        } else if (status == kStatusRequestTimeout || status == kStatusTooManyRequests || status >= kStatusServerErrorFirst) {
            RetryLater(nowSeconds);
        } else {
            LOG_WARNING(Format("WebUploadSink: the server answered {} to piece {} of match {}: the match is dropped", status, _queue.front().index, _queue.front().matchId));
            DropMatch(_queue.front().matchId);
        }
    }

    void WebUploadSink::RetryLater(double nowSeconds) {
        ++_counters.retried;
        _retryAt = nowSeconds + _backoff;
        _backoff = std::min(_backoff * 2.0, kMaxBackoffSeconds);
    }

    void WebUploadSink::DropMatch(eastl::string matchId) {
        const size_t before = _queue.size();
        if (!_queue.empty() && _inFlight != 0 && _queue.front().matchId == matchId) {
            _inFlight = 0;  // the answer, when it comes, is nobody's
        }
        _queue.erase(std::remove_if(_queue.begin(), _queue.end(), [&matchId](const UploadPiece& queued) { return queued.matchId == matchId; }), _queue.end());
        _counters.droppedPieces += static_cast<uint32_t>(before - _queue.size());
        ++_counters.droppedMatches;
        _droppedMatchId = matchId;
    }

    void WebUploadSink::Disable(WebUploadState reason) {
        LOG_WARNING(Format("WebUploadSink: the server answered {}: nothing more is sent in this session", EnumUtils<WebUploadState>::ToString(reason).data()));
        _state = reason;
        _counters.droppedPieces += static_cast<uint32_t>(_queue.size());
        _queue.clear();
        _inFlight = 0;
    }

    void WebUploadSink::EnforceBounds() {
        while (!_queue.empty() && (_queue.size() > kMaxQueuedPieces || QueuedBytes() > kMaxQueuedBytes)) {
            LOG_WARNING(Format("WebUploadSink: the queue is full: match {} is dropped", _queue.front().matchId));
            DropMatch(_queue.front().matchId);
        }
    }

    void WebUploadSink::BuildPath(const UploadPiece& piece) {
        _path = Format("/matches/{}/{}", piece.matchId, piece.index);
    }

    size_t WebUploadSink::QueuedBytes() const {
        size_t bytes = 0;
        for (const UploadPiece& piece : _queue) {
            bytes += piece.lines.size();
        }
        return bytes;
    }

}  // namespace BECore::Mahjong
