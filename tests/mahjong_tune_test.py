#!/usr/bin/env python3
"""Tests of the pure part of tools/mahjong_tune.py: the list of weights against the C++ it mirrors, the arena spec, the CEM sampling and update,
and the guards of the ranges and of the log path; no build.

    python3 tests/mahjong_tune_test.py

Registered as the ctest entry MahjongTune."""

import contextlib
import math
import re
import sys
import tempfile
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY / "tools"))

import mahjong_tune as tune  # noqa: E402

SKIP_ON_WINDOWS = unittest.skipIf(sys.platform == "win32", "the guard of the log path asks git by a POSIX path")

# What the judged ranges of docs/bot-weights.md are, written again here so a change of the constants in the tool is noticed.
DECISION_RANGE = (100000, 101000)
CONFIRMATION_RANGE = (130000, 131000)

class SourcesTest(unittest.TestCase):
    """The list of weights is held to the C++ it mirrors."""

    def test_the_weights_are_the_w_knobs_of_the_arena_players_in_order(self) -> None:
        text = (REPOSITORY / "src/Mahjong/Arena/ArenaPlayer.h").read_text(encoding="utf-8")
        names = re.findall(r'\{"(w[A-Z]\w*)", &LevelProfile::\1, nullptr, kWeightMax\}', text)
        self.assertEqual([weight.name for weight in tune.WEIGHTS], names)

    def test_the_maximum_is_kWeightMax(self) -> None:
        text = (REPOSITORY / "src/Mahjong/Game/ComputerLevel.h").read_text(encoding="utf-8")
        self.assertEqual(int(re.search(r"kWeightMax = (\d+);", text).group(1)), tune.WEIGHT_MAX)
        self.assertTrue(all(weight.maximum == tune.WEIGHT_MAX for weight in tune.WEIGHTS))

    def test_the_starts_are_the_normal_defaults_of_ComputerLevel_h(self) -> None:
        # The defaults are named constants of arithmetic on other constants (`kNormalWeightClaimMargin`): read them all and evaluate.
        text = (REPOSITORY / "src/Mahjong/Game/ComputerLevel.h").read_text(encoding="utf-8")
        constants = {"kKindCount": 34, "kCopiesPerTile": 4}  # Tile.h; a change of either moves `wClaimMargin` and fails the comparison below
        for name, expression in re.findall(r"inline constexpr int (\w+) = ([^;]+);", text):
            if re.fullmatch(r"[\w\s*+\-()]+", expression):
                with contextlib.suppress(NameError):  # a name of another header
                    constants[name] = int(eval(expression, {"__builtins__": {}}, dict(constants)))  # noqa: S307 (names and + - * only)
        for name in ("kAcceptanceMax", "kClaimRankMax"):
            self.assertIn(name, constants)
        self.assertEqual(constants["kAcceptanceMax"], 136)
        for weight in tune.WEIGHTS:
            self.assertEqual(constants["kNormalWeight" + weight.name[1:]], weight.start, weight.name)


class SpecTest(unittest.TestCase):
    def test_the_spec_has_every_weight_once_in_order_and_one_player(self) -> None:
        spec = tune.weights_spec(tune.start_weights())
        self.assertTrue(spec.startswith("normal:weighted=1,"))
        knobs = spec.split(":", 1)[1].split(",")
        self.assertEqual(knobs[0], "weighted=1")
        self.assertEqual([knob.split("=")[0] for knob in knobs[1:]], [weight.name for weight in tune.WEIGHTS])
        self.assertNotIn(",,", spec)
        self.assertFalse(spec.endswith(","))

    def test_the_values_round_trip(self) -> None:
        weights = tuple(range(7, 7 + len(tune.WEIGHTS)))
        knobs = dict(item.split("=") for item in tune.weights_spec(weights).split(":", 1)[1].split(","))
        self.assertEqual(tuple(int(knobs[weight.name]) for weight in tune.WEIGHTS), weights)
        self.assertEqual(tune.weights_from_dict(tune.weights_dict(weights)), weights)

    def test_the_start_state_is_the_start_weights(self) -> None:
        state = tune.initial_state()
        self.assertEqual(tune.mean_weights(state), tune.start_weights())
        for weight, sigma in zip(tune.WEIGHTS, state.sigma):
            self.assertEqual(sigma, tune.SIGMA_START if weight.start > 0 else tune.SIGMA_START_ZERO)


