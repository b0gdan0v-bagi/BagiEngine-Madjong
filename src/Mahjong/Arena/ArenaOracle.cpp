#include "ArenaOracle.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/HandShape.h>

namespace BECore::Mahjong::Arena {

    namespace {

        /// Normal's answer on `view` by `profile`; a search decision is handed back, given `source` for its worlds when there is one, and
        /// its worlds are played through `worlds`, or one by one when there is no loop (the same draws and the same answer as the plain
        /// chooser).
        HandAction AskNormal(const TableView& view, MahjongRng& rng, const LevelProfile& profile, const WorldLoop* worlds, WorldSource source = {}) {
            SearchHandoff handoff;
            HandAction action = HandAction::MakePass();
            switch (view.phase) {
                case Phase::Claim:
                case Phase::RobKong:
                    action = ChooseClaimAction(view, rng, profile, &handoff);
                    break;
                case Phase::Discard:
                    action = ChooseTurnAction(view, rng, profile, &handoff);
                    break;
                case Phase::Finished:
                    break;
            }
            if (!handoff.run.has_value()) {
                return action;
            }
            if (source) {
                [[maybe_unused]] const bool taken = handoff.run->UseWorldSource(eastl::move(source));
                ASSERT(taken, "an oracle's run did not take its source of worlds");
            }
            if (worlds != nullptr) {
                handoff.run->RunWorlds(*worlds);
            } else {
                handoff.run->RunWorlds([](int count, const std::function<void(int index)>& body) {
                    for (int index = 0; index < count; ++index) {
                        body(index);
                    }
                });
            }
            return handoff.run->Result();
        }

        /// The kinds that complete the concealed tiles of a declared seat other than `seat`, in the real hands.
        TileCounts DeclaredWinningKinds(const HandModel& hand, Seat seat) {
            TileCounts wins{};
            for (Seat other = 0; other < kSeatCount; ++other) {
                const PlayerHand& opponent = hand.GetHand(other);
                if (other == seat || !opponent.callingDeclared) {
                    continue;
                }
                for (const Tile tile : WinningTiles(opponent)) {
                    wins[tile.Id()] = 1;
                }
            }
            return wins;
        }

        /// The defence of `oracle-defence`: `normal` stands unless it discards, or upgrades a kong with, a tile a declared opponent
        /// wins on; then it is the best discard (`RateDiscards`' order: acceptance, then `KeepValue`, then the lowest id) among the listed
        /// discards of the same `declareCalling` that leave the same shanten and win for nobody. The shanten to keep is that of Normal's
        /// own discard, or, for a kong upgrade, the lowest a discard of the hand can leave. No such discard: Normal's action stands.
        HandAction Defend(const HandModel& hand, const TableView& view, const HandAction& normal) {
            if (view.phase != Phase::Discard || (normal.kind != ActionKind::Discard && normal.kind != ActionKind::UpgradeKong)) {
                return normal;
            }
            const TileCounts wins = DeclaredWinningKinds(hand, view.seat);
            if (wins[normal.tile.Id()] == 0) {
                return normal;
            }
            const eastl::vector<DiscardOption> options = RateDiscards(CountTiles(view.own.concealed), MeldCountOf(view), view);
            if (options.empty()) {
                return normal;
            }
            int keepShanten = options.front().shanten;
            if (normal.kind == ActionKind::Discard) {
                for (const DiscardOption& option : options) {
                    if (option.tile == normal.tile) {
                        keepShanten = option.shanten;
                    }
                }
            }
            for (const DiscardOption& option : options) {
                const HandAction safe = HandAction::MakeDiscard(option.tile, normal.declareCalling);
                if (option.shanten == keepShanten && wins[option.tile.Id()] == 0 && eastl::find(view.legal.begin(), view.legal.end(), safe) != view.legal.end()) {
                    return safe;
                }
            }
            return normal;
        }

    }  // namespace

    WorldSource MakeTrueWorlds(const HandModel& hand, bool wallOrderKnown) {
        const std::shared_ptr<const HandState> real = std::make_shared<const HandState>(hand.Capture());
        return [real, wallOrderKnown](const TableView&, MahjongRng& rng) -> std::optional<HandModel> {
            HandState world = *real;
            if (!wallOrderKnown) {
                const size_t looseCount = world.loose.size();
                const size_t liveCount = world.live.size();
                eastl::vector<Tile> wall = world.loose;
                wall.insert(wall.end(), world.live.begin(), world.live.end());
                wall.insert(wall.end(), world.dead.begin(), world.dead.end());
                rng.Shuffle(wall);
                world.loose.assign(wall.begin(), wall.begin() + looseCount);
                world.live.assign(wall.begin() + looseCount, wall.begin() + looseCount + liveCount);
                world.dead.assign(wall.begin() + looseCount + liveCount, wall.end());
            }
            std::expected<HandModel, eastl::string> restored = HandModel::Restore(eastl::move(world));
            if (!restored.has_value()) {
                return std::nullopt;
            }
            return eastl::move(*restored);
        };
    }

    TableView TrueAcceptanceView(const HandModel& hand, Seat seat) {
        TableView view = MakeTableView(hand, seat);
        TileCounts drawable{};
        for (const Tile tile : hand.GetWall().LiveTiles()) {
            ++drawable[tile.Id()];
        }
        for (const Tile tile : hand.GetWall().LooseTiles()) {
            ++drawable[tile.Id()];
        }
        for (uint8_t id = 0; id < kKindCount; ++id) {
            view.visible[id] = static_cast<uint8_t>(kCopiesPerTile - drawable[id]);
        }
        return view;
    }

    HandAction ChooseOracle(const HandModel& hand, Seat seat, const ArenaPlayer& player, MahjongRng& rng, const WorldLoop* worlds) {
        switch (player.oracle) {
            case OracleKind::Acceptance:
                return AskNormal(TrueAcceptanceView(hand, seat), rng, player.profile, worlds);
            case OracleKind::Defence: {
                const TableView view = MakeTableView(hand, seat);
                return Defend(hand, view, AskNormal(view, rng, player.profile, worlds));
            }
            case OracleKind::Hands:
            case OracleKind::WholeHand:
                return AskNormal(MakeTableView(hand, seat), rng, player.profile, worlds, MakeTrueWorlds(hand, player.oracle == OracleKind::WholeHand));
            case OracleKind::None:
                break;
        }
        return AskNormal(MakeTableView(hand, seat), rng, player.profile, worlds);
    }

}  // namespace BECore::Mahjong::Arena
