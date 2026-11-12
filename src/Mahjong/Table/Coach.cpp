#include "Coach.h"

#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Table/ScoreText.h>

namespace BECore::Mahjong {

    namespace {

        CoachSide RatedSide(int steps, int copies) {
            return {CoachNumbers::Rated, steps, copies};
        }

        CoachSide WaitingSide(int copies) {
            return {CoachNumbers::Waiting, 0, copies};
        }

        CoachSide RatedSide(const DiscardRating& rating) {
            return RatedSide(rating.shanten, rating.acceptance);
        }

        bool IsKong(const HandAction& action) noexcept {
            return action.kind == ActionKind::ConcealedKong || action.kind == ActionKind::UpgradeKong;
        }

        CoachNote MakeNote(const TableView& before, const HandAction& advised, const HandAction& taken) {
            CoachNote note;
            note.seat = before.seat;
            note.advised = advised;
            note.taken = taken;
            note.pending = before.pendingTile;
            return note;
        }

        const DiscardOption* FindOption(const eastl::vector<DiscardOption>& rated, Tile tile) {
            for (const DiscardOption& option : rated) {
                if (option.tile == tile) {
                    return &option;
                }
            }
            return nullptr;
        }

        /// The unseen winning copies of the 13 tiles that stay after the discard `taken`, when they wait (as `WaitingDiscards` lists it for the
        /// kind of discard, declaring or not).
        std::optional<int> WaitingCopiesOf(const TableView& before, const HandAction& taken, bool readDanger) {
            for (const WaitingDiscard& discard : WaitingDiscards(before, taken.declareCalling, readDanger)) {
                if (discard.tile == taken.tile) {
                    return discard.copies;
                }
            }
            return std::nullopt;
        }

        /// The note of a turn whose advice is `turn`; nullopt when `taken` agrees.
        std::optional<CoachNote> JudgeTurn(const TableView& before, const TurnExplanation& turn, const HandAction& taken) {
            CoachNote note = MakeNote(before, turn.action, taken);
            note.stepsBefore = turn.shantenBefore;
            const TileCounts counts = CountTiles(before.own.concealed);
            const bool readDanger = ProfileOf(kDefaultLevel).readDanger;
            const auto rate = [&](Tile tile) -> std::optional<DiscardOption> {
                const eastl::vector<DiscardOption> rated = RateDiscards(counts, MeldCountOf(before), before, readDanger);
                const DiscardOption* option = FindOption(rated, tile);
                return option != nullptr ? std::optional<DiscardOption>(*option) : std::nullopt;
            };
            if (IsKong(taken)) {
                note.takenSide = RatedSide(RateKong(before, counts, taken));
            }
            switch (turn.step) {
                case TurnStep::DeclareWin:
                    note.takenSide = {};
                    return note;
                case TurnStep::Declare:
                case TurnStep::KeepWaiting: {
                    note.advisedSide = WaitingSide(turn.chosen.waitCopies);
                    if (taken.kind != ActionKind::Discard) {
                        return note;
                    }
                    if (const std::optional<int> copies = WaitingCopiesOf(before, taken, readDanger)) {
                        note.takenSide = WaitingSide(*copies);
                        const bool declaring = turn.step == TurnStep::Declare;
                        return taken.declareCalling == declaring && *copies == turn.chosen.waitCopies ? std::nullopt : std::optional<CoachNote>(note);
                    }
                    if (const std::optional<DiscardOption> option = rate(taken.tile)) {
                        note.takenSide = RatedSide(option->shanten, option->acceptance);
                    }
                    return note;
                }
                case TurnStep::Kong: {
                    note.advisedSide = RatedSide(turn.kongShantenAfter, turn.kongAcceptanceAfter);
                    if (IsKong(taken)) {
                        const bool noWorse = KongIsNoWorse(before, counts, taken, BestDiscardRating(counts, MeldCountOf(before), before));
                        return noWorse ? std::nullopt : std::optional<CoachNote>(note);
                    }
                    if (const std::optional<DiscardOption> option = taken.kind == ActionKind::Discard ? rate(taken.tile) : std::nullopt) {
                        note.takenSide = RatedSide(option->shanten, option->acceptance);
                        if (note.takenSide == note.advisedSide) {
                            return std::nullopt;
                        }
                    }
                    return note;
                }
                case TurnStep::Discard: {
                    note.advisedSide = RatedSide(turn.chosen.shanten, turn.chosen.acceptance);
                    if (taken.kind == ActionKind::Discard) {
                        if (const std::optional<DiscardOption> option = rate(taken.tile)) {
                            note.takenSide = RatedSide(option->shanten, option->acceptance);
                            if (note.takenSide == note.advisedSide) {
                                return std::nullopt;
                            }
                        }
                    }
                    return note;
                }
            }
            return note;
        }