class SamplingTest(unittest.TestCase):
    def test_a_candidate_set_is_a_function_of_the_state_the_seed_and_the_generation(self) -> None:
        state = tune.initial_state()
        first = tune.sample_candidates(state, 8, 3, 5)
        self.assertEqual(first, tune.sample_candidates(state, 8, 3, 5))
        self.assertNotEqual(first, tune.sample_candidates(state, 8, 3, 6))
        self.assertNotEqual(first, tune.sample_candidates(state, 8, 4, 5))

    def test_candidates_are_whole_numbers_within_the_range(self) -> None:
        wide = tune.State(tuple([tune.X_CEILING] * len(tune.WEIGHTS)), tuple([5.0] * len(tune.WEIGHTS)))
        for candidate in tune.sample_candidates(wide, 200, 1, 0):
            for value in candidate:
                self.assertIsInstance(value, int)
                self.assertTrue(0 <= value <= tune.WEIGHT_MAX)

    def test_a_zero_start_samples_above_zero_and_clamps_below(self) -> None:
        index = [weight.name for weight in tune.WEIGHTS].index("wSafe")
        values = [candidate[index] for candidate in tune.sample_candidates(tune.initial_state(), 200, 2, 0)]
        self.assertTrue(any(value > 0 for value in values))
        self.assertTrue(any(value == 0 for value in values))

    def test_the_elite_are_the_best_quarter_ties_by_index(self) -> None:
        self.assertEqual(tune.elite_indices([1.0, 5.0, 3.0, 5.0, 0.0, 2.0, 4.0, 1.0]), [1, 3])
        self.assertEqual(tune.elite_indices([2.0] * 8), [0, 1])
        self.assertEqual(len(tune.elite_indices([0.0] * 5)), 2)  # ceil(5 / 4)
        self.assertEqual(len(tune.elite_indices([0.0] * 2)), 1)


class UpdateTest(unittest.TestCase):
    def test_the_mean_moves_halfway_to_the_elite_and_the_spread_shrinks(self) -> None:
        count = len(tune.WEIGHTS)
        state = tune.State(tuple([2.0] * count), tuple([1.0] * count))
        high = tuple([54] * count)  # x = log1p(54), about 4
        candidates = [high, high, tuple([0] * count), tuple([0] * count)]
        updated = tune.update_state(state, candidates, [0, 1])
        x = math.log1p(54)
        for mu in updated.mu:
            self.assertAlmostEqual(mu, 0.5 * 2.0 + 0.5 * x, places=5)
        for sigma in updated.sigma:  # the elite agree: their std is 0, so the spread halves
            self.assertAlmostEqual(sigma, 0.5, places=6)

    def test_the_spread_follows_the_elite_std(self) -> None:
        count = len(tune.WEIGHTS)
        state = tune.State(tuple([3.0] * count), tuple([0.4] * count))
        candidates = [tuple([0] * count), tuple([round(math.expm1(2.0))] * count)]
        updated = tune.update_state(state, candidates, [0, 1])
        xs = [0.0, math.log1p(round(math.expm1(2.0)))]
        std = abs(xs[1] - xs[0]) / 2
        for sigma in updated.sigma:
            self.assertAlmostEqual(sigma, 0.5 * 0.4 + 0.5 * std, places=5)

    def test_the_spread_never_goes_below_the_floor(self) -> None:
        count = len(tune.WEIGHTS)
        state = tune.State(tuple([2.0] * count), tuple([tune.SIGMA_FLOOR] * count))
        updated = tune.update_state(state, [tuple([7] * count)] * 4, [0, 1])
        self.assertEqual(updated.sigma, tuple([tune.SIGMA_FLOOR] * count))

    def test_the_state_is_kept_at_the_precision_of_the_log(self) -> None:
        count = len(tune.WEIGHTS)
        updated = tune.update_state(tune.initial_state(), [tuple([13] * count), tuple([17] * count)], [0, 1])
        for value in updated.mu + updated.sigma:
            self.assertEqual(value, round(value, tune.STATE_DIGITS))


