#include "HintCard.h"

#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/HandShape.h>
#include <Mahjong/Table/ScoreText.h>

namespace BECore::Mahjong {

    namespace {

        eastl::string Num(int value) {
            return Format("{}", value);
        }

        /// The steps to a ready hand as the card says them: a number, the word "ready" for none left.
        eastl::string StepsText(const UIStrings& strings, int steps) {
            return steps <= 0 ? Tr(strings, "hints.card.ready") : Num(steps);
        }

        CardTrend TrendOf(int now, int after) noexcept {
            if (after < now) {
                return CardTrend::Closer;
            }
            return after == now ? CardTrend::Same : CardTrend::Farther;
        }

        /// The advice that ends the hand: nothing is compared with a win.
        bool EndsTheHand(const HandAction& action) noexcept {
            return action.kind == ActionKind::DeclareWin || (action.kind == ActionKind::Claim && action.claim == ClaimKind::Mahjong);
        }

        /// The pictures of `action`: the tile of a discard or a kong of the hand, the pending tile of a pung or kong claim, the three of a chow.
        eastl::array<Tile, kMaxCardTiles> TilesOf(const HandAction& action, Tile pending) noexcept {
            eastl::array<Tile, kMaxCardTiles> tiles{};
            switch (action.kind) {
                case ActionKind::Discard:
                case ActionKind::ConcealedKong:
                case ActionKind::UpgradeKong:
                    tiles[0] = action.tile;
                    break;
                case ActionKind::Claim:
                    if (action.claim == ClaimKind::Chow) {
                        if (action.chowFirst.IsValid()) {
                            const auto chow = ChowTiles(action.chowFirst);
                            eastl::copy(chow.begin(), chow.end(), tiles.begin());
                        }
                    } else if (action.claim != ClaimKind::Mahjong) {
                        tiles[0] = pending;
                    }
                    break;
                case ActionKind::DeclareWin:
                case ActionKind::Pass:
                    break;
            }
            return tiles;
        }

        /// The title of a card that names `action`: the sentence of the advice, but a chow only by its kind (its three tiles are the
        /// pictures of the row, and their names would not fit the title).
        eastl::string TitleOf(const UIStrings& strings, Tile pending, const HandAction& action) {
            if (action.kind == ActionKind::Claim && action.claim == ClaimKind::Chow) {
                return Tr(strings, "hints.card.title.chow");
            }
            // `AdviceText` names the pending tile of a window from a view: only that member is read of this one
            TableView named;
            named.pendingTile = pending;
            return AdviceText(strings, named, action);
        }

        /// The word of the kind of `action`; a plain discard needs none (its picture says it), a declaring one says so.
        eastl::string MoveWord(const UIStrings& strings, const HandAction& action) {
            switch (action.kind) {
                case ActionKind::Discard:
                    return action.declareCalling ? Tr(strings, "hints.card.declare") : eastl::string();
                case ActionKind::ConcealedKong:
                case ActionKind::UpgradeKong:
                    return Tr(strings, "hints.card.kong");
                case ActionKind::Claim:
                    switch (action.claim) {
                        case ClaimKind::Chow:
                            return Tr(strings, "hints.card.chow");
                        case ClaimKind::Pung:
                            return Tr(strings, "hints.card.pung");
                        case ClaimKind::Kong:
                            return Tr(strings, "hints.card.kong");
                        case ClaimKind::Mahjong:
                            break;
                    }
                    return Tr(strings, "hints.card.mahjong");
                case ActionKind::DeclareWin:
                    return Tr(strings, "hints.card.mahjong");
                case ActionKind::Pass:
                    break;
            }
            return Tr(strings, "hints.card.pass");
        }

        eastl::string WhoWord(const UIStrings& strings, CardRowKind kind) {
            switch (kind) {
                case CardRowKind::Best:
                    return Tr(strings, "hints.card.best");
                case CardRowKind::Yours:
                    return Tr(strings, "hints.card.yours");
                case CardRowKind::Other:
                    break;
            }
            return {};
        }

