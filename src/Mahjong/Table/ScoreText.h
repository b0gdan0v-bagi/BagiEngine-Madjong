#pragma once

#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Coach.h>
#include <Mahjong/Table/SeatName.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /// The most lines of the winner's score the table shows; a hand with more has the rest summed up as "+n more" in the last line.
    inline constexpr int kMaxScoreLines = 16;

    /// The most tiles the plate of the winner shows: four kongs and the pair.
    inline constexpr int kMaxWinningTiles = kMaxMeldTiles + 2;

    /// The cells of one seat's row in the score table.
    struct ScoreRow {
        /// The seat's name without its wind ("Seat 2", "Seat 1 You", the player's name in a room): the wind has a column of its own.
        eastl::string name;
        /// The seat's wind in this hand, the one letter the plates of the table show.
        eastl::string wind;
        /// Its score in the hand ("290"); `-` for a drawn hand.
        eastl::string hand;
        /// The change of its points with the sign ("+1160", "-600"); `-` for a drawn hand.
        eastl::string change;
        /// Its points after the hand.
        eastl::string total;
    };

    /// What the screen at the end of a hand says, as plain text (the component only puts it into variables).
    struct ScoreTable {
        /// Who won and how ("Seat 2 (South) wins by self-draw"), or "Drawn hand".
        eastl::string headline;
        /// The winner's sum and the winning tile ("Winning tile 4 Dots: 36 points x 8 = 288, score 290"); for a drawn hand "No payments".
        eastl::string summary;
        /// The captions of the five columns of `rows`: seat, wind, hand, change, total.
        eastl::array<eastl::string, 5> columns;
        /// One row per seat, by seat.
        eastl::array<ScoreRow, kSeatCount> rows;
        /// The winning seat, `kNoSeat` for a drawn hand: its row is the one that is lit.
        Seat winner = kNoSeat;
        /// The tiles of the winning hand as the plate shows them: the melds as laid, then the concealed tiles, the winning tile last;
        /// at most `kMaxWinningTiles`. Empty for a drawn hand. When the caller passes no hand, only `result.winningTile`.
        eastl::vector<Tile> winningTiles;
        /// The winner's lines in the order of the breakdown, at most `kMaxScoreLines`; empty for a drawn hand.
        eastl::vector<eastl::string> lines;
    };

    /// One seat of the standings.
    struct StandingRow {
        /// 1..4; seats with equal points share a place.
        int place = 0;
        /// The seat's name without a wind (the match is over).
        eastl::string name;
        eastl::string score;
        /// A first place: a tie lights more than one row.
        bool first = false;
    };

    /// The screen at the end of a match: the winner (or the tied seats) and the seats by points, best first.
    struct StandingsTable {
        eastl::string headline;
        eastl::array<StandingRow, kSeatCount> rows;
    };

    /// "5 Dots", "East Wind", "Red Dragon", "Flower 1", "Season 3"; "No tile" for an invalid one.
    /// (words from the keys `tile.*`, `suit.*`, `wind.*` of the current language of `strings`)
    [[nodiscard]] eastl::string TileName(const UIStrings& strings, Tile tile);

    /// A readable name for every kind of line ("Pung", "One suit, no honours", "Thirteen orphans"); never empty.
    /// The key is `score.kind.<ScoreKind>`.
    [[nodiscard]] eastl::string ScoreKindName(const UIStrings& strings, ScoreKind kind);

    /// One line of a score: "Pung of 5 Dots: 8", "Win: 20", "One suit, no honours: x8", "Limit: Thirteen orphans".
    [[nodiscard]] eastl::string ScoreLineText(const UIStrings& strings, const ScoreLine& line);

    /**
     * @brief The rows of the screen that follows a finished hand.
     *
     * `result` is `Hand().Result()` (the settlement does not carry the winning tile and the source), `totalsAfter` the points of the
     * seats once the settlement is applied (`Match::Scores()` plus `deltas`), `who` says who is "You" and what a seat is called. The winds
     * are those of the hand (`settlement.east`). `winnerHand` is the winner's hand when the caller has it (`Hand().GetHand(winner)`: all
     * four hands are face up once a hand is over); `winningTiles` is then the whole hand, else the winning tile alone. Tiles the hand
     * does not know (a display hand in a room) are left out.
     */
    [[nodiscard]] ScoreTable FormatScoreTable(const UIStrings& strings, const HandSettlement& settlement, const HandResult& result, const SeatScores& totalsAfter, const SeatWho& who,
                                              const PlayerHand* winnerHand = nullptr);

    /// The one-line sentence the node of the score table still shows for `seat` (`handover.seat.*`): replaced by the columns at WP 2.
    [[nodiscard]] eastl::string FormatSeatSentence(const UIStrings& strings, const ScoreTable& table, Seat seat);

    /// The one-line standing the node of the standings still shows (`matchover.row`): replaced by the badge row at WP 4.
    [[nodiscard]] eastl::string FormatStandingSentence(const UIStrings& strings, const StandingRow& row);

    /// The standings of a match: equal points share a place and keep the seat order. The seats carry no wind (the match is over).
    [[nodiscard]] StandingsTable FormatStandings(const UIStrings& strings, const Match& match, const SeatWho& who);

    /**
     * @brief The line of the coach's tally under a score table: "Moves like the computer's: 3 of 5 this hand, 9 of 12 in the match".
     *
     * Empty when the match tally has no move (a person made none, or the mode was not Coach). A hand with no move of its own says the
     * match only (`coach.tally.match`); the standings pass an empty `hand`.
     */
    [[nodiscard]] eastl::string FormatCoachTally(const UIStrings& strings, const CoachTally& hand, const CoachTally& match);

}  // namespace BECore::Mahjong
