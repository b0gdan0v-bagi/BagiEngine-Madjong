#include "Hints.h"

#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Table/ScoreText.h>

namespace BECore::Mahjong {

    namespace {

        /// A hand that waits holds sets of `kTilesPerSet` and one tile over (3n + 1 concealed tiles); with the tile to discard (3n + 2) it does not wait yet.
        constexpr size_t kWaitingRemainder = 1;

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

        eastl::string Num(int value) {
            return Format("{}", value);
        }

        void AddLine(eastl::vector<eastl::string>& lines, eastl::string line) {
            if (!line.empty() && lines.size() < kMaxReasonLines) {
                lines.push_back(eastl::move(line));
            }
        }

        eastl::string StepsLine(const UIStrings& strings, int before, int after) {
            return Tr(strings, "hints.why.steps", {{"before", Num(before)}, {"after", Num(after)}});
        }

        eastl::string WaitsLine(const UIStrings& strings, int kinds, int copies) {
            return Tr(strings, "hints.why.waits", {{"kinds", Num(kinds)}, {"copies", Num(copies)}});
        }

        /// The word for a `kKeep...` class.
        const char* KeepKey(int keep) noexcept {
            if (keep <= kKeepWorthless) {
                return "hints.why.keep.worthless";
            }
            if (keep == kKeepTerminal) {
                return "hints.why.keep.terminal";
            }
            return keep == kKeepSimple ? "hints.why.keep.simple" : "hints.why.keep.value";
        }

        /// How the tie of equal choices was settled. The first alternative is the best of the others, so it is the equal one with the
        /// lowest keep value: an equal keep value too leaves the choice to chance.
        eastl::string TieLine(const UIStrings& strings, const TurnExplanation& turn) {
            if (turn.equalChoices == 0) {
                return Tr(strings, "hints.why.tie.none");
            }
            const bool alike = !turn.alternatives.empty() && turn.alternatives.front().keep == turn.chosen.keep;
            if (alike) {
                return Tr(strings, "hints.why.tie.chance", {{"n", Num(turn.equalChoices)}});
            }
            return Tr(strings, "hints.why.tie.keep", {{"n", Num(turn.equalChoices)}, {"keep", Tr(strings, KeepKey(turn.chosen.keep))}});
        }

        eastl::string DiscardAlternativeLine(const UIStrings& strings, const TurnOption& option, bool waiting) {
            if (waiting) {
                return Tr(strings, "hints.why.alt.wait", {{"tile", TileName(strings, option.tile)}, {"copies", Num(option.waitCopies)}});
            }
            return Tr(strings, "hints.why.alt.discard", {{"tile", TileName(strings, option.tile)}, {"steps", Num(option.shanten)}, {"copies", Num(option.acceptance)}});
        }

        eastl::string ClaimAlternativeLine(const UIStrings& strings, const TableView& view, const RatedClaim& option, int before) {
            const WordArg beforeArg{"before", Num(before)};
            const WordArg afterArg{"after", Num(option.after.shanten)};
            if (option.action.claim == ClaimKind::Chow) {
                const auto tiles = option.action.chowFirst.IsValid() ? ChowTiles(option.action.chowFirst) : eastl::array<Tile, kTilesPerSet>{};
                return Tr(strings, "hints.why.alt.chow", {{"first", TileName(strings, tiles[0])}, {"third", TileName(strings, tiles[2])}, beforeArg, afterArg});
            }
            const char* key = option.action.claim == ClaimKind::Kong ? "hints.why.alt.kong" : "hints.why.alt.pung";
            return Tr(strings, key, {{"tile", TileName(strings, view.pendingTile)}, beforeArg, afterArg});
        }

