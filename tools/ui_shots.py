#!/usr/bin/env python3
"""Screenshots of the Mahjong UI for a review: nine scenes in the window size of the run, into a folder outside the repository.

    python3 tools/ui_shots.py --exe build/macos-debug/bin/BagiMahjong --label before
    python3 tools/ui_shots.py --exe build/macos-debug/bin/BagiMahjong --label before --window 720x1280

Starts one BagiMahjong (--headless --control --manual-clock) and, for each scene, sets up the state through the control API, steps a few
frames and writes `<NN>-<scene>-<W>x<H>.png` (the native output of the headless window) and a copy scaled down by `maxSize 640` (its longer
side is 640: 640x360 for the landscape window, 360x640 for the portrait one) into `<out>/<label>/`. The scenes: 01-fresh-table,
02-claim-window, 03-score-table, 04-standings, 05-setup, 06-handoff, 07-options, 08-rules (the rules page: Options, then its Rules button)
and 09-online (the online panel: New game, then the Online button of Setup; the panel opens without a server, it only shows its menu page).
A scene that needs a particular state (a claim window, a won hand, the cover) tries the seeds `--seed .. --seed + 29` and prints the seed it
used.

The size is the window of the headless game (1920 x 1080 from the engine's `SDLWindowConfig.xml`), and no control call changes it. With
`--window WxH` that is not the default the tool writes a **temporary** `config/SDLWindowConfig.xml` of the game (next to `SceneConfig.xml`;
the game's file overrides the engine's) and removes it when it ends, also on Ctrl-C or a signal; it refuses to start when such a file is
already there (a developer's own file is never overwritten) and the file is never committed. The scene picks its portrait view for an output
at least as high as wide. The pictures are never committed: `--out` may not lie inside a git work tree, and the default is
`~/.claude/plans/mahjong-ui-skin-shots`. The engine's helpers are found through BAGI_ENGINE_DIR (default: `engine` of this repository).
Exit code: 0 done, 1 failed, 77 skipped (the executable does not exist).
"""

import argparse
import base64
import os
import signal
import subprocess
import sys
from contextlib import contextmanager
from pathlib import Path
from typing import Any, Iterator

ENGINE_DIR = Path(os.environ.get("BAGI_ENGINE_DIR") or Path(__file__).resolve().parents[1] / "engine")
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control"))
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control" / "tests"))

from bagi_control_client import BagiControlClient, ConnectionClosed, ControlError  # noqa: E402
from game_process import GameLaunchError, GameProcess, build_game_command, free_port  # noqa: E402
from png_reader import PngError, read_png  # noqa: E402

EXIT_DONE = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77
EXIT_TERMINATED = 143  # 128 + SIGTERM

DEFAULT_OUT = Path("~/.claude/plans/mahjong-ui-skin-shots")
DEFAULT_WINDOW = (1920, 1080)  # the headless window of the engine's SDLWindowConfig.xml (engine/src/Modules/SDL/CLAUDE.md, "Headless and screenshots")
NATIVE_MAX_SIZE = 4096  # the largest maxSize: a picture is never enlarged, so this gives the native size
SMALL_MAX_SIZE = 640
GAME_CONFIG_DIR = Path(__file__).resolve().parents[1] / "config"  # next to the game's SceneConfig.xml
WINDOW_CONFIG_NAME = "SDLWindowConfig.xml"
WINDOW_CONFIG = (
    '<?xml version="1.0"?>\n<root><window width="{width}" height="{height}" '
    'windowFlags="SDL_WINDOW_RESIZABLE|SDL_WINDOW_VULKAN|SDL_WINDOW_MAXIMIZED|SDL_WINDOW_HIGH_PIXEL_DENSITY"/></root>\n'
)
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
    client.call("ui_click", {"name": "NewGame", "canvas": "HudMenu"})
    tree = settle(client)
    check(canvas_visible(tree, "Setup"), "New game did not open the seat choice")
    return f"seed {seed}"


def options_screen(client: BagiControlClient, seed: int) -> str:
    """The page Options over the table: the button Options of the HUD."""
    reset(client, seed, "PCCC")
    settle(client)
    client.call("ui_click", {"name": "Options", "canvas": "HudMenu"})
    tree = settle(client)
    check(canvas_visible(tree, "Options"), "the button Options did not open the page")
    return f"seed {seed}"


def handoff(client: BagiControlClient, seed: int) -> str:
    """Two people at seats 0 and 1: the cover is up right after the start, the screen is to be passed to the person who acts first."""
    reset(client, seed, "PPCC")
    tree = settle(client)
    check(canvas_visible(tree, "Handoff"), "the hot-seat cover is not shown after the start with two people")
    return f"seed {seed}"


