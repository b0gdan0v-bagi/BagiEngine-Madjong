#pragma once

#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Coach.h>
#include <Mahjong/Table/SeatName.h>
#include <Mahjong/Table/TableLayout.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /// The most lines of the winner's score the table shows; the rest is summed up as "+n more" in the last line.
    inline constexpr int kMaxScoreLines = 16;

    /// The most tiles the plate of the winner shows: four kongs and the pair.
    inline constexpr int kMaxWinningTiles = kMaxMeldTiles + 2;

    /// The cells of one seat's row in the score table.
    struct ScoreRow {
        /// The seat's name without its wind: the wind has a column of its own.
        String name;
        /// The seat's wind in this hand, as on the plates.
        String wind;
        /// Its score in the hand; `-` for a drawn hand.
        String hand;
        /// The change of its points with the sign; `-` for a drawn hand.
        String change;
        /// Its points after the hand.
        String total;
    };

    /// What the screen at the end of a hand says, as plain text (the component only puts it into variables).
    struct ScoreTable {
        /// Who won and how ("Seat 2 (South) wins by self-draw"), or "Drawn hand".
        String headline;
        /// The winner's sum and the winning tile; for a drawn hand "No payments".
        String summary;
        /// The captions of the five columns of `rows`: seat, wind, hand, change, total.
        Array<String, 5> columns;
        /// One row per seat, by seat.
        Array<ScoreRow, kSeatCount> rows;
        /// The winning seat, `kNoSeat` for a drawn hand: its row is the one that is lit.
        Seat winner = kNoSeat;
        /// The winning hand as the plate shows it: melds as laid, then the concealed tiles, the winning tile last; at most `kMaxWinningTiles`.
        /// Empty for a drawn hand; only `result.winningTile` when the caller passes no hand.
        Vector<Tile> winningTiles;
        /// The winner's lines in the order of the breakdown, at most `kMaxScoreLines`; empty for a drawn hand.
        Vector<String> lines;
    };

    /// One seat of the standings.
    struct StandingRow {
        /// 1..4; seats with equal points share a place.
        int place = 0;
        /// The seat's name without a wind (the match is over).
        String name;
        String score;
        /// A first place: a tie lights more than one row.
        bool first = false;
    };

    /// The screen at the end of a match: the winner (or the tied seats) and the seats by points, best first.
    struct StandingsTable {
        String headline;
        Array<StandingRow, kSeatCount> rows;
    };

    /// "5 Dots", "East Wind", "Flower 1"; "No tile" for an invalid one. The words are the keys `tile.*`, `suit.*`, `wind.*`.
    [[nodiscard]] String TileName(const UIStrings& strings, Tile tile);

    /// A readable name for every kind of line, the key `score.kind.<ScoreKind>`; never empty.
    [[nodiscard]] String ScoreKindName(const UIStrings& strings, ScoreKind kind);

    /// One line of a score: "Pung of 5 Dots: 8", "One suit, no honours: x8", "Limit: Thirteen orphans".
    [[nodiscard]] String ScoreLineText(const UIStrings& strings, const ScoreLine& line);

    /**
     * @brief The rows of the screen that follows a finished hand.
     *
     * `result` is `Hand().Result()` (the settlement carries neither the winning tile nor the source), `totalsAfter` the points once the
     * settlement is applied (`Match::Scores()` plus `deltas`). The winds are those of the hand (`settlement.east`). `winnerHand` is
     * `Hand().GetHand(winner)` when the caller has it (all hands are face up once a hand is over); tiles the hand does not know (a
     * display hand in a room) are left out of `winningTiles`.
     */
    [[nodiscard]] ScoreTable FormatScoreTable(const UIStrings& strings, const HandSettlement& settlement, const HandResult& result, const SeatScores& totalsAfter, const SeatWho& who,
                                              const PlayerHand* winnerHand = nullptr);

    /// The standings of a match: equal points share a place and keep the seat order. The seats carry no wind (the match is over).
    [[nodiscard]] StandingsTable FormatStandings(const UIStrings& strings, const Match& match, const SeatWho& who);

    /**
     * @brief The line of the coach's tally under a score table ("Moves like the computer's: 3 of 5 this hand, 9 of 12 in the match").
     *
     * Empty when the match tally has no move. A hand with no move of its own says the match only (`coach.tally.match`); the standings
     * pass an empty `hand`.
     */
    [[nodiscard]] String FormatCoachTally(const UIStrings& strings, const CoachTally& hand, const CoachTally& match);

}  // namespace BECore::Mahjong
