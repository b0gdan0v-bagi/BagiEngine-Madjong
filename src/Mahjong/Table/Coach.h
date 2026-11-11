#pragma once

#include <Mahjong/Table/Hints.h>

namespace BECore::Mahjong {

    /**
     * The coach: after an accepted action of a person that is not what the computer player would have done, a short note says what it
     * would have done and what the two cost. Plain C++ over the `TableView` the person had **before** the action (`MakeTableView`), so
     * the judge knows nothing the person did not see; no scene, no game. The words are keys `coach.*` of `config/UIStrings.xml`.
     * The judge (`JudgeMove`) and the words (`CoachNoteLines`) are here; the ledger (`CoachLedger`) holds the note and the tally for
     * the table (`MahjongGame` owns one, the components read it).
     */

    /// What the hints of the table do: nothing, the hints (`Hint`, the waiting row, the marks), or the hints and the coach's notes. Kept on
    /// the game and in the saved preferences, never a rule of the match and unknown to the control API.
    CORE_ENUM(HintsMode, uint8_t, Off, Hints, Coach)

    inline constexpr HintsMode kDefaultHintsMode = HintsMode::Hints;

    /// The id of a mode in the saved preferences, the variable `hintsChoice` and the values of the buttons of the page: `off`, `hints`, `coach`.
    [[nodiscard]] constexpr std::string_view HintsModeText(HintsMode mode) noexcept {
        switch (mode) {
            case HintsMode::Off:
                return "off";
            case HintsMode::Coach:
                return "coach";
            case HintsMode::Hints:
                break;
        }
        return "hints";
    }

    /// The mode of an id, or nothing for any other text (client text, a damaged save).
    [[nodiscard]] constexpr std::optional<HintsMode> TryParseHintsMode(std::string_view text) noexcept {
        for (const HintsMode mode : EnumUtils<HintsMode>::Values()) {
            if (HintsModeText(mode) == text) {
                return mode;
            }
        }
        return std::nullopt;
    }

    /// The most sentences of a note: what the computer would have done, your move with its numbers, the computer's with its numbers.
    inline constexpr size_t kMaxCoachLines = 3;

    /// The most letters a line of a note may have in any language, the budget the panel of the table is made for.
    inline constexpr size_t kMaxCoachLineLetters = 96;

    /// What the numbers of one side of a note are: none (a pass, a complete hand), the steps to a ready hand after the move with the unseen
    /// copies that help (`Rated`), or the unseen copies of the tiles a ready hand waits on (`Waiting`).
    CORE_ENUM(CoachNumbers, uint8_t, None, Rated, Waiting)

    /// The numbers of one move: `steps` and `copies` for `Rated` (the shanten and the acceptance of the hand after it), `copies` for `Waiting`.
    struct CoachSide {
        CoachNumbers numbers = CoachNumbers::None;
        int steps = 0;
        int copies = 0;

        bool operator==(const CoachSide&) const = default;
    };

    /**
     * @brief A move that differs from the computer's choice: plain data, no text (`CoachNoteLines` words it).
     *
     * `pending` is the discard of a claim window (invalid in a turn). `stepsBefore` is the shanten of the hand as it was (of the 14 tiles
     * of a turn, of the hand of a window). `handWaits`: the advice is to pass a claim because the hand waits (and not for lack of gain).
     */
    struct CoachNote {
        Seat seat = kNoSeat;
        HandAction advised;
        HandAction taken;
        Tile pending;
        int stepsBefore = 0;
        bool handWaits = false;
        CoachSide advisedSide;
        CoachSide takenSide;

        bool operator==(const CoachNote&) const = default;
    };

    /// Whether `taken` is something `before` can judge: `before.legal` lists it.
    [[nodiscard]] bool CanJudge(const TableView& before, const HandAction& taken);

