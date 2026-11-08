#include "Hints.h"

#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Table/ScoreText.h>

namespace BECore::Mahjong {

    namespace {

        /// The key of the advice for `advice`, one per kind of action (and per kind of claim).
        const char* AdviceKey(const HandAction& advice) noexcept {
            switch (advice.kind) {
                case ActionKind::Discard:
                    return advice.declareCalling ? "hints.advice.discardCalling" : "hints.advice.discard";
                case ActionKind::DeclareWin:
                    return "hints.advice.win";
                case ActionKind::ConcealedKong:
                    return "hints.advice.concealedKong";
                case ActionKind::UpgradeKong:
                    return "hints.advice.upgradeKong";
                case ActionKind::Claim:
                    switch (advice.claim) {
                        case ClaimKind::Chow:
                            return "hints.advice.claimChow";
                        case ClaimKind::Pung:
                            return "hints.advice.claimPung";
                        case ClaimKind::Kong:
                            return "hints.advice.claimKong";
                        case ClaimKind::Mahjong:
                            return "hints.advice.claimMahjong";
                    }
                    break;
                case ActionKind::Pass:
                    break;
            }
            return "hints.advice.pass";
        }

    }  // namespace

    eastl::vector<WaitEntry> WaitingTiles(const TableView& view) {
        eastl::vector<WaitEntry> entries;
        if (view.own.concealed.size() % 3 != 1) {
            return entries;
        }
        for (const Tile tile : WinningTiles(view.own)) {
            entries.push_back({tile, UnseenCopies(view, tile)});
        }
        return entries;
    }

    eastl::array<bool, kTileIdCount> DeclaringDiscards(const TableView& view) {
        eastl::array<bool, kTileIdCount> declaring{};
        for (const HandAction& action : view.legal) {
            if (action.kind == ActionKind::Discard && action.declareCalling && action.tile.IsValid()) {
                declaring[action.tile.Id()] = true;
            }
        }
        return declaring;
    }

    std::optional<HandAction> AdviceFor(const TableView& view) {
        if (view.legal.empty()) {
            return std::nullopt;
        }
        ComputerPlayer player(kAdviceSeed);
        return player.Choose(view);
    }

    eastl::string AdviceText(const UIStrings& strings, const TableView& view, const HandAction& advice) {
        const char* key = AdviceKey(advice);
        switch (advice.kind) {
            case ActionKind::Discard:
            case ActionKind::ConcealedKong:
            case ActionKind::UpgradeKong:
                return Tr(strings, key, {{"tile", TileName(strings, advice.tile)}});
            case ActionKind::Claim:
                if (advice.claim == ClaimKind::Chow) {
                    const Tile first = advice.chowFirst;
                    const eastl::string a = TileName(strings, first);
                    const eastl::string b = TileName(strings, first.IsValid() ? Tile(static_cast<uint8_t>(first.Id() + 1)) : Tile{});
                    const eastl::string c = TileName(strings, first.IsValid() ? Tile(static_cast<uint8_t>(first.Id() + 2)) : Tile{});
                    return Tr(strings, key, {{"first", a}, {"second", b}, {"third", c}});
                }
                if (advice.claim == ClaimKind::Pung || advice.claim == ClaimKind::Kong) {
                    return Tr(strings, key, {{"tile", TileName(strings, view.pendingTile)}});
                }
                return Tr(strings, key);
            case ActionKind::DeclareWin:
            case ActionKind::Pass:
                break;
        }
        return Tr(strings, key);
    }

    bool HintsApply(bool hintsOn, bool setupOpen, const TableSession& session) {
        if (!hintsOn || setupOpen || session.GetPhase() != SessionPhase::Playing || session.HandoffPending()) {
            return false;
        }
        const Seat viewer = session.Viewer();
        return IsSeat(viewer) && session.KindOf(viewer) == SeatKind::Person;
    }

}  // namespace BECore::Mahjong
