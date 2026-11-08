#!/usr/bin/env python3
"""Screenshots of the Mahjong UI for a review: six scenes, each in two sizes, into a folder outside the repository.

    python3 tools/ui_shots.py --exe build/ClangmacOS/BagiMahjong --label before

Starts one BagiMahjong (--headless --control --manual-clock) and, for each scene, sets up the state through the control API, steps a few
frames and writes `<NN>-<scene>-1920x1080.png` (the native output of the headless window) and `<NN>-<scene>-640x360.png` (the same render
scaled down by `maxSize 640`) into `<out>/<label>/`. The scenes: 01-fresh-table, 02-claim-window, 03-score-table, 04-standings, 05-setup,
06-handoff. A scene that needs a particular state (a claim window, a won hand, the cover) tries the seeds `--seed .. --seed + 29` and
prints the seed it used. The pictures are never committed: `--out` may not lie inside a git work tree, and the default is
`~/.claude/plans/mahjong-ui-polish-shots`. The engine's helpers are found through BAGI_ENGINE_DIR (default: `engine` of this repository).
Exit code: 0 done, 1 failed, 77 skipped (the executable does not exist).
"""

import argparse
import base64
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

ENGINE_DIR = Path(os.environ.get("BAGI_ENGINE_DIR") or Path(__file__).resolve().parents[1] / "engine")
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control" / "tests"))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
from game_process import GameLaunchError, GameProcess, build_game_command, free_port  # noqa: E402
from png_reader import PngError, read_png  # noqa: E402

EXIT_DONE = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77

# The default folder is the one of the entry that made the tool (mahjong-ui-polish); a later entry passes --out with its own
DEFAULT_OUT = Path("~/.claude/plans/mahjong-ui-polish-shots")
NATIVE_WIDTH = 1920  # the headless window (engine/src/Modules/SDL/CLAUDE.md, "Headless and screenshots")
NATIVE_HEIGHT = 1080
NATIVE_MAX_SIZE = 4096  # the largest maxSize: a picture is never enlarged, so this gives the native size
SMALL_MAX_SIZE = 640
SMALL_WIDTH = 640
SMALL_HEIGHT = 360
SEED_TRIES = 30
SETTLE_FRAMES = 2  # frames after a change of the game before the canvases show it
HANDS = 8
STEP_LIMIT = 2000  # actions of one hand of a person
HAND_LIMIT = 100  # hands of one match of 8 (a drawn hand is dealt again)
TABLE_SCENE = "MahjongScene"


class ShotFailure(Exception):
    pass


def check(condition: bool, message: str) -> None:
    if not condition:
        raise ShotFailure(message)


def get_state(client: BagiControlClient) -> dict[str, Any]:
    return client.call("get_state")["state"]


def settle(client: BagiControlClient) -> dict[str, Any]:
    """Steps the frames in which the scene reads the game, and returns the UI tree."""
    client.call("step", {"frames": SETTLE_FRAMES})
    tree = client.call("ui_tree")
    check(tree["scene"] == TABLE_SCENE, f"the active scene is {tree['scene']}")
    return tree


def canvas_visible(tree: dict[str, Any], node: str) -> bool:
    return any(canvas["node"] == node and canvas["visible"] for canvas in tree["canvases"])


def reset(client: BagiControlClient, seed: int, seats: str) -> None:
    client.call("reset", {"seed": seed, "seats": seats, "hands": HANDS})


def seeds(first: int) -> range:
    return range(first, first + SEED_TRIES)


# Each scene sets the game up and returns a note for the log (the seed used, if it had to be searched for)

def fresh_table(client: BagiControlClient, seed: int) -> str:
    reset(client, seed, "PCCC")
    tree = settle(client)
    check(not canvas_visible(tree, "Setup"), "the seat choice is open on a fresh table")
    return f"seed {seed}"


def offers_a_claim(client: BagiControlClient) -> bool:
    return get_state(client)["turn"]["yourTurn"] and any(action["type"] == "claim" for action in client.call("list_actions")["actions"])


def claim_window(client: BagiControlClient, first_seed: int) -> str:
    """The person discards (the first listed discard) until a window opens in which the person may claim."""
    for seed in seeds(first_seed):
        reset(client, seed, "PCCC")
        for _ in range(STEP_LIMIT):
            if get_state(client)["phase"] != "playing" or offers_a_claim(client):
                break
            discards = [action for action in client.call("list_actions")["actions"] if action["type"] == "discard"]
            if not discards:  # a window where the person can only pass: take the first action
                actions = client.call("list_actions")["actions"]
                check(len(actions) > 0, "the person has to act but has no action")
                client.call("perform_action", actions[0])
            else:
                client.call("perform_action", discards[0])
        if get_state(client)["phase"] == "playing" and offers_a_claim(client):
            settle(client)
            return f"seed {seed}"
    raise ShotFailure(f"no claim window for the person in {SEED_TRIES} seeds from {first_seed}")


def score_table(client: BagiControlClient, first_seed: int) -> str:
    """Four computers play a hand and it is won (the drawn hand has no lines)."""
    for seed in seeds(first_seed):
        reset(client, seed, "CCCC")
        state = get_state(client)
        if state["phase"] == "hand_over" and state["result"]["kind"] == "win":
            tree = settle(client)
            check(canvas_visible(tree, "HandOver"), "the score table is not shown after a won hand")
            lines = state["result"]["seats"][state["result"]["winner"]]["breakdown"]["lines"]
            return f"seed {seed}, {len(lines)} lines"
    raise ShotFailure(f"no won hand in {SEED_TRIES} seeds from {first_seed}")


