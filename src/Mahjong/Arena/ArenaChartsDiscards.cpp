#include "ArenaChartsDiscards.h"

#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/OwnDraws.h>

namespace BECore::Mahjong::Arena {

    namespace {

        constexpr int kShantenOne = 1;
        constexpr int kShantenTwo = 2;
        constexpr int kCopiesSingle = 1;
        constexpr int kValueDepth = 1;

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
