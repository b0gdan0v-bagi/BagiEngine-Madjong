#!/usr/bin/env python3
"""Tunes the weights of the weighted computer player (`docs/bot-weights.md`) by self-play in the bot arena, with the cross-entropy method.

    python3 tools/mahjong_tune.py train --seed 1 --candidates 16 --seeds 250 [--generations 20] [--monitor-seeds 250] [--max-seconds 480]
        [--arena build/macos-release/bin/BagiMahjongArena] [--log ~/.claude/plans/mahjong-tune/train.jsonl] [--resume] [--jobs 0]

One generation is one arena call (`--json`): the current mean is the first focus player and the baseline of the paired rows, the N sampled
candidates follow it, and all three other seats are the mean; a candidate's score is its paired points a hand. The elite (the best quarter)
move the mean and the spread of `log1p(weight)` (`update_state`). The mean is then looked at once, against three Normal seats, on the
exploration range: the curve of the run, never used to choose. Training seeds are `>= 200000`; the judged ranges of `docs/bot-weights.md`
("The split") are never played, and a command that would is refused, not clamped (`check_ranges`).

The log is a JSONL file, one line per record, written and flushed after each generation (a header first). It lies outside git
(`check_log_path`), is never overwritten, and `--resume` appends to it: the state is read from its last line, and a resumed run writes the
same bytes (but `wallSeconds`) as an uninterrupted one. `--generations` is the number of generations the log should hold; `--max-seconds`
stops before a generation that would end after that many seconds since the command started (exit 0, the log is resumable).

Exit code: 0 done (also when stopped by `--max-seconds`), 1 the arena failed or a file cannot be read, 2 a refused or wrong command line.
Standard library only.
"""

import argparse
import json
import math
import os
import random
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Optional, Sequence

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_ARENA = ROOT / "build" / "macos-release" / "bin" / "BagiMahjongArena"
DEFAULT_LOG = Path.home() / ".claude" / "plans" / "mahjong-tune" / "train.jsonl"

EXIT_DONE = 0
EXIT_FAILED = 1
EXIT_REFUSED = 2

# ----- the weights -----

WEIGHT_MAX = 65535  # `kWeightMax` of ComputerLevel.h


@dataclass(frozen=True)
class Weight:
    name: str  # the knob of `kProfileKnobs` (ArenaPlayer.h)
    start: int  # the Normal-equivalent default of ComputerLevel.h
    maximum: int = WEIGHT_MAX


# The one place a coordinate is added (stage 3 adds two). The order is the order of the spec and of every list in the log.
WEIGHTS = (
    Weight("wShanten", 4400),
    Weight("wAcceptance", 16),
    Weight("wKeep", 1),
    Weight("wSafe", 0),
    Weight("wClaimRank", 1),
    Weight("wClaimValue", 4400),
    Weight("wClaimMargin", 2178),
    Weight("wClaimWall", 0),
    Weight("wValueDiscard", 0),
    Weight("wValueClaim", 0),
)

Weights = tuple  # whole numbers, in the order of WEIGHTS


def start_weights() -> Weights:
    return tuple(weight.start for weight in WEIGHTS)


def weights_spec(weights: Sequence[int]) -> str:
    """The arena player of `weights`: `--focus` splits on a comma only when the part has no `=` or has a `:`, so this is one player."""
    knobs = ",".join(f"{weight.name}={value}" for weight, value in zip(WEIGHTS, weights))
    return f"normal:weighted=1,{knobs}"


def weights_dict(weights: Sequence[int]) -> dict:
    return {weight.name: value for weight, value in zip(WEIGHTS, weights)}


def weights_from_dict(named: dict) -> Weights:
    return tuple(int(named[weight.name]) for weight in WEIGHTS)


# ----- the CEM -----

