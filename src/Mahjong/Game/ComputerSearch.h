#pragma once

#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/MahjongRng.h>

namespace BECore::Mahjong {

    /// A decision with fewer candidates than this is not searched (there is nothing to compare), and draws nothing.
    inline constexpr int kSearchMinCandidates = 2;
    /// A discard is a candidate when it leaves a shanten at most this much above the best discard's.
    inline constexpr int kSearchShantenSlack = 1;
    /// The range of the one number a search decision draws from the player's rng (the base seed of its worlds and rollouts).
    inline constexpr uint32_t kSearchBaseSeedRange = 0x7fffffffU;
    /// The fixed mixing of the base seed and the index of a world: `MahjongRng(base * kSearchSeedMixer + index)` rolled once. Never a
    /// draw from the machine, so one seed is one match.
    inline constexpr uint32_t kSearchSeedMixer = 2654435761U;
    /// What `profile.searchMargin` counts in: tenths of a standard error.
    inline constexpr double kSearchMarginUnit = 0.1;

    /**
     * @brief The candidates of a search on the own turn (`Phase::Discard`): `baseline` (the Normal choice) first, then at most
     *        `maxCount - 1` others, all elements of `view.legal`, no two equal.
     *
     * When Normal declares the calling hand the others are the other declaring discards, by the unseen copies they wait on (a
     * declaration is never worse, and a win needs it); when the hand is declared, the other discards that keep it waiting. Otherwise
     * the listed concealed and upgraded kongs, then the discards (not the declaring ones) that leave a shanten at most
     * `kSearchShantenSlack` above the best, best first (the shanten, the acceptance, the lowest `KeepValue`, the lowest id).
     */
    [[nodiscard]] eastl::vector<HandAction> TurnCandidates(const TableView& view, const HandAction& baseline, int maxCount);

    /**
     * @brief The candidates of a search in a claim window: `baseline` first, then `Pass` and every listed pung, kong and chow, the
     *        claims by `RateClaim` (the lowest shanten, the highest acceptance, `ClaimRank`), at most `maxCount` in all, no two equal.
     */
    [[nodiscard]] eastl::vector<HandAction> ClaimCandidates(const TableView& view, const HandAction& baseline, int maxCount);

    /**
     * @brief The search on the own turn (`ChooseTurnAction` calls it after `DeclareWin`): the Normal choice is played against the
     *        other candidates in `profile.searchWorlds` sampled worlds, and replaced only by a candidate that is clearly better.
     *
     * Each world is `SampleWorld(view, ...)`; the candidate is applied in it and four Normal players (`kNormalProfile`) play the hand
     * to its end; the value is the seat's points (`deltas` of `SettleHand`). Every candidate is played on the same worlds with the same
     * rollout seeds (common random numbers) and is compared by the paired difference to the Normal choice: the best candidate wins
     * when its mean advantage is above 0 and above `profile.searchMargin` tenths of the standard error of the difference (a margin
     * above 0 also needs a spread to measure, so two worlds at least). The baseline is the Normal choice by the profile with the search
     * off, made on a copy of `rng`.
     *
     * Draws exactly one number from `rng` when it searches (two candidates at least) and nothing otherwise; when no world can be
     * sampled the answer is the Normal choice, after that one draw. `nullopt` means "answer as Normal does" with nothing drawn: the
     * search is off or there is nothing to compare; the caller then runs the Normal chooser, which on the state `rng` had gives the
     * baseline again.
     */
    [[nodiscard]] std::optional<HandAction> SearchTurnAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile);

    /// `SearchTurnAction` for a claim window (`ChooseClaimAction` calls it after the mahjong claim and the pass of a waiting hand).
    /// Not searched: a window whose only action is `Pass`.
    [[nodiscard]] std::optional<HandAction> SearchClaimAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile);

}  // namespace BECore::Mahjong
