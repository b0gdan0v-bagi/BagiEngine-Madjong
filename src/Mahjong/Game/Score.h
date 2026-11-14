#pragma once

#include <Mahjong/Game/HandModel.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/PlayerHand.h>
#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    // ----- points (the numbers of the rules) -----

    /// For winning, and for the winning tile drawn from the wall (a loose tile too), and for the only possible winning tile.
    inline constexpr int kWinPoints = 20;
    inline constexpr int kWinTileFromWallPoints = 2;
    inline constexpr int kOnlyWinningTilePoints = 2;
    /// A pung of simples (2..8) laid open; every other pung is this times a factor below.
    inline constexpr int kOpenSimplePungPoints = 2;
    /// A pung of terminals, winds or dragons is worth this times more than a pung of simples.
    inline constexpr int kHonourPointsFactor = 2;
    /// A concealed pung or kong is worth this times more than an open one.
    inline constexpr int kConcealedPointsFactor = 2;
    /// A kong is worth this times more than the pung of the same tile.
    inline constexpr int kKongPointsFactor = 4;
    /// A pair of dragons, of the own wind and of the round wind is worth this each (the sum when one pair is several).
    inline constexpr int kPairPoints = 2;
    inline constexpr int kBonusPoints = 4;

    // ----- doubles (each is x2) -----

    inline constexpr int kDragonSetDoubles = 1;
    inline constexpr int kOwnWindSetDoubles = 1;
    inline constexpr int kRoundWindSetDoubles = 1;
    inline constexpr int kOneSuitWithHonoursDoubles = 1;
    inline constexpr int kOneSuitNoHonoursDoubles = 3;
    /// A bonus tile of the own wind's number, and one of the round wind's (a tile that is both doubles twice).
    inline constexpr int kOwnBonusDoubles = 1;
    inline constexpr int kRoundBonusDoubles = 1;
    inline constexpr int kScoreRounding = 10;

    // ----- doubles for the winner (the rules of the hand; a line each) -----

    inline constexpr int kOnlyPungsDoubles = 1;
    inline constexpr int kNoPointsDoubles = 1;
    inline constexpr int kLooseTileDoubles = 1;
    inline constexpr int kLastTileDoubles = 1;
    inline constexpr int kRobbedKongDoubles = 1;
    inline constexpr int kLittleFourWindsDoubles = 2;
    inline constexpr int kLittleThreeDragonsDoubles = 1;
    inline constexpr int kTerminalsOrHonoursEverywhereDoubles = 1;
    inline constexpr int kFullyConcealedDoubles = 1;
    inline constexpr int kSeveralKongsDoubles = 1;
    inline constexpr int kObviouslyLastDoubles = 1;
    inline constexpr int kCallingFirstHandDoubles = 1;
    /// Sets (besides the pair) that make the little four winds and the little three dragons.
    inline constexpr int kLittleFourWindsSets = 3;
    inline constexpr int kLittleThreeDragonsSets = 2;
    /// A hand with at least this many kongs doubles (all four is a limit hand).
    inline constexpr int kSeveralKongsCount = 2;
    /// The winning tile is obviously the last of its four when at least this many other copies lie open.
    inline constexpr int kObviouslyLastCopies = 3;

    /// What a line of a score is about. Order matters: the kinds only a winner has are `Win`, `WinTileFromWall`, `OnlyWinningTile` and
    /// everything from `OnlyPungs` on; a test tells a winner's line from the others by it, and `FindLimitHand` takes the first match.
    CORE_ENUM(ScoreKind, uint8_t,
              // points
              Win,              // the winner
              WinTileFromWall,  // a self-draw (a loose tile too)
              OnlyWinningTile,  // the hand waits on exactly one kind
              Pung,             // a pung, per set (`tile` is the pung)
              Kong,             // a kong, per set
              Pair,             // a pair of dragons, of the own wind, of the round wind
              Bonus,            // a flower or a season, per tile
              // doubles for everyone
              DragonSet,           // a pung or kong of dragons, per set
              OwnWindSet,          // a pung or kong of the own wind
              RoundWindSet,        // a pung or kong of the round wind
              OneSuitWithHonours,  // one suit and at least one honour
              OneSuitNoHonours,    // one suit, no honours
              OwnBonus,            // a flower or a season of the own wind's number, per tile
              RoundBonus,          // a flower or a season of the round wind's number, per tile
              // doubles for the winner
              OnlyPungs, NoPoints, LooseTile, LastTile, RobbedKong, LittleFourWinds, LittleThreeDragons, TerminalsOrHonoursEverywhere, FullyConcealed, SeveralKongs, ObviouslyLast, CallingFirstHand,
              // limit hands (the winner only)
              ConcealedPungsOneSuit, HonoursOnly, TerminalsOnly, AllGreen, SevenPairsHonours, SevenPairsOneSuit, BigThreeDragons, BigFourWinds, FourKongs, ThirteenOrphans, LastTileOneDot,
              LooseTileFiveDots)

    /// What a seat's hand is scored from. Scoring is a pure function of this, so a test builds a hand from notation, no `HandModel`.
    struct ScoreInput {
        /// The seat's hand; for the winner the concealed tiles include the winning tile.
        PlayerHand hand;
        Wind seatWind = Wind::East;
        Wind roundWind = Wind::East;
        /// The most a hand scores, used rounded down (`LimitCap`).
        int limit = kDefaultLimit;
        bool won = false;
        /// Read only when `won`: `winningTile`, `source`, `loose`, `lastLiveTile`.
        HandResult result;
        /// Copies of the winning kind lying open on the table (discards and open sets), not counting the winning tile itself.
        int visibleWinningTileCopies = 0;
    };

    /// One line of a score. `tile` is the set, pair or bonus tile the line is about, invalid for a line that is about the whole hand.
    struct ScoreLine {
        /// Every line is made with its kind (`AddLine`); `Win` is only the zero value of the enum.
        ScoreKind kind = ScoreKind::Win;
        Tile tile;
        int points = 0;
        int doubles = 0;
    };

    /// The score of one seat: the lines of the best reading and what they add up to.
    struct ScoreBreakdown {
        bool won = false;
        /// The lines of the best reading, in the order `ScoreReading` (`Score.cpp`) adds them, family by family: the winner's points; the
        /// points of the pungs and kongs in the reading's set order, then of the pair, then of the bonus tiles; the doubles of the sets,
        /// of one suit, of the bonus tiles; the winner's doubles. The score table prints them as they are.
        Vector<ScoreLine> lines;
        int points = 0;
        int doubles = 0;
        /// `points` x 2^`doubles`, saturating at the largest `int`.
        int raw = 0;
        /// The limit hand the winner holds, if any (its single line is in `lines`).
        Optional<ScoreKind> limitHand;
        /// `RoundUpToTen(min(raw, LimitCap(limit)))`; a limit hand scores `LimitCap(limit)`.
        int score = 0;
    };

    [[nodiscard]] constexpr int RoundDownToTen(int value) noexcept {
        return value - value % kScoreRounding;
    }

    /// The most a score can be for the setting `limit`: the limit rounded down to a multiple of `kScoreRounding`, 0 for a negative one.
    /// Every score is at most this and a multiple of `kScoreRounding`, whatever the limit is (also `INT_MAX`).
    [[nodiscard]] constexpr int LimitCap(int limit) noexcept {
        return limit > 0 ? RoundDownToTen(limit) : 0;
    }

    [[nodiscard]] constexpr int RoundUpToTen(int value) noexcept {
        const int rest = value % kScoreRounding;
        return rest == 0 ? value : value + kScoreRounding - rest;
    }

    /**
     * @brief The score of one seat: every reading of its hand is scored and the best one (the largest `raw`; a tie keeps the first) is
     *        returned with all its lines.
     *
     * A seat that does not win has no winning lines and no winner doubles. A winner who holds a limit hand (`ScoreLimit.h`) scores
     * `LimitCap(limit)` with the one line of the hand and no reading is scored.
     * Readings and the lines of a reading: `ScoreReading.h`, `Game/CLAUDE.md` ("Scoring" and "Scoring readings decided by the owner").
     */
    [[nodiscard]] ScoreBreakdown ScoreSeat(const ScoreInput& input);

}  // namespace BECore::Mahjong