SIGMA_START = 0.5  # of a weight whose start is above 0 (a factor of about 1.6 either way)
SIGMA_START_ZERO = 2.0  # of a weight that starts at 0: log1p(6), and what samples below 0 become 0
SIGMA_FLOOR = 0.05
SMOOTHING = 0.5  # `a` of the update
ELITE_DIVISOR = 4  # the elite are the best quarter
STATE_DIGITS = 6  # the state is kept at the log's precision, so a resumed run starts from exactly what the log says
SEED_STRIDE = 1000003  # the stream of a generation: Random(seed * SEED_STRIDE + generation)
X_CEILING = math.log1p(WEIGHT_MAX)


@dataclass(frozen=True)
class State:
    mu: tuple  # per weight, of log1p(weight)
    sigma: tuple


def initial_state() -> State:
    mu = tuple(round(math.log1p(weight.start), STATE_DIGITS) for weight in WEIGHTS)
    sigma = tuple(SIGMA_START if weight.start > 0 else SIGMA_START_ZERO for weight in WEIGHTS)
    return State(mu, sigma)


def weight_of(x: float, maximum: int = WEIGHT_MAX) -> int:
    """The whole weight an `x = log1p(weight)` stands for, clamped to `0 .. maximum`."""
    return max(0, min(maximum, round(math.expm1(min(x, X_CEILING)))))


def mean_weights(state: State) -> Weights:
    return tuple(weight_of(mu, weight.maximum) for mu, weight in zip(state.mu, WEIGHTS))


def sample_candidates(state: State, count: int, seed: int, generation: int) -> list:
    """`count` candidates for a generation: a function of the state, the seed and the generation alone."""
    rng = random.Random(seed * SEED_STRIDE + generation)
    candidates = []
    for _ in range(count):
        candidates.append(tuple(
            weight_of(mu + sigma * rng.gauss(0.0, 1.0), weight.maximum)
            for mu, sigma, weight in zip(state.mu, state.sigma, WEIGHTS)))
    return candidates