        /// The note of a window whose advice is `claim`; nullopt when `taken` agrees.
        std::optional<CoachNote> JudgeClaim(const TableView& before, const ClaimExplanation& claim, const HandAction& taken) {
            CoachNote note = MakeNote(before, claim.action, taken);
            note.stepsBefore = claim.shantenBefore;
            if (claim.step == ClaimStep::Mahjong) {
                return note;
            }
            const bool waits = claim.step == ClaimStep::WaitingHand || claim.step == ClaimStep::WaitingPass || claim.step == ClaimStep::WaitingKong;
            note.handWaits = claim.step == ClaimStep::WaitingHand || claim.step == ClaimStep::WaitingPass;
            note.byPoints = claim.step == ClaimStep::WaitingPass || claim.step == ClaimStep::WaitingKong;
            int copies = 0;
            for (const TileCopies& wait : claim.waits) {
                copies += wait.unseen;
            }
            if ((claim.step == ClaimStep::Claim || claim.step == ClaimStep::WaitingKong) && claim.chosen.has_value()) {
                note.advisedSide = RatedSide(claim.chosen->after);
            } else if (note.handWaits) {
                note.advisedSide = WaitingSide(copies);
            }
            if (waits && taken.kind == ActionKind::Pass) {
                note.takenSide = WaitingSide(copies);
            }
            if (taken.kind == ActionKind::Claim && taken.claim != ClaimKind::Mahjong) {
                for (const RatedClaim& option : RateClaims(before, claim.shantenBefore, ProfileOf(kDefaultLevel))) {
                    if (option.action == taken) {
                        note.takenSide = RatedSide(option.after);
                    }
                }
                if (claim.step == ClaimStep::Claim && note.takenSide == note.advisedSide) {
                    return std::nullopt;
                }
            }
            return note;
        }

    }  // namespace

    bool CanJudge(const TableView& before, const HandAction& taken) {
        for (const HandAction& action : before.legal) {
            if (action == taken) {
                return true;
            }
        }
        return false;
    }

    std::optional<CoachNote> JudgeMove(const TableView& before, const HandAction& taken) {
        if (!CanJudge(before, taken)) {
            return std::nullopt;
        }
        const std::optional<Explanation> explained = ExplainAdviceFor(before);
        if (!explained.has_value() || explained->Action() == taken) {
            return std::nullopt;
        }
        return explained->turn.has_value() ? JudgeTurn(before, *explained->turn, taken) : JudgeClaim(before, *explained->claim, taken);
    }

    void CoachLedger::SetEnabled(bool enabled) {
        if (enabled == _enabled) {
            return;
        }
        _enabled = enabled;
        ClearNote();
        _keyed = false;
        _hand = {};
        _match = {};
    }

    void CoachLedger::ClearNote() {
        if (_note.has_value()) {
            _note.reset();
            ++_version;
        }
    }

    void CoachLedger::OnPersonMove(const TableView& before, const HandAction& action, const MoveContext& context) {
        if (!_enabled || context.personCount != 1 || !CanJudge(before, action)) {
            return;
        }
        ClearNote();
        const CoachHandKey key = HandKeyOf(context);
        if (!_keyed || key.starts != _key.starts) {
            _match = {};
            _hand = {};
        } else if (key != _key) {
            _hand = {};
        }
        _key = key;
        _keyed = true;
        std::optional<CoachNote> note = JudgeMove(before, action);
        ++_hand.moves;
        ++_match.moves;
        if (!note.has_value()) {
            ++_hand.agreed;
            ++_match.agreed;
            return;
        }
        _note = eastl::move(note);
        ++_version;
    }

}  // namespace BECore::Mahjong
