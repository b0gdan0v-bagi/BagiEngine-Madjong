#include "ScoreText.h"

#include <Mahjong/Table/SeatName.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kDrawnHand = "Drawn hand";
        constexpr eastl::string_view kNoPayments = "No payments";
        constexpr eastl::string_view kTiedForFirst = "Tied for first:";

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
            switch (result.source) {
                case WinSource::SelfDraw:
                    return Format("{} wins by self-draw", winner);
                case WinSource::Discard:
                    return IsSeat(result.from) ? Format("{} wins on the discard of {}", winner, label(result.from)) : Format("{} wins on a discard", winner);
                case WinSource::RobbedKong:
                    return IsSeat(result.from) ? Format("{} wins by robbing the kong of {}", winner, label(result.from)) : Format("{} wins by robbing a kong", winner);
            }
            return winner;
        }

        eastl::string WinSummary(const UIStrings& strings, const ScoreBreakdown& breakdown, const HandResult& result) {
            const eastl::string tile = TileName(strings, result.winningTile);
            if (breakdown.limitHand) {
                return Format("Winning tile {}: score {}", tile, breakdown.score);
            }
            if (breakdown.doubles > 0) {
                return Format("Winning tile {}: {} points x {} = {}, score {}", tile, breakdown.points, Multiplier(breakdown.doubles), breakdown.raw, breakdown.score);
            }
            return Format("Winning tile {}: {} points, score {}", tile, breakdown.points, breakdown.score);
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
            table.seats[seat] =
                won ? Format("{}: hand {}, {:+}, total {}", name, settlement.scores[seat], settlement.deltas[seat], totalsAfter[seat]) : Format("{}: no payment, total {}", name, totalsAfter[seat]);
        }
        if (!won) {
            table.headline = eastl::string(kDrawnHand);
            table.summary = eastl::string(kNoPayments);
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
            table.lines.push_back(Format("+{} more", count - shown));
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
            table.rows[i] = Format("{}. {}: {}", place, SeatLabel(strings, order[i], seats), scores[order[i]]);
            tied += scores[order[i]] == scores[order[0]] ? 1 : 0;
        }
        if (tied == 1) {
            table.headline = Format("{} wins the match", SeatLabel(strings, order[0], seats));
        } else {
            table.headline.assign(kTiedForFirst.data(), kTiedForFirst.size());
            for (int i = 0; i < tied; ++i) {
                table.headline += Format("{} {}", i > 0 ? "," : "", SeatLabel(strings, order[i], seats));
            }
        }
        return table;
    }

}  // namespace BECore::Mahjong
