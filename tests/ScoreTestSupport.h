#pragma once

#include "HandTestSupport.h"

#include <Mahjong/Game/Payments.h>
#include <Mahjong/Game/Score.h>
#include <Mahjong/Game/ScoreHand.h>

/**
 * What the scoring tests share (`ScorePointsTest`, `ScoreDoublesTest`, `ScoreWinnerTest`, `ScoreLimitTest`, `ScoreHandTest`,
 * `MatchPlayoutTest`): a `ScoreInput` made from notation, melds, the lines of a breakdown by kind, and the checks every settled hand
 * must pass. Header only. A hand written here needs no 13 or 14 tiles: scoring reads what the seat holds and does not check the
 * count, so a test lists only the tiles it is about (plus `kQuietChows`).
 */
namespace BECore::Mahjong::Tests {

    // The table of the rules, written out here and not taken from `Score.h`, so a wrong constant there fails a test.
    inline constexpr int kSimplePungOpen = 2;
    inline constexpr int kSimplePungConcealed = 4;
    inline constexpr int kHonourPungOpen = 4;
    inline constexpr int kHonourPungConcealed = 8;
    inline constexpr int kSimpleKongOpen = 8;
    inline constexpr int kSimpleKongConcealed = 16;
    inline constexpr int kHonourKongOpen = 16;
    inline constexpr int kHonourKongConcealed = 32;
    inline constexpr int kScoringPair = 2;
    inline constexpr int kPairOfOwnAndRoundWind = 4;
    inline constexpr int kBonusTile = 4;
    /// For winning, for the winning tile from the wall, for the only possible winning tile.
    inline constexpr int kWinning = 20;
    inline constexpr int kWallTile = 2;
    inline constexpr int kOnlyTile = 2;

    inline constexpr int kOneDouble = 1;
    inline constexpr int kTwoDoubles = 2;
    inline constexpr int kOneSuitNoHonoursDoubles = 3;

    inline constexpr int kLimit = 500;
    inline constexpr int kLowerLimit = 300;

    /// Two chows of two suits: no points, no doubles, no pair, and the mixed suits keep the one-suit doubles away from a hand.
    inline constexpr const char* kQuietChows = "1p 2p 3p 7m 8m 9m";

    inline Meld MakeMeld(MeldKind kind, bool concealed, Tile first) {
        Meld meld;
        meld.kind = kind;
        meld.concealed = concealed;
        meld.first = first;
        meld.fromDiscard = !concealed;
        meld.claimed = first;
        return meld;
    }

    /// A set claimed from a discard; a kong that was claimed or upgraded is open as well.
    inline Meld OpenPung(Tile tile) {
        return MakeMeld(MeldKind::Pung, false, tile);
    }
    inline Meld OpenChow(Tile first) {
        return MakeMeld(MeldKind::Chow, false, first);
    }
    inline Meld OpenKong(Tile tile) {
        return MakeMeld(MeldKind::Kong, false, tile);
    }
    /// A kong declared from four tiles of the hand.
    inline Meld ConcealedKong(Tile tile) {
        return MakeMeld(MeldKind::Kong, true, tile);
    }

    /// The input of a seat that does not win: the concealed tiles and the bonus tiles as notation, the melds, the winds.
    inline ScoreInput MakeInput(const std::string& concealed, std::initializer_list<Meld> melds = {}, const std::string& bonus = "", Wind seatWind = Wind::East, Wind roundWind = Wind::East) {
        ScoreInput input;
        const eastl::vector<Tile> tiles = ParseTiles(concealed).value();
        for (const Tile tile : tiles) {
            input.hand.AddConcealed(tile);
        }
        for (const Meld& meld : melds) {
            input.hand.melds.push_back(meld);
        }
        input.hand.bonus = ParseTiles(bonus).value();
        input.seatWind = seatWind;
        input.roundWind = roundWind;
        return input;
    }

    /// The input of the winner: `concealed` holds the winning tile `winningTile`, which came by `source`.
    inline ScoreInput MakeWinnerInput(const std::string& concealed, const char* winningTile, WinSource source = WinSource::SelfDraw, std::initializer_list<Meld> melds = {}, Wind seatWind = Wind::East,
                                      Wind roundWind = Wind::East) {
        ScoreInput input = MakeInput(concealed, melds, "", seatWind, roundWind);
        input.won = true;
        input.result.kind = ResultKind::Win;
        input.result.winningTile = T(winningTile);
        input.result.source = source;
        return input;
    }

    /// The points of the lines of `kind` added up.
    inline int LinePoints(const ScoreBreakdown& breakdown, ScoreKind kind) {
        int points = 0;
        for (const ScoreLine& line : breakdown.lines) {
            points += line.kind == kind ? line.points : 0;
        }
        return points;
    }

    /// The doubles of the lines of `kind` added up.
    inline int LineDoubles(const ScoreBreakdown& breakdown, ScoreKind kind) {
        int doubles = 0;
        for (const ScoreLine& line : breakdown.lines) {
            doubles += line.kind == kind ? line.doubles : 0;
        }
        return doubles;
    }

    /// How many lines are of `kind`.
    inline int CountLines(const ScoreBreakdown& breakdown, ScoreKind kind) {
        return static_cast<int>(eastl::count_if(breakdown.lines.begin(), breakdown.lines.end(), [kind](const ScoreLine& line) { return line.kind == kind; }));
    }

    // ----- what every settled hand is -----

    /// A line that only a winner has.
    inline bool IsWinnerLine(const ScoreLine& line) {
        return line.kind == ScoreKind::Win || line.kind == ScoreKind::WinTileFromWall || line.kind == ScoreKind::OnlyWinningTile ||
               static_cast<int>(line.kind) >= static_cast<int>(ScoreKind::OnlyPungs);
    }

    inline void ExpectNoWinnerLine(const ScoreBreakdown& breakdown, Seat seat) {
        for (const ScoreLine& line : breakdown.lines) {
            EXPECT_FALSE(IsWinnerLine(line)) << "seat " << seat << " has the winner's line " << Name(line.kind);
        }
        EXPECT_FALSE(breakdown.won) << "seat " << seat;
        EXPECT_FALSE(breakdown.limitHand.has_value()) << "seat " << seat;
    }

    /// What every settlement is: the scores are those of the breakdowns, the deltas are what `Settle` makes of the scores, the
    /// change sums to zero.
    inline void ExpectConsistent(const HandSettlement& settlement) {
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(settlement.scores[seat], settlement.breakdowns[seat].score) << "seat " << seat;
            EXPECT_EQ(settlement.scores[seat] % kScoreRounding, 0) << "seat " << seat;
        }
        const SeatScores expected = Settle(settlement.scores, settlement.winner, settlement.east);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            EXPECT_EQ(settlement.deltas[seat], expected[seat]) << "seat " << seat;
        }
        EXPECT_EQ(Sum(settlement.deltas), 0);
    }

}  // namespace BECore::Mahjong::Tests
