#include "ComputerSearch.h"

#include <EASTL/sort.h>
#include <Mahjong/Game/ComputerClaim.h>
#include <Mahjong/Game/ComputerPlayer.h>
#include <Mahjong/Game/ComputerTurn.h>
#include <Mahjong/Game/ComputerWorld.h>
#include <Mahjong/Game/ScoreHand.h>

namespace BECore::Mahjong {

    namespace {

        /// A discard that could be a candidate: how good the rest is (lower shanten, higher acceptance or waiting copies, lower keep value).
        struct RankedDiscard {
            HandAction action;
            int shanten = 0;
            int score = 0;
            int keep = 0;
        };

        bool IsListedBefore(const RankedDiscard& a, const RankedDiscard& b) noexcept {
            if (a.shanten != b.shanten) {
                return a.shanten < b.shanten;
            }
            if (a.score != b.score) {
                return a.score > b.score;
            }
            if (a.keep != b.keep) {
                return a.keep < b.keep;
            }
            return a.action.tile.Id() < b.action.tile.Id();
        }

        bool Contains(const eastl::vector<HandAction>& actions, const HandAction& action) {
            return eastl::find(actions.begin(), actions.end(), action) != actions.end();
        }

        /// Adds `action` when there is room and it is not there yet.
        void Add(eastl::vector<HandAction>& actions, const HandAction& action, int maxCount) {
            if (static_cast<int>(actions.size()) < maxCount && !Contains(actions, action)) {
                actions.push_back(action);
            }
        }

        void AddRanked(eastl::vector<HandAction>& actions, eastl::vector<RankedDiscard>& ranked, int maxCount) {
            eastl::sort(ranked.begin(), ranked.end(), IsListedBefore);
            for (const RankedDiscard& discard : ranked) {
                Add(actions, discard.action, maxCount);
            }
        }

        /// The listed discards with `declareCalling == declaring` that leave a waiting hand, ranked by the unseen copies they wait on (the
        /// list `ComputerTurn.h` makes for steps 2 and 4 of the turn, so the candidates are ranked as Normal ranks them).
        eastl::vector<RankedDiscard> RankedWaitingDiscards(const TableView& view, bool declaring) {
            eastl::vector<RankedDiscard> ranked;
            for (const WaitingDiscard& discard : WaitingDiscards(view, declaring)) {
                ranked.push_back({HandAction::MakeDiscard(discard.tile, declaring), 0, discard.copies, discard.keep});
            }
            return ranked;
        }

        /// The listed plain discards within `kSearchShantenSlack` of the best shanten, ranked by the shanten and the acceptance of the rest.
        eastl::vector<RankedDiscard> NearBestDiscards(const TableView& view) {
            TileCounts counts = CountTiles(view.own.concealed);
            const int melds = MeldCountOf(view);
            eastl::vector<RankedDiscard> ranked;
            int best = std::numeric_limits<int>::max();
            for (const HandAction& action : view.legal) {
                if (action.kind != ActionKind::Discard || action.declareCalling) {
                    continue;
                }
                --counts[action.tile.Id()];
                const int shanten = Shanten(counts, melds);
                ranked.push_back({action, shanten, AcceptanceOf(view, counts, melds), KeepValue(action.tile, view.seatWind, view.roundWind)});
                ++counts[action.tile.Id()];
                best = eastl::min(best, shanten);
            }
            ranked.erase(eastl::remove_if(ranked.begin(), ranked.end(), [best](const RankedDiscard& d) { return d.shanten > best + kSearchShantenSlack; }), ranked.end());
            return ranked;
        }

        /// Each world uses two derived seeds: the index of the world's own `MahjongRng` and, one after it, the seed of the rollout table.
        constexpr uint32_t kSeedsPerWorld = 2;
        constexpr uint32_t kRolloutSeedOffset = 1;