def standings(client: BagiControlClient, seed: int) -> str:
    reset(client, seed, "CCCC")
    for _ in range(HAND_LIMIT):
        if get_state(client)["phase"] == "match_over":
            break
        client.call("perform_action", {"type": "next_hand"})
    check(get_state(client)["phase"] == "match_over", f"the match did not end in {HAND_LIMIT} hands")
    tree = settle(client)
    check(canvas_visible(tree, "MatchOver"), "the standings are not shown at the end of the match")
    return f"seed {seed}"


def setup_screen(client: BagiControlClient, seed: int) -> str:
    reset(client, seed, "PCCC")
    settle(client)
    client.call("ui_click", {"name": "NewGame", "canvas": "Hud"})
    tree = settle(client)
    check(canvas_visible(tree, "Setup"), "New game did not open the seat choice")
    return f"seed {seed}"


def handoff(client: BagiControlClient, seed: int) -> str:
    """Two people at seats 0 and 1: the cover is up right after the start, the screen is to be passed to the person who acts first."""
    reset(client, seed, "PPCC")
    tree = settle(client)
    check(canvas_visible(tree, "Handoff"), "the hot-seat cover is not shown after the start with two people")
    return f"seed {seed}"


SCENES = [
    ("01-fresh-table", fresh_table),
    ("02-claim-window", claim_window),
    ("03-score-table", score_table),
    ("04-standings", standings),
    ("05-setup", setup_screen),
    ("06-handoff", handoff),
]


def screenshot(client: BagiControlClient, max_size: int) -> tuple[bytes, int, int]:
    """The PNG bytes of a `screenshot` call, with the size read from the file (it has to be the size the answer reports)."""
    shot = client.call("screenshot", {"maxSize": max_size})
    data = base64.b64decode(shot["png_base64"])
    try:
        image = read_png(data)
    except (PngError, ValueError) as error:
        raise ShotFailure(f"the screenshot is not a readable PNG: {error}") from error
    check((image.width, image.height) == (shot["width"], shot["height"]), f"the screenshot says {shot['width']}x{shot['height']}, the picture is {image.width}x{image.height}")
    check(len(image.colors()) > 1, "the screenshot is one flat colour")
    return data, image.width, image.height


def write_shots(client: BagiControlClient, folder: Path, name: str) -> list[Path]:
    written = []
    for max_size, width, height in ((NATIVE_MAX_SIZE, NATIVE_WIDTH, NATIVE_HEIGHT), (SMALL_MAX_SIZE, SMALL_WIDTH, SMALL_HEIGHT)):
        data, got_width, got_height = screenshot(client, max_size)
        check((got_width, got_height) == (width, height), f"{name}: the picture is {got_width}x{got_height}, expected {width}x{height}")
        path = folder / f"{name}-{width}x{height}.png"
        path.write_bytes(data)
        written.append(path)
    return written


def inside_a_git_work_tree(path: Path) -> bool:
    """True when the nearest existing parent of `path` is in a git work tree."""
    parent = path
    while not parent.exists():
        parent = parent.parent
    result = subprocess.run(["git", "-C", str(parent), "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    return result.returncode == 0


def run(exe: Path, folder: Path, seed: int) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    port = free_port()
    with GameProcess(build_game_command(exe, port, headless=True, manual_clock=True), cwd=exe.parent) as game:
        client = game.connect(port)
        try:
            for name, scene in SCENES:
                note = scene(client, seed)
                for path in write_shots(client, folder, name):
                    print(f"{path.name}: {note}", flush=True)
            client.call("quit")
        except (ShotFailure, ControlError, ConnectionClosed, OSError) as error:
            raise ShotFailure(f"{error}\n--- game log (tail) ---\n{game.log_tail()}") from error
        finally:
            try:
                client.close()
            except (OSError, ConnectionClosed):
                pass


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--exe", required=True, type=Path, help="the game executable")
    parser.add_argument("--label", required=True, help="the name of the folder in --out (before, after, ...)")
    parser.add_argument("--out", type=Path, default=DEFAULT_OUT, help=f"the folder of the folders (default {DEFAULT_OUT}); outside every git work tree")
    parser.add_argument("--seed", type=int, default=1, help="the first seed (scenes that need a state search from here)")
    args = parser.parse_args()

    if not args.exe.is_file():
        print(f"SKIPPED: {args.exe} does not exist")
        return EXIT_SKIPPED
    if not args.label or args.label in (".", "..") or "/" in args.label or "\\" in args.label or os.sep in args.label:
        print(f"FAILED: the label '{args.label}' is not a folder name")
        return EXIT_FAILED
    out = args.out.expanduser().resolve()
    if inside_a_git_work_tree(out):
        print(f"FAILED: {out} lies inside a git work tree; the pictures are never committed")
        return EXIT_FAILED

    folder = out / args.label
    try:
        run(args.exe.resolve(), folder, args.seed)  # the game runs with its own directory as the working directory
    except (ShotFailure, GameLaunchError) as error:
        print(f"FAILED: {error}")
        return EXIT_FAILED
    print(f"DONE: {len(SCENES) * 2} pictures in {folder}")
    return EXIT_DONE


if __name__ == "__main__":
    sys.exit(main())