        void ReasonTurn(const UIStrings& strings, const TurnExplanation& turn, eastl::vector<eastl::string>& lines) {
            const bool waiting = turn.step == TurnStep::Declare || turn.step == TurnStep::KeepWaiting;
            switch (turn.step) {
                case TurnStep::DeclareWin:
                    AddLine(lines, Tr(strings, "hints.why.win"));
                    return;
                case TurnStep::Kong:
                    AddLine(lines, StepsLine(strings, turn.shantenBefore, turn.kongShantenAfter));
                    AddLine(lines, Tr(strings, "hints.why.help.change", {{"before", Num(turn.kongAcceptanceBefore)}, {"after", Num(turn.kongAcceptanceAfter)}}));
                    AddLine(lines, Tr(strings, "hints.why.kong.noWorse"));
                    return;
                case TurnStep::Declare:
                    AddLine(lines, Tr(strings, "hints.why.declare"));
                    break;
                case TurnStep::KeepWaiting:
                    AddLine(lines, Tr(strings, "hints.why.keepWaiting"));
                    break;
                case TurnStep::Discard:
                    AddLine(lines, StepsLine(strings, turn.shantenBefore, turn.chosen.shanten));
                    break;
            }
            if (waiting) {
                AddLine(lines, WaitsLine(strings, turn.chosen.waitKinds, turn.chosen.waitCopies));
            } else {
                AddLine(lines, Tr(strings, "hints.why.help", {{"copies", Num(turn.chosen.acceptance)}}));
            }
            AddLine(lines, TieLine(strings, turn));
            for (const TurnOption& option : turn.alternatives) {
                AddLine(lines, DiscardAlternativeLine(strings, option, waiting));
            }
        }

        void ReasonClaim(const UIStrings& strings, const TableView& view, const ClaimExplanation& claim, eastl::vector<eastl::string>& lines) {
            switch (claim.step) {
                case ClaimStep::Mahjong:
                    AddLine(lines, Tr(strings, "hints.why.claim.mahjong"));
                    return;
                case ClaimStep::WaitingHand: {
                    int copies = 0;
                    for (const TileCopies& wait : claim.waits) {
                        copies += wait.unseen;
                    }
                    AddLine(lines, Tr(strings, "hints.why.pass.waiting"));
                    AddLine(lines, WaitsLine(strings, static_cast<int>(claim.waits.size()), copies));
                    return;
                }
                case ClaimStep::Claim:
                    if (claim.chosen.has_value()) {
                        AddLine(lines, StepsLine(strings, claim.shantenBefore, claim.chosen->after.shanten));
                        AddLine(lines, Tr(strings, "hints.why.help", {{"copies", Num(claim.chosen->after.acceptance)}}));
                        if (claim.chosen->after.shanten >= claim.shantenBefore) {
                            AddLine(lines, Tr(strings, "hints.why.claim.value"));
                        }
                    }
                    break;
                case ClaimStep::NoGain:
                    AddLine(lines, Tr(strings, "hints.why.pass.noGain"));
                    break;
            }
            for (const RatedClaim& option : claim.alternatives) {
                AddLine(lines, ClaimAlternativeLine(strings, view, option, claim.shantenBefore));
            }
        }

    }  // namespace

    eastl::vector<WaitEntry> WaitingTiles(const TableView& view) {
        eastl::vector<WaitEntry> entries;
        if (view.own.concealed.size() % kTilesPerSet != kWaitingRemainder) {
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

    std::optional<Explanation> ExplainAdviceFor(const TableView& view) {
        return ExplainAdvice(view);
    }

    eastl::vector<eastl::string> ReasonLines(const UIStrings& strings, const TableView& view, const Explanation& explained) {
        eastl::vector<eastl::string> lines;
        if (explained.turn.has_value()) {
            ReasonTurn(strings, *explained.turn, lines);
        } else if (explained.claim.has_value()) {
            ReasonClaim(strings, view, *explained.claim, lines);
        }
        return lines;
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
                    // An action of the legal list always has a valid `chowFirst`; a made-up one names three invalid tiles.
                    const auto tiles = advice.chowFirst.IsValid() ? ChowTiles(advice.chowFirst) : eastl::array<Tile, kTilesPerSet>{};
                    return Tr(strings, key, {{"first", TileName(strings, tiles[0])}, {"second", TileName(strings, tiles[1])}, {"third", TileName(strings, tiles[2])}});
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

    bool HintsApply(bool hintsOn, bool tablePaused, const TableSession& session) {
        if (!hintsOn || tablePaused || session.GetPhase() != SessionPhase::Playing || session.HandoffPending()) {
            return false;
        }
        const Seat viewer = session.Viewer();
        return IsSeat(viewer) && session.KindOf(viewer) == SeatKind::Person;
    }

}  // namespace BECore::Mahjong
