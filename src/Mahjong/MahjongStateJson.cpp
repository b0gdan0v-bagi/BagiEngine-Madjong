#include "MahjongStateJson.h"

#include <BECore/Control/Json.h>

namespace BECore::Mahjong {

    namespace {

        constexpr int kChowTiles = 3;
        constexpr int kPungTiles = 3;
        constexpr int kKongTiles = 4;

        eastl::string_view TurnPhaseName(Phase phase) {
            switch (phase) {
                case Phase::Discard:
                    return "discard";
                case Phase::Claim:
                    return "claim";
                case Phase::RobKong:
                    return "rob_kong";
                case Phase::Finished:
                    return "finished";
            }
            return "finished";
        }

        eastl::string_view MeldKindName(MeldKind kind) {
            switch (kind) {
                case MeldKind::Chow:
                    return "chow";
                case MeldKind::Pung:
                    return "pung";
                case MeldKind::Kong:
                    return "kong";
            }
            return "pung";
        }

        eastl::string_view ResultKindName(ResultKind kind) {
            return kind == ResultKind::Win ? "win" : "drawn";
        }

        eastl::string_view SourceName(WinSource source) {
            switch (source) {
                case WinSource::Discard:
                    return "discard";
                case WinSource::SelfDraw:
                    return "self_draw";
                case WinSource::RobbedKong:
                    return "robbed_kong";
            }
            return "self_draw";
        }

        /// The tiles of a meld as they lie: three in sequence for a chow, three or four equal ones.
        Json MeldTilesToJson(const Meld& meld) {
            Json tiles = Json::array();
            if (meld.kind == MeldKind::Chow) {
                for (int offset = 0; offset < kChowTiles; ++offset) {
                    tiles.push_back(TileToJson(Tile::Suited(meld.first.Suit(), meld.first.Rank() + offset)));
                }
                return tiles;
            }
            const int count = meld.kind == MeldKind::Kong ? kKongTiles : kPungTiles;
            for (int index = 0; index < count; ++index) {
                tiles.push_back(TileToJson(meld.first));
            }
            return tiles;
        }

        Json MeldToJson(const Meld& meld) {
            Json json = Json::object();
            json["kind"] = MakeJsonString(MeldKindName(meld.kind));
            json["tiles"] = MeldTilesToJson(meld);
            json["concealed"] = meld.concealed;
            json["from"] = meld.concealed ? Json(nullptr) : SeatOrNull(meld.from);
            json["fromDiscard"] = meld.fromDiscard;
            json["claimed"] = meld.concealed ? Json(nullptr) : TileToJson(meld.claimed);
            return json;
        }

    }  // namespace

    SessionPhase PhaseOfMatch(const Match& match) noexcept {
        if (match.GetPhase() != MatchPhase::InHand) {
            return SessionPhase::MatchOver;
        }
        return match.Hand().GetPhase() == Phase::Finished ? SessionPhase::HandOver : SessionPhase::Playing;
    }

    eastl::string_view PhaseName(SessionPhase phase) {
        switch (phase) {
            case SessionPhase::Playing:
                return "playing";
            case SessionPhase::HandOver:
                return "hand_over";
            case SessionPhase::MatchOver:
                return "match_over";
        }
        return "playing";
    }

    Json MeldsToJson(const eastl::vector<Meld>& melds) {
        Json list = Json::array();
        for (const Meld& meld : melds) {
            list.push_back(MeldToJson(meld));
        }
        return list;
    }

    Json ResultToJson(const HandModel& hand) {
        const HandSettlement settlement = SettleHand(hand);
        Json json = Json::object();
        const HandResult result = hand.Result().value_or(HandResult{});
        json["kind"] = MakeJsonString(ResultKindName(result.kind));
        json["winner"] = SeatOrNull(settlement.winner);
        const bool won = result.kind == ResultKind::Win;
        json["winningTile"] = won ? TileToJson(result.winningTile) : Json(nullptr);
        json["source"] = won ? MakeJsonString(SourceName(result.source)) : Json(nullptr);
        json["from"] = SeatOrNull(result.from);
        json["loose"] = result.loose;
        json["lastLiveTile"] = result.lastLiveTile;

        Json seats = Json::array();
        for (Seat seat = 0; seat < kSeatCount; ++seat) {
            const PlayerHand& tiles = hand.GetHand(seat);
            Json entry = Json::object();
            entry["concealed"] = TilesToJson(tiles.concealed);
            entry["melds"] = MeldsToJson(tiles.melds);
            entry["bonus"] = TilesToJson(tiles.bonus);
            entry["breakdown"] = BreakdownToJson(settlement.breakdowns[static_cast<size_t>(seat)]);
            seats.push_back(std::move(entry));
        }
        json["seats"] = std::move(seats);
        json["scores"] = SeatScoresToJson(settlement.scores);
        json["deltas"] = SeatScoresToJson(settlement.deltas);
        return json;
    }

