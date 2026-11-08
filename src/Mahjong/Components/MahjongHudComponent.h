#pragma once

#include <BECore/Scene/IComponent.h>
#include <Mahjong/Game/TableSession.h>

namespace BECore {

    namespace InputEvents {
        struct ActionEvent;
    }

}  // namespace BECore

namespace BECore::Mahjong {

    class MahjongGame;

    /// The seat count of the variable names `seatName0` .. `seatName3`.
    inline constexpr int kHudSeats = kSeatCount;

    /**
     * @brief The words and numbers of the HUD, and the seat choice of a new game: the canvases `Hud` and `Setup` of
     *        `config/SceneConfig.xml` show what this component publishes to `CoreManager::GetUIVars()` and emit the actions it answers.
     *
     * **Variables** (set when `TableSession::Version()` moved, or the game changed; nothing is set again otherwise): `round` (the round
     * wind), `hand` (the number of the hand in progress, at most `handsTotal`), `handsTotal`, `wall` (live tiles left), `turn` (the seat
     * whose turn it is, never the seat that is asked to claim: that would tell a person that this computer holds a claim; `Hand over`
     * and `Match over` when the hand or the match is over), and per seat `seatName` (`Seat 2 (South)`, with ` You` for the person when
     * exactly one person plays), `seatWind`, `seatScore`, `seatMark` (`>` on the seat whose turn it is) and `seatDealer` (`*` on East).
     * They carry no tile and nothing a seat could not see.
     *
     * **Setup.** `MahjongGame::IsSetupOpen()` decides whether the modal canvas `Setup` is shown (this component shows and hides it every
     * frame). Its four `UISegmented` groups are bound to `seatKind0` .. `seatKind3` (`person` | `computer`) and emit
     * `MahjongSetSeat0` .. `MahjongSetSeat3` with the value; this component changes the pending `SeatSetup` and the variable, and
     * `MahjongStart` gives the pending setup to the game and begins a match with a random seed (`StartRandomMatch`), which closes the
     * panel; a start the game refuses leaves it open. `MahjongNewGame` (the HUD button) opens it again, with the pending setup of the
     * last match. Nothing is played while it is open (`MahjongTableComponent` takes no click and steps no computer).
     */
    class MahjongHudComponent : public IComponent {
        BE_CLASS(MahjongHudComponent)
    public:
        MahjongHudComponent() = default;
        ~MahjongHudComponent() override;

        void OnAttached() override;
        void OnDraw() override;

        /// The seats the panel shows and `MahjongStart` would begin a match with.
        [[nodiscard]] const SeatSetup& PendingSeats() const noexcept {
            return _pending;
        }

        /// The attached HUD, if any; the game assumes a single one.
        static MahjongHudComponent* GetActive() noexcept {
            return s_active;
        }

    private:
        void OnAction(const InputEvents::ActionEvent& event);
        /// Sets the variables of the HUD when the version of the session (or the game) moved since the last time.
        void PublishNumbers(const MahjongGame* game);
        /// Shows or hides `Setup` after the game, and takes the setup of the game when the panel opens.
        void PublishSetup(const MahjongGame* game);
        void PublishSeatKinds() const;

        SeatSetup _pending = kDefaultSeatSetup;
        bool _setupShown = false;
        const MahjongGame* _numbersOf = nullptr;  // the game and the version the variables show
        uint64_t _numbersVersion = 0;
        bool _numbersPublished = false;
        static MahjongHudComponent* s_active;
    };

}  // namespace BECore::Mahjong