        /// A seed of the search: a fixed mixing of the decision's base seed and an index (never the machine's randomness).
        uint32_t DerivedSeed(uint32_t base, uint32_t index) {
            MahjongRng mixer(base * kSearchSeedMixer + index);
            return mixer.Below(std::numeric_limits<uint32_t>::max());
        }

        /// The points of `seat` when `action` is applied in `world` and four Normal players play the hand out with `tableSeed`; nullopt
        /// when the model refuses a step (a defect of the sample, not a result).
        std::optional<int> Rollout(const HandModel& world, Seat seat, const HandAction& action, uint32_t tableSeed) {
            HandModel model = world;
            if (!model.Apply(seat, action).has_value()) {
                return std::nullopt;
            }
            ComputerTable table(tableSeed, kNormalProfile);
            if (!table.PlayHand(model).has_value()) {
                return std::nullopt;
            }
            return SettleHand(model).deltas[seat];
        }

        /// The paired difference of a candidate to the baseline over the worlds: the mean and the standard error of the mean.
        struct PairedDifference {
            double mean = 0.0;
            double error = 0.0;
        };

        PairedDifference Difference(const eastl::vector<int>& candidate, const eastl::vector<int>& baseline) {
            const size_t n = baseline.size();
            PairedDifference result;
            for (size_t i = 0; i < n; ++i) {
                result.mean += static_cast<double>(candidate[i] - baseline[i]);
            }
            result.mean /= static_cast<double>(n);
            if (n > 1) {
                double squares = 0.0;
                for (size_t i = 0; i < n; ++i) {
                    const double d = static_cast<double>(candidate[i] - baseline[i]) - result.mean;
                    squares += d * d;
                }
                result.error = std::sqrt(squares / static_cast<double>(n - 1) / static_cast<double>(n));
            }
            return result;
        }

        /// Whether the advantage is clear enough to leave the Normal choice: above 0, and above the margin in tenths of the error.
        bool BeatsMargin(const PairedDifference& difference, int margin) noexcept {
            if (difference.mean <= 0.0) {
                return false;
            }
            return margin <= 0 || (difference.error > 0.0 && difference.mean > margin * kSearchMarginUnit * difference.error);
        }

        /// The search proper over `candidates` (the baseline first, at least two): one number from `rng`, then the worlds.
        std::optional<HandAction> Search(const TableView& view, MahjongRng& rng, const LevelProfile& profile, const eastl::vector<HandAction>& candidates) {
            if (static_cast<int>(candidates.size()) < kSearchMinCandidates) {
                return std::nullopt;
            }
            const uint32_t base = rng.Below(kSearchBaseSeedRange);
            eastl::vector<eastl::vector<int>> values(candidates.size());
            for (int world = 0; world < profile.searchWorlds; ++world) {
                const uint32_t index = static_cast<uint32_t>(world) * kSeedsPerWorld;
                MahjongRng worldRng(DerivedSeed(base, index));
                const std::optional<HandModel> sampled = SampleWorld(view, worldRng);
                if (!sampled.has_value()) {
                    continue;
                }
                const uint32_t tableSeed = DerivedSeed(base, index + kRolloutSeedOffset);
                eastl::vector<int> row;
                for (const HandAction& candidate : candidates) {
                    const std::optional<int> value = Rollout(*sampled, view.seat, candidate, tableSeed);
                    if (!value.has_value()) {
                        break;
                    }
                    row.push_back(*value);
                }
                if (row.size() == candidates.size()) {
                    for (size_t i = 0; i < row.size(); ++i) {
                        values[i].push_back(row[i]);
                    }
                }
            }
            if (values.front().empty()) {
                // No world could be made (the view does not fit the rules of a sample): Normal's choice, after the one draw.
                return candidates.front();
            }
            size_t best = 0;
            PairedDifference bestDifference;
            for (size_t i = 1; i < candidates.size(); ++i) {
                const PairedDifference difference = Difference(values[i], values.front());
                if (difference.mean > bestDifference.mean) {
                    best = i;
                    bestDifference = difference;
                }
            }
            return BeatsMargin(bestDifference, profile.searchMargin) ? candidates[best] : candidates.front();
        }

