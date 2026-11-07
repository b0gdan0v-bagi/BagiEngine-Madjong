#include "ScoreHand.h"

#include <Mahjong/Game/HandShape.h>

namespace BECore::Mahjong {

    namespace {

        /// How many tiles of the kind `tile` the open meld `meld` holds.
        int CopiesInMeld(const Meld& meld, Tile tile) noexcept {
            if (meld.concealed) {
                return 0;
            }
            switch (meld.kind) {
                case MeldKind::Chow: {
                    const eastl::array<Tile, kTilesPerSet> members = ChowTiles(meld.first);
                    return static_cast<int>(eastl::count(members.begin(), members.end(), tile));
                }
                case MeldKind::Pung:
                    return meld.first == tile ? kTilesPerSet : 0;
                case MeldKind::Kong:
                    return meld.first == tile ? kCopiesPerTile : 0;
            }
            return 0;
        }

        ScoreInput MakeScoreInput(const HandModel& model, Seat seat, const std::optional<HandResult>& result) {
            ScoreInput input;
            input.hand = model.GetHand(seat);
            input.seatWind = model.SeatWind(seat);
            input.roundWind = model.RoundWind();
            input.limit = model.GetSettings().limit;
            if (result.has_value() && result->kind == ResultKind::Win && result->winner == seat) {
                input.won = true;
                input.result = *result;
                input.visibleWinningTileCopies = CountVisibleCopies(model, result->winningTile);
            }
            return input;
        }

    }  // namespace

    int CountVisibleCopies(const HandModel& model, Tile winningTile) noexcept {
        int copies = static_cast<int>(eastl::count_if(model.Discards().begin(), model.Discards().end(), [winningTile](const DiscardEntry& entry) { return entry.tile == winningTile; }));
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            for (const Meld& meld : model.GetHand(seat).melds) {
                copies += CopiesInMeld(meld, winningTile);
            }
        }
        return copies;
    }

    HandSettlement SettleHand(const HandModel& finished) {
        const std::optional<HandResult>& result = finished.Result();
        HandSettlement settlement;
        settlement.kind = result.has_value() ? result->kind : ResultKind::Drawn;
        settlement.winner = settlement.kind == ResultKind::Win ? result->winner : kNoSeat;
        settlement.east = finished.East();
        settlement.roundWind = finished.RoundWind();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            settlement.breakdowns[seat] = ScoreSeat(MakeScoreInput(finished, seat, result));
            settlement.scores[seat] = settlement.breakdowns[seat].score;
        }
        settlement.deltas = Settle(settlement.scores, settlement.winner, settlement.east);
        return settlement;
    }

}  // namespace BECore::Mahjong
