#include "ComputerView.h"

namespace BECore::Mahjong {

    namespace {

        void AddCounts(TileCounts& into, const TileCounts& more) noexcept {
            for (uint8_t id = 0; id < kKindCount; ++id) {
                into[id] = static_cast<uint8_t>(into[id] + more[id]);
            }
        }

        PublicSeat MakePublicSeat(const PlayerHand& hand) {
            PublicSeat info;
            for (const Meld& meld : hand.melds) {
                if (meld.concealed) {
                    ++info.concealedKongs;
                } else {
                    info.openMelds.push_back(meld);
                }
            }
            info.bonusCount = static_cast<int>(hand.bonus.size());
            info.discardCount = hand.discardCount;
            info.callingDeclared = hand.callingDeclared;
            return info;
        }

        /// The copies `seat` sees: see `TableView`.
        TileCounts CountVisible(const HandModel& model, Seat seat, const eastl::array<PublicSeat, kSeatCount>& seats) {
            const PlayerHand& own = model.GetHand(seat);
            TileCounts visible = CountTiles(own.concealed);
            AddCounts(visible, CountMeldTiles(own.melds));
            for (Seat other = 0; other < kSeatCount; ++other) {
                if (other != seat) {
                    AddCounts(visible, CountMeldTiles(seats[other].openMelds));
                }
            }
            for (const DiscardEntry& entry : model.Discards()) {
                if (entry.tile.IsPlayable()) {
                    ++visible[entry.tile.Id()];
                }
            }
            const Tile pending = model.PendingTile();
            if (model.GetPhase() == Phase::RobKong && model.CurrentSeat() != seat && pending.IsPlayable()) {
                ++visible[pending.Id()];
            }
            return visible;
        }

    }  // namespace

    TableView MakeTableView(const HandModel& model, Seat seat) {
        TableView view;
        if (!IsSeat(seat)) {
            return view;
        }
        view.seat = seat;
        view.phase = model.GetPhase();
        view.currentSeat = model.CurrentSeat();
        view.pendingTile = model.PendingTile();
        view.seatWind = model.SeatWind(seat);
        view.roundWind = model.RoundWind();
        view.own = model.GetHand(seat);
        for (Seat other = 0; other < kSeatCount; ++other) {
            view.seats[other] = MakePublicSeat(model.GetHand(other));
        }
        view.visible = CountVisible(model, seat, view.seats);
        view.liveCount = static_cast<int>(model.GetWall().LiveCount());
        view.legal = model.LegalActions(seat);
        return view;
    }

}  // namespace BECore::Mahjong
