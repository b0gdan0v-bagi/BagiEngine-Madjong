#include "ScoreWinner.h"

#include <Mahjong/Game/HandShape.h>

namespace BECore::Mahjong {

    namespace {

        bool IsCompleteReading(const Reading& reading) noexcept {
            return reading.sets.size() == static_cast<size_t>(kSetsInHand) && reading.pair.IsValid();
        }

        bool IsGroupSet(const ReadingSet& set) noexcept {
            return set.kind != MeldKind::Chow;
        }

        bool HoldsTerminalOrHonour(const ReadingSet& set) noexcept {
            if (set.kind != MeldKind::Chow) {
                return set.first.IsTerminal() || set.first.IsHonour();
            }
            return set.first.Rank() == 1 || set.first.Rank() == kLastChowStartRank;
        }

        int CountSets(const Reading& reading, bool (*isWanted)(const ReadingSet&)) noexcept {
            int count = 0;
            for (const ReadingSet& set : reading.sets) {
                count += isWanted(set) ? 1 : 0;
            }
            return count;
        }

        int SumPoints(const ScoreBreakdown& breakdown) noexcept {
            int points = 0;
            for (const ScoreLine& line : breakdown.lines) {
                points += line.points;
            }
            return points;
        }

        // ----- the shape doubles; each reads a reading of four sets and a pair -----

        void AddOnlyPungs(const Reading& reading, ScoreBreakdown& breakdown) {
            if (CountSets(reading, IsGroupSet) == kSetsInHand) {
                AddLine(breakdown, ScoreKind::OnlyPungs, Tile{}, 0, kOnlyPungsDoubles);
            }
        }

        /// Nothing scores but the 20 for winning: no wall tile, no only winning tile, no flower, no pung, kong or scoring pair.
        void AddNoPoints(ScoreBreakdown& breakdown) {
            if (SumPoints(breakdown) == kWinPoints) {
                AddLine(breakdown, ScoreKind::NoPoints, Tile{}, 0, kNoPointsDoubles);
            }
        }

        void AddLittleFourWinds(const Reading& reading, ScoreBreakdown& breakdown) {
            int winds = 0;
            for (const ReadingSet& set : reading.sets) {
                winds += IsGroupSet(set) && set.first.IsWind() ? 1 : 0;
            }
            if (winds == kLittleFourWindsSets && reading.pair.IsWind()) {
                AddLine(breakdown, ScoreKind::LittleFourWinds, reading.pair, 0, kLittleFourWindsDoubles);
            }
        }

        void AddLittleThreeDragons(const Reading& reading, ScoreBreakdown& breakdown) {
            int dragons = 0;
            for (const ReadingSet& set : reading.sets) {
                dragons += IsGroupSet(set) && set.first.IsDragon() ? 1 : 0;
            }
            if (dragons == kLittleThreeDragonsSets && reading.pair.IsDragon()) {
                AddLine(breakdown, ScoreKind::LittleThreeDragons, reading.pair, 0, kLittleThreeDragonsDoubles);
            }
        }

        /// Every set and the pair holds a terminal or an honour (a chow `1 2 3` or `7 8 9` does, the pair counts).
        void AddTerminalsOrHonoursEverywhere(const Reading& reading, ScoreBreakdown& breakdown) {
            const bool setsOk = CountSets(reading, HoldsTerminalOrHonour) == kSetsInHand;
            if (setsOk && (reading.pair.IsTerminal() || reading.pair.IsHonour())) {
                AddLine(breakdown, ScoreKind::TerminalsOrHonoursEverywhere, Tile{}, 0, kTerminalsOrHonoursEverywhereDoubles);
            }
        }

        /// No open set. A set completed by a winning discard or a robbed kong is open, which the reading says.
        void AddFullyConcealed(const Reading& reading, ScoreBreakdown& breakdown) {
            const bool open = eastl::any_of(reading.sets.begin(), reading.sets.end(), [](const ReadingSet& set) { return !set.concealed; });
            if (!open) {
                AddLine(breakdown, ScoreKind::FullyConcealed, Tile{}, 0, kFullyConcealedDoubles);
            }
        }

        /// Two or three kongs, open or concealed (four is a limit hand).
        void AddSeveralKongs(const Reading& reading, ScoreBreakdown& breakdown) {
            int kongs = 0;
            for (const ReadingSet& set : reading.sets) {
                kongs += set.kind == MeldKind::Kong ? 1 : 0;
            }
            if (kongs >= kSeveralKongsCount && kongs < kSetsInHand) {
                AddLine(breakdown, ScoreKind::SeveralKongs, Tile{}, 0, kSeveralKongsDoubles);
            }
        }

        // ----- the doubles that do not read the shape -----

        void AddHandCircumstances(const ScoreInput& input, ScoreBreakdown& breakdown) {
            if (input.result.loose) {
                AddLine(breakdown, ScoreKind::LooseTile, input.result.winningTile, 0, kLooseTileDoubles);
            }
            if (input.result.lastLiveTile) {
                AddLine(breakdown, ScoreKind::LastTile, input.result.winningTile, 0, kLastTileDoubles);
            }
            if (input.result.source == WinSource::RobbedKong) {
                AddLine(breakdown, ScoreKind::RobbedKong, input.result.winningTile, 0, kRobbedKongDoubles);
            }
            if (input.visibleWinningTileCopies >= kObviouslyLastCopies) {
                AddLine(breakdown, ScoreKind::ObviouslyLast, input.result.winningTile, 0, kObviouslyLastDoubles);
            }
            if (input.hand.callingDeclared && input.hand.callingAtDiscardCount == 0) {
                AddLine(breakdown, ScoreKind::CallingFirstHand, Tile{}, 0, kCallingFirstHandDoubles);
            }
        }

    }  // namespace

    bool WaitsOnlyForTheWinningTile(const ScoreInput& input) {
        PlayerHand before = input.hand;
        const Tile winning = input.result.winningTile;
        if (!winning.IsValid() || !before.RemoveConcealed(winning)) {
            return false;
        }
        const eastl::vector<Tile> waits = WinningTiles(before);
        return waits.size() == 1 && waits[0] == winning;
    }

    void AddWinnerPoints(const ScoreInput& input, bool onlyWinning, ScoreBreakdown& breakdown) {
        AddLine(breakdown, ScoreKind::Win, Tile{}, kWinPoints, 0);
        if (input.result.source == WinSource::SelfDraw) {
            AddLine(breakdown, ScoreKind::WinTileFromWall, input.result.winningTile, kWinTileFromWallPoints, 0);
        }
        if (onlyWinning) {
            AddLine(breakdown, ScoreKind::OnlyWinningTile, input.result.winningTile, kOnlyWinningTilePoints, 0);
        }
    }

    void AddWinnerDoubles(const Reading& reading, const ScoreInput& input, ScoreBreakdown& breakdown) {
        if (IsCompleteReading(reading)) {
            AddOnlyPungs(reading, breakdown);
            AddNoPoints(breakdown);
            AddLittleFourWinds(reading, breakdown);
            AddLittleThreeDragons(reading, breakdown);
            AddTerminalsOrHonoursEverywhere(reading, breakdown);
            AddFullyConcealed(reading, breakdown);
            AddSeveralKongs(reading, breakdown);
        }
        AddHandCircumstances(input, breakdown);
    }

}  // namespace BECore::Mahjong
