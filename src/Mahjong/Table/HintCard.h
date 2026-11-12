#pragma once

#include <EASTL/array.h>
#include <Mahjong/Table/Coach.h>

namespace BECore::Mahjong {

    /**
     * The card of the coach (the panel `CoachCard` of the node `HintsWhy`): one table in the middle of the wall ring that says, in the mode
     * Coach, either the advice of the viewer's table with its numbers (`MakeAdviceCard`) or the note of the last differing move
     * (`MakeNoteCard`), with the tally of the coach under both. Plain C++ over an `Explanation` or a `CoachNote`, no scene: the numbers
     * are those of the chooser (`Game/ComputerExplain.h`, `JudgeMove`), nothing is computed again, so no card knows more than the view.
     *
     * The card is a title (the sentence of the advice; a chow by its kind only), a head row (the steps to a ready hand now), at most `kMaxCardRows` rows of moves
     * (who, the tiles as pictures, a word for the kind, the steps to a ready hand after the move, the unseen tiles that help) and at most
     * `kMaxCardFooter` lines under them (why, in words, then the tally). The words are keys `hints.card.*`, `hints.why.*` and `coach.*` of
     * `config/UIStrings.xml`; a number never takes a plural form.
     */

    /// The most rows of moves: the advice and the two alternatives of `Explanation`, or the advice and the person's move of a note.
    inline constexpr size_t kMaxCardRows = 3;
    /// The most tile pictures of one row: the three tiles of a chow.
    inline constexpr size_t kMaxCardTiles = 3;
    /// The most lines under the rows, the tally included.
    inline constexpr size_t kMaxCardFooter = 2;

    /// Whose move a row is: the computer's choice, one of the next best, or the person's own (only in a note).
    CORE_ENUM(CardRowKind, uint8_t, Best, Other, Yours)

    /// How the steps to a ready hand after a row's move compare with the steps now: fewer, the same, more; `None` for a row with no steps.
    CORE_ENUM(CardTrend, uint8_t, None, Closer, Same, Farther)

    /// One move of the card. `tiles` are the pictures, the invalid ones left out; `steps` and `useful` are empty when the move has no such
    /// number (a pass that gains nothing has no unseen tiles that help).
    struct CardRow {
        CardRowKind kind = CardRowKind::Other;
        eastl::array<Tile, kMaxCardTiles> tiles{};
        eastl::string who;
        eastl::string move;
        eastl::string steps;
        eastl::string useful;
        CardTrend trend = CardTrend::None;
    };

    /// The whole card in the language it was made in. `now` is empty when there are no rows (a complete hand: nothing to compare).
    struct HintCard {
        eastl::string title;
        eastl::string now;
        eastl::vector<CardRow> rows;
        eastl::vector<eastl::string> footer;
    };

    /// The card of the advice of `explained` on `view` (`ExplainAdviceFor(view)`), with `tally` (`CardTallyText`, maybe empty) as the last line.
    [[nodiscard]] HintCard MakeAdviceCard(const UIStrings& strings, const TableView& view, const Explanation& explained, eastl::string_view tally);

    /// The card of a coach's note: the computer's choice and the person's move with the numbers of `JudgeMove`, `tally` as the last line.
    [[nodiscard]] HintCard MakeNoteCard(const UIStrings& strings, const CoachNote& note, eastl::string_view tally);

    /// The short tally of the card: the hand and the match, or the match alone with no move in the hand; empty with no move in the match.
    [[nodiscard]] eastl::string CardTallyText(const UIStrings& strings, const CoachTally& hand, const CoachTally& match);

}  // namespace BECore::Mahjong
