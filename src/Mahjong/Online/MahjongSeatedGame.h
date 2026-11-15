#pragma once

#include <Games/ISeatedGame.h>
#include <Games/SeatedGameRegistry.h>
#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/Match.h>

namespace BECore::Mahjong {

    /// The name a room server registers the game under (`create_room {game}`).
    inline constexpr StringView kSeatedGameName = "mahjong";

    /// The `format` in every view of a started game: a client reads exactly its own number and refuses another in words. Bump it with every
    /// change of the keys or the meaning of a view that an old client cannot read (also one made in `MahjongStateJson`).
    inline constexpr int kOnlineViewFormat = 1;

    /// An option of `Start` and a boolean in every view. Additive, so it did not bump `kOnlineViewFormat`: a reader that does not know the
    /// key reads "not allowed".
    inline constexpr StringView kHintsAllowedKey = "hintsAllowed";

    /// The seed of a computer's answer: one function of the match seed and the state version, so the same state gives the same answer.
    [[nodiscard]] uint32_t MixSeed(uint32_t seed, uint64_t version) noexcept;

    /**
     * @brief Mahjong for four seats in a room: `ISeatedGame` over `Match` and `HandModel`, with no scene, no `MahjongGame` and no
     *        `TableSession`. Contract and reasons: `src/Mahjong/Online/CLAUDE.md`; the interface: `engine/src/Modules/Games/CLAUDE.md`,
     *        "Seats".
     *
     * Room seat `i` is Mahjong seat `i`. The adapter knows no person from a computer: the room decides who is a computer at any moment
     * and asks `ChooseComputerAction` for those seats. The options of `Start` are untrusted: a wrong member is an error that changes
     * nothing, others are ignored.
     *
     * The view of a seat is its own hand and the public table, never another hand; only a finished hand shows all four, in `result`. A
     * change of its keys is a change of the wire format (`kOnlineViewFormat`).
     */
    class MahjongSeatedGame : public ISeatedGame {
    public:
        [[nodiscard]] static SeatedGameInfo Info();

        [[nodiscard]] int GetSeatCount() const override;
        std::expected<void, ControlError> Start(uint64_t seed, const Json& options) override;
        [[nodiscard]] std::expected<Json, ControlError> GetSeatView(int seat) const override;
        [[nodiscard]] std::expected<Json, ControlError> GetSeatActions(int seat) const override;
        std::expected<Json, ControlError> PerformSeatAction(int seat, const Json& action) override;
        [[nodiscard]] Optional<Json> ChooseComputerAction(int seat) const override;
        [[nodiscard]] bool IsFinished() const override;
        [[nodiscard]] uint64_t GetStateVersion() const override;

        /// For tests, never from the server: the truth behind the views. Changing the match through the non-const one does not move the
        /// version: `AnnounceChange()` does.
        [[nodiscard]] const Match& GetMatch() const noexcept {
            return _match;
        }
        [[nodiscard]] Match& GetMatch() noexcept {
            return _match;
        }
        /// For tests: sends a position laid through `GetMatch()` to the clients as if a move had made it.
        void AnnounceChange() noexcept {
            ++_version;
        }

    private:
        /// `nextHand` stands for the ready check.
        struct SeatMoves {
            bool nextHand = false;
            Vector<HandAction> hand;

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
        Array<bool, kSeatCount> _ready{};
    };

}  // namespace BECore::Mahjong
