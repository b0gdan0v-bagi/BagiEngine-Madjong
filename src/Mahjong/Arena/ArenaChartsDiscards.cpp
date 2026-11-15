#include "ArenaChartsDiscards.h"

#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/OwnDraws.h>

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr int kShantenOne = 1;
        constexpr int kShantenTwo = 2;
        constexpr int kCopiesSingle = 1;
        constexpr int kValueDepth = 1;
        /// The rows of `SearchRun::Values()` of a turn: the candidates in the order they are given.
        constexpr size_t kFastRow = 0;
        constexpr size_t kValuedRow = 1;
        constexpr size_t kPairSize = 2;

        Vector<DiscardOption> RatedDiscardsOf(const TableView& view) {
            return RateDiscards(CountTiles(view.own.concealed), MeldCountOf(view), view);
        }

        /// The entry of `rated` for `tile`; the first entry (never reached for a pair of a measured turn) when there is none.
        const DiscardOption& OptionOf(const Vector<DiscardOption>& rated, Tile tile) {
            for (const DiscardOption& option : rated) {
                if (option.tile == tile) {
                    return option;
                }
            }
            ASSERT(false, "OptionOf: the tile is not a discard of the view");
            return rated.front();
        }

        Vector<double> AsDoubles(const Vector<int>& values) {
            Vector<double> result;
            result.reserve(values.size());
            for (const int value : values) {
                result.push_back(static_cast<double>(value));
            }
            return result;
        }

        bool Lists(const TableView& view, Tile tile) {
            return eastl::find(view.legal.begin(), view.legal.end(), HandAction::MakeDiscard(tile)) != view.legal.end();
        }

        bool SameRating(const DiscardOption& a, const DiscardOption& b) noexcept {
            return a.shanten == b.shanten && a.acceptance == b.acceptance && a.danger == b.danger && a.keep == b.keep;
        }

    }  // namespace

    DiscardShanten DiscardShantenBin(int shanten) noexcept {
        if (shanten <= kShantenOne) {
            return DiscardShanten::One;
        }
        return shanten == kShantenTwo ? DiscardShanten::Two : DiscardShanten::ThreeOrMore;
    }

    ValueCost ValueCostOf(int normalShanten, int valuedShanten) noexcept {
        return valuedShanten > normalShanten ? ValueCost::Higher : ValueCost::Same;
    }

    ValueHeld ValueHeldBin(int copies) noexcept {
        if (copies < kCopiesSingle) {
            return ValueHeld::None;
        }
        return copies == kCopiesSingle ? ValueHeld::Single : ValueHeld::PairOrSet;
    }

    int MostValueCopies(const TableView& view) noexcept {
        const TileCounts concealed = CountTiles(view.own.concealed);
        const TileCounts melds = CountMeldTiles(view.own.melds);
        int most = 0;
        for (uint8_t id = 0; id < kKindCount; ++id) {
            if (IsValueTile(Tile{id}, view.seatWind, view.roundWind)) {
                most = eastl::max(most, concealed[id] + melds[id]);
            }
        }
        return most;
    }

    DiscardSelection SelectDiscardPair(const TableView& view) {
        DiscardSelection selection;
        if (view.phase != Phase::Discard || !IsSeat(view.seat) || view.currentSeat != view.seat) {
            return selection;
        }
        MahjongRng rng(Analyzer::kNormalChoiceSeed);
        const TurnDecision normal = DecideTurn(view, rng, kNormalProfile);
        if (normal.step != TurnStep::Discard || normal.action.kind != ActionKind::Discard) {
            return selection;
        }
        const Vector<DiscardOption> rated = RatedDiscardsOf(view);
        if (rated.empty() || rated.front().shanten < kShantenOne) {
            return selection;
        }
        const Vector<DiscardValue> values = ValueDiscards(view, kValueDepth);
        if (values.empty()) {
            selection.outcome = DiscardOutcome::NoValue;
            return selection;
        }
        const Tile valued = values.front().tile;
        for (const DiscardOption& option : rated) {
            if (!SameRating(option, rated.front())) {
                break;
            }
            if (option.tile == valued) {
                selection.outcome = DiscardOutcome::SameAsNormal;
                return selection;
            }
        }
        selection.outcome = DiscardOutcome::Measured;
        selection.pair = {normal.action.tile, valued};
        return selection;
    }

    DiscardClass ClassifyDiscard(const TableView& view, const DiscardPair& pair) {
        const Vector<DiscardOption> rated = RatedDiscardsOf(view);
        const int normalShanten = OptionOf(rated, pair.fast).shanten;
        DiscardClass cls;
        cls.valueHeld = ValueHeldBin(MostValueCopies(view));
        cls.shanten = DiscardShantenBin(normalShanten);
        cls.cost = ValueCostOf(normalShanten, OptionOf(rated, pair.valued).shanten);
        cls.stage = StageOfLiveCount(view.liveCount);
        cls.openSets = OpenSetsBin(OpenMeldCount(view.own));
        cls.east = view.seatWind == Wind::East;
        return cls;
    }

    std::expected<DiscardTurns, String> CollectDiscardTurns(uint32_t seed, const ArenaPlayers& players) {
        DiscardTurns collected;
        const HandObserver observer = [&collected](const HandModel& hand, Seat actor) {
            if (hand.GetPhase() != Phase::Discard) {
                return;
            }
            TableView view = MakeTableView(hand, actor);
            const int index = collected.ownTurns++;
            const DiscardSelection selection = SelectDiscardPair(view);
            switch (selection.outcome) {
                case DiscardOutcome::Measured:
                    collected.turns.push_back({index, eastl::move(view), selection.pair});
                    break;
                case DiscardOutcome::OtherStep:
                    ++collected.otherStep;
                    break;
                case DiscardOutcome::NoValue:
                    ++collected.noValue;
                    break;
                case DiscardOutcome::SameAsNormal:
                    ++collected.sameAsNormal;
                    break;
            }
        };
        const std::expected<HandRecord, String> record = PlayArenaHand(seed, players, false, nullptr, &observer);
        if (!record.has_value()) {
            return std::unexpected(record.error());
        }
        return collected;
    }

    std::expected<DiscardMeasure, String> MeasureDiscardTurn(const TableView& view, const DiscardPair& pair, uint32_t handSeed, int turnIndex, int worlds, const WorldLoop& loop) {
        const auto failure = [handSeed, turnIndex](const char* why) { return std::unexpected(Format("seed {} turn {}: {}", handSeed, turnIndex, why)); };
        if (worlds < 1) {
            return failure("the number of worlds must be at least 1");
        }
        if (view.phase != Phase::Discard || !IsSeat(view.seat) || view.currentSeat != view.seat || !Lists(view, pair.fast) || !Lists(view, pair.valued)) {
            return failure("the view is not an own discard turn that lists both discards of the pair");
        }
        Optional<SearchRun> run = BeginSearchOver(view, {HandAction::MakeDiscard(pair.fast), HandAction::MakeDiscard(pair.valued)}, Analyzer::BaseSeedOf(AnalysisSeedOf(handSeed, turnIndex)), worlds);
        if (!run.has_value()) {
            return failure("the search over the two discards could not begin");
        }
        run->RunWorlds(loop);
        const Vector<Vector<int>>& values = run->Values();
        DiscardMeasure measured;
        if (values.size() != kPairSize) {
            return failure("the search gave no row for a discard of the pair");
        }
        if (values.front().empty()) {
            measured.dropped = true;
            measured.reason = String(Analyzer::kNoWorldReason);
            return measured;
        }
        measured.row.cls = ClassifyDiscard(view, pair);
        measured.row.pair = pair;
        measured.row.gain = Arena::MeanCi(AsDoubles(values[kValuedRow])).mean - Arena::MeanCi(AsDoubles(values[kFastRow])).mean;
        return measured;
    }

    StringView DiscardShantenName(DiscardShanten band) noexcept {
        switch (band) {
            case DiscardShanten::One:
                return "1";
            case DiscardShanten::Two:
                return "2";
            case DiscardShanten::ThreeOrMore:
                return "3+";
        }
        return "";
    }

    StringView ValueCostName(ValueCost cost) noexcept {
        switch (cost) {
            case ValueCost::Same:
                return "same shanten";
            case ValueCost::Higher:
                return "one higher";
        }
        return "";
    }

    StringView ValueHeldName(ValueHeld held) noexcept {
        switch (held) {
            case ValueHeld::None:
                return "none";
            case ValueHeld::Single:
                return "single";
            case ValueHeld::PairOrSet:
                return "pair or set";
        }
        return "";
    }

}  // namespace BECore::Mahjong::Arena
