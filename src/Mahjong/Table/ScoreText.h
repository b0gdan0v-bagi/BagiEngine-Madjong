#pragma once

#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/ScoreHand.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /// The most lines of the winner's score the table shows; a hand with more has the rest summed up as "+n more" in the last line.
    inline constexpr int kMaxScoreLines = 16;

    /// What the screen at the end of a hand says, as plain text rows (the component only puts them into variables).
    struct ScoreTable {
        /// Who won and how ("Seat 2 (South) wins by self-draw"), or "Drawn hand".
        eastl::string headline;
        /// The winner's sum and the winning tile ("Winning tile 4 Dots: 36 points x 8 = 288, score 290"); for a drawn hand "No payments".
        eastl::string summary;
        /// One row per seat, by seat: its score in the hand, the change of its points and its points after the hand.
        eastl::array<eastl::string, kSeatCount> seats;
        /// The winner's lines in the order of the breakdown, at most `kMaxScoreLines`; empty for a drawn hand.
        eastl::vector<eastl::string> lines;
    };

    /// The screen at the end of a match: the winner (or the tied seats) and the seats by points, best first.
    struct StandingsTable {
        eastl::string headline;
        eastl::array<eastl::string, kSeatCount> rows;
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
     * seats once the settlement is applied (`Match::Scores()` plus `deltas`), `seats` says who is "You". The seats are named with the
     * winds of the hand (`settlement.east`).
     */
    [[nodiscard]] ScoreTable FormatScoreTable(const UIStrings& strings, const HandSettlement& settlement, const HandResult& result, const SeatScores& totalsAfter, const SeatSetup& seats);

    /// The standings of a match: equal points share a place and keep the seat order. The seats carry no wind (the match is over).
    [[nodiscard]] StandingsTable FormatStandings(const UIStrings& strings, const Match& match, const SeatSetup& seats);

}  // namespace BECore::Mahjong
