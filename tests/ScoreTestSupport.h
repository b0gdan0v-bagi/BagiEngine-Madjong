#pragma once

#include "HandTestSupport.h"

#include <Mahjong/Game/Score.h>

/**
 * What the scoring tests share (`ScorePointsTest`, `ScoreDoublesTest`, and the winner and limit tests to come): a `ScoreInput` made
 * from notation, melds, and the lines of a breakdown by kind. Header only. A hand written here needs no 13 or 14 tiles: scoring reads
 * what the seat holds and does not check the count, so a test lists only the tiles it is about (plus `kQuietChows`).
 */
namespace BECore::Mahjong::Tests {

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
    inline ScoreInput MakeWinnerInput(const std::string& concealed, const char* winningTile, WinSource source = WinSource::SelfDraw, std::initializer_list<Meld> melds = {}) {
        ScoreInput input = MakeInput(concealed, melds);
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

}  // namespace BECore::Mahjong::Tests