        /// The profile of the baseline: the same numbers with the search off.
        LevelProfile WithoutSearch(const LevelProfile& profile) {
            LevelProfile plain = profile;
            plain.searchWorlds = 0;
            return plain;
        }

    }  // namespace

    eastl::vector<HandAction> TurnCandidates(const TableView& view, const HandAction& baseline, int maxCount) {
        eastl::vector<HandAction> candidates;
        Add(candidates, baseline, maxCount);
        if (baseline.kind == ActionKind::Discard && baseline.declareCalling) {
            eastl::vector<RankedDiscard> declaring = RankedWaitingDiscards(view, true);
            AddRanked(candidates, declaring, maxCount);
        } else if (baseline.kind == ActionKind::Discard && view.own.callingDeclared) {
            eastl::vector<RankedDiscard> keeping = RankedWaitingDiscards(view, false);
            AddRanked(candidates, keeping, maxCount);
        } else {
            for (const HandAction& action : view.legal) {
                if (action.kind == ActionKind::ConcealedKong || action.kind == ActionKind::UpgradeKong) {
                    Add(candidates, action, maxCount);
                }
            }
            eastl::vector<RankedDiscard> near = NearBestDiscards(view);
            AddRanked(candidates, near, maxCount);
        }
        return candidates;
    }

    eastl::vector<HandAction> ClaimCandidates(const TableView& view, const HandAction& baseline, int maxCount) {
        struct RankedClaim {
            HandAction action;
            DiscardRating after;
            int rank = 0;
            size_t order = 0;
        };
        eastl::vector<HandAction> candidates;
        Add(candidates, baseline, maxCount);
        Add(candidates, HandAction::MakePass(), maxCount);
        eastl::vector<RankedClaim> claims;
        for (const HandAction& action : view.legal) {
            if (action.kind == ActionKind::Claim && action.claim != ClaimKind::Mahjong) {
                claims.push_back({action, RateClaim(view, action), ClaimRank(action.claim), claims.size()});
            }
        }
        eastl::sort(claims.begin(), claims.end(), [](const RankedClaim& a, const RankedClaim& b) {
            if (a.after.shanten != b.after.shanten) {
                return a.after.shanten < b.after.shanten;
            }
            if (a.after.acceptance != b.after.acceptance) {
                return a.after.acceptance > b.after.acceptance;
            }
            return a.rank != b.rank ? a.rank > b.rank : a.order < b.order;
        });
        for (const RankedClaim& claim : claims) {
            Add(candidates, claim.action, maxCount);
        }
        return candidates;
    }

    std::optional<HandAction> SearchTurnAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile) {
        if (profile.searchWorlds <= 0 || profile.searchCandidates < kSearchMinCandidates || view.phase != Phase::Discard || view.legal.empty() || FindAction(view, ActionKind::DeclareWin) != nullptr) {
            return std::nullopt;
        }
        MahjongRng baselineRng = rng;
        const HandAction baseline = ChooseTurnAction(view, baselineRng, WithoutSearch(profile));
        return Search(view, rng, profile, TurnCandidates(view, baseline, profile.searchCandidates));
    }

    std::optional<HandAction> SearchClaimAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile) {
        if (profile.searchWorlds <= 0 || profile.searchCandidates < kSearchMinCandidates || view.legal.empty() || (view.phase != Phase::Claim && view.phase != Phase::RobKong) ||
            FindMahjong(view) != nullptr) {
            return std::nullopt;
        }
        MahjongRng baselineRng = rng;
        const HandAction baseline = ChooseClaimAction(view, baselineRng, WithoutSearch(profile));
        return Search(view, rng, profile, ClaimCandidates(view, baseline, profile.searchCandidates));
    }

}  // namespace BECore::Mahjong
