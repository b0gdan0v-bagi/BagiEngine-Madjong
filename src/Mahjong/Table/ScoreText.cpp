#include "ScoreText.h"

#include <Mahjong/Table/SeatName.h>

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

        /// 2^doubles, never past the range of a 64-bit number. The clamp only matters for made-up input: a real hand's raw score
        /// saturates at INT_MAX long before 40 doubles, so the shown multiplier of a real line is exact.
        int64_t Multiplier(int doubles) noexcept {
            constexpr int kMostDoubles = 40;
            return int64_t{1} << std::clamp(doubles, 0, kMostDoubles);
        }

        eastl::string WinHeadline(const UIStrings& strings, const HandSettlement& settlement, const HandResult& result, const SeatSetup& seats) {
            const SeatWinds winds = MakeSeatWinds(settlement.east);
            const auto label = [&](Seat seat) { return SeatLabel(strings, seat, winds[seat], seats); };
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

    ScoreTable FormatScoreTable(const UIStrings& strings, const HandSettlement& settlement, const HandResult& result, const SeatScores& totalsAfter, const SeatSetup& seats) {
        ScoreTable table;
        const SeatWinds winds = MakeSeatWinds(settlement.east);
        const bool won = settlement.kind == ResultKind::Win && IsSeat(settlement.winner);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const eastl::string name = SeatLabel(strings, seat, winds[seat], seats);
            const eastl::string total = Format("{}", totalsAfter[seat]);
            if (won) {
                const eastl::string hand = Format("{}", settlement.scores[seat]);
                const eastl::string delta = Format("{:+}", settlement.deltas[seat]);
                table.seats[seat] = Tr(strings, "handover.seat.won", {{"name", name}, {"hand", hand}, {"delta", delta}, {"total", total}});
            } else {
                table.seats[seat] = Tr(strings, "handover.seat.drawn", {{"name", name}, {"total", total}});
            }
        }
        if (!won) {
            table.headline = Tr(strings, "handover.drawn");
            table.summary = Tr(strings, "handover.noPayments");
            return table;
        }
        table.headline = WinHeadline(strings, settlement, result, seats);
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

    StandingsTable FormatStandings(const UIStrings& strings, const Match& match, const SeatSetup& seats) {
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
            table.rows[i] = Tr(strings, "matchover.row", {{"place", Format("{}", place)}, {"name", SeatLabel(strings, order[i], seats)}, {"score", Format("{}", scores[order[i]])}});
            tied += scores[order[i]] == scores[order[0]] ? 1 : 0;
        }
        if (tied == 1) {
            table.headline = Tr(strings, "matchover.wins", {{"winner", SeatLabel(strings, order[0], seats)}});
        } else {
            eastl::string names;
            for (int i = 0; i < tied; ++i) {
                names += Format("{}{}", i > 0 ? ", " : "", SeatLabel(strings, order[i], seats));
            }
            table.headline = Tr(strings, "matchover.tied", {{"seats", names}});
        }
        return table;
    }

}  // namespace BECore::Mahjong
