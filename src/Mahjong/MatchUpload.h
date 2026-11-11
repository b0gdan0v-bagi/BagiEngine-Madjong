#pragma once

#include <Mahjong/Game/MatchLog.h>

namespace BECore::Mahjong {

    /// What the player said about sending the matches. `Unasked` is off, like `Declined`: nothing is kept and nothing leaves.
    CORE_ENUM(UploadConsent, uint8_t, Unasked, Granted, Declined)

    /// How a piece ends the match it belongs to: `Open` more may follow, `Ended` the match was played to its `End`, `Abandoned` it was given up.
    CORE_ENUM(UploadPieceEnd, uint8_t, Open, Ended, Abandoned)

    /**
     * @brief One piece of a match on its way to the store, as plain data. **Nothing else is in it** (no name, no address, no user agent, no
     * time): the origin of the machine is not in a piece, and what the transport adds is its own business.
     */
    struct UploadPiece {
        /// The random id of the match (chosen by the caller of `MatchUploader::SetMatchIdSource`), the same in every piece of it
        eastl::string matchId;
        /// 0 for the first piece of the match, then 1, 2 ...
        uint32_t index = 0;
        /// The JSON lines of the events of this piece, each ending in its `\n`: exactly what `MatchLogWriter` writes for them
        eastl::string lines;
        UploadPieceEnd end = UploadPieceEnd::Open;
        /// False when the choice of the player is not kept anywhere, so it will be asked again at the next visit: the record says so
        bool consentRemembered = false;
    };

    /// Where the pieces go (the network, later). Tests use a fake that keeps them.
    class IMatchUploadSink {
    public:
        virtual ~IMatchUploadSink() = default;
        virtual void Send(const UploadPiece& piece) = 0;
    };

    /**
     * @brief Keeps the match in progress in memory, cut in pieces, and hands the pieces to a sink. A match log sink, so it sits in the chain
     * of a session; every event also goes to `next`, whatever the consent.
     *
     * **Off means nothing leaves.** A match is recorded only if the consent is `Granted` and a sink and a match id source are set **at its
     * `Start`** (and the source gives a non-empty id). Otherwise nothing is buffered and nothing is sent, and no id is drawn.
     *
     * - A piece is cut after each `Settlement` (a closed tab loses at most the last hand) and at `End` (`Ended`). A new `Start`, or
     *   `Abandon()`, while a match is recorded sends what is buffered as an `Abandoned` piece (an empty remainder too, so that the store
     *   learns the match was given up). After an `Ended` or `Abandoned` piece the match is closed.
     * - A consent that is not `Granted` any more, or a sink taken away, in the middle of a match drops the buffer: nothing more leaves,
     *   no `Abandoned` piece. A consent granted in the middle of a match does not record it (a log without its `Start` would not replay):
     *   the next match is recorded.
     *
     * Not thread safe (the thread that drives the table).
     */
    class MatchUploader final : public IMatchLogSink {
    public:
        void OnEvent(const MatchLogEvent& event) override;

        /// Where every event goes first; not owned, may be null
        void SetNext(IMatchLogSink* next) noexcept {
            _next = next;
        }
        /// Where the pieces go; not owned, null for none (a recorded match is dropped)
        void SetSink(IMatchUploadSink* sink);
        /// The choice of the player and whether it is kept for the next visit (copied into every piece sent from now on)
        void SetConsent(UploadConsent consent, bool remembered);
        /// Gives the id of the next recorded match: called once at a `Start` that is recorded. The caller chooses it (random, with nothing
        /// of the seed, the time or the machine in it), as the seed is chosen by the game and never here.
        void SetMatchIdSource(std::function<eastl::string()> source);

        /// The match in progress is given up (the game stops): sends the `Abandoned` piece when one is recorded, else does nothing
        void Abandon();

        [[nodiscard]] UploadConsent Consent() const noexcept {
            return _consent;
        }
        /// Whether the match in progress is being recorded (the state as it was at its `Start`)
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
        std::function<eastl::string()> _idSource;
        bool _recording = false;
        eastl::string _matchId;
        uint32_t _index = 0;
        eastl::string _buffer;
    };

    /**
     * @brief The log of a match from its pieces, for the store side and the tests: the pieces in `index` order, their `lines`
     * concatenated. The error says why not: no pieces, pieces of different matches, an index repeated or missing (the first is 0), or a
     * piece after one that ended the match. A match that stops on an `Open` piece (the tab was closed) is assembled as it is.
     */
    [[nodiscard]] std::expected<eastl::string, eastl::string> AssemblePieces(const eastl::vector<UploadPiece>& pieces);

}  // namespace BECore::Mahjong
