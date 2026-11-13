#include "ScoreText.h"

#include <Mahjong/Table/SeatName.h>
#include <Mahjong/Table/TileFlights.h>

namespace BECore::Mahjong {

    namespace {

        /// The keys of the three dragons, in tile order.
        constexpr eastl::array<eastl::string_view, kDragonCount> kDragonKeys{"tile.dragon.Red", "tile.dragon.White", "tile.dragon.Green"};

        /// Lines about a set or a pair: "Pung of 5 Dots".
        bool NamesItsTileWithOf(ScoreKind kind) noexcept {
            return kind == ScoreKind::Pung || kind == ScoreKind::Kong || kind == ScoreKind::Pair;
        }

        /// Lines about one tile that read better with the tile in brackets: "Bonus tile (Flower 1)".
        bool NamesItsTileInBrackets(ScoreKind kind) noexcept {
            switch (kind) {
                case ScoreKind::Bonus:
                case ScoreKind::DragonSet:
                case ScoreKind::OwnWindSet:
                case ScoreKind::RoundWindSet:
                case ScoreKind::OwnBonus:
                case ScoreKind::RoundBonus:
                    return true;
                default:
                    return false;
            }
        }

        /// The limit hands are the kinds from the first of them on (`ScoreKind` keeps them last).
        static_assert(ScoreKind::ConcealedPungsOneSuit > ScoreKind::CallingFirstHand, "the limit hands come after every other kind");
        static_assert(ScoreKind::LooseTileFiveDots > ScoreKind::ConcealedPungsOneSuit, "the limit hands are the last kinds");
        static_assert(static_cast<size_t>(ScoreKind::LooseTileFiveDots) + 1 == EnumUtils<ScoreKind>::Count(), "LooseTileFiveDots is the last kind");

        bool IsLimitKind(ScoreKind kind) noexcept {
            return kind >= ScoreKind::ConcealedPungsOneSuit;
        }

        constexpr int kTilesPerSet = 3;
        constexpr int kTilesPerKong = 4;
        /// The cell of a drawn hand where a hand score and a change would stand.
        constexpr eastl::string_view kNoCell = "-";

        constexpr eastl::array<eastl::string_view, 5> kColumnKeys{"handover.col.seat", "handover.col.wind", "handover.col.hand", "handover.col.change", "handover.col.total"};

        /// The winner's tiles for the plate: the melds as laid, the concealed tiles in their order, the winning tile moved to the end.
        eastl::vector<Tile> WinningTilesOf(const HandResult& result, const PlayerHand* hand) {
            eastl::vector<Tile> tiles;
            if (hand == nullptr) {
                if (result.winningTile.IsValid()) {
                    tiles.push_back(result.winningTile);
                }
                return tiles;
            }
            tiles.reserve(kMaxWinningTiles);
            for (const Meld& meld : hand->melds) {
                const int count = meld.kind == MeldKind::Kong ? kTilesPerKong : kTilesPerSet;
                for (int part = 0; part < count; ++part) {
                    if (const Tile tile = MeldTileAt(meld, part); tile.IsValid()) {
                        tiles.push_back(tile);
                    }
                }
            }
            bool skipped = !result.winningTile.IsValid();  // the winning tile has its own place at the end: leave one copy out here
            for (const Tile tile : hand->concealed) {
                if (!tile.IsValid()) {
                    continue;
                }
                if (!skipped && tile == result.winningTile) {
                    skipped = true;
                    continue;
                }
                tiles.push_back(tile);
            }
            if (result.winningTile.IsValid()) {
                tiles.push_back(result.winningTile);
            }
            if (tiles.size() > static_cast<size_t>(kMaxWinningTiles)) {
                tiles.resize(kMaxWinningTiles);
            }
            return tiles;
        }

        /// 2^doubles, never past the range of a 64-bit number. The clamp only matters for made-up input: a real hand's raw score
        /// saturates at INT_MAX long before 40 doubles, so the shown multiplier of a real line is exact.
        int64_t Multiplier(int doubles) noexcept {
            constexpr int kMostDoubles = 40;
            return int64_t{1} << std::clamp(doubles, 0, kMostDoubles);
        }

