#include "MatchUpload.h"

#include <Mahjong/MatchLogJson.h>

namespace BECore::Mahjong {

    void MatchUploader::OnEvent(const MatchLogEvent& event) {
        if (_next != nullptr) {
            _next->OnEvent(event);
        }
        if (event.kind == MatchLogKind::Start) {
            if (_recording) {
                Cut(UploadPieceEnd::Abandoned);
            }
            if (CanRecord()) {
                _matchId = _idSource();
                _recording = !_matchId.empty();
                _index = 0;
                _buffer.clear();
            }
        }
        if (!_recording) {
            return;
        }
        _buffer.append(EventToJson(event).dump().c_str());
        _buffer.push_back('\n');
        if (event.kind == MatchLogKind::Settlement) {
            Cut(UploadPieceEnd::Open);
        } else if (event.kind == MatchLogKind::End) {
            Cut(UploadPieceEnd::Ended);
        }
    }

    void MatchUploader::SetSink(IMatchUploadSink* sink) {
        _sink = sink;
        if (_sink == nullptr) {
            Drop();
        }
    }

    void MatchUploader::SetConsent(UploadConsent consent, bool remembered) {
        _consent = consent;
        _remembered = remembered;
        if (_consent != UploadConsent::Granted) {
            Drop();
        }
    }

    void MatchUploader::SetMatchIdSource(std::function<eastl::string()> source) {
        _idSource = std::move(source);
        if (!_idSource) {
            Drop();
        }
    }

    void MatchUploader::Abandon() {
        if (_recording) {
            Cut(UploadPieceEnd::Abandoned);
        }
    }

    bool MatchUploader::CanRecord() const {
        return _wanted && _consent == UploadConsent::Granted && _sink != nullptr && static_cast<bool>(_idSource);
    }

    void MatchUploader::Drop() {
        _recording = false;
        _buffer.clear();
    }

    void MatchUploader::Cut(UploadPieceEnd end) {
        UploadPiece piece;
        piece.matchId = _matchId;
        piece.index = _index++;
        piece.lines = std::move(_buffer);
        piece.end = end;
        piece.consentRemembered = _remembered;
        _buffer.clear();
        if (end != UploadPieceEnd::Open) {
            _recording = false;
        }
        _sink->Send(piece);
    }

    std::expected<eastl::string, eastl::string> AssemblePieces(const eastl::vector<UploadPiece>& pieces) {
        if (pieces.empty()) {
            return std::unexpected(eastl::string("no pieces"));
        }
        eastl::vector<const UploadPiece*> ordered;
        for (const UploadPiece& piece : pieces) {
            if (piece.matchId != pieces.front().matchId) {
                return std::unexpected(eastl::string("the pieces are of different matches"));
            }
            ordered.push_back(&piece);
        }
        std::stable_sort(ordered.begin(), ordered.end(), [](const UploadPiece* left, const UploadPiece* right) { return left->index < right->index; });
        eastl::string log;
        for (size_t position = 0; position < ordered.size(); ++position) {
            const UploadPiece& piece = *ordered[position];
            if (position > 0 && piece.index == ordered[position - 1]->index) {
                return std::unexpected(Format("piece {} is there twice", piece.index));
            }
            if (piece.index != position) {
                return std::unexpected(Format("piece {} is missing", position));
            }
            if (position > 0 && ordered[position - 1]->end != UploadPieceEnd::Open) {
                return std::unexpected(Format("piece {} comes after the piece that ended the match", piece.index));
            }
            log.append(piece.lines);
        }
        return log;
    }

}  // namespace BECore::Mahjong