        /// A row of `action` with no numbers yet.
        CardRow RowOf(const UIStrings& strings, CardRowKind kind, const HandAction& action, Tile pending) {
            CardRow row;
            row.kind = kind;
            row.tiles = TilesOf(action, pending);
            row.who = WhoWord(strings, kind);
            row.move = MoveWord(strings, action);
            return row;
        }

        /// Puts the steps after the move (against `now`) and the unseen tiles that help on `row`.
        void Rate(const UIStrings& strings, CardRow& row, int now, int steps, int useful) {
            row.steps = StepsText(strings, steps);
            row.trend = TrendOf(now, steps);
            row.useful = Num(useful);
        }

        void AddRow(HintCard& card, CardRow row) {
            if (card.rows.size() < kMaxCardRows) {
                card.rows.push_back(eastl::move(row));
            }
        }

        /// The lines under the rows: the reasons first as far as they fit, the tally always last.
        struct Footer {
            HintCard& card;
            size_t room;

            Footer(HintCard& target, eastl::string_view tally) : card(target), room(kMaxCardFooter - (tally.empty() ? 0 : 1)) {}

            void Add(eastl::string line) {
                if (!line.empty() && card.footer.size() < room) {
                    card.footer.push_back(eastl::move(line));
                }
            }
        };