        eastl::string WinHeadline(const UIStrings& strings, const HandSettlement& settlement, const HandResult& result, const SeatWho& who) {
            const SeatWinds winds = MakeSeatWinds(settlement.east);
            const auto label = [&](Seat seat) { return SeatLabel(strings, seat, winds[seat], who); };
            const eastl::string winner = label(settlement.winner);
            const bool knowsFrom = IsSeat(result.from);
            switch (result.source) {
                case WinSource::SelfDraw:
                    return Tr(strings, "handover.win.selfDraw", {{"winner", winner}});
                case WinSource::Discard:
                    return knowsFrom ? Tr(strings, "handover.win.discard", {{"winner", winner}, {"from", label(result.from)}}) : Tr(strings, "handover.win.discardUnknown", {{"winner", winner}});
                case WinSource::RobbedKong:
                    return knowsFrom ? Tr(strings, "handover.win.robbedKong", {{"winner", winner}, {"from", label(result.from)}}) : Tr(strings, "handover.win.robbedKongUnknown", {{"winner", winner}});
            }
            return winner;
        }

        eastl::string WinSummary(const UIStrings& strings, const ScoreBreakdown& breakdown, const HandResult& result) {
            const eastl::string tile = TileName(strings, result.winningTile);
            const eastl::string score = Format("{}", breakdown.score);
            if (breakdown.limitHand) {
                return Tr(strings, "handover.summary.limit", {{"tile", tile}, {"score", score}});
            }
            const eastl::string points = Format("{}", breakdown.points);
            if (breakdown.doubles > 0) {
                const eastl::string multiplier = Format("{}", Multiplier(breakdown.doubles));
                const eastl::string raw = Format("{}", breakdown.raw);
                return Tr(strings, "handover.summary.doubles", {{"tile", tile}, {"points", points}, {"multiplier", multiplier}, {"raw", raw}, {"score", score}});
            }
            return Tr(strings, "handover.summary.plain", {{"tile", tile}, {"points", points}, {"score", score}});
        }

    }  // namespace

    eastl::string TileName(const UIStrings& strings, Tile tile) {
        if (!tile.IsValid()) {
            return Tr(strings, "tile.none");
        }
        if (tile.IsSuit()) {
            const eastl::string rank = Format("{}", tile.Rank());
            const eastl::string suit = Tr(strings, Format("suit.{}", EnumUtils<TileSuit>::ToString(tile.Suit())).c_str());
            return Tr(strings, "tile.suit", {{"rank", rank}, {"suit", suit}});
        }
        if (tile.IsWind()) {
            return Tr(strings, "tile.wind", {{"wind", WindName(strings, static_cast<Wind>(tile.Id() - kFirstWindId))}});
        }
        if (tile.IsDragon()) {
            return Tr(strings, kDragonKeys[tile.Id() - kFirstDragonId]);
        }
        if (tile.Id() < kFirstSeasonId) {
            const eastl::string number = Format("{}", tile.Id() - kFirstFlowerId + 1);
            return Tr(strings, "tile.flower", {{"n", number}});
        }
        const eastl::string number = Format("{}", tile.Id() - kFirstSeasonId + 1);
        return Tr(strings, "tile.season", {{"n", number}});
    }

    eastl::string ScoreKindName(const UIStrings& strings, ScoreKind kind) {
        return Tr(strings, Format("score.kind.{}", EnumUtils<ScoreKind>::ToString(kind)).c_str());
    }

    eastl::string ScoreLineText(const UIStrings& strings, const ScoreLine& line) {
        const eastl::string name = ScoreKindName(strings, line.kind);
        if (IsLimitKind(line.kind)) {
            return Tr(strings, "score.limit", {{"name", name}});
        }
        eastl::string text = name;
        if (line.tile.IsValid() && NamesItsTileWithOf(line.kind)) {
            text = Tr(strings, "score.of", {{"name", name}, {"tile", TileName(strings, line.tile)}});
        } else if (line.tile.IsValid() && NamesItsTileInBrackets(line.kind)) {
            text = Tr(strings, "score.bracket", {{"name", name}, {"tile", TileName(strings, line.tile)}});
        }
        if (line.doubles > 0) {
            const eastl::string multiplier = Format("{}", Multiplier(line.doubles));
            return Tr(strings, "score.doubles", {{"name", text}, {"multiplier", multiplier}});
        }
        const eastl::string points = Format("{}", line.points);
        return Tr(strings, "score.points", {{"name", text}, {"points", points}});
    }

