#pragma once

#include <Mahjong/Game/Seats.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/Table/Words.h>

namespace BECore::Mahjong {

    /// How a seat is named on screen: "Seat 2 (South)" (seats count from 1 for a person; `wind` is the seat's wind in the hand). The words are the keys `seat.name` and `wind.*`.
    [[nodiscard]] inline eastl::string SeatName(const UIStrings& strings, Seat seat, Wind wind) {
        const eastl::string number = Format("{}", seat + 1);
        const eastl::string windName = WindName(strings, wind);
        return Tr(strings, "seat.name", {{"n", number}, {"wind", windName}});
    }

    /// Whether `seat` is "you": the person at a table where exactly one person plays (with two or more nobody is, with none nobody is).
    [[nodiscard]] inline bool IsYou(Seat seat, const SeatSetup& seats) {
        int people = 0;
        for (const SeatKind kind : seats) {
            people += kind == SeatKind::Person ? 1 : 0;
        }
        return people == 1 && IsSeat(seat) && seats[seat] == SeatKind::Person;
    }

    /**
     * @brief Who sits at the seats, for the words that name a seat. Built from the seat kinds (a local game: the one person is "you", nobody
     *        has a name) or from a session, which in remote mode (a room) knows the players' names and the own seat.
     *
     * A person with a name is called by it ("Ann"), a computer or a person without one by the seat's number; "you" is the viewer's seat in a room
     * (several persons sit there) and the one person of a local table. Holds a reference: use it within the call it is made for.
     */
    class SeatWho {
    public:
        /// A table with no names: a local game, `seats` says who is "you".
        SeatWho(const SeatSetup& seats) noexcept : _seats(&seats) {}  // NOLINT(google-explicit-constructor): the words took a `SeatSetup` before names existed
        /// The seats of @p session, with its players' names and its viewer in a room.
        SeatWho(const TableSession& session) noexcept : _seats(&session.Seats()), _room(session.IsRemote() ? &session : nullptr) {}  // NOLINT(google-explicit-constructor)

        /// The name of the person at @p seat in a room; null for a computer, a seat with no name and a local table.
        [[nodiscard]] const eastl::string* PlayerName(Seat seat) const noexcept {
            if (_room == nullptr || !IsSeat(seat) || (*_seats)[seat] != SeatKind::Person || _room->NameOf(seat).empty()) {
                return nullptr;
            }
            return &_room->NameOf(seat);
        }

        [[nodiscard]] bool IsYou(Seat seat) const noexcept {
            return _room != nullptr ? IsSeat(seat) && seat == _room->Viewer() : Mahjong::IsYou(seat, *_seats);
        }

    private:
        const SeatSetup* _seats;
        const TableSession* _room = nullptr;
    };

    /// `name`, and the word `seat.you` after it when `seat` is "you".
    [[nodiscard]] inline eastl::string MarkYou(const UIStrings& strings, eastl::string name, Seat seat, const SeatWho& who) {
        if (who.IsYou(seat)) {
            name += Tr(strings, "seat.you");
        }
        return name;
    }

    /// `SeatName`, and the word `seat.you` after it for "you"; a person with a name in a room has it instead of the number ("Ann (South)").
    [[nodiscard]] inline eastl::string SeatLabel(const UIStrings& strings, Seat seat, Wind wind, const SeatWho& who) {
        if (const eastl::string* player = who.PlayerName(seat)) {
            return MarkYou(strings, Tr(strings, "seat.playerWind", {{"name", *player}, {"wind", WindName(strings, wind)}}), seat, who);
        }
        return MarkYou(strings, SeatName(strings, seat, wind), seat, who);
    }

    /// "Seat 2" without the wind (where the wind would name the wrong deal), or the name of the player in a room, and the word `seat.you` for "you".
    [[nodiscard]] inline eastl::string SeatLabel(const UIStrings& strings, Seat seat, const SeatWho& who) {
        if (const eastl::string* player = who.PlayerName(seat)) {
            return MarkYou(strings, *player, seat, who);
        }
        const eastl::string number = Format("{}", seat + 1);
        return MarkYou(strings, Tr(strings, "seat.short", {{"n", number}}), seat, who);
    }

}  // namespace BECore::Mahjong
