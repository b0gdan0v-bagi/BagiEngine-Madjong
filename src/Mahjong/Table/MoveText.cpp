#include "MoveText.h"

#include <Mahjong/Table/ScoreText.h>
#include <Mahjong/Table/SeatName.h>

namespace BECore::Mahjong {

    namespace {

        /// The key of the sentence of a kind of move.
        eastl::string_view KeyOf(const HandMove& move) noexcept {
            switch (move.kind) {
                case MoveKind::Discard:
                    return move.declaredCalling ? "move.discardCalling" : "move.discard";
                case MoveKind::ClaimChow:
                    return "move.chow";
                case MoveKind::ClaimPung:
                    return "move.pung";
                case MoveKind::ClaimKong:
                    return "move.kong";
                case MoveKind::ConcealedKong:
                    return "move.concealedKong";
                case MoveKind::UpgradeKong:
                    return "move.upgradeKong";
            }
            return "move.discard";
        }

    }  // namespace

    eastl::string MoveText(const UIStrings& strings, const HandMove& move, Wind seatWind) {
        if (!IsSeat(move.seat)) {
            return {};
        }
        const eastl::string seat = SeatName(strings, move.seat, seatWind);
        if (move.kind == MoveKind::ConcealedKong) {
            return Tr(strings, KeyOf(move), {{"seat", seat}});  // the tiles of a concealed kong are not public
        }
        return Tr(strings, KeyOf(move), {{"seat", seat}, {"tile", TileName(strings, move.tile)}});
    }

}  // namespace BECore::Mahjong
