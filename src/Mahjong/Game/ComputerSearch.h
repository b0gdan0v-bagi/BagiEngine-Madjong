#pragma once

#include <Mahjong/Game/ComputerLevel.h>
#include <Mahjong/Game/ComputerView.h>
#include <Mahjong/Game/HandModel.h>
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

    /// One world of a search played through: `values[c]` is the points of candidate `c` in it. `played` is false for a world that
    /// cannot be sampled, or whose rollout the model refuses: the worlds `Step()` drops.
    struct SearchWorld {
        bool played = false;
        eastl::vector<int> values;
    };

    /// The loop a tool supplies to `SearchRun::RunWorlds`: runs `body(i)` for every `i` in `[0, count)`, in any order and on any
    /// threads, and returns when all are done.
    using WorldLoop = std::function<void(int count, const std::function<void(int index)>& body)>;

    /**
     * @brief A search decision as a resumable object: `Step()` plays one rollout, `Result()` is the answer when `Done()`.
     *
     * Made only by `BeginTurnSearch` / `BeginClaimSearch`, which draw the one number from the player's rng, or by `BeginSearchOver`
     * (a tool's own list of actions and base seed, no rng); after that the run holds
     * copies only (the view, the candidates, the margin, the base seed, the values so far) and touches no rng, session, model or scene,
     * so it can be stepped on another thread and dropped with nothing changed. A step is candidate `i` of world `w`; the world is
     * sampled when `i` is 0. A world that cannot be sampled, or one whose rollout the model refuses, is skipped whole in the step that
     * finds it. `SearchTurnAction` is "begin, step to the end", so the answer in the background is the one computed at once.
     *
     * The other way to play it: the worlds are independent, so a tool may play them in any order on any threads (`PlayWorld`) and hand
     * them back in order (`Finish`), or give a `WorldLoop` to `RunWorlds`. `Game/` starts no thread; the loop is the tool's. Every
     * number is a function of the base seed and the index of the world, and the sums run in world order, so the answer and the
     * counters are the stepped run's whatever the order and the thread count.
     */
    class SearchRun {
    public:
        [[nodiscard]] bool Done() const noexcept;

        /// Plays one rollout; a no-op when `Done()`.
        void Step();

        /// The number of worlds the run plays (`searchWorlds`).
        [[nodiscard]] int WorldCount() const noexcept;

        /// World `index` played through, the same one `Step()` would play: pure, repeatable, changes nothing of the run, so several
        /// threads may call it at once for different (or equal) indexes. An index outside `[0, WorldCount())` is a world not played.
        [[nodiscard]] SearchWorld PlayWorld(int index) const;

        /// Takes the worlds `PlayWorld` gave, one slot per index in order, and makes the run `Done()`. A no-op (the contract) on a run
        /// that was stepped or finished already, when the count is not `WorldCount()`, or a played world has not one value per candidate.
        void Finish(eastl::span<const SearchWorld> worlds);

        /// `PlayWorld` for every index through `loop` (slot `i` is written by `body(i)` only), then `Finish`. A no-op on a run that was
        /// stepped or finished already.
        void RunWorlds(const WorldLoop& loop);

        /// The answer: the best candidate when its advantage clears the margin, else the baseline (also when no world could be sampled).
        /// Valid when `Done()`. It takes the front candidate for Normal's choice: a run made by `BeginSearchOver` has no such baseline,
        /// so its reader ignores `Result()` and reads `Values()`.
        [[nodiscard]] HandAction Result() const;

        /// The candidates in the order the values are listed in (the order `BeginSearchOver` was given).
        [[nodiscard]] const eastl::vector<HandAction>& Candidates() const noexcept;

        /// `Values()[c]` is the points of candidate `c` in every world that was played, the same worlds in the same order for every
        /// candidate (so the rows have one length and pair by index). Complete when `Done()`.
        [[nodiscard]] const eastl::vector<eastl::vector<int>>& Values() const noexcept;

        /// The worlds that could not be played (`WorldCount()` minus the length of one row of `Values()`). Valid when `Done()`.
        [[nodiscard]] int DroppedWorlds() const noexcept;

        /// The rollouts played and the most there can be (a skipped world counts all its rollouts as played).
        [[nodiscard]] int StepsDone() const noexcept;
        [[nodiscard]] int StepsTotal() const noexcept;

    private:
        friend std::optional<SearchRun> BeginTurnSearch(const TableView& view, MahjongRng& rng, const LevelProfile& profile);
        friend std::optional<SearchRun> BeginClaimSearch(const TableView& view, MahjongRng& rng, const LevelProfile& profile);
        friend std::optional<SearchRun> BeginSearchOver(const TableView& view, eastl::vector<HandAction> actions, uint32_t baseSeed, int worlds);

        SearchRun(const TableView& view, eastl::vector<HandAction> candidates, uint32_t base, const LevelProfile& profile);

        [[nodiscard]] int StepsPerWorld() const noexcept;
        [[nodiscard]] bool Untouched() const noexcept;
        void NextWorld();

        TableView _view;
        eastl::vector<HandAction> _candidates;
        uint32_t _base = 0;
        int _worlds = 0;
        int _margin = 0;
        /// `_values[c]`: the points of candidate `c` in every world that was played through (the same worlds for every candidate).
        eastl::vector<eastl::vector<int>> _values;
        int _worldIndex = 0;
        int _candidateIndex = 0;
        std::optional<HandModel> _sampled;
        uint32_t _tableSeed = 0;
        eastl::vector<int> _row;
    };

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
     * baseline again. The run is the same decision, not yet played.
     */
    [[nodiscard]] std::optional<SearchRun> BeginTurnSearch(const TableView& view, MahjongRng& rng, const LevelProfile& profile);

    /// `BeginTurnSearch` for a claim window (`ChooseClaimAction` calls it after the mahjong claim and the pass of a waiting hand).
    /// Not searched: a window whose only action is `Pass`.
    [[nodiscard]] std::optional<SearchRun> BeginClaimSearch(const TableView& view, MahjongRng& rng, const LevelProfile& profile);

    /**
     * @brief A run over a list of actions the caller chose (the EV analyzer's way in): every action is played in `worlds` sampled worlds
     *        with the rollouts of the search, the base seed given instead of drawn.
     *
     * The worlds, their rollout seeds and the rollouts are those of `BeginTurnSearch` for the same base seed, so a value here is the
     * value that search would have computed. Draws from no rng; asks for no Normal baseline and no second candidate, and does not check
     * that the actions are listed (an action the model refuses drops its world, as in any run). `nullopt` for a view with no legal
     * action, an empty list or fewer than one world. The margin is 0; read `Values()` after `RunWorlds` (or the steps), not `Result()`.
     */
    [[nodiscard]] std::optional<SearchRun> BeginSearchOver(const TableView& view, eastl::vector<HandAction> actions, uint32_t baseSeed, int worlds);

    /// Where a chooser puts a search it did not run (`ChooseTurnAction` / `ChooseClaimAction` with a non-null handoff): the run that
    /// `SearchTurnAction` / `SearchClaimAction` would have stepped to its end. Empty when the decision is not a search; the chooser's
    /// return value is then the answer, and when it is set the return value is a placeholder the caller discards.
    struct SearchHandoff {
        std::optional<SearchRun> run;
    };

    /// `BeginTurnSearch`, then every step at once: what a caller that does not wait gets. Same draws, same answer.
    [[nodiscard]] std::optional<HandAction> SearchTurnAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile);

    /// `BeginClaimSearch`, then every step at once.
    [[nodiscard]] std::optional<HandAction> SearchClaimAction(const TableView& view, MahjongRng& rng, const LevelProfile& profile);

}  // namespace BECore::Mahjong
