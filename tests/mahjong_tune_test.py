#!/usr/bin/env python3
"""Tests of tools/mahjong_tune.py: the pure functions of the CEM and the guards, then `train` against a fake arena (a script that prints the
`--json` document of BagiMahjongArena); no build.

    python3 tests/mahjong_tune_test.py

The fake arena values a player `f(w) = -scale * sum((log1p(w_i) - log1p(target_i))^2)`, `normal` having the start weights, and answers the
paired points a hand of a focus player as `f(player) - f(first)` plus a noise from a hash of the seed and the player index. Every call it gets
is appended to a file, so a test reads back what the tuner asked for. Registered as the ctest entry MahjongTune.
"""

import contextlib
import io
import json
import math
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

REPOSITORY = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPOSITORY / "tools"))

import mahjong_tune as tune  # noqa: E402

SKIP_ON_WINDOWS = unittest.skipIf(sys.platform == "win32", "the fake arena is a script with a shebang, the log guard asks git by a POSIX path")

# What the judged ranges of docs/bot-weights.md are, written again here so a change of the constants in the tool is noticed.
DECISION_RANGE = (100000, 101000)
CONFIRMATION_RANGE = (130000, 131000)

FAKE_ARENA = """#!{python}
import hashlib, json, math, sys
config = json.load(open({config!r}))
argv = sys.argv[1:]
with open({calls!r}, "a") as calls:
    calls.write(json.dumps(argv) + "\\n")
count = sum(1 for _ in open({calls!r}))
options = {{argv[i]: argv[i + 1] for i in range(0, len(argv) - 1, 2) if argv[i].startswith("--")}}
if count - 1 == config.get("failOnCall"):
    print("the fake arena fails on purpose", file=sys.stderr)
    sys.exit(1)
if count - 1 == config.get("garbageOnCall"):
    print("this is not a document")
    sys.exit(0)
names = {names!r}
start = {start!r}
target = config["target"]
normal = config.get("normal", start)

def players(text):
    parts = []
    for part in text.split(","):
        if parts and "=" in part and ":" not in part:
            parts[-1] += "," + part
        else:
            parts.append(part)
    return parts

def weights_of(spec):
    if spec == "normal":
        return list(normal)
    knobs = dict(item.split("=") for item in spec.split(":", 1)[1].split(","))
    assert knobs.pop("weighted") == "1"
    return [int(knobs[name]) for name in names]

def value(weights):
    return -config["scale"] * sum((math.log1p(w) - math.log1p(t)) ** 2 for w, t in zip(weights, target))

def noise(first, index):
    digest = hashlib.sha256(f"{{first}}:{{index}}".encode()).digest()
    return (digest[0] / 255 * 2 - 1) * config["noise"]

def cell(mean):
    return {{"mean": round(mean, 6), "half": config["half"], "hasInterval": True}}

def paired(mean):
    return dict(cell(mean), significant=abs(mean) > config["half"])

first = int(options["--first"])
focus = players(options["--focus"])
base = weights_of(focus[0])
entries = []
for index, spec in enumerate(focus):
    weights = weights_of(spec)
    points = 0.0 if weights == base else value(weights) - value(base) + noise(first, index)
    wins = points * 2
    entry = dict(name=spec, hands=4 * int(options["--seeds"]), winPercent=cell(25 + wins), pointsPerHand=cell(value(base) + points),
                 drawnPercent=0, declaredPercent=0, avgWinScore=0)
    entry["paired"] = None if index == 0 else dict(against=focus[0], pointsPerHand=paired(points), winPercent=paired(wins),
                                                   gaveWinPercent=paired(0), standingScore=paired(0))
    entries.append(entry)
print(json.dumps({{"format": "bagi-arena-report", "version": 1, "first": first, "seeds": int(options["--seeds"]),
                  "handsPerFocus": 4 * int(options["--seeds"]), "others": options["--others"], "focus": entries}}))
"""

DEFAULT_CONFIG = {"scale": 1.0, "noise": 0.05, "half": 0.1}


