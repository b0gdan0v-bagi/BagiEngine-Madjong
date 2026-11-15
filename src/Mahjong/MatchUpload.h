#pragma once

#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong {

    /// What the player said about sending the matches. `Unasked` is off, like `Declined`: nothing is kept and nothing leaves.
    CORE_ENUM(UploadConsent, uint8_t, Unasked, Granted, Declined)

    /// How a piece ends its match: `Open` more may follow, `Ended` it was played to its `End`, `Abandoned` it was given up.
    CORE_ENUM(UploadPieceEnd, uint8_t, Open, Ended, Abandoned)

    /**
     * @brief One piece of a match on its way to the store, as plain data. **Nothing else is in it** (no name, no address, no user agent,
     * no time); a test pins the member list.
     */
    struct UploadPiece {
        /// The random id of the match, the same in every piece of it (never the seed)
        String matchId;
        uint32_t index = 0;
        /// Exactly the bytes `MatchLogWriter` writes for these events, each line ending in `\n`
        String lines;
        UploadPieceEnd end = UploadPieceEnd::Open;
        /// False when the player's choice is not kept anywhere, so it is asked again at the next visit
        bool consentRemembered = false;
    };

    /// Where the pieces go; tests use a fake that keeps them.
    class IMatchUploadSink {
    public:
        virtual ~IMatchUploadSink() = default;
        virtual void Send(const UploadPiece& piece) = 0;
    };

    /**
     * @brief Keeps the match in progress in memory, cut in pieces, and hands them to a sink (`src/Mahjong/CLAUDE.md`, "Upload buffer").
     * A match log sink: every event also goes to `next`, whatever the consent.
     *
     * **Off means nothing leaves.** A match is recorded only if the consent is `Granted`, `SetRecording` allows it and a sink and a match
     * id source are set **at its `Start`**; otherwise nothing is buffered or sent and no id is drawn. A consent granted mid-match does not
     * record it (a log without its `Start` would not replay); one withdrawn, or a sink taken away, drops the buffer with no `Abandoned`
     * piece. A piece is cut after each `Settlement` (a closed tab loses at most the last hand) and at `End`; a new `Start` or `Abandon()`
     * sends the rest as `Abandoned`, an empty one too, so that the store learns the match was given up.
     *
     * Not thread safe (the thread that drives the table).
     */
    class MatchUploader final : public IMatchLogSink {
    public:
        void OnEvent(const MatchLogEvent& event) override;

        /// Where every event goes first; not owned, may be null.
        void SetNext(IMatchLogSink* next) noexcept {
            _next = next;
        }
        /// Where the pieces go; not owned, null for none (a recorded match is dropped).
        void SetSink(IMatchUploadSink* sink);
        /// The player's choice and whether it is kept for the next visit (copied into every piece sent from now on).
        void SetConsent(UploadConsent consent, bool remembered);
        /// The id of the next recorded match, asked once at a `Start` that is recorded. It must carry nothing of the seed, the time or the
        /// machine.
        void SetMatchIdSource(std::function<String()> source);

        /// Whether the matches that begin from now on may be recorded (read at their `Start`, like `MatchRecorder`). The game turns it off
        /// for the match it deals behind the panel, which nobody plays.
        void SetRecording(bool wanted) noexcept {
            _wanted = wanted;
        }

        /// The game stops: sends the `Abandoned` piece when a match is recorded, else does nothing.
        void Abandon();

        [[nodiscard]] UploadConsent Consent() const noexcept {
            return _consent;
        }
        /// Whether the match in progress is recorded (decided at its `Start`).
        [[nodiscard]] bool IsRecording() const noexcept {
            return _recording;
        }

    private:
        [[nodiscard]] bool CanRecord() const;
        void Drop();
        void Cut(UploadPieceEnd end);

        IMatchLogSink* _next = nullptr;
        IMatchUploadSink* _sink = nullptr;
        UploadConsent _consent = UploadConsent::Unasked;
        bool _remembered = false;
        bool _wanted = true;
        std::function<String()> _idSource;
        bool _recording = false;
        String _matchId;
        uint32_t _index = 0;
        String _buffer;
    };

    /**
     * @brief The log of a match from its pieces (store side and tests): the `lines` in `index` order. The error says why not: no pieces,
     * different matches, an index repeated or missing, or a piece after one that ended the match. A match that stops on an `Open`
     * piece (the tab was closed) is assembled as it is.
     */
    [[nodiscard]] std::expected<String, String> AssemblePieces(const Vector<UploadPiece>& pieces);

}  // namespace BECore::Mahjong