class GuardTest(unittest.TestCase):
    DEFAULTS = dict(train_first=200000, seeds=250, generations=20, explore_first=4000, explore_seeds=250)

    def refused(self, **changes) -> str:
        with self.assertRaises(tune.TuneRefused) as caught:
            tune.check_ranges(**{**self.DEFAULTS, **changes})
        return str(caught.exception)

    def test_the_defaults_pass(self) -> None:
        tune.check_ranges(**self.DEFAULTS)
        tune.check_ranges(**{**self.DEFAULTS, "explore_seeds": 1000})

    def test_the_judged_ranges_are_the_ones_of_the_doc(self) -> None:
        self.assertEqual(tune.JUDGED_RANGES, (DECISION_RANGE, CONFIRMATION_RANGE))
        self.assertEqual(tune.TRAIN_FIRST_MIN, 200000)
        self.assertEqual(tune.EXPLORE_RANGE, (4000, 5000))

    def test_a_training_call_below_200000_is_refused(self) -> None:
        self.assertIn("200000", self.refused(train_first=199999))
        self.assertIn("200000", self.refused(train_first=100000))

    def test_the_exploration_range_must_lie_in_4000_to_5000(self) -> None:
        self.assertIn("[4000, 5000)", self.refused(explore_first=3999))
        self.assertIn("[4000, 5000)", self.refused(explore_first=4000, explore_seeds=1001))
        self.assertIn("[4000, 5000)", self.refused(explore_first=100000, explore_seeds=100))

    def test_a_judged_range_is_refused_by_name_whoever_asks(self) -> None:
        # A training start that the first rule would also refuse is not the way to test this one: take the exploration and a generation.
        # No legal command can reach a judged range (the rules above keep it away); the check stays as the last line, tried here by moving it.
        original = tune.JUDGED_RANGES
        try:
            tune.JUDGED_RANGES = ((200100, 200200),)
            self.assertIn("judged range [200100, 200200)", self.refused())
            tune.JUDGED_RANGES = ((4100, 4200),)
            self.assertIn("the exploration range", self.refused())
        finally:
            tune.JUDGED_RANGES = original

    def test_the_seed_must_not_overflow_a_uint32(self) -> None:
        self.assertIn("uint32", self.refused(train_first=2 ** 32 - 1000, seeds=1000, generations=2))

    def test_counts_below_one_are_refused(self) -> None:
        for name in ("seeds", "generations", "explore_seeds"):
            self.assertIn("at least 1", self.refused(**{name: 0}))

    @SKIP_ON_WINDOWS
    def test_a_log_inside_a_git_work_tree_is_refused_even_when_ignored(self) -> None:
        with self.assertRaises(tune.TuneRefused) as caught:
            tune.check_log_path(REPOSITORY / "logs" / "mahjong-tune-never-created.jsonl")  # `logs/` is git-ignored
        self.assertIn("git work tree", str(caught.exception))
        self.assertFalse((REPOSITORY / "logs" / "mahjong-tune-never-created.jsonl").exists())

    def test_a_log_outside_git_passes(self) -> None:
        with tempfile.TemporaryDirectory(prefix="mahjong_tune_test_") as folder:
            tune.check_log_path(Path(folder) / "deeper" / "not-yet" / "log.jsonl")

    def test_without_git_a_path_under_the_repository_is_refused_and_another_passes(self) -> None:
        with self.assertRaises(tune.TuneRefused):
            tune.check_log_path(REPOSITORY / "tune.jsonl", git="no-such-git-program")
        with tempfile.TemporaryDirectory(prefix="mahjong_tune_test_") as folder:
            tune.check_log_path(Path(folder) / "log.jsonl", git="no-such-git-program")

    def test_the_default_log_is_outside_the_repository(self) -> None:
        self.assertFalse(tune.DEFAULT_LOG.is_relative_to(REPOSITORY))
        self.assertEqual(tune.DEFAULT_LOG.parent, Path.home() / ".claude" / "plans" / "mahjong-tune")


if __name__ == "__main__":
    unittest.main()
