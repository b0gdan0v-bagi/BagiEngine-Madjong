#!/usr/bin/env python3
"""The pure part of the tuner of the weighted computer player (docs/bot-weights.md): the list of weights, the arena spec of a weight vector,
the cross-entropy sampling and update over log1p(weight), and the guards that keep the training off the exploration and the judged seeds.
Standard library only."""

import math
import random
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Optional, Sequence

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_LOG = Path.home() / ".claude" / "plans" / "mahjong-tune" / "train.jsonl"

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