        void EndFooter(HintCard& card, eastl::string_view tally) {
            if (!tally.empty()) {
                card.footer.emplace_back(tally);
            }
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

        /// The danger of the advised discard (only with a declared opponent, for a step that discards).
        eastl::string DangerLine(const UIStrings& strings, Danger danger) {
            switch (danger) {
                case Danger::Safe:
                    return Tr(strings, "hints.why.danger.safe");
                case Danger::Low:
                    return Tr(strings, "hints.why.danger.low");
                case Danger::Medium:
                    return Tr(strings, "hints.why.danger.medium");
                case Danger::High:
                    break;
            }
            return Tr(strings, "hints.why.danger.high");
        }

        /// How the tie of equal choices was settled. The first alternative is the best of the others, so it is the equal one with the
        /// danger and then the keep value that come first: when the danger (read only with the knob of the advice player, against a
        /// declared opponent) differs, it decided; an equal keep value too leaves the choice to chance.
        eastl::string TieLine(const UIStrings& strings, const TurnExplanation& turn) {
            if (turn.equalChoices == 0) {
                return Tr(strings, "hints.why.tie.none");
            }
            const bool dangerDecided = turn.readDanger && turn.declaredOpponents > 0 && !turn.alternatives.empty() && turn.alternatives.front().danger != turn.chosen.danger;
            if (dangerDecided) {
                return Tr(strings, "hints.why.tie.danger", {{"n", Num(turn.equalChoices)}});
            }
            const bool alike = !turn.alternatives.empty() && turn.alternatives.front().keep == turn.chosen.keep;
            if (alike) {
                return Tr(strings, "hints.why.tie.chance", {{"n", Num(turn.equalChoices)}});
            }
            return Tr(strings, "hints.why.tie.keep", {{"n", Num(turn.equalChoices)}, {"keep", Tr(strings, KeepKey(turn.chosen.keep))}});
        }

        /// The steps and the unseen tiles that help of a discard option: a waiting hand is ready and counts its winning copies.
        void RateOption(const UIStrings& strings, CardRow& row, int now, const TurnOption& option, bool waiting) {
            if (waiting) {
                Rate(strings, row, now, 0, option.waitCopies);
            } else {
                Rate(strings, row, now, option.shanten, option.acceptance);
            }
        }

        void TurnCard(const UIStrings& strings, const TurnExplanation& turn, HintCard& card, Footer& footer) {
            const int now = turn.shantenBefore;
            switch (turn.step) {
                case TurnStep::DeclareWin:
                    footer.Add(Tr(strings, "hints.why.win"));
                    return;
                case TurnStep::Kong: {
                    CardRow kong = RowOf(strings, CardRowKind::Best, turn.action, Tile{});
                    Rate(strings, kong, now, turn.kongShantenAfter, turn.kongAcceptanceAfter);
                    AddRow(card, eastl::move(kong));
                    CardRow without;
                    without.move = Tr(strings, "hints.card.noKong");
                    Rate(strings, without, now, now, turn.kongAcceptanceBefore);
                    AddRow(card, eastl::move(without));
                    footer.Add(Tr(strings, "hints.why.kong.noWorse"));
                    return;
                }
                case TurnStep::Declare:
                case TurnStep::KeepWaiting:
                case TurnStep::Discard:
                    break;
            }
            const bool waiting = turn.step != TurnStep::Discard;
            CardRow best = RowOf(strings, CardRowKind::Best, turn.action, Tile{});
            RateOption(strings, best, now, turn.chosen, waiting);
            AddRow(card, eastl::move(best));
            for (const TurnOption& option : turn.alternatives) {
                CardRow other = RowOf(strings, CardRowKind::Other, HandAction::MakeDiscard(option.tile), Tile{});
                RateOption(strings, other, now, option, option.waitKinds > 0);
                AddRow(card, eastl::move(other));
            }
            if (turn.declaredOpponents > 0) {
                footer.Add(DangerLine(strings, turn.chosen.danger));
            }
            footer.Add(TieLine(strings, turn));
        }

        /// The unseen copies of the tiles a waiting hand of a window waits on.
        int WaitCopies(const ClaimExplanation& claim) {
            int copies = 0;
            for (const TileCopies& wait : claim.waits) {
                copies += wait.unseen;
            }
            return copies;
        }

        void ClaimCard(const UIStrings& strings, const TableView& view, const ClaimExplanation& claim, HintCard& card, Footer& footer) {
            const int now = claim.shantenBefore;
            switch (claim.step) {
                case ClaimStep::Mahjong:
                    footer.Add(Tr(strings, "hints.why.claim.mahjong"));
                    return;
                case ClaimStep::WaitingHand:
                case ClaimStep::WaitingPass: {
                    CardRow pass = RowOf(strings, CardRowKind::Best, claim.action, view.pendingTile);
                    Rate(strings, pass, now, 0, WaitCopies(claim));
                    AddRow(card, eastl::move(pass));
                    footer.Add(Tr(strings, claim.step == ClaimStep::WaitingPass ? "hints.why.pass.points" : "hints.why.pass.waiting"));
                    break;
                }
                case ClaimStep::WaitingKong:
                    if (claim.chosen.has_value()) {
                        CardRow best = RowOf(strings, CardRowKind::Best, claim.chosen->action, view.pendingTile);
                        Rate(strings, best, now, claim.chosen->after.shanten, claim.chosen->after.acceptance);
                        AddRow(card, eastl::move(best));
                    }
                    {
                        // The pass keeps the hand as it waits now: the rows compare directly
                        CardRow pass = RowOf(strings, CardRowKind::Other, HandAction::MakePass(), view.pendingTile);
                        Rate(strings, pass, now, 0, WaitCopies(claim));
                        AddRow(card, eastl::move(pass));
                    }
                    footer.Add(Tr(strings, "hints.why.kong.points"));
                    break;
                case ClaimStep::Claim:
                    if (claim.chosen.has_value()) {
                        CardRow best = RowOf(strings, CardRowKind::Best, claim.chosen->action, view.pendingTile);
                        Rate(strings, best, now, claim.chosen->after.shanten, claim.chosen->after.acceptance);
                        AddRow(card, eastl::move(best));
                        if (claim.chosen->after.shanten >= now) {
                            footer.Add(Tr(strings, "hints.why.claim.value"));
                        }
                    }
                    break;
                case ClaimStep::NoGain: {
                    // A pass keeps the hand as it is: its steps are the steps now, and nothing counts the tiles that help it
                    CardRow pass = RowOf(strings, CardRowKind::Best, claim.action, view.pendingTile);
                    pass.steps = StepsText(strings, now);
                    pass.trend = CardTrend::Same;
                    AddRow(card, eastl::move(pass));
                    footer.Add(Tr(strings, "hints.why.pass.noGain"));
                    break;
                }
            }
            for (const RatedClaim& option : claim.alternatives) {
                CardRow other = RowOf(strings, CardRowKind::Other, option.action, view.pendingTile);
                Rate(strings, other, now, option.after.shanten, option.after.acceptance);
                AddRow(card, eastl::move(other));
            }
        }

        /// One side of a note as a row: the numbers of `side`; a pass with none keeps the steps it had (ready when the hand waits).
        CardRow NoteRow(const UIStrings& strings, const CoachNote& note, CardRowKind kind, const HandAction& action, const CoachSide& side) {
            CardRow row = RowOf(strings, kind, action, note.pending);
            switch (side.numbers) {
                case CoachNumbers::Rated:
                    Rate(strings, row, note.stepsBefore, side.steps, side.copies);
                    break;
                case CoachNumbers::Waiting:
                    Rate(strings, row, note.stepsBefore, 0, side.copies);
                    break;
                case CoachNumbers::None:
                    if (action.kind == ActionKind::Pass) {
                        const int steps = note.handWaits ? 0 : note.stepsBefore;
                        row.steps = StepsText(strings, steps);
                        row.trend = TrendOf(note.stepsBefore, steps);
                    }
                    break;
            }
            return row;
        }

    }  // namespace