    ScoreTable FormatScoreTable(const UIStrings& strings, const HandSettlement& settlement, const HandResult& result, const SeatScores& totalsAfter, const SeatWho& who, const PlayerHand* winnerHand) {
        ScoreTable table;
        const SeatWinds winds = MakeSeatWinds(settlement.east);
        const bool won = settlement.kind == ResultKind::Win && IsSeat(settlement.winner);
        for (size_t column = 0; column < kColumnKeys.size(); ++column) {
            table.columns[column] = Tr(strings, kColumnKeys[column]);
        }
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            ScoreRow& row = table.rows[seat];
            row.name = SeatLabel(strings, seat, who);
            row.wind = WindLetter(strings, winds[seat]);
            row.total = Format("{}", totalsAfter[seat]);
            if (won) {
                row.hand = Format("{}", settlement.scores[seat]);
                row.change = Format("{:+}", settlement.deltas[seat]);
            } else {
                row.hand = kNoCell;
                row.change = kNoCell;
            }
        }
        if (!won) {
            table.headline = Tr(strings, "handover.drawn");
            table.summary = Tr(strings, "handover.noPayments");
            return table;
        }
        table.winner = settlement.winner;
        table.winningTiles = WinningTilesOf(result, winnerHand);
        table.headline = WinHeadline(strings, settlement, result, who);
        const ScoreBreakdown& breakdown = settlement.breakdowns[settlement.winner];
        table.summary = WinSummary(strings, breakdown, result);
        const int count = static_cast<int>(breakdown.lines.size());
        const int shown = count > kMaxScoreLines ? kMaxScoreLines - 1 : count;
        for (int i = 0; i < shown; ++i) {
            table.lines.push_back(ScoreLineText(strings, breakdown.lines[i]));
        }
        if (shown < count) {
            table.lines.push_back(Tr(strings, "handover.more", {{"n", Format("{}", count - shown)}}));
        }
        return table;
    }

    StandingsTable FormatStandings(const UIStrings& strings, const Match& match, const SeatWho& who) {
        StandingsTable table;
        const eastl::array<Seat, kSeatCount> order = match.Standings();
        const SeatScores& scores = match.Scores();
        // The wind is not part of the name here: the match is over and "Seat 3 (East)" would name the next deal
        int place = 1;
        int tied = 0;
        for (int i = 0; i < kSeatCount; ++i) {
            if (i > 0 && scores[order[i]] != scores[order[i - 1]]) {
                place = i + 1;
            }
            StandingRow& row = table.rows[i];
            row.place = place;
            row.name = SeatLabel(strings, order[i], who);
            row.score = Format("{}", scores[order[i]]);
            row.first = scores[order[i]] == scores[order[0]];
            tied += row.first ? 1 : 0;
        }
        if (tied == 1) {
            table.headline = Tr(strings, "matchover.wins", {{"winner", SeatLabel(strings, order[0], who)}});
        } else {
            eastl::string names;
            for (int i = 0; i < tied; ++i) {
                names += Format("{}{}", i > 0 ? ", " : "", SeatLabel(strings, order[i], who));
            }
            table.headline = Tr(strings, "matchover.tied", {{"seats", names}});
        }
        return table;
    }

    eastl::string FormatStandingSentence(const UIStrings& strings, const StandingRow& row) {
        return Tr(strings, "matchover.row", {{"place", Format("{}", row.place)}, {"name", row.name}, {"score", row.score}});
    }

    eastl::string FormatCoachTally(const UIStrings& strings, const CoachTally& hand, const CoachTally& match) {
        if (match.moves <= 0) {
            return {};
        }
        if (hand.moves <= 0) {
            return Tr(strings, "coach.tally.match", {{"agreed", Format("{}", match.agreed)}, {"moves", Format("{}", match.moves)}});
        }
        return Tr(strings, "coach.tally.hand",
                  {{"agreed", Format("{}", hand.agreed)}, {"moves", Format("{}", hand.moves)}, {"matchAgreed", Format("{}", match.agreed)}, {"matchMoves", Format("{}", match.moves)}});
    }

}  // namespace BECore::Mahjong