def run_main(*argv: str) -> tuple:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            code = tune.main(list(argv))
        except SystemExit as exit_request:  # argparse
            code = exit_request.code
    return code, out.getvalue(), err.getvalue()


def true_value(weights, target) -> float:
    return -sum((math.log1p(w) - math.log1p(t)) ** 2 for w, t in zip(weights, target))


def read_records(path: Path) -> list:
    return [json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()]


def without_wall_seconds(path: Path) -> str:
    lines = []
    for record in read_records(path):
        record.pop("wallSeconds", None)
        lines.append(tune.log_line(record))
    return "".join(lines)


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


@SKIP_ON_WINDOWS
class TrainTest(unittest.TestCase):
    CANDIDATES = 12
    SEEDS = 40
    MONITOR_SEEDS = 60

    def setUp(self) -> None:
        self.temp = Path(tempfile.mkdtemp(prefix="mahjong_tune_test_"))
        self.addCleanup(shutil.rmtree, self.temp, ignore_errors=True)
        self.start = tune.start_weights()
        # The optimum is a factor of about 3 away in the big weights and well above 0 in the weights that start at 0.
        self.target = tuple(40 if weight.start == 0 else round(weight.start * 2.7) for weight in tune.WEIGHTS)
        self.config_path = self.temp / "config.json"
        self.set_config(**DEFAULT_CONFIG)
        self.calls_path = self.temp / "calls.jsonl"
        self.arena = self.temp / "arena.py"
        self.arena.write_text(FAKE_ARENA.format(python=sys.executable, config=str(self.config_path), calls=str(self.calls_path),
                                                names=[weight.name for weight in tune.WEIGHTS], start=list(self.start)), encoding="utf-8")
        self.arena.chmod(self.arena.stat().st_mode | stat.S_IXUSR)

    def set_config(self, **config) -> None:
        self.config_path.write_text(json.dumps({"target": list(self.target), **config}), encoding="utf-8")

    def train(self, log: Path, *extra: str, seed: int = 7, generations: int = 6, candidates: int = None) -> tuple:
        return run_main("train", "--arena", str(self.arena), "--log", str(log), "--seed", str(seed), "--candidates", str(candidates or self.CANDIDATES),
                        "--seeds", str(self.SEEDS), "--monitor-seeds", str(self.MONITOR_SEEDS), "--generations", str(generations), "--jobs", "1", *extra)

    def calls(self) -> list:
        if not self.calls_path.exists():
            return []
        calls = []
        for line in self.calls_path.read_text(encoding="utf-8").splitlines():
            argv = json.loads(line)
            call = {argv[i]: argv[i + 1] for i in range(0, len(argv) - 1, 2) if argv[i] != "--json" and argv[i].startswith("--")}
            call["--json"] = argv[-1] == "--json"
            calls.append(call)
        return calls

    def test_the_tuner_climbs_the_noisy_objective(self) -> None:
        log = self.temp / "climb.jsonl"
        code, out, err = self.train(log, generations=12)
        self.assertEqual((code, err), (0, ""), out)
        records = read_records(log)
        self.assertEqual(records[0]["kind"], "header")
        self.assertEqual(len(records), 13)
        final = tune.weights_from_dict(records[-1]["newMean"])
        self.assertGreater(true_value(final, self.target), true_value(self.start, self.target) + 10.0)
        # the curve the monitoring reports says the same
        self.assertGreater(records[-1]["monitor"]["pairedPoints"]["mean"], 1.0)
        self.assertEqual(records[0]["startMonitor"]["pairedPoints"]["mean"], 0)

    def test_the_record_of_a_generation_holds_what_the_doc_promises(self) -> None:
        log = self.temp / "record.jsonl"
        self.train(log, generations=1)
        header, record = read_records(log)
        self.assertEqual(header["weightNames"], [weight.name for weight in tune.WEIGHTS])
        for key in ("baseline", "candidates", "elite", "first", "generation", "hands", "mean", "monitor", "mu", "newMean", "seeds", "sigma", "wallSeconds"):
            self.assertIn(key, record)
        self.assertEqual(record["mean"], tune.weights_dict(self.start))
        self.assertEqual(len(record["candidates"]), self.CANDIDATES)
        self.assertEqual(len(record["elite"]), 3)
        self.assertEqual(record["hands"], (1 + self.CANDIDATES) * 4 * self.SEEDS + 2 * 4 * self.MONITOR_SEEDS)
        self.assertEqual(set(record["monitor"]["pairedPoints"]), {"mean", "half", "significant"})
        self.assertEqual(record["elite"], tune.elite_indices([c["pairedPoints"]["mean"] for c in record["candidates"]]))
        self.assertEqual(self.log_keys_sorted(log), True)

    def log_keys_sorted(self, log: Path) -> bool:
        return all(tune.log_line(json.loads(line)) == line + "\n" for line in log.read_text(encoding="utf-8").splitlines())

    def test_the_same_seed_gives_the_same_log_and_another_seed_another(self) -> None:
        one, two, other = (self.temp / name for name in ("one.jsonl", "two.jsonl", "other.jsonl"))
        self.train(one, generations=3)
        self.train(two, generations=3)
        self.train(other, generations=3, seed=8)
        self.assertEqual(without_wall_seconds(one), without_wall_seconds(two))
        self.assertNotEqual(without_wall_seconds(one), without_wall_seconds(other))

    def test_a_resumed_run_equals_an_uninterrupted_one(self) -> None:
        whole, parts = self.temp / "whole.jsonl", self.temp / "parts.jsonl"
        self.assertEqual(self.train(whole, generations=5)[0], 0)
        self.assertEqual(self.train(parts, generations=2)[0], 0)
        code, out, err = self.train(parts, "--resume", generations=5)
        self.assertEqual((code, err), (0, ""), out)
        self.assertEqual(without_wall_seconds(parts), without_wall_seconds(whole))
        code, out, _ = self.train(parts, "--resume", generations=5)  # nothing left to do
        self.assertEqual(code, 0)
        self.assertEqual(without_wall_seconds(parts), without_wall_seconds(whole))

    def test_every_call_of_the_arena_is_a_legal_one(self) -> None:
        log = self.temp / "legal.jsonl"
        self.train(log, generations=4)
        calls = self.calls()
        self.assertEqual(len(calls), 1 + 4 * 2)  # the start's monitoring, then a training and a monitoring call a generation
        trained, ranges = [], []
        mean = tune.weights_spec(self.start)
        for call in calls:
            first, seeds = int(call["--first"]), int(call["--seeds"])
            ranges.append((first, first + seeds))
            self.assertTrue(call["--json"])
            if call["--others"] == "normal":  # a monitoring call
                self.assertEqual(first, 4000)
                self.assertLessEqual(seeds, 1000)
                self.assertTrue(call["--focus"].startswith("normal,normal:weighted=1,"))
            else:
                self.assertGreaterEqual(first, 200000)
                trained.append((first, first + seeds))
                self.assertTrue(call["--focus"].startswith(call["--others"] + ","))
                self.assertEqual(call["--others"].count(","), len(tune.WEIGHTS))
                self.assertEqual(len(call["--focus"].split("normal:weighted=1")) - 1, 1 + self.CANDIDATES)
            for judged in (DECISION_RANGE, CONFIRMATION_RANGE):
                self.assertFalse(first < judged[1] and judged[0] < first + seeds, (first, seeds))
        self.assertEqual(len(trained), 4)
        self.assertEqual(len(set(trained)), 4)
        for left, right in zip(sorted(trained), sorted(trained)[1:]):
            self.assertLessEqual(left[1], right[0])  # no overlap
        self.assertEqual(calls[1]["--others"], mean)  # generation 0 plays the start weights as the mean

    def test_a_judged_range_is_refused_through_the_command_line(self) -> None:
        log = self.temp / "refused.jsonl"
        code, _, err = self.train(log, "--train-first", "100000")
        self.assertEqual(code, 2)
        self.assertIn("200000", err)
        code, _, err = self.train(log, "--explore-first", "100000")
        self.assertEqual(code, 2)
        self.assertIn("[4000, 5000)", err)
        code, _, err = self.train(log, "--train-first", "99000", generations=1)
        self.assertEqual(code, 2)
        self.assertFalse(log.exists())
        self.assertEqual(self.calls(), [])

    def test_a_log_inside_the_repository_is_refused_before_anything_is_made(self) -> None:
        log = REPOSITORY / "tune-test-never-created" / "log.jsonl"
        code, _, err = self.train(log)
        self.assertEqual(code, 2)
        self.assertIn("git work tree", err)
        self.assertFalse(log.parent.exists())
        self.assertEqual(self.calls(), [])

    def test_a_tilde_in_the_log_is_expanded_once_for_the_guard_and_the_files(self) -> None:
        home = self.temp / "home"
        home.mkdir()
        with mock.patch.dict(os.environ, {"HOME": str(home), "USERPROFILE": str(home)}):
            code, _, err = self.train(Path("~") / "kept" / "log.jsonl", generations=1)
        self.assertEqual(code, 0, err)
        self.assertTrue((home / "kept" / "log.jsonl").is_file())
        self.assertFalse((Path.cwd() / "~").exists())
        self.assertFalse((REPOSITORY / "~").exists())

    def test_a_tilde_that_leads_into_the_repository_is_refused(self) -> None:
        home = REPOSITORY / "tune-test-never-created-home"
        with mock.patch.dict(os.environ, {"HOME": str(home), "USERPROFILE": str(home)}):
            code, _, err = self.train(Path("~") / "log.jsonl")
        self.assertEqual(code, 2)
        self.assertIn("git work tree", err)
        self.assertFalse(home.exists())
        self.assertEqual(self.calls(), [])

    def test_an_existing_log_is_never_overwritten(self) -> None:
        log = self.temp / "exists.jsonl"
        log.write_text("keep me\n", encoding="utf-8")
        code, _, err = self.train(log)
        self.assertEqual(code, 2)
        self.assertIn("--resume", err)
        self.assertEqual(log.read_text(encoding="utf-8"), "keep me\n")

    def test_resume_of_a_missing_log_is_refused(self) -> None:
        code, _, err = self.train(self.temp / "missing.jsonl", "--resume")
        self.assertEqual(code, 2)
        self.assertIn("does not exist", err)

    def test_a_resume_with_other_options_says_which(self) -> None:
        log = self.temp / "mismatch.jsonl"
        self.train(log, generations=1)
        before = log.read_text(encoding="utf-8")
        code, _, err = self.train(log, "--resume", seed=99)
        self.assertEqual(code, 2)
        self.assertIn("seed", err)
        code, _, err = self.train(log, "--resume", candidates=5)
        self.assertEqual(code, 2)
        self.assertIn("candidates", err)
        code, _, err = self.train(log, "--resume", "--train-first", "300000")
        self.assertEqual(code, 2)
        self.assertIn("trainFirst", err)
        self.assertEqual(log.read_text(encoding="utf-8"), before)

    def test_start_weights_that_are_not_normal_stop_the_run_with_its_message(self) -> None:
        self.set_config(normal=[weight + 1 for weight in self.start], **DEFAULT_CONFIG)
        log = self.temp / "notnormal.jsonl"
        code, _, err = self.train(log)
        self.assertEqual(code, 1)
        self.assertIn("the start weights are not Normal's", err)
        self.assertFalse(log.exists())
        self.assertEqual(len(self.calls()), 1)

    def test_an_arena_that_fails_stops_without_a_partial_line(self) -> None:
        for config in ({"failOnCall": 1}, {"garbageOnCall": 1}, {"failOnCall": 2}, {"garbageOnCall": 2}):
            with self.subTest(config):
                self.calls_path.unlink(missing_ok=True)
                self.set_config(**DEFAULT_CONFIG, **config)
                log = self.temp / ("fail%s.jsonl" % json.dumps(config).replace(" ", ""))
                code, _, err = self.train(log, generations=3)
                self.assertEqual(code, 1)
                self.assertTrue(("fake arena fails" in err) or ("cannot read" in err), err)
                lines = log.read_text(encoding="utf-8").splitlines()
                self.assertEqual(len(lines), 1)  # the header only: no line of the generation that did not finish
                self.assertEqual(json.loads(lines[0])["kind"], "header")

    def test_a_failing_start_call_leaves_no_log(self) -> None:
        self.set_config(**DEFAULT_CONFIG, failOnCall=0)
        log = self.temp / "nostart.jsonl"
        code, _, err = self.train(log)
        self.assertEqual(code, 1)
        self.assertIn("the arena failed (exit 1)", err)
        self.assertFalse(log.exists())

    def test_a_missing_arena_is_a_failure_not_a_crash(self) -> None:
        code, _, err = run_main("train", "--arena", str(self.temp / "nothing-here"), "--log", str(self.temp / "x.jsonl"), "--seed", "1",
                                "--candidates", "4", "--seeds", "10")
        self.assertEqual(code, 1)
        self.assertIn("cannot run the arena", err)

    def test_max_seconds_stops_before_a_generation_that_would_not_fit(self) -> None:
        log = self.temp / "budget.jsonl"
        code, out, _ = self.train(log, "--max-seconds", "0")
        self.assertEqual(code, 0)
        self.assertIn("stopped before generation 0", out)
        self.assertEqual([record["kind"] for record in read_records(log)], ["header"])
        calls_after_header = len(self.calls())
        self.assertEqual(calls_after_header, 1)  # the start's monitoring only
        code, out, _ = self.train(log, "--resume", generations=2)  # and it goes on from there
        self.assertEqual(code, 0)
        self.assertEqual([record["kind"] for record in read_records(log)], ["header", "generation", "generation"])

    def test_a_clock_decides_the_stop_by_the_last_generation(self) -> None:
        log = self.temp / "clock.jsonl"
        ticks = iter(range(0, 10000, 10))  # every look at the clock is 10 s later
        options = tune.TrainOptions(arena=self.arena, log=log, seed=3, candidates=4, seeds=10, generations=50, monitor_seeds=20, train_first=200000,
                                    explore_first=4000, jobs=1, max_seconds=95.0, resume=False)
        out = io.StringIO()
        self.assertEqual(tune.train(options, clock=lambda: next(ticks), out=out), 0)
        done = len(read_records(log)) - 1
        self.assertTrue(1 <= done < 50, out.getvalue())
        self.assertIn("stopped before generation %d" % done, out.getvalue())

    def test_the_log_is_read_back_by_the_tuner_itself(self) -> None:
        log = self.temp / "readback.jsonl"
        self.train(log, generations=2)
        header, generations = tune.read_log(log)
        self.assertEqual(len(generations), 2)
        self.assertEqual(tune.mean_weights(tune.state_of_record(generations[0])), tune.weights_from_dict(generations[0]["newMean"]))
        self.assertEqual(tune.weights_from_dict(generations[1]["mean"]), tune.weights_from_dict(generations[0]["newMean"]))
        log.write_text(log.read_text(encoding="utf-8") + "{broken\n", encoding="utf-8")
        with self.assertRaises(tune.TuneError):
            tune.read_log(log)

    def test_the_tool_runs_as_a_program(self) -> None:
        result = subprocess.run([sys.executable, str(REPOSITORY / "tools" / "mahjong_tune.py"), "train", "--seed", "1", "--candidates", "4", "--seeds", "10",
                                 "--log", str(REPOSITORY / "tune-test-never-created.jsonl")], capture_output=True, text=True)
        self.assertEqual(result.returncode, 2)
        self.assertIn("git work tree", result.stderr)


if __name__ == "__main__":
    unittest.main()
