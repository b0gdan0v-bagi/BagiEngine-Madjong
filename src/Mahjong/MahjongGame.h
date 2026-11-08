#pragma once

#include <Games/IGame.h>
#include <Mahjong/Game/MahjongSettings.h>
#include <Mahjong/Game/Match.h>
#include <Mahjong/Game/TableSession.h>
#include <Mahjong/MahjongGameControl.h>

namespace BECore::Mahjong {

    /**
     * @brief Concrete IGame for mahjong: it owns the match (`Game/Match.h`), the rules of classic Chinese mahjong for four seats, and
     *        the `TableSession` over it (`Game/TableSession.h`), which every front end drives.
     *
     * Activates "MahjongScene" when started. Auto-registered into AbstractFactory<IGame> via the BE_CLASS reflection machinery, no
     * hand-written registrar. The rules are plain C++ under Game/; the scene glue in Components/ reaches the match through
     * GetActive()->GetMatch() and plays it through GetActive()->GetSession().
     *
     * `Start()` and `Reset()` begin a new match with a seed from `std::random_device`: the randomness is chosen here, outside the rules,
     * which only ever see the seed (`StartMatch(seed)` is how a test or a control API replays a match). A match is begun with
     * `GetSettings()` (the defaults, `MahjongSettings{}`, until `SetSettings`) and the seat setup `GetSeats()` (`kDefaultSeatSetup`:
     * seat 0 a person, the others computers); both are kept across `Reset()`. `AsControllable()` hands out the control adapter, which
     * drives the match through the session. The seat choice of a new game is the panel `Setup` of the scene (`IsSetupOpen`).
     */
    class MahjongGame : public IGame {
        BE_CLASS(MahjongGame)
    public:
        MahjongGame() = default;
        ~MahjongGame() override = default;

        PoolString GetName() const override;
        PoolString GetSceneName() const override;

        /// The adapter of the external control API (`MahjongGameControl.h`): a client plays the person seats of the session.
        IControllableGame* AsControllable() override;

        void Start() override;
        void Reset() override;

        /// The match: started by `Start()` / `Reset()` / `StartMatch`, `NotStarted` before the game is first started.
        Match& GetMatch() {
            return _match;
        }
        const Match& GetMatch() const {
            return _match;
        }

        /// The session over the match: seat setup, computer players, viewer, state version. Drive the match through it.
        TableSession& GetSession() {
            return _session;
        }
        const TableSession& GetSession() const {
            return _session;
        }

        /// The settings of the next match; a match in progress keeps the ones it was started with.
        const MahjongSettings& GetSettings() const {
            return _settings;
        }
        void SetSettings(const MahjongSettings& settings) {
            _settings = settings;
        }
        /// Who plays each seat in the next match.
        const SeatSetup& GetSeats() const {
            return _seats;
        }
        void SetSeats(const SeatSetup& seats) {
            _seats = seats;
        }

        /// Begins a new match through the session with `GetSettings()`, `GetSeats()` and `seed` (one seed is one match, the first hand
        /// included). `BadSettings`, and nothing changes, for settings that are not playable. A match that began closes the setup panel.
        MatchStatus StartMatch(uint32_t seed);

        /// `StartMatch` with a seed from `std::random_device`; a refusal (it cannot happen with the default settings) is logged and
        /// returned.
        MatchStatus StartRandomMatch();

        /// Whether the panel that asks which seats are people is open (`MahjongHudComponent` shows it). `Start()` opens it over the match
        /// that it begins; every new match (`StartMatch`, a control `reset`) and a control action close it; the HUD button opens it.
        /// While it is open the table is not played: no click is taken and no computer steps.
        bool IsSetupOpen() const {
            return _setupOpen;
        }
        void OpenSetup() {
            _setupOpen = true;
        }
        void CloseSetup() {
            _setupOpen = false;
        }

        /// The active game, or nullptr when no game is active or the active game is not Mahjong.
        static MahjongGame* GetActive();

    private:
        Match _match;
        MahjongSettings _settings;
        SeatSetup _seats = kDefaultSeatSetup;
        bool _setupOpen = false;
        TableSession _session{_match};  // after _match: it keeps a reference
        MahjongGameControl _control{*this};
    };

}  // namespace BECore::Mahjong
