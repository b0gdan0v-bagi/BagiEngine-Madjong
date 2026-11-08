#include "MahjongHudComponent.h"

#include <BECore/GameManager/CoreManager.h>
#include <BECore/Scene/Scene.h>
#include <BECore/UI/UICanvasComponent.h>
#include <Events/InputEvents.h>
#include <Generated/MahjongHudComponent.gen.hpp>
#include <Mahjong/MahjongGame.h>
#include <Mahjong/Table/SeatName.h>

namespace BECore::Mahjong {

    namespace {

        constexpr eastl::string_view kSetSeatPrefix = "MahjongSetSeat";
        constexpr eastl::string_view kPersonId = "person";
        constexpr eastl::string_view kComputerId = "computer";
        constexpr eastl::string_view kTurnMark = ">";
        constexpr eastl::string_view kDealerMark = "*";
        constexpr eastl::string_view kHandOver = "Hand over";
        constexpr eastl::string_view kMatchOver = "Match over";
        constexpr eastl::string_view kYou = " You";

        /// "seatName2": the variable of a seat.
        PoolString SeatVar(const char* base, Seat seat) {
            return PoolString::Intern(Format("{}{}", base, seat));
        }

        eastl::string_view KindId(SeatKind kind) {
            return kind == SeatKind::Person ? kPersonId : kComputerId;
        }

        /// The seat that `MahjongSetSeatN` is about; nullopt for another action.
        std::optional<Seat> SeatOfAction(eastl::string_view action) {
            if (action.size() != kSetSeatPrefix.size() + 1 || action.substr(0, kSetSeatPrefix.size()) != kSetSeatPrefix) {
                return std::nullopt;
            }
            const int seat = action.back() - '0';
            return IsSeat(seat) ? std::optional<Seat>(seat) : std::nullopt;
        }

        std::optional<SeatKind> KindOfValue(eastl::string_view value) {
            if (value == kPersonId) {
                return SeatKind::Person;
            }
            if (value == kComputerId) {
                return SeatKind::Computer;
            }
            return std::nullopt;
        }

        /// Who is whose turn on the table, for the label `Turn:`.
        eastl::string TurnText(const TableSession& session, const Match& match) {
            switch (session.GetPhase()) {
                case SessionPhase::HandOver:
                    return eastl::string(kHandOver);
                case SessionPhase::MatchOver:
                    return eastl::string(kMatchOver);
                case SessionPhase::Playing:
                    break;
            }
            const HandModel& hand = match.Hand();
            return SeatName(hand.CurrentSeat(), hand.SeatWind(hand.CurrentSeat()));
        }

    }  // namespace

    MahjongHudComponent* MahjongHudComponent::s_active = nullptr;

    MahjongHudComponent::~MahjongHudComponent() {
        if (s_active == this) {
            s_active = nullptr;
        }
    }

    void MahjongHudComponent::OnAttached() {
        s_active = this;
        SubscribeInScene<InputEvents::ActionEvent, &MahjongHudComponent::OnAction>(this);
    }

    void MahjongHudComponent::OnDraw() {
        const MahjongGame* game = MahjongGame::GetActive();
        PublishNumbers(game);
        PublishSetup(game);
    }

    void MahjongHudComponent::PublishNumbers(const MahjongGame* game) {
        const bool started = game != nullptr && game->GetMatch().GetPhase() != MatchPhase::NotStarted;
        if (!started) {
            _numbersOf = nullptr;
            _numbersPublished = false;
            return;
        }
        const TableSession& session = game->GetSession();
        if (_numbersPublished && _numbersOf == game && _numbersVersion == session.Version()) {
            return;
        }
        _numbersOf = game;
        _numbersVersion = session.Version();
        _numbersPublished = true;

        const Match& match = game->GetMatch();
        const HandModel& hand = match.Hand();
        UIVars& vars = CoreManager::GetUIVars();
        vars.Set("round"_intern, EnumUtils<Wind>::ToString(match.RoundWind()));
        vars.Set("hand"_intern, std::min(match.HandsPlayed() + 1, match.HandsTotal()));
        vars.Set("handsTotal"_intern, match.HandsTotal());
        vars.Set("wall"_intern, static_cast<int>(hand.GetWall().LiveCount()));
        vars.Set("turn"_intern, eastl::string_view(TurnText(session, match)));

        const bool playing = session.GetPhase() == SessionPhase::Playing;
        const bool singlePerson = session.PersonCount() == 1;
        for (Seat seat = 0; seat < kHudSeats; ++seat) {
            const Wind wind = hand.SeatWind(seat);
            eastl::string name = SeatName(seat, wind);
            if (singlePerson && session.KindOf(seat) == SeatKind::Person) {
                name.append(kYou.data(), kYou.size());
            }
            vars.Set(SeatVar("seatName", seat), eastl::string_view(name));
            vars.Set(SeatVar("seatWind", seat), EnumUtils<Wind>::ToString(wind));
            vars.Set(SeatVar("seatScore", seat), match.Scores()[seat]);
            vars.Set(SeatVar("seatMark", seat), playing && hand.CurrentSeat() == seat ? kTurnMark : eastl::string_view());
            vars.Set(SeatVar("seatDealer", seat), match.East() == seat ? kDealerMark : eastl::string_view());
        }
    }

    void MahjongHudComponent::PublishSeatKinds() const {
        UIVars& vars = CoreManager::GetUIVars();
        for (Seat seat = 0; seat < kHudSeats; ++seat) {
            vars.Set(SeatVar("seatKind", seat), KindId(_pending[seat]));
        }
    }

    void MahjongHudComponent::PublishSetup(const MahjongGame* game) {
        const bool open = game != nullptr && game->IsSetupOpen();
        if (open && !_setupShown) {
            _pending = game->GetSeats();  // the panel opens on the seats of the last match
        }
        if (open) {
            PublishSeatKinds();
        }
        if (open != _setupShown) {
            _setupShown = open;
            UICanvasComponent::SetVisibleByName(GetScene(), "Setup"_intern, open);
        }
    }

    void MahjongHudComponent::OnAction(const InputEvents::ActionEvent& event) {
        MahjongGame* game = MahjongGame::GetActive();
        if (game == nullptr) {
            return;
        }
        const eastl::string_view action = event.action.ToStringView();
        if (action == "MahjongNewGame") {
            game->OpenSetup();
            return;
        }
        // The rest belongs to the panel: a click that reached it while it is not open is out of date
        if (!game->IsSetupOpen()) {
            return;
        }
        if (action == "MahjongStart") {
            game->SetSeats(_pending);
            if (!game->StartRandomMatch().has_value()) {
                LOG_WARNING("MahjongHudComponent: the match did not start, the seat choice stays open");
            }
            return;
        }
        const std::optional<Seat> seat = SeatOfAction(action);
        const std::optional<SeatKind> kind = KindOfValue(event.value.ToStringView());
        if (seat && kind) {
            _pending[*seat] = *kind;
            PublishSeatKinds();
        }
    }

}  // namespace BECore::Mahjong