    Json SettlementToJson(const HandSettlement& settlement) {
        Json json = Json::object();
        json["kind"] = MakeJsonString(ResultKindName(settlement.kind));
        json["winner"] = SeatOrNull(settlement.winner);
        json["scores"] = SeatScoresToJson(settlement.scores);
        json["deltas"] = SeatScoresToJson(settlement.deltas);
        return json;
    }

    Json SeatToJson(const StateJsonContext& context, const TableView& view, Seat seat) {
        const HandModel& hand = context.match.Hand();
        const PublicSeat& open = view.seats[static_cast<size_t>(seat)];
        Json json = Json::object();
        if (context.kinds != nullptr) {
            json["kind"] = MakeJsonString((*context.kinds)[static_cast<size_t>(seat)] == SeatKind::Person ? "person" : "computer");
        }
        json["wind"] = MakeJsonString(EnumUtils<Wind>::ToString(hand.SeatWind(seat)));
        json["score"] = context.match.Scores()[static_cast<size_t>(seat)];
        json["concealedCount"] = hand.GetHand(seat).concealed.size();
        json["melds"] = MeldsToJson(seat == context.viewSeat ? view.own.melds : open.openMelds);
        json["concealedKongs"] = open.concealedKongs;
        json["bonus"] = TilesToJson(open.bonus);
        json["discardCount"] = open.discardCount;
        json["calling"] = open.callingDeclared;
        return json;
    }

    Json TableStateToJson(const StateJsonContext& context, const TableView& view) {
        const HandModel& hand = context.match.Hand();
        const bool spectator = context.viewSeat == kNoSeat;

        Json turn = Json::object();
        turn["phase"] = MakeJsonString(TurnPhaseName(view.phase));
        turn["current"] = view.phase == Phase::Finished ? Json(nullptr) : SeatOrNull(view.currentSeat);
        turn["pendingTile"] = TileToJson(view.pendingTile);
        turn["yourTurn"] = !spectator && !view.legal.empty();

        Json seats = Json::array();
        for (Seat other = 0; other < kSeatCount; ++other) {
            seats.push_back(SeatToJson(context, view, other));
        }

        Json discards = Json::array();
        for (const DiscardEntry& entry : hand.Discards()) {
            Json item = Json::object();
            item["tile"] = TileToJson(entry.tile);
            item["seat"] = entry.seat;
            discards.push_back(std::move(item));
        }

        Json state = Json::object();
        state["phase"] = MakeJsonString(PhaseName(context.phase));
        state["seat"] = SeatOrNull(context.viewSeat);
        state["seatWind"] = spectator ? Json(nullptr) : MakeJsonString(EnumUtils<Wind>::ToString(view.seatWind));
        state["roundWind"] = MakeJsonString(EnumUtils<Wind>::ToString(view.roundWind));
        state["east"] = hand.East();
        if (context.levels != nullptr) {
            constexpr SeatSetup kAllComputers{SeatKind::Computer, SeatKind::Computer, SeatKind::Computer, SeatKind::Computer};
            state["level"] = LevelNumber(FirstComputerLevel(context.kinds != nullptr ? *context.kinds : kAllComputers, *context.levels));
            Json levels = Json::array();
            for (const ComputerLevel level : *context.levels) {
                levels.push_back(LevelNumber(level));
            }
            state["levels"] = std::move(levels);
        }
        state["hand"] = spectator ? Json::array() : TilesToJson(view.own.concealed);
        state["turn"] = std::move(turn);
        state["seats"] = std::move(seats);
        state["discards"] = std::move(discards);
        state["wall"] = view.liveCount;
        state["handsPlayed"] = context.match.HandsPlayed();
        state["handsTotal"] = context.match.HandsTotal();
        state["version"] = context.version;
        if (context.phase != SessionPhase::Playing) {
            state["result"] = ResultToJson(hand);
        }
        return state;
    }

}  // namespace BECore::Mahjong