def elite_indices(scores: Sequence[float]) -> list:
    """The best quarter of the candidates (`ceil(N / 4)`), best first; equal scores by candidate index."""
    count = max(1, -(-len(scores) // ELITE_DIVISOR))
    return sorted(range(len(scores)), key=lambda index: (-scores[index], index))[:count]


def update_state(state: State, candidates: Sequence[Weights], elite: Sequence[int]) -> State:
    """`mu' = (1-a) mu + a mean(x_elite)`, `sigma' = max(floor, (1-a) sigma + a std(x_elite))`, `x = log1p(weight played)`."""
    mu, sigma = [], []
    for index, weight in enumerate(WEIGHTS):
        xs = [math.log1p(candidates[chosen][index]) for chosen in elite]
        mean = sum(xs) / len(xs)
        spread = math.sqrt(sum((x - mean) ** 2 for x in xs) / len(xs))
        mu.append(round((1 - SMOOTHING) * state.mu[index] + SMOOTHING * mean, STATE_DIGITS))
        sigma.append(round(max(SIGMA_FLOOR, (1 - SMOOTHING) * state.sigma[index] + SMOOTHING * spread), STATE_DIGITS))
    return State(tuple(mu), tuple(sigma))


# ----- the guards -----


class TuneError(Exception):
    """A run that cannot go on (the arena failed, a file cannot be read): exit 1."""

    exit_code = EXIT_FAILED


class TuneRefused(TuneError):
    """A command that is refused, never clamped: exit 2."""

    exit_code = EXIT_REFUSED


TRAIN_FIRST_MIN = 200000  # training seeds only above this (docs/bot-weights.md, "The split")
EXPLORE_RANGE = (4000, 5000)  # the exploration range may be looked at; the monitoring call stays inside it
DEFAULT_EXPLORE_FIRST = EXPLORE_RANGE[0]
JUDGED_RANGES = ((100000, 101000), (130000, 131000))  # decision and confirmation: played once, with the final weights, by no call here
SEED_LIMIT = 2 ** 32  # the arena's seed is a uint32


def training_range(train_first: int, seeds: int, generation: int) -> tuple:
    first = train_first + generation * seeds
    return first, first + seeds


def check_ranges(train_first: int, seeds: int, generations: int, explore_first: int, explore_seeds: int) -> None:
    """Refuses (`TuneRefused`) a run whose arena calls would leave the allowed ranges; nothing is changed to fit."""
    if seeds < 1 or generations < 1 or explore_seeds < 1:
        raise TuneRefused("--seeds, --generations and --monitor-seeds are at least 1")
    if train_first < TRAIN_FIRST_MIN:
        raise TuneRefused(f"a training call may not start below {TRAIN_FIRST_MIN} (--train-first {train_first}): "
                          "the lower seeds are the exploration and the judged ranges")
    last_end = training_range(train_first, seeds, generations - 1)[1]
    if last_end > SEED_LIMIT:
        raise TuneRefused(f"the training calls end at seed {last_end}, past the uint32 seed of the arena ({SEED_LIMIT})")
    low, high = EXPLORE_RANGE
    if explore_first < low or explore_first + explore_seeds > high:
        raise TuneRefused(f"the exploration range [{explore_first}, {explore_first + explore_seeds}) is not inside [{low}, {high})")
    calls = [("training generation %d" % generation, training_range(train_first, seeds, generation)) for generation in range(generations)]
    calls.append(("the exploration range", (explore_first, explore_first + explore_seeds)))
    for label, (first, end) in calls:
        for judged_first, judged_end in JUDGED_RANGES:
            if first < judged_end and judged_first < end:
                raise TuneRefused(f"{label} [{first}, {end}) touches the judged range [{judged_first}, {judged_end})")


def _git_toplevel(folder: Path, git: str) -> Optional[Path]:
    """The work tree `folder` is in; None when it is in none. OSError when git cannot be run."""
    result = subprocess.run([git, "-C", str(folder), "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    return Path(result.stdout.strip()) if result.returncode == 0 and result.stdout.strip() else None


def check_log_path(path: Path, git: str = "git") -> None:
    """Refuses (`TuneRefused`) a log inside a git work tree, even a git-ignored place: a log is never committed.

    The nearest existing parent is asked. When git cannot be run, a path under the repository root of this script is refused."""
    path = Path(path).expanduser().resolve()
    nearest = path
    while not nearest.exists() and nearest != nearest.parent:
        nearest = nearest.parent
    folder = nearest if nearest.is_dir() else nearest.parent
    try:
        toplevel = _git_toplevel(folder, git)
    except OSError:
        if path.is_relative_to(ROOT.resolve()):
            raise TuneRefused(f"{path} lies inside the repository {ROOT} (git cannot be run to ask); a tuning log is kept outside git")
        return
    if toplevel is not None:
        raise TuneRefused(f"{path} lies inside the git work tree {toplevel}; a tuning log is kept outside git (default {DEFAULT_LOG.parent})")


# ----- the arena -----

DOCUMENT_FORMAT = "bagi-arena-report"
DOCUMENT_VERSION = 1
NORMAL_SPEC = "normal"
ROUND_DIGITS = 6


def run_arena(arena: Path, focus: Sequence[str], others: str, first: int, seeds: int, jobs: int) -> dict:
    """One arena call with `--json`; the parsed document, or `TuneError` with the arena's own message."""
    command = [str(arena), "--focus", ",".join(focus), "--others", others, "--first", str(first), "--seeds", str(seeds), "--jobs", str(jobs), "--json"]
    try:
        result = subprocess.run(command, capture_output=True, text=True)
    except OSError as error:
        raise TuneError(f"cannot run the arena {arena}: {error}") from error
    if result.returncode != 0:
        message = (result.stderr.strip() or result.stdout.strip() or "no message")
        raise TuneError(f"the arena failed (exit {result.returncode}): {message}")
    try:
        document = json.loads(result.stdout)
        if document["format"] != DOCUMENT_FORMAT or document["version"] != DOCUMENT_VERSION or len(document["focus"]) != len(focus):
            raise ValueError("another document than the one asked for")
        read_document_cells(document)
    except (ValueError, KeyError, TypeError) as error:
        raise TuneError(f"the arena printed a document the tuner cannot read ({error!r}): {result.stdout[:200]!r}") from error
    return document


def read_document_cells(document: dict) -> None:
    """Reads every cell the tuner uses later, so a missing one is a `KeyError` here (a `TuneError` of `run_arena`), not a crash mid-record."""
    document["handsPerFocus"]
    for index, entry in enumerate(document["focus"]):
        entry["pointsPerHand"]["mean"], entry["winPercent"]["mean"]
        if index > 0:
            for metric in ("pointsPerHand", "winPercent"):
                cell = entry["paired"][metric]
                cell["half"], cell["mean"], cell["significant"]


def interval_of(cell: dict) -> dict:
    return {"half": cell["half"], "mean": cell["mean"]}


def paired_of(cell: dict) -> dict:
    return {"half": cell["half"], "mean": cell["mean"], "significant": cell["significant"]}


def monitor_record(document: dict) -> dict:
    """The mean (the second focus player) against Normal (the first) on the exploration range."""
    entry = document["focus"][1]
    return {
        "pointsPerHand": entry["pointsPerHand"]["mean"],
        "pairedPoints": paired_of(entry["paired"]["pointsPerHand"]),
        "pairedWins": paired_of(entry["paired"]["winPercent"]),
        "winPercent": entry["winPercent"]["mean"],
    }


def hands_of(document: dict) -> int:
    return document["handsPerFocus"] * len(document["focus"])


# ----- the log -----

HEADER = "header"
GENERATION = "generation"


def log_line(record: dict) -> str:
    return json.dumps(record, sort_keys=True, separators=(",", ":")) + "\n"


def read_log(path: Path) -> tuple:
    """`(header, generation records)`; `TuneError` for a line that is not a record, or a record out of order."""
    header, generations = None, []
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        try:
            record = json.loads(line)
            kind = record["kind"]
        except (ValueError, KeyError, TypeError) as error:
            raise TuneError(f"{path}, line {number}: not a record of the log ({error!r})") from error
        if number == 1:
            if kind != HEADER:
                raise TuneError(f"{path}, line 1: the log does not start with a header")
            header = record
        elif kind != GENERATION or record.get("generation") != number - 2:
            raise TuneError(f"{path}, line {number}: expected the record of generation {number - 2}")
        else:
            generations.append(record)
    if header is None:
        raise TuneError(f"{path} is empty")
    return header, generations


def state_of_record(record: dict) -> State:
    return State(tuple(float(record["mu"][weight.name]) for weight in WEIGHTS), tuple(float(record["sigma"][weight.name]) for weight in WEIGHTS))


def check_header(found: dict, expected: dict) -> None:
    """A resume continues the same run: every option that decides the numbers must be the log's."""
    for key in sorted(expected):
        if found.get(key) != expected[key]:
            raise TuneRefused(f"the log was made with {key} {found.get(key)!r}, this command says {expected[key]!r}; --resume continues the same run")


# ----- train -----


@dataclass(frozen=True)
class TrainOptions:
    arena: Path
    log: Path
    seed: int
    candidates: int
    seeds: int
    generations: int
    monitor_seeds: int
    train_first: int
    explore_first: int
    jobs: int
    max_seconds: Optional[float]
    resume: bool


def header_of(options: TrainOptions) -> dict:
    return {
        "candidates": options.candidates,
        "exploreFirst": options.explore_first,
        "kind": HEADER,
        "monitorSeeds": options.monitor_seeds,
        "seed": options.seed,
        "seeds": options.seeds,
        "startWeights": weights_dict(start_weights()),
        "trainFirst": options.train_first,
        "weightNames": [weight.name for weight in WEIGHTS],
    }


def monitor_call(options: TrainOptions, weights: Weights) -> dict:
    return run_arena(options.arena, [NORMAL_SPEC, weights_spec(weights)], NORMAL_SPEC, options.explore_first, options.monitor_seeds, options.jobs)


def start_header(options: TrainOptions) -> dict:
    """The header with the monitoring row of the start weights, which must be Normal's: exactly 0 on points and on wins."""
    document = monitor_call(options, start_weights())
    row = monitor_record(document)
    if row["pairedPoints"]["mean"] != 0 or row["pairedWins"]["mean"] != 0:
        raise TuneError(f"the start weights are not Normal's: against Normal they score {row['pairedPoints']['mean']} points a hand and "
                        f"{row['pairedWins']['mean']} wins %, not exactly 0 (ComputerLevel.h and WEIGHTS differ?)")
    header = header_of(options)
    header["startMonitor"] = row
    return header


def play_generation(options: TrainOptions, state: State, generation: int, clock: Callable[[], float]) -> tuple:
    """One generation: the record to write and the new state."""
    began = clock()
    mean = mean_weights(state)
    candidates = sample_candidates(state, options.candidates, options.seed, generation)
    first, _ = training_range(options.train_first, options.seeds, generation)
    players = [weights_spec(mean)] + [weights_spec(weights) for weights in candidates]
    document = run_arena(options.arena, players, weights_spec(mean), first, options.seeds, options.jobs)
    focus = document["focus"]
    scores = [entry["paired"]["pointsPerHand"]["mean"] for entry in focus[1:]]
    elite = elite_indices(scores)
    new_state = update_state(state, candidates, elite)
    monitor = monitor_call(options, mean_weights(new_state))
    record = {
        "baseline": {"pointsPerHand": focus[0]["pointsPerHand"]["mean"], "winPercent": focus[0]["winPercent"]["mean"]},
        "candidates": [
            {"pairedPoints": interval_of(entry["paired"]["pointsPerHand"]), "pairedWins": interval_of(entry["paired"]["winPercent"]),
             "weights": weights_dict(weights)}
            for entry, weights in zip(focus[1:], candidates)],
        "elite": elite,
        "first": first,
        "generation": generation,
        "hands": hands_of(document) + hands_of(monitor),
        "kind": GENERATION,
        "mean": weights_dict(mean),
        "monitor": monitor_record(monitor),
        "mu": {weight.name: value for weight, value in zip(WEIGHTS, new_state.mu)},
        "newMean": weights_dict(mean_weights(new_state)),
        "seeds": options.seeds,
        "sigma": {weight.name: value for weight, value in zip(WEIGHTS, new_state.sigma)},
        "wallSeconds": round(clock() - began, 3),
    }
    return record, new_state


def append_line(path: Path, record: dict) -> None:
    with open(path, "a", encoding="utf-8") as stream:
        stream.write(log_line(record))
        stream.flush()
        os.fsync(stream.fileno())


def signed(cell: dict) -> str:
    return f"{cell['mean']:+.2f} +- {cell['half']:.2f}"


def train(options: TrainOptions, clock: Callable[[], float] = time.monotonic, out=None) -> int:
    out = out or sys.stdout
    began = clock()
    # One path for the guard and every file operation: a literal `~` (a shell that did not expand it) must not pass as a relative name.
    log = options.log.expanduser().resolve()
    check_ranges(options.train_first, options.seeds, options.generations, options.explore_first, options.monitor_seeds)
    check_log_path(log)
    expected = header_of(options)
    if log.exists() and not options.resume:
        raise TuneRefused(f"{log} exists; --resume continues it, and a log is never overwritten")
    if options.resume and not log.exists():
        raise TuneRefused(f"--resume, but {log} does not exist")
    if options.resume:
        header, generations = read_log(log)
        check_header(header, expected)
        state = state_of_record(generations[-1]) if generations else initial_state()
        previous_seconds = generations[-1]["wallSeconds"] if generations else 0.0
        print(f"resuming {log}: {len(generations)} generations done, target {options.generations}", file=out)
    else:
        header = start_header(options)
        log.parent.mkdir(parents=True, exist_ok=True)
        append_line(log, header)
        state, generations, previous_seconds = initial_state(), [], 0.0
        print(f"start weights against Normal on [{options.explore_first}, {options.explore_first + options.monitor_seeds}): exactly 0, as it must be", file=out)
    done = len(generations)
    for generation in range(done, options.generations):
        if options.max_seconds is not None and clock() - began + previous_seconds >= options.max_seconds:
            print(f"stopped before generation {generation} (--max-seconds {options.max_seconds:g}, the last generation took {previous_seconds:.1f} s): "
                  f"{generation} of {options.generations} generations in the log, --resume goes on", file=out)
            return EXIT_DONE
        record, state = play_generation(options, state, generation, clock)
        append_line(log, record)
        previous_seconds = record["wallSeconds"]
        monitor = record["monitor"]
        print(f"generation {generation}: the mean against Normal {signed(monitor['pairedPoints'])} points a hand, {signed(monitor['pairedWins'])} wins %"
              f"{' (significant)' if monitor['pairedPoints']['significant'] else ''}, {record['wallSeconds']:.1f} s", file=out)
    print(f"{options.generations} generations in {log}", file=out)
    return EXIT_DONE


# ----- the command line -----

DEFAULT_GENERATIONS = 20
DEFAULT_MONITOR_SEEDS = 250


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Tunes the weights of the weighted computer player by self-play in the bot arena (docs/bot-weights.md).")
    commands = parser.add_subparsers(dest="command", required=True)
    train_parser = commands.add_parser("train", help="CEM generations over the weights of WEIGHTS")
    train_parser.add_argument("--arena", type=Path, default=DEFAULT_ARENA, help=f"BagiMahjongArena (default {DEFAULT_ARENA.relative_to(ROOT)})")
    train_parser.add_argument("--log", type=Path, default=DEFAULT_LOG, help=f"the JSONL log, outside git (default {DEFAULT_LOG})")
    train_parser.add_argument("--seed", type=int, required=True, help="the seed of the run (the candidates are a function of it)")
    train_parser.add_argument("--candidates", type=int, required=True, help="candidates a generation")
    train_parser.add_argument("--seeds", type=int, required=True, help="the arena's seeds in a generation (four hands for each player a seed)")
    train_parser.add_argument("--generations", type=int, default=DEFAULT_GENERATIONS, help=f"generations the log should hold (default {DEFAULT_GENERATIONS})")
    train_parser.add_argument("--monitor-seeds", type=int, default=DEFAULT_MONITOR_SEEDS,
                              help=f"seeds of the monitoring call (default {DEFAULT_MONITOR_SEEDS})")
    train_parser.add_argument("--max-seconds", type=float, default=None, help="stop before a generation that would end after this many seconds")
    train_parser.add_argument("--resume", action="store_true", help="continue an existing log of the same run")
    train_parser.add_argument("--jobs", type=int, default=0, help="the arena's --jobs (default 0: one per core)")
    train_parser.add_argument("--train-first", type=int, default=TRAIN_FIRST_MIN, help=f"the first training seed (default and least {TRAIN_FIRST_MIN})")
    train_parser.add_argument("--explore-first", type=int, default=DEFAULT_EXPLORE_FIRST,
                              help=f"the first seed of the monitoring call (default {DEFAULT_EXPLORE_FIRST}, inside [{EXPLORE_RANGE[0]}, {EXPLORE_RANGE[1]}))")
    return parser


def main(argv: Optional[Sequence[str]] = None) -> int:
    arguments = build_parser().parse_args(argv)
    if arguments.candidates < 2:
        print("error: --candidates is at least 2", file=sys.stderr)
        return EXIT_REFUSED
    options = TrainOptions(
        arena=arguments.arena, log=arguments.log, seed=arguments.seed, candidates=arguments.candidates, seeds=arguments.seeds,
        generations=arguments.generations, monitor_seeds=arguments.monitor_seeds, train_first=arguments.train_first,
        explore_first=arguments.explore_first, jobs=arguments.jobs, max_seconds=arguments.max_seconds, resume=arguments.resume)
    try:
        return train(options)
    except TuneError as error:
        print(f"error: {error}", file=sys.stderr)
        return error.exit_code


if __name__ == "__main__":
    sys.exit(main())
