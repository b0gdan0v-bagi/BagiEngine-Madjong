#include "DiscardDanger.h"

#include <EASTL/algorithm.h>
#include <Mahjong/Game/ComputerTurn.h>

namespace BECore::Mahjong {

    namespace {

        bool IsDeclaredOpponent(const TableView& view, Seat seat) noexcept {
            return seat != view.seat && view.seats[seat].callingDeclared;
        }

        /// Whether `d` can hold the kind with the id `id`: at least one copy is unseen.
        bool HasUnseen(const TableView& view, int id) noexcept {
            return UnseenCopies(view, Tile{static_cast<uint8_t>(id)}) >= 1;
        }

        /// Whether the partner kinds of rank `lowRank` and `highRank` of the suit of `kind` both have an unseen copy; a rank outside 1..9
        /// is no way.
        bool IsSequenceWayLive(const TableView& view, Tile kind, int lowRank, int highRank) noexcept {
            if (lowRank < 1 || highRank > kRanksPerSuit) {
                return false;
            }
            const int rank = kind.Rank();
            return HasUnseen(view, kind.Id() + lowRank - rank) && HasUnseen(view, kind.Id() + highRank - rank);
        }

        /// The number of live sequence ways of a suit kind (0 for an honour).
        int LiveSequenceWays(const TableView& view, Tile kind) noexcept {
            if (!kind.IsSuit()) {
                return 0;
            }
            const int rank = kind.Rank();
            int ways = 0;
            ways += IsSequenceWayLive(view, kind, rank - 2, rank - 1) ? 1 : 0;
            ways += IsSequenceWayLive(view, kind, rank - 1, rank + 1) ? 1 : 0;
            ways += IsSequenceWayLive(view, kind, rank + 1, rank + 2) ? 1 : 0;
            return ways;
        }

        /// Rule 1.
        bool PassedAfterDeclaration(const TableView& view, Seat declared, Tile kind) noexcept {
            return view.seats[declared].passedKinds[kind.Id()] > 0;
        }

        /// Rule 2: not even a pair way (no unseen copy) and no sequence way.
        bool NoLiveWay(const TableView& view, Tile kind, int sequenceWays) noexcept {
            return UnseenCopies(view, kind) < 1 && sequenceWays == 0;
        }

        /// Rule 3.
        Danger GradeBySequenceWays(int sequenceWays) noexcept {
            if (sequenceWays >= kSequenceWaysHigh) {
                return Danger::High;
            }
            return sequenceWays == kSequenceWaysMedium ? Danger::Medium : Danger::Low;
        }

        /// Rules 2 and 3: the grade of `kind` by its live ways alone, the same against every declared opponent.
        Danger GradeByWays(const TableView& view, Tile kind) noexcept {
            const int sequenceWays = LiveSequenceWays(view, kind);
            return NoLiveWay(view, kind, sequenceWays) ? Danger::Safe : GradeBySequenceWays(sequenceWays);
        }

        /// Rule 4.
        bool OwnDiscard(const TableView& view, Seat declared, Tile kind) noexcept {
            return view.seats[declared].discardedKinds[kind.Id()] > 0;
        }

        /// The grade of `kind` against the declared opponent `declared`, `byWays` being `GradeByWays`.
        Danger DangerAgainst(const TableView& view, Seat declared, Tile kind, Danger byWays) noexcept {
            if (PassedAfterDeclaration(view, declared, kind)) {
                return Danger::Safe;
            }
            return OwnDiscard(view, declared, kind) && byWays > Danger::Low ? Danger::Low : byWays;
        }

    }  // namespace

    bool HasDeclaredOpponent(const TableView& view) noexcept {
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (IsDeclaredOpponent(view, seat)) {
                return true;
            }
        }
        return false;
    }

    Danger DangerOf(const TableView& view, Tile tile) noexcept {
        if (!tile.IsPlayable()) {
            return Danger::Safe;
        }
        const Danger byWays = GradeByWays(view, tile);
        Danger worst = Danger::Safe;
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (IsDeclaredOpponent(view, seat)) {
                worst = eastl::max(worst, DangerAgainst(view, seat, tile, byWays));
            }
        }
        return worst;
    }

    DangerTable DiscardDangers(const TableView& view) noexcept {
        DangerTable table;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            table[id] = DangerOf(view, Tile{id});
        }
        return table;
    }

}  // namespace BECore::Mahjong