def rules_screen(client: BagiControlClient, seed: int) -> str:
    """The rules page over the table: Options of the HUD, then the Rules button of the page Options."""
    reset(client, seed, "PCCC")
    settle(client)
    client.call("ui_click", {"name": "Options", "canvas": "HudMenu"})
    settle(client)
    client.call("ui_click", {"name": "OptionsRules", "canvas": "Options"})
    tree = settle(client)
    check(canvas_visible(tree, "Rules"), "the button Rules of the options did not open the rules page")
    return f"seed {seed}"


def online_screen(client: BagiControlClient, seed: int) -> str:
    """The online panel over Setup: New game, then the Online button of Setup (hidden only in a browser build without a server address)."""
    reset(client, seed, "PCCC")
    settle(client)
    client.call("ui_click", {"name": "NewGame", "canvas": "HudMenu"})
    settle(client)
    client.call("ui_click", {"name": "SetupOnline", "canvas": "Setup"})
    tree = settle(client)
    check(canvas_visible(tree, "Online"), "the button Online of Setup did not open the online panel")
    return f"seed {seed}"


SCENES = [
    ("01-fresh-table", fresh_table),
    ("02-claim-window", claim_window),
    ("03-score-table", score_table),
    ("04-standings", standings),
    ("05-setup", setup_screen),
    ("06-handoff", handoff),
    ("07-options", options_screen),
    ("08-rules", rules_screen),
    ("09-online", online_screen),
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


def small_size(window: tuple[int, int]) -> tuple[int, int]:
    """The size of the copy `maxSize` makes: the longer side becomes SMALL_MAX_SIZE, never enlarged, the other side rounded."""
    scale = min(1.0, SMALL_MAX_SIZE / max(window))
    return round(window[0] * scale), round(window[1] * scale)


def write_shots(client: BagiControlClient, folder: Path, name: str, window: tuple[int, int]) -> list[Path]:
    written = []
    for max_size, (width, height) in ((NATIVE_MAX_SIZE, window), (SMALL_MAX_SIZE, small_size(window))):
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


@contextmanager
def temporary_window_config(window: tuple[int, int]) -> Iterator[None]:
    """The game's `config/SDLWindowConfig.xml` with the window `window` while the block runs; nothing for the default window.
    Refuses when such a file exists (it is the developer's), and removes its own in every way out (an error, Ctrl-C, SIGTERM)."""
    if window == DEFAULT_WINDOW:
        yield
        return
    path = GAME_CONFIG_DIR / WINDOW_CONFIG_NAME
    if path.exists():
        raise ShotFailure(f"{path} exists: it is not overwritten; move it away for the run or shoot the default window")

    def on_sigterm(_signal: int, _frame: Any) -> None:
        raise SystemExit(EXIT_TERMINATED)

    previous = signal.signal(signal.SIGTERM, on_sigterm)
    try:
        path.write_text(WINDOW_CONFIG.format(width=window[0], height=window[1]), encoding="utf-8", newline="\n")
        yield
    finally:
        path.unlink(missing_ok=True)
        signal.signal(signal.SIGTERM, previous)


def parse_window(text: str) -> tuple[int, int]:
    parts = text.lower().split("x")
    if len(parts) != 2 or not all(part.isdigit() and int(part) > 0 for part in parts):
        raise argparse.ArgumentTypeError(f"'{text}' is not WxH (for example 720x1280)")
    return int(parts[0]), int(parts[1])


def run(exe: Path, folder: Path, seed: int, window: tuple[int, int]) -> None:
    folder.mkdir(parents=True, exist_ok=True)
    port = free_port()
    with GameProcess(build_game_command(exe, port, headless=True, manual_clock=True), cwd=exe.parent) as game:
        client = game.connect(port)
        try:
            for name, scene in SCENES:
                note = scene(client, seed)
                for path in write_shots(client, folder, name, window):
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
    parser.add_argument("--window", type=parse_window, default=DEFAULT_WINDOW, metavar="WxH",
                        help="the size of the headless window (default %dx%d); another size is made by a temporary config/SDLWindowConfig.xml" % DEFAULT_WINDOW)
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
        with temporary_window_config(args.window):
            run(args.exe.resolve(), folder, args.seed, args.window)  # the game runs with its own directory as the working directory
    except (ShotFailure, GameLaunchError) as error:
        print(f"FAILED: {error}")
        return EXIT_FAILED
    print(f"DONE: {len(SCENES) * 2} pictures in {folder}")
    return EXIT_DONE


if __name__ == "__main__":
    sys.exit(main())
