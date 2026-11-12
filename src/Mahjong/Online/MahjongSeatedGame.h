#pragma once

#include <Games/ISeatedGame.h>
#include <Games/SeatedGameRegistry.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/Match.h>

namespace BECore::Mahjong {

    /// The name a room server registers the game under (`create_room {game}`).
    inline constexpr eastl::string_view kSeatedGameName = "mahjong";

    /// The `format` number in every view of a started game. A client reads exactly its own number and says so in words when a server
    /// sends another. Bump it with every change of the keys or the meaning of a view that an old client cannot read (a change in
    /// `MahjongStateJson` that reaches the view, a new required key, a changed type).
    inline constexpr int kOnlineViewFormat = 1;

    /// The key of the room setting "hints and the coach are allowed at this table": an option of `Start` and a boolean in every view of a
    /// started game. Additive, so `kOnlineViewFormat` stays: a reader that does not know the key reads "not allowed".
    inline constexpr eastl::string_view kHintsAllowedKey = "hintsAllowed";

    /// The seed of the computer that answers at state version `version` of a match seeded `seed`: one function of both, so the same state
    /// gives the same answer and the next state another one.
    [[nodiscard]] uint32_t MixSeed(uint32_t seed, uint64_t version) noexcept;

    /**
     * @brief Mahjong for four seats in a room: `ISeatedGame` over `Match` and `HandModel`, with no scene, no `MahjongGame` and no
     *        `TableSession`. Contract and reasons: `Online/CLAUDE.md`.
     *
     * Room seat `i` is Mahjong seat `i`. The adapter knows no person from a computer: the room decides who is a computer at any moment
     * and asks `ChooseComputerAction` for those seats. Options of `Start` (untrusted, other members ignored): `hands` 8 or 16 (default
     * 16), `level` 1..10 (the level of every computer seat, default `kDefaultLevel`), `hintsAllowed` (boolean, default false: the host's
     * decision whether the players may use hints and the coach; kept for the whole match).
     *
     * The view of a seat is the table state of `MahjongStateJson` for that seat without `kind`, `level` and `levels`, plus `format`
     * (`kOnlineViewFormat`), `hintsAllowed` (the room's setting, the same for every seat) and `ready` (four booleans: the seats that sent
     * `next_hand` in the finished hand). The tiles of the other hands are never in it; in a finished hand `result` shows all four (the
     * payments depend on them).
     *
     * Actions: those of `ActionToJson` for the seats that have to decide (the player to act, every undecided seat of a claim window);
     * `{"type":"next_hand"}` once per seat in a finished hand, and the hand is settled when all four seats have sent it.
     */
    class MahjongSeatedGame : public ISeatedGame {
    public:
        /// What the registry needs to know without creating the game.
        [[nodiscard]] static SeatedGameInfo Info();

        [[nodiscard]] int GetSeatCount() const override;
        std::expected<void, ControlError> Start(uint64_t seed, const Json& options) override;
        [[nodiscard]] std::expected<Json, ControlError> GetSeatView(int seat) const override;
        [[nodiscard]] std::expected<Json, ControlError> GetSeatActions(int seat) const override;
        std::expected<Json, ControlError> PerformSeatAction(int seat, const Json& action) override;
        [[nodiscard]] std::optional<Json> ChooseComputerAction(int seat) const override;
        [[nodiscard]] bool IsFinished() const override;
        [[nodiscard]] uint64_t GetStateVersion() const override;

        /// For tests, never from the server: the truth behind the views. A test that changes the match through the non-const one
        /// keeps the version in step itself (it does not move; `AnnounceChange()` does).
        [[nodiscard]] const Match& GetMatch() const noexcept {
            return _match;
        }
        [[nodiscard]] Match& GetMatch() noexcept {
            return _match;
        }
        /// For tests: moves the version, so that a position laid through `GetMatch()` is sent to the clients as if a move had made it.
        void AnnounceChange() noexcept {
            ++_version;
        }

    private:
        /// What `seat` may send now; `nextHand` stands for the ready check, `hand` for the actions in the hand.
        struct SeatMoves {
            bool nextHand = false;
            eastl::vector<HandAction> hand;

            [[nodiscard]] bool Empty() const noexcept {
                return !nextHand && hand.empty();
            }
        };

        [[nodiscard]] SeatMoves MovesOf(Seat seat) const;
        [[nodiscard]] bool AllReadyBut(Seat seat) const;

        Match _match;
        ComputerLevel _level = kDefaultLevel;
        bool _hintsAllowed = false;
        uint32_t _matchSeed = 0;
        uint64_t _version = 0;
        eastl::array<bool, kSeatCount> _ready{};
    };

}  // namespace BECore::Mahjong
