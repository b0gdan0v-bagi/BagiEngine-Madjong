#include "ComputerWeights.h"

#include <EASTL/sort.h>

namespace BECore::Mahjong {

    int64_t RateDiscardWeighted(const LevelProfile& profile, int shanten, int acceptance, int keep, int danger) noexcept {
        return static_cast<int64_t>(profile.wShanten) * (kShantenCeiling - shanten) + static_cast<int64_t>(profile.wAcceptance) * acceptance +
               static_cast<int64_t>(profile.wKeep) * (kKeepValueTile - keep) + static_cast<int64_t>(profile.wSafe) * (kDangerWorst - danger);
    }

    Vector<WeightedDiscard> RateDiscardsWeighted(const TableView& view, const TileCounts& counts, int meldCount, const LevelProfile& profile) {
        // `RateDiscards` reads the danger only with a declared opponent, so the weight alone decides whether it is looked at.
        Vector<DiscardOption> options = RateDiscards(counts, meldCount, view, profile.wSafe > 0);
        eastl::sort(options.begin(), options.end(), [](const DiscardOption& a, const DiscardOption& b) { return a.tile.Id() < b.tile.Id(); });
        Vector<WeightedDiscard> rated;
        rated.reserve(options.size());
        for (const DiscardOption& option : options) {
            rated.push_back({option.tile, option.shanten, option.acceptance, option.keep, option.danger, RateDiscardWeighted(profile, option.shanten, option.acceptance, option.keep, option.danger)});
        }
        return rated;
    }

    Tile ChooseWeightedDiscard(const TableView& view, MahjongRng& rng, const LevelProfile& profile) {
        const Vector<WeightedDiscard> rated = RateDiscardsWeighted(view, CountTiles(view.own.concealed), MeldCountOf(view), profile);
        if (rated.empty()) {
            return Tile{};
        }
        int64_t best = rated.front().rating;
        for (const WeightedDiscard& discard : rated) {
            best = discard.rating > best ? discard.rating : best;
        }
        Vector<Tile> ties;
        for (const WeightedDiscard& discard : rated) {
            if (discard.rating == best) {
                ties.push_back(discard.tile);
            }
        }
        return PickOne(ties, rng);
    }

}  // namespace BECore::Mahjong
