#pragma once

#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/Tile.h>

namespace BECore::Mahjong {

    CORE_ENUM(MeldKind, uint8_t, Chow, Pung, Kong)

    /**
     * @brief A set laid beside the hand: a chow, a pung or a kong.
     *
     * `first` is the lowest tile of a chow and the tile of a pung or kong. A kong declared from four tiles of the hand is `concealed`
     * (`fromDiscard` false); every other meld is open. `from` and `claimed` are the discarder and the tile of an open meld, unused for
     * a concealed kong. An upgraded kong is the pung's own meld turned into a `Kong`: `from` and `claimed` stay those of the pung.
     */
    struct Meld {
        MeldKind kind = MeldKind::Pung;
        bool concealed = false;
        Tile first;
        Seat from = 0;
        bool fromDiscard = false;
        Tile claimed;
    };

    /**
     * @brief What one seat holds in a hand.
     *
     * `concealed` is kept sorted (`AddConcealed`). Four equal tiles in it are not a kong: a kong exists only as a `Meld`.
     * `callingAtDiscardCount` is the number of discards the seat had made when it declared (0 on the first discard).
     */
    struct PlayerHand {
        Vector<Tile> concealed;
        Vector<Meld> melds;
        Vector<Tile> bonus;
        bool callingDeclared = false;
        int callingAtDiscardCount = 0;
        int discardCount = 0;

        void AddConcealed(Tile tile) {
            concealed.insert(eastl::upper_bound(concealed.begin(), concealed.end(), tile), tile);
        }

        /// Takes one copy of `tile` out of `concealed`; false (and nothing changes) when there is none. The model only removes tiles it
        /// has checked (`CountConcealed`) or knows are there (the tiles of a claim, a kong or a discard), so a false is a defect of the
        /// caller, not an input to answer.
        bool RemoveConcealed(Tile tile) {
            const auto found = eastl::find(concealed.begin(), concealed.end(), tile);
            if (found == concealed.end()) {
                return false;
            }
            concealed.erase(found);
            return true;
        }

        [[nodiscard]] int CountConcealed(Tile tile) const noexcept {
            return static_cast<int>(eastl::count(concealed.begin(), concealed.end(), tile));
        }
    };

}  // namespace BECore::Mahjong
