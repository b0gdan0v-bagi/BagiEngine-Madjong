#include "HandModel.h"

#include <Mahjong/Game/HandShape.h>

namespace BECore::Mahjong {

    namespace {

        bool IsSeat(Seat seat) noexcept {
            return seat >= 0 && seat < kSeatCount;
        }

        /// Whether `hand` is waiting without its concealed tile number `index`.
        bool IsWaitingWithout(const PlayerHand& hand, size_t index) {
            PlayerHand rest = hand;
            rest.concealed.erase(rest.concealed.begin() + index);
            return !WinningTiles(rest).empty();
        }

    }  // namespace

    void HandModel::Begin(const MahjongSettings& settings, Wall wall, const SeatWinds& seatWinds, Wind roundWind) {
        *this = HandModel{};
        _settings = settings;
        _seatWinds = seatWinds;
        _roundWind = roundWind;
        _wall = eastl::move(wall);
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            if (_seatWinds[seat] == Wind::East) {
                _east = seat;
                break;
            }
        }
        _hands = DealHands(_wall, _east, _settings.flowers);
        StartTurn(_east);
    }

    HandStatus HandModel::Discard(Seat seat, Tile tile, bool declareCalling) {
        if (_phase != Phase::Discard) {
            return std::unexpected(HandError::WrongPhase);
        }
        if (seat != _current || !IsSeat(seat)) {
            return std::unexpected(HandError::NotYourTurn);
        }
        PlayerHand& hand = _hands[seat];
        const auto found = eastl::find(hand.concealed.begin(), hand.concealed.end(), tile);
        if (found == hand.concealed.end()) {
            return std::unexpected(HandError::TileNotInHand);
        }
        if (declareCalling) {
            if (hand.callingDeclared) {
                return std::unexpected(HandError::AlreadyCalling);
            }
            if (!IsWaitingWithout(hand, static_cast<size_t>(found - hand.concealed.begin()))) {
                return std::unexpected(HandError::NotWaiting);
            }
        }

        hand.concealed.erase(found);
        if (declareCalling) {
            hand.callingDeclared = true;
            hand.callingAtDiscardCount = hand.discardCount;
        }
        ++hand.discardCount;
        _discards.push_back({tile, seat});
        _drawnTile = Tile{};
        _drawnLoose = false;
        AfterDiscard(seat, tile);
        return {};
    }

    HandStatus HandModel::DeclareWin(Seat seat) {
        if (_phase != Phase::Discard) {
            return std::unexpected(HandError::WrongPhase);
        }
        if (seat != _current || !IsSeat(seat)) {
            return std::unexpected(HandError::NotYourTurn);
        }
        const PlayerHand& hand = _hands[seat];
        if (!hand.callingDeclared) {
            return std::unexpected(HandError::NotCalling);
        }
        if (!_drawnTile.IsValid() || !IsWinningShape(CountTiles(hand.concealed), static_cast<int>(hand.melds.size()))) {
            return std::unexpected(HandError::CannotWin);
        }
        FinishWin(seat, _drawnTile, WinSource::SelfDraw, seat, _drawnLoose);
        return {};
    }

    void HandModel::StartTurn(Seat seat) {
        Receive(seat, _wall.Draw(), false);
    }

    void HandModel::Receive(Seat seat, std::optional<Tile> first, bool loose) {
        std::optional<Tile> tile = first;
        while (tile.has_value() && _settings.flowers && tile->IsBonus()) {
            _hands[seat].bonus.push_back(*tile);
            tile = _wall.DrawLoose();
            loose = true;
        }
        if (!tile.has_value()) {
            FinishDrawn();
            return;
        }
        _hands[seat].AddConcealed(*tile);
        _current = seat;
        _drawnTile = *tile;
        _drawnLoose = loose;
        _phase = Phase::Discard;
    }

    void HandModel::AfterDiscard(Seat seat, Tile /*tile*/) {
        StartTurn(NextSeat(seat));
    }

    void HandModel::FinishDrawn() {
        _result = HandResult{};
        _phase = Phase::Finished;
    }

    void HandModel::FinishWin(Seat winner, Tile tile, WinSource source, Seat from, bool loose) {
        HandResult result;
        result.kind = ResultKind::Win;
        result.winner = winner;
        result.winningTile = tile;
        result.source = source;
        result.from = from;
        result.loose = loose;
        result.lastLiveTile = _wall.LiveCount() == 0;
        _result = result;
        _phase = Phase::Finished;
    }

}  // namespace BECore::Mahjong
