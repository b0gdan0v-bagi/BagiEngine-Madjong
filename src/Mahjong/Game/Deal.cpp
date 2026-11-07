#include "Deal.h"

namespace BECore::Mahjong {

    namespace {

        /// Gives `hand` one tile for a slot of the deal: the drawn tile, or after the bonus tiles the loose tile that is not one.
        /// Returns false when the wall has nothing left to give.
        bool DealOne(Wall& wall, PlayerHand& hand, bool flowers) {
            std::optional<Tile> tile = wall.Draw();
            while (tile.has_value() && flowers && tile->IsBonus()) {
                hand.bonus.push_back(*tile);
                tile = wall.DrawLoose();
            }
            if (!tile.has_value()) {
                return false;
            }
            hand.AddConcealed(*tile);
            return true;
        }

    }  // namespace

    eastl::array<PlayerHand, kSeatCount> DealHands(Wall& wall, Seat east, bool flowers) {
        eastl::array<PlayerHand, kSeatCount> hands;
        for (int round = 0; round < kDealBlockRounds; ++round) {
            for (int step = 0; step < kSeatCount; ++step) {
                PlayerHand& hand = hands[(east + step) % kSeatCount];
                for (int i = 0; i < kDealBlock; ++i) {
                    if (!DealOne(wall, hand, flowers)) {
                        return hands;
                    }
                }
            }
        }
        for (int step = 0; step < kSeatCount; ++step) {
            if (!DealOne(wall, hands[(east + step) % kSeatCount], flowers)) {
                return hands;
            }
        }
        return hands;
    }

}  // namespace BECore::Mahjong