    HintCard MakeAdviceCard(const UIStrings& strings, const TableView& view, const Explanation& explained, eastl::string_view tally) {
        HintCard card;
        card.title = TitleOf(strings, view.pendingTile, explained.Action());
        Footer footer(card, tally);
        int now = 0;
        if (explained.turn.has_value()) {
            TurnCard(strings, *explained.turn, card, footer);
            now = explained.turn->shantenBefore;
        } else if (explained.claim.has_value()) {
            ClaimCard(strings, view, *explained.claim, card, footer);
            now = explained.claim->shantenBefore;
        }
        if (!card.rows.empty()) {
            card.now = Tr(strings, "hints.card.now", {{"now", StepsText(strings, now)}});
        }
        EndFooter(card, tally);
        return card;
    }

    HintCard MakeNoteCard(const UIStrings& strings, const CoachNote& note, eastl::string_view tally) {
        HintCard card;
        card.title = Tr(strings, "coach.would", {{"advice", TitleOf(strings, note.pending, note.advised)}});
        Footer footer(card, tally);
        AddRow(card, NoteRow(strings, note, CardRowKind::Best, note.advised, note.advisedSide));
        AddRow(card, NoteRow(strings, note, CardRowKind::Yours, note.taken, note.takenSide));
        card.now = Tr(strings, "hints.card.now", {{"now", StepsText(strings, note.stepsBefore)}});
        if (EndsTheHand(note.advised)) {
            footer.Add(Tr(strings, "coach.complete"));
        } else if (note.advised.kind == ActionKind::Pass) {
            footer.Add(Tr(strings, note.byPoints ? "hints.why.pass.points" : (note.handWaits ? "hints.why.pass.waiting" : "hints.why.pass.noGain")));
        } else if (note.byPoints) {
            footer.Add(Tr(strings, "hints.why.kong.points"));
        } else if (note.advised.declareCalling && note.taken.kind == ActionKind::Discard && !note.taken.declareCalling) {
            footer.Add(Tr(strings, "coach.undeclared"));
        }
        EndFooter(card, tally);
        return card;
    }

    eastl::string CardTallyText(const UIStrings& strings, const CoachTally& hand, const CoachTally& match) {
        if (match.moves <= 0) {
            return {};
        }
        if (hand.moves <= 0) {
            return Tr(strings, "coach.card.tally.match", {{"agreed", Num(match.agreed)}, {"moves", Num(match.moves)}});
        }
        return Tr(strings, "coach.card.tally.hand", {{"agreed", Num(hand.agreed)}, {"moves", Num(hand.moves)}, {"matchAgreed", Num(match.agreed)}, {"matchMoves", Num(match.moves)}});
    }

}  // namespace BECore::Mahjong
