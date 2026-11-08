#include "ScoreText.h"

#include <Mahjong/Table/SeatName.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kNoTile = "No tile";
        constexpr eastl::string_view kDrawnHand = "Drawn hand";
        constexpr eastl::string_view kNoPayments = "No payments";
        constexpr eastl::string_view kLimitHand = "Limit";

        /// The word of a suit: "5 Dots".
        eastl::string_view SuitName(TileSuit suit) noexcept {
            switch (suit) {
                case TileSuit::Dots:
                    return "Dots";
                case TileSuit::Bamboo:
                    return "Bamboo";
                case TileSuit::Characters:
                    return "Characters";
                case TileSuit::None:
                    break;
            }
            return {};
        }

        constexpr eastl::array<eastl::string_view, kDragonCount> kDragonNames{"Red Dragon", "White Dragon", "Green Dragon"};

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
        bool IsLimitKind(ScoreKind kind) noexcept {
            return kind >= ScoreKind::ConcealedPungsOneSuit;
        }

        /// 2^doubles, never past the range of a 64-bit number.
        int64_t Multiplier(int doubles) noexcept {
            constexpr int kMostDoubles = 40;
            return int64_t{1} << std::clamp(doubles, 0, kMostDoubles);
        }

        eastl::string WinHeadline(const HandSettlement& settlement, const HandResult& result, const SeatSetup& seats) {
            const SeatWinds winds = MakeSeatWinds(settlement.east);
            const auto label = [&](Seat seat) { return SeatLabel(seat, winds[seat], seats); };
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

        eastl::string WinSummary(const ScoreBreakdown& breakdown, const HandResult& result) {
            const eastl::string tile = TileName(result.winningTile);
            if (breakdown.limitHand) {
                return Format("Winning tile {}: score {}", tile, breakdown.score);
            }
            if (breakdown.doubles > 0) {
                return Format("Winning tile {}: {} points x {} = {}, score {}", tile, breakdown.points, Multiplier(breakdown.doubles), breakdown.raw, breakdown.score);
            }
            return Format("Winning tile {}: {} points, score {}", tile, breakdown.points, breakdown.score);
        }

    }  // namespace

    eastl::string TileName(Tile tile) {
        if (!tile.IsValid()) {
            return eastl::string(kNoTile);
        }
        if (tile.IsSuit()) {
            const eastl::string_view suit = SuitName(tile.Suit());
            return Format("{} {}", tile.Rank(), suit);
        }
        if (tile.IsWind()) {
            return Format("{} Wind", EnumUtils<Wind>::ToString(static_cast<Wind>(tile.Id() - kFirstWindId)));
        }
        if (tile.IsDragon()) {
            return eastl::string(kDragonNames[tile.Id() - kFirstDragonId]);
        }
        if (tile.Id() < kFirstSeasonId) {
            return Format("Flower {}", tile.Id() - kFirstFlowerId + 1);
        }
        return Format("Season {}", tile.Id() - kFirstSeasonId + 1);
    }

    eastl::string_view ScoreKindName(ScoreKind kind) noexcept {
        switch (kind) {
            case ScoreKind::Win:
                return "Win";
            case ScoreKind::WinTileFromWall:
                return "Tile from the wall";
            case ScoreKind::OnlyWinningTile:
                return "Only winning tile";
            case ScoreKind::Pung:
                return "Pung";
            case ScoreKind::Kong:
                return "Kong";
            case ScoreKind::Pair:
                return "Pair";
            case ScoreKind::Bonus:
                return "Bonus tile";
            case ScoreKind::DragonSet:
                return "Dragon set";
            case ScoreKind::OwnWindSet:
                return "Own wind set";
            case ScoreKind::RoundWindSet:
                return "Round wind set";
            case ScoreKind::OneSuitWithHonours:
                return "One suit with honours";
            case ScoreKind::OneSuitNoHonours:
                return "One suit, no honours";
            case ScoreKind::OwnBonus:
                return "Own wind bonus";
            case ScoreKind::RoundBonus:
                return "Round wind bonus";
            case ScoreKind::OnlyPungs:
                return "Only pungs";
            case ScoreKind::NoPoints:
                return "No points";
            case ScoreKind::LooseTile:
                return "Loose tile";
            case ScoreKind::LastTile:
                return "Last tile";
            case ScoreKind::RobbedKong:
                return "Robbed kong";
            case ScoreKind::LittleFourWinds:
                return "Little four winds";
            case ScoreKind::LittleThreeDragons:
                return "Little three dragons";
            case ScoreKind::TerminalsOrHonoursEverywhere:
                return "Terminal or honour in every set";
            case ScoreKind::FullyConcealed:
                return "Fully concealed";
            case ScoreKind::SeveralKongs:
                return "Several kongs";
            case ScoreKind::ObviouslyLast:
                return "Obviously the last tile";
            case ScoreKind::CallingFirstHand:
                return "Calling hand, first hand";
            case ScoreKind::ConcealedPungsOneSuit:
                return "Concealed pungs, one suit";
            case ScoreKind::HonoursOnly:
                return "Honours only";
            case ScoreKind::TerminalsOnly:
                return "Terminals only";
            case ScoreKind::AllGreen:
                return "All green";
            case ScoreKind::SevenPairsHonours:
                return "Seven pairs of honours";
            case ScoreKind::SevenPairsOneSuit:
                return "Seven pairs of one suit";
            case ScoreKind::BigThreeDragons:
                return "Big three dragons";
            case ScoreKind::BigFourWinds:
                return "Big four winds";
            case ScoreKind::FourKongs:
                return "Four kongs";
            case ScoreKind::ThirteenOrphans:
                return "Thirteen orphans";
            case ScoreKind::LastTileOneDot:
                return "Last tile, 1 of dots";
            case ScoreKind::LooseTileFiveDots:
                return "Loose tile, 5 of dots";
        }
        return "Unknown";
    }

    eastl::string ScoreLineText(const ScoreLine& line) {
        const eastl::string_view name = ScoreKindName(line.kind);
        if (IsLimitKind(line.kind)) {
            return Format("{}: {}", kLimitHand, name);
        }
        eastl::string text(name);
        if (line.tile.IsValid() && NamesItsTileWithOf(line.kind)) {
            text = Format("{} of {}", name, TileName(line.tile));
        } else if (line.tile.IsValid() && NamesItsTileInBrackets(line.kind)) {
            text = Format("{} ({})", name, TileName(line.tile));
        }
        if (line.doubles > 0) {
            return Format("{}: x{}", text, Multiplier(line.doubles));
        }
        return Format("{}: {}", text, line.points);
    }

    ScoreTable FormatScoreTable(const HandSettlement& settlement, const HandResult& result, const SeatScores& totalsAfter, const SeatSetup& seats) {
        ScoreTable table;
        const SeatWinds winds = MakeSeatWinds(settlement.east);
        const bool won = settlement.kind == ResultKind::Win && IsSeat(settlement.winner);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const eastl::string name = SeatLabel(seat, winds[seat], seats);
            table.seats[seat] =
                won ? Format("{}: hand {}, {:+}, total {}", name, settlement.scores[seat], settlement.deltas[seat], totalsAfter[seat]) : Format("{}: no payment, total {}", name, totalsAfter[seat]);
        }
        if (!won) {
            table.headline = eastl::string(kDrawnHand);
            table.summary = eastl::string(kNoPayments);
            return table;
        }
        table.headline = WinHeadline(settlement, result, seats);
        const ScoreBreakdown& breakdown = settlement.breakdowns[settlement.winner];
        table.summary = WinSummary(breakdown, result);
        const int count = static_cast<int>(breakdown.lines.size());
        const int shown = count > kMaxScoreLines ? kMaxScoreLines - 1 : count;
        for (int i = 0; i < shown; ++i) {
            table.lines.push_back(ScoreLineText(breakdown.lines[i]));
        }
        if (shown < count) {
            table.lines.push_back(Format("+{} more", count - shown));
        }
        return table;
    }

    StandingsTable FormatStandings(const Match& match, const SeatSetup& seats) {
        StandingsTable table;
        const eastl::array<Seat, kSeatCount> order = match.Standings();
        const SeatScores& scores = match.Scores();
        // The wind is not part of the name here: the match is over and "Seat 3 (East)" would name the next deal
        const auto plain = [&](Seat seat) {
            eastl::string name = Format("Seat {}", seat + 1);
            if (IsYou(seat, seats)) {
                name.append(kYouMark.data(), kYouMark.size());
            }
            return name;
        };
        int place = 1;
        int tied = 0;
        for (int i = 0; i < kSeatCount; ++i) {
            if (i > 0 && scores[order[i]] != scores[order[i - 1]]) {
                place = i + 1;
            }
            table.rows[i] = Format("{}. {}: {}", place, plain(order[i]), scores[order[i]]);
            tied += scores[order[i]] == scores[order[0]] ? 1 : 0;
        }
        if (tied == 1) {
            table.headline = Format("{} wins the match", plain(order[0]));
        } else {
            table.headline = "Tied for first:";
            for (int i = 0; i < tied; ++i) {
                table.headline += Format("{} {}", i > 0 ? "," : "", plain(order[i]));
            }
        }
        return table;
    }

}  // namespace BECore::Mahjong