    /**
     * @brief Whether an accepted action of a person agrees with the computer's choice on the same view; the note when it does not.
     *
     * The advice is `AdviceFor(before)` with the numbers of `ExplainAdviceFor`. A move agrees when it is the advice, or has the same
     * numbers, rated by the chooser's own pieces (no second rule): a discard of a kind with the shanten and the acceptance of the advised
     * kind; for a declaring or keeping-waiting turn the waiting copies, and only a declaring discard declared (a declaring advice with
     * the same tile not declared differs); a claim with the shanten and the acceptance after it of the advised claim; a pass when the
     * advice is a pass; a kong when the advice is a kong and the hand without it is no worse (`KongIsNoWorse`). The advice of a
     * complete hand (`DeclareWin`, a mahjong claim) differs from anything else. nullopt when it agrees and when there is nothing to
     * judge (`CanJudge` false).
     */
    [[nodiscard]] std::optional<CoachNote> JudgeMove(const TableView& before, const HandAction& taken);

    /// The sentences of `note` in the language of `strings`, at most `kMaxCoachLines`, none empty: the computer's choice (`AdviceText`),
    /// your move with its numbers, the computer's with its numbers (a complete hand says so in one more line instead). One key `coach.*`
    /// per sentence, tiles by `TileName`, a number never in a plural form.
    [[nodiscard]] eastl::vector<eastl::string> CoachNoteLines(const UIStrings& strings, const CoachNote& note);

    /// The moves that agreed with the computer's choice and all the moves judged.
    struct CoachTally {
        int agreed = 0;
        int moves = 0;

        bool operator==(const CoachTally&) const = default;
    };

    /// One hand of one match, as `MahjongTableComponent::SeenHand` tells it: a replayed drawn hand is not the hand it replays.
    struct CoachHandKey {
        uint64_t starts = 0;
        int handsPlayed = 0;
        size_t settled = 0;

        bool operator==(const CoachHandKey&) const = default;
    };

    /// The key of the hand a move was made in.
    [[nodiscard]] constexpr CoachHandKey HandKeyOf(const MoveContext& context) noexcept {
        return {context.startCount, context.handsPlayed, context.historySize};
    }

    /**
     * @brief The note and the tally of the coach, fed by the session (`TableSession::SetMoveObserver`).
     *
     * Records only while enabled and with exactly one person at the table (two would see each other's hand in a note). Every recorded move
     * drops the note before it and judges itself (`JudgeMove`): an agreeing one counts, a differing one counts and keeps its note. A new
     * hand starts the hand tally over, a new match both. `SetEnabled` with a changed value drops the note and the tallies (a mode change
     * starts no tally of the past); while disabled nothing is held. `Version()` grows whenever the note changes, so a front end
     * publishes only when it moved.
     */
    class CoachLedger final : public IPersonMoveObserver {
    public:
        void SetEnabled(bool enabled);
        [[nodiscard]] bool Enabled() const noexcept {
            return _enabled;
        }

        void OnPersonMove(const TableView& before, const HandAction& action, const MoveContext& context) override;

        /// The note of the last recorded move, empty when it agreed, when it was dropped or while disabled.
        [[nodiscard]] const std::optional<CoachNote>& Note() const noexcept {
            return _note;
        }
        void ClearNote();
        /// Grows with every change of `Note()`.
        [[nodiscard]] uint64_t Version() const noexcept {
            return _version;
        }

        /// The tally of the last hand a move was recorded in and of its match; nothing before the first one.
        [[nodiscard]] CoachTally HandTally() const noexcept {
            return _hand;
        }
        [[nodiscard]] CoachTally MatchTally() const noexcept {
            return _match;
        }
        /// The same, but nothing unless that hand (that match, by `TableSession::StartCount()`) is the one recorded in: a hand a person made
        /// no move in has no tally, not the last one's.
        [[nodiscard]] CoachTally HandTallyOf(const CoachHandKey& key) const noexcept {
            return _keyed && key == _key ? _hand : CoachTally{};
        }
        [[nodiscard]] CoachTally MatchTallyOf(uint64_t startCount) const noexcept {
            return _keyed && startCount == _key.starts ? _match : CoachTally{};
        }

    private:
        bool _enabled = false;
        std::optional<CoachNote> _note;
        uint64_t _version = 0;
        bool _keyed = false;
        CoachHandKey _key;
        CoachTally _hand;
        CoachTally _match;
    };

}  // namespace BECore::Mahjong
