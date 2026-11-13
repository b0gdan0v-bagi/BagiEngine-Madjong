#!/usr/bin/env python3
"""Browser smoke test of the packaged Mahjong page (tools/web/package.py --game BagiMahjong): the engine's smoke, then a short game.

    python tools/web_smoke.py --dist dist --browser-channel chrome --expect-console "Initialized with \\d+ worker threads"
    python tools/web_smoke.py --dist dist-single --browser-channel chrome --expect-console "single-thread mode"
    python tools/web_smoke.py --dist dist --tile-click 5,5      # deliberately wrong: has to exit 1
    python tools/web_smoke.py --check                           # the browser-free helpers; no Playwright, no build

Standard library plus Playwright (a venv in build/web-venv, engine/tools/web/CLAUDE.md, "The browser smoke"). It imports
engine/tools/web/tests/web_smoke.py by path (that file is not edited) and uses its parts.

Scenario 1: the engine's checks 1 to 5 (`main([...])` of the engine's script) with the click of check 3 on `Start` of the seat
choice: the picture changes because the panel closes and the table is dealt.

Scenario 2 (a second page load, the engine's helpers `served`, `launch_browser`, `poll`):
  (6) a click on Start changes the picture;
  (7) the table stands still (no change for STILL_SECONDS): the computers have played until the person must act;
  (8) a click on the first tile of the person's hand changes the picture (the discarded tile flies out; a game that ignored the click
      would leave the table as it was). Judged against a shot taken right before the click, after the pointer was moved there.
Seat 1 (the person) is NOT always first to act: East comes from a dice draw, so after Start the computers may have played and the
person may face a claim window (Chow, Pung, Kong, Mahjong and Pass), where a tile click does nothing. The scenario therefore
answers a window if a tile click changes nothing: it presses, one by one, the places of the first two rows of the action buttons
(CLAIM_ANSWER_CLICKS; Pass is the last button of a window, so its place depends on how many claims there are), the first that changes
the picture ends the round, the table stands still again and the tile is clicked again, up to TILE_ROUNDS times. Every outcome of
the first move is accepted; the scenario fails only when no tile click ever changes the picture. (Pressing a claim button instead of
Pass is as good: a person who claimed must discard.) Nothing is assumed about the seed or the first hand.

The coordinates are in pixels from the top left of the canvas at the viewport VIEWPORT (1280x720 is the design size of the table,
kViewWidth x kViewHeight of Table/TableLayout.h, so one design unit is one pixel; at 640x480 the viewer's hand is cut off at the bottom):
  PLATE_REGION     the Plate of config/nodes/MahjongSetup.xml: 640 wide (x 320..960), at most 528 high (centred: y 96..624).
  START_CLICK      the middle of Start: the last item of ButtonRow, the last row of the plate (padding 16, 64 high: y 544..608, middle
                   576). The row inside the plate is x 336..944 and holds Continue (flex 1.5, hidden at a first start), Online (1.1,
                   there only when MahjongOnlineConfig.xml names a server) and Start (1), 8 apart, so Start is at best x 704..944 (no
                   Online, Continue shown) and at most 658..944 (Online, no Continue) whichever of them are there: x 860 is in it always.
  CONTINUE_CLICK   the middle of Continue, the first of the row: x 336..582 with Online and 336..696 without, so x 459 is in it always.
  HAND_TILE_CLICK  the middle of the first tile of the viewer's hand (kViewerHandTileW x kViewerHandTileH = 48 x 64, the hand starts
                   at x 72, y 648).
  CLAIM_ANSWER_CLICKS  the buttons of config/nodes/MahjongActions.xml: ActionRow at x 724, y 460, three buttons of 132 x 56 per
                   row, 8 apart.
`--start-click X,Y`, `--continue-click X,Y`, `--tile-click X,Y` replace them. **When the Setup screen changes (the entry
mahjong-setup-options changes it), re-check START_CLICK, CONTINUE_CLICK and PLATE_REGION:** take a screenshot at 1280x720 and read
the middles of the buttons from it.

Scenario 3 (a new browser context, so the site's IndexedDB is empty: the saved match of the page):
  (9) Start, the table stands still (the frame that changed the table has flushed it), the page is RELOADED in the same context:
      the Setup plate now has Continue and a narrower Start, so a shot of the plate region (PLATE_REGION: the table behind the
      panel is random and does not count) differs from the shot of the first load; Continue is hidden at a first start;
  (10) a click on Continue changes the picture: the saved table is shown.
The engine's own smoke already covers a written file across a reload and a page whose IndexedDB fails (its check 8), and the game
without a remembered store is covered by the tests (MahjongSaveGameTest), so neither is repeated here.

Exit code as the engine's: 0 passed, 1 failed (the reason is printed), 77 skipped (no Playwright or no browser), 2 a wrong command line.
The console allow-list is the engine's (favicon.ico) plus --allow-console; a Debug page needs
--allow-console "Blocking on the main thread is very dangerous".
"""

import argparse
import importlib.util
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Callable

ROOT = Path(__file__).resolve().parents[1]
ENGINE_SMOKE = ROOT / "engine" / "tools" / "web" / "tests" / "web_smoke.py"


def load_engine_smoke():
    """The engine's smoke as a module `engine_web_smoke` (loaded by path: this file is also called web_smoke, so `import web_smoke`
    would find this one). It puts tools/web and tools/control/tests on sys.path itself."""
    spec = importlib.util.spec_from_file_location("engine_web_smoke", ENGINE_SMOKE)
    module = importlib.util.module_from_spec(spec)
    sys.modules["engine_web_smoke"] = module  # the engine's dataclasses look their module up here
    spec.loader.exec_module(module)
    return module


engine = load_engine_smoke()

VIEWPORT = (1280, 720)
PLATE_REGION = (320, 96, 640, 528)  # x, y, width, height of the Plate of Setup
START_CLICK = (860.0, 576.0)
CONTINUE_CLICK = (459.0, 576.0)
HAND_TILE_CLICK = (95.0, 683.0)
# The first two rows of the action buttons, three of 132 x 56 per row, left to right; the order puts the second place first (the
# Pass of a window with one claim: Chow or Pung), then the third, then the first and the second row.
CLAIM_ANSWER_CLICKS = ((930.0, 488.0), (1070.0, 488.0), (790.0, 488.0), (930.0, 552.0), (1070.0, 552.0), (790.0, 552.0))
STILL_SECONDS = 2.0  # computers step every 0.6 s while it is their turn; a table with no change for longer is waiting for the person
STILL_TIMEOUT_SECONDS = 40.0
TILE_ROUNDS = 3
ANSWER_WINDOW_SECONDS = 1.5  # how long a claim-answer click is watched for a change (a button gone is a change at once)
CLICK_WINDOW_SECONDS = 5.0  # how long a tile click is watched (the engine's CLICK_TIMEOUT_SECONDS)
POLL_INTERVAL_SECONDS = 0.5


# ----- command line -----

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Open the packaged Mahjong page in a headless Chromium-based browser: the engine's "
                                                 "checks, then Start and a click on a tile of the person's hand.")
    parser.add_argument("--dist", type=Path, help="folder made by tools/web/package.py --game BagiMahjong")
    parser.add_argument("--check", action="store_true", help="run the browser-free checks of this script and exit")
    parser.add_argument("--start-click", type=engine.parse_point, default=START_CLICK, metavar="X,Y",
                        help=f"the middle of Start, in pixels from the top left of the canvas (default: {START_CLICK[0]:g},{START_CLICK[1]:g})")
    parser.add_argument("--continue-click", type=engine.parse_point, default=CONTINUE_CLICK, metavar="X,Y",
                        help=f"the middle of Continue (default: {CONTINUE_CLICK[0]:g},{CONTINUE_CLICK[1]:g})")
    parser.add_argument("--tile-click", type=engine.parse_point, default=HAND_TILE_CLICK, metavar="X,Y",
                        help=f"the middle of the first tile of the hand (default: {HAND_TILE_CLICK[0]:g},{HAND_TILE_CLICK[1]:g})")
    parser.add_argument("--viewport", type=engine.parse_size, default=VIEWPORT, metavar="WxH",
                        help=f"size of the browser window; the coordinates are for {VIEWPORT[0]}x{VIEWPORT[1]} (default)")
    parser.add_argument("--timeout", type=float, default=engine.DEFAULT_TIMEOUT_SECONDS,
                        help=f"seconds to wait for the page and the first frame (default: {engine.DEFAULT_TIMEOUT_SECONDS:g})")
    parser.add_argument("--settle", type=float, default=engine.DEFAULT_SETTLE_SECONDS,
                        help=f"seconds to wait after the pointer move and after a click (default: {engine.DEFAULT_SETTLE_SECONDS:g})")
    parser.add_argument("--allow-console", action="append", default=[], metavar="REGEX",
                        help="an error-level console message matching this is not a failure (repeatable)")
    parser.add_argument("--expect-console", action="append", default=[], metavar="REGEX",
                        help="fail unless a console message matching this appears within --timeout (repeatable; scenario 1)")
    parser.add_argument("--chromium-arg", action="append", default=[], metavar="ARG",
                        help="an extra command-line flag for Chromium (repeatable)")
    parser.add_argument("--browser-channel", metavar="NAME", help="an installed browser instead of Playwright's Chromium: chrome, msedge")
    parser.add_argument("--executable-path", type=Path, metavar="FILE", help="a Chromium-based executable to launch")
    return parser


def format_point(point: tuple[float, float]) -> str:
    return f"{point[0]:g},{point[1]:g}"


def engine_arguments(args: argparse.Namespace) -> list[str]:
    """The command line of the engine's smoke for scenario 1: the same page and browser, the click on Start."""
    argv = ["--dist", str(args.dist), "--click", format_point(args.start_click), "--viewport", f"{args.viewport[0]}x{args.viewport[1]}",
            "--timeout", f"{args.timeout:g}", "--settle", f"{args.settle:g}"]
    for option, values in (("--allow-console", args.allow_console), ("--expect-console", args.expect_console),
                           ("--chromium-arg", args.chromium_arg)):
        for value in values:
            argv += [option, value]
    if args.browser_channel:
        argv += ["--browser-channel", args.browser_channel]
    if args.executable_path:
        argv += ["--executable-path", str(args.executable_path)]
    return argv


# ----- judging: no browser needed -----

def pictures_differ(first: bytes, second: bytes) -> bool:
    """Whether two screenshots (PNG bytes) show different pictures: the same bytes are the same picture without decoding."""
    if first == second:
        return False
    return engine.images_differ(engine.decode(first, "screenshot"), engine.decode(second, "screenshot"))


def wait_until_still(grab: Callable[[], bytes], differs: Callable[[bytes, bytes], bool], quiet: float, timeout: float,
                     interval: float = POLL_INTERVAL_SECONDS, now: Callable[[], float] = time.monotonic,
                     sleep: Callable[[float], None] = time.sleep) -> tuple[bytes, bool]:
    """Takes shots until none of them has differed from the reference for `quiet` seconds (the reference is the last shot that
    differed from the one before it). Returns the last shot and whether the table stood still before `timeout` seconds were gone."""
    deadline = now() + timeout
    reference = grab()
    since = now()
    while True:
        if now() - since >= quiet:
            return reference, True
        if now() >= deadline:
            return reference, False
        sleep(interval)
        current = grab()
        if differs(reference, current):
            reference = current
            since = now()


def reload_problem(before: bytes, after: bytes, differs: Callable[[bytes, bytes], bool] = pictures_differ) -> str | None:
    """Scenario 3's verdict on the shots of the Setup plate at the first load and after the reload: they must differ (Continue is
    on the plate after the reload, hidden at a first start). None when they do."""
    if differs(before, after):
        return None
    return ("(9) the Setup plate is the same after the reload: no Continue, so the match was not kept in the browser "
            "(the save did not reach IndexedDB, or the page did not read it; or PLATE_REGION is off: Setup moved?)")


@dataclass
class TableRun:
    """What scenario 2 and 3 saw, as plain data; table_problems() judges it."""

    console: list = field(default_factory=list)
    page_errors: list[str] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)  # found while running
    notes: list[str] = field(default_factory=list)  # the ok lines
    status: int | None = None


def table_problems(run: TableRun, allow_console: list[str]) -> list[str]:
    problems = list(run.problems)
    if run.status != 200:
        problems.append(f"(1) the page answered HTTP {run.status}, not 200")
    for error in run.page_errors:
        problems.append(f"(1) uncaught error in the page: {error}")
    for entry in run.console:
        if entry.kind == "error" and not engine.console_error_allowed(entry, allow_console):
            problems.append(f"(4) console error: {entry.text}" + (f" ({entry.url})" if entry.url else ""))
    return problems


# ----- the browser -----

def click_changes_picture(page, canvas, box, point: tuple[float, float], settle: float, window: float, sleep) -> bool:
    """Moves the pointer to `point` (canvas pixels), waits `settle`, shoots, clicks, and watches for `window` seconds for a picture
    that differs from the shot: True when one came. The pointer is there before the shot, so a hover effect is in it."""
    x, y = box["x"] + point[0], box["y"] + point[1]
    page.mouse.move(x, y)
    page.wait_for_timeout(settle * 1000)
    before = canvas.screenshot()
    engine.press(page, x, y, False)
    page.wait_for_timeout(settle * 1000)
    _, changed = engine.poll(canvas.screenshot, lambda png: pictures_differ(before, png), window, sleep=sleep)
    return changed


def play_table(page, canvas, box, run: TableRun, args: argparse.Namespace) -> None:
    sleep = lambda seconds: page.wait_for_timeout(seconds * 1000)  # noqa: E731
    if points_outside(box, run, ("start click", args.start_click), ("tile click", args.tile_click)):
        return

    if not click_changes_picture(page, canvas, box, args.start_click, args.settle, CLICK_WINDOW_SECONDS, sleep):
        run.problems.append("(6) the picture is the same after the click on Start: the seat choice did not close "
                            "(START_CLICK may be off: Setup moved?)")
        return
    run.notes.append("ok: (6) a click on Start changed the picture")

    started = time.monotonic()
    _, still = wait_until_still(canvas.screenshot, pictures_differ, STILL_SECONDS, STILL_TIMEOUT_SECONDS, sleep=sleep)
    if not still:
        run.problems.append(f"(7) the table did not stand still for {STILL_SECONDS:g} s within {STILL_TIMEOUT_SECONDS:g} s of Start")
        return
    run.notes.append(f"ok: (7) the table stood still {time.monotonic() - started:.1f} s after Start (the computers have played)")

    for round_number in range(1, TILE_ROUNDS + 1):
        if click_changes_picture(page, canvas, box, args.tile_click, args.settle, CLICK_WINDOW_SECONDS, sleep):
            how = "at once" if round_number == 1 else f"after a claim window was answered ({round_number - 1} round(s))"
            run.notes.append(f"ok: (8) a click on a hand tile changed the table {how}")
            return
        # A tile click that does nothing: the person may face a claim window. Press the places of its buttons until one answers.
        answered = next((point for point in CLAIM_ANSWER_CLICKS
                         if click_changes_picture(page, canvas, box, point, args.settle, ANSWER_WINDOW_SECONDS, sleep)), None)
        if answered is None:
            break  # no window to answer: the tile click is what does nothing
        run.notes.append(f"note: the tile click did nothing in round {round_number}; a button at {format_point(answered)} answered")
        _, still = wait_until_still(canvas.screenshot, pictures_differ, STILL_SECONDS, STILL_TIMEOUT_SECONDS, sleep=sleep)
        if not still:
            run.problems.append("(7) the table did not stand still after a claim window was answered")
            return
    run.problems.append(f"(8) the picture is the same after a click on the hand tile at {format_point(args.tile_click)}: "
                        "the click did not reach the game (HAND_TILE_CLICK may be off: the hand moved?)")


def wait_for_first_frame(page, canvas, args: argparse.Namespace, run: TableRun):
    """Waits for the canvas and a drawn frame, then `--settle` seconds. Returns the canvas box, or None with the problem noted."""
    try:
        canvas.wait_for(state="visible", timeout=args.timeout * 1000)
    except Exception as error:
        run.problems.append(f"(2) no visible {engine.CANVAS_SELECTOR} within {args.timeout:g} s: {str(error).splitlines()[0]}")
        return None
    sleep = lambda seconds: page.wait_for_timeout(seconds * 1000)  # noqa: E731
    _, drawn = engine.poll(canvas.screenshot, engine.png_is_drawn, args.timeout, sleep=sleep)
    if not drawn:
        run.problems.append(f"(2) nothing was drawn within {args.timeout:g} s")
        return None
    page.wait_for_timeout(args.settle * 1000)
    box = canvas.bounding_box()
    if box is None:
        run.problems.append("the canvas has no box to click in")
    return box


def points_outside(box, run: TableRun, *points: tuple[str, tuple[float, float]]) -> bool:
    """Notes a problem for each named point outside the canvas; True when there was one."""
    outside = False
    for name, point in points:
        if point[0] >= box["width"] or point[1] >= box["height"]:
            run.problems.append(f"the {name} point {format_point(point)} is outside the canvas ({box['width']:g}x{box['height']:g})")
            outside = True
    return outside


def play_reload(page, canvas, box, run: TableRun, args: argparse.Namespace) -> None:
    sleep = lambda seconds: page.wait_for_timeout(seconds * 1000)  # noqa: E731
    if points_outside(box, run, ("start click", args.start_click), ("continue click", args.continue_click)):
        return

    canvas_box = box

    def plate_shot() -> bytes:  # a clip in page coordinates: the Setup plate only, the table behind it is random
        x, y, width, height = PLATE_REGION
        return page.screenshot(clip={"x": canvas_box["x"] + x, "y": canvas_box["y"] + y, "width": width, "height": height})

    page.mouse.move(box["x"] + 2, box["y"] + 2)  # away from the plate: no hover in any shot
    before, still = wait_until_still(plate_shot, pictures_differ, STILL_SECONDS, STILL_TIMEOUT_SECONDS, sleep=sleep)
    if not still:
        run.problems.append("(9) the Setup plate did not stand still at the first load")
        return
    if not click_changes_picture(page, canvas, box, args.start_click, args.settle, CLICK_WINDOW_SECONDS, sleep):
        run.problems.append("(6) the picture is the same after the click on Start (START_CLICK may be off: Setup moved?)")
        return
    _, still = wait_until_still(canvas.screenshot, pictures_differ, STILL_SECONDS, STILL_TIMEOUT_SECONDS, sleep=sleep)
    if not still:
        run.problems.append(f"(7) the table did not stand still for {STILL_SECONDS:g} s within {STILL_TIMEOUT_SECONDS:g} s of Start")
        return

    page.reload(wait_until="load", timeout=args.timeout * 1000)
    canvas_box = wait_for_first_frame(page, canvas, args, run)
    if canvas_box is None:
        return
    page.mouse.move(canvas_box["x"] + 2, canvas_box["y"] + 2)
    after, still = wait_until_still(plate_shot, pictures_differ, STILL_SECONDS, STILL_TIMEOUT_SECONDS, sleep=sleep)
    problem = reload_problem(before, after) if still else "(9) the Setup plate did not stand still after the reload"
    if problem:
        run.problems.append(problem)
        return
    run.notes.append("ok: (9) after a reload the Setup plate differs from the first load (Continue is there)")

    if not click_changes_picture(page, canvas, canvas_box, args.continue_click, args.settle, CLICK_WINDOW_SECONDS, sleep):
        run.problems.append("(10) the picture is the same after the click on Continue (CONTINUE_CLICK may be off: Setup moved?)")
        return
    run.notes.append("ok: (10) a click on Continue changed the picture (the saved table is shown)")


def run_browser_scenario(playwright_api, url: str, args: argparse.Namespace, options, play) -> TableRun:
    """Loads the page in a new browser context (an empty IndexedDB) and lets `play(page, canvas, box, run, args)` act."""
    run = TableRun()
    with playwright_api.sync_playwright() as playwright:
        browser = engine.launch_browser(playwright, options)
        try:
            context = browser.new_context(viewport={"width": args.viewport[0], "height": args.viewport[1]})
            page = context.new_page()
            page.on("console", lambda message: run.console.append(
                engine.ConsoleEntry(message.type, message.text, (message.location or {}).get("url", ""))))
            page.on("pageerror", lambda error: run.page_errors.append(str(error)))
            page.on("crash", lambda *_: run.page_errors.append("the page crashed"))

            response = page.goto(url, wait_until="load", timeout=args.timeout * 1000)
            run.status = response.status if response else None
            canvas = page.locator(engine.CANVAS_SELECTOR).first
            box = wait_for_first_frame(page, canvas, args, run)
            if box is None:
                return run
            try:
                play(page, canvas, box, run, args)
            except engine.SmokeFailure as error:
                run.problems.append(str(error))
        finally:
            browser.close()
    return run


def run_scenario(args: argparse.Namespace, playwright_api, play, name: str) -> int:
    options = engine.parse_options(engine_arguments(args))
    with engine.served(args.dist) as url:
        try:
            run = run_browser_scenario(playwright_api, url, args, options, play)
        except engine.BrowserMissing as error:
            print(f"SKIP: the browser is not installed: {error}")
            return engine.EXIT_SKIPPED
        except Exception as error:  # a timeout of goto, a closed browser
            message = str(error).strip()
            print(f"FAIL: (1) the browser run failed: {message.splitlines()[0] if message else type(error).__name__}")
            return engine.EXIT_FAILED
    problems = table_problems(run, args.allow_console)
    for line in run.notes:
        print(line)
    if problems:
        for problem in problems:
            print(f"FAIL: {problem}")
        return engine.EXIT_FAILED
    errors = sum(1 for entry in run.console if entry.kind == "error")
    print(f"ok: (4) no console error outside the allow-list in the {name} run ({errors} allowed, {len(run.console)} messages)")
    print(f"PASS ({name})")
    return engine.EXIT_PASSED


# ----- --check: the helpers, no browser -----

def run_checks() -> int:
    def inside(point: tuple[float, float], size: tuple[int, int]) -> bool:
        return 0 <= point[0] < size[0] and 0 <= point[1] < size[1]

    # the constants are inside the view they are for (and the 640x480 shot of the engine's default would cut the tile off)
    for name, point in (("START_CLICK", START_CLICK), ("CONTINUE_CLICK", CONTINUE_CLICK), ("HAND_TILE_CLICK", HAND_TILE_CLICK),
                        *(("claim answer", p) for p in CLAIM_ANSWER_CLICKS)):
        assert inside(point, VIEWPORT), f"{name} {point} is outside {VIEWPORT}"
    assert not inside(HAND_TILE_CLICK, (640, 480))

    # the answers are the first two rows of three buttons of ActionRow (x 724, width 412), in a row each, none outside it
    assert all(724 <= x < 724 + 412 for x, _ in CLAIM_ANSWER_CLICKS)
    assert len(set(CLAIM_ANSWER_CLICKS)) == len(CLAIM_ANSWER_CLICKS)

    # the hand tile is on the first tile of the viewer's hand: 48 x 64 from (72, 648)
    assert 72 <= HAND_TILE_CLICK[0] < 72 + 48 and 648 <= HAND_TILE_CLICK[1] < 648 + 64

    # the plate is in the view, centred, and Start and Continue are in its last row (padding 16, 64 high) with Continue left of Start;
    # 860 and 459 stay in their buttons with or without the Online button (see the docstring)
    plate_x, plate_y, plate_w, plate_h = PLATE_REGION
    assert plate_x + plate_w <= VIEWPORT[0] and plate_y + plate_h <= VIEWPORT[1] and plate_x * 2 + plate_w == VIEWPORT[0]
    assert plate_y * 2 + plate_h == VIEWPORT[1]
    row_top, row_bottom = plate_y + plate_h - 16 - 64, plate_y + plate_h - 16
    assert START_CLICK[1] == CONTINUE_CLICK[1] == (row_top + row_bottom) / 2
    inner_left, inner_right = plate_x + 16, plate_x + plate_w - 16
    assert inner_left < CONTINUE_CLICK[0] < 336 + 246 and inner_left + 360 > CONTINUE_CLICK[0]
    assert 704 <= START_CLICK[0] < inner_right

    # parsing: the engine's point and size parsers, and the arguments handed to the engine's smoke
    parsed = build_parser().parse_args(["--dist", "dist", "--start-click", "10, 20", "--allow-console", "x", "--expect-console", "y",
                                        "--browser-channel", "chrome"])
    assert parsed.start_click == (10.0, 20.0) and parsed.tile_click == HAND_TILE_CLICK and parsed.viewport == VIEWPORT
    assert parsed.continue_click == CONTINUE_CLICK
    assert build_parser().parse_args(["--continue-click", "1,2"]).continue_click == (1.0, 2.0)
    argv = engine_arguments(parsed)
    assert argv[:6] == ["--dist", "dist", "--click", "10,20", "--viewport", "1280x720"], argv
    assert ["--allow-console", "x"] == argv[argv.index("--allow-console"):][:2] and "--browser-channel" in argv
    options = engine.parse_options(argv)  # what the engine will read from it
    assert options.click == (10.0, 20.0) and options.viewport == (1280, 720) and options.expect_console == ["y"]

    # wait_until_still on a fake clock: a changing table is not still, a table that stops is still after `quiet` seconds
    class Clock:
        def __init__(self) -> None:
            self.time = 0.0

        def now(self) -> float:
            return self.time

        def sleep(self, seconds: float) -> None:
            self.time += seconds

    def run_wait(shots: list[bytes], quiet: float, timeout: float) -> tuple[bytes, bool, float]:
        clock = Clock()
        sequence = iter(shots)
        last = [shots[0]]

        def grab() -> bytes:
            last[0] = next(sequence, last[0])
            return last[0]

        result, still = wait_until_still(grab, lambda a, b: a != b, quiet, timeout, 0.5, clock.now, clock.sleep)
        return result, still, clock.time

    result, still, elapsed = run_wait([b"a", b"b", b"c", b"c", b"c", b"c", b"c", b"c"], 2.0, 30.0)
    assert still and result == b"c" and 3.0 <= elapsed <= 4.0, (result, still, elapsed)
    _, still, elapsed = run_wait([bytes([n]) for n in range(200)], 2.0, 10.0)
    assert not still and 10.0 <= elapsed <= 10.5

    # the verdict of the reload scenario: a plate that changed after the reload passes, the same plate fails (the shots are compared
    # by `differs`, a plain comparison here: no PNGs are needed)
    assert reload_problem(b"first", b"second", lambda a, b: a != b) is None
    assert reload_problem(b"same", b"same", lambda a, b: a != b).startswith("(9)")
    assert reload_problem(b"same", b"same") is not None  # the real comparison: equal bytes are the same picture without decoding

    # the verdict of a run: errors outside the allow-list fail, the allowed ones do not
    run = TableRun(status=200, console=[engine.ConsoleEntry("error", "Blocking on the main thread is very dangerous"),
                                         engine.ConsoleEntry("error", "bad", "http://x/favicon.ico"),
                                         engine.ConsoleEntry("warning", "GPU stall")])
    assert len(table_problems(run, [])) == 1
    assert table_problems(run, ["Blocking on the main thread"]) == []
    assert table_problems(TableRun(status=404), [])
    print("ok: the browser-free checks of tools/web_smoke.py")
    return engine.EXIT_PASSED


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.check:
        return run_checks()
    if not args.dist:
        parser.error("name --dist (or --check)")
    if args.timeout <= 0 or args.settle < 0:
        parser.error("--timeout must be positive and --settle not negative")
    engine_argv = engine_arguments(args)
    engine.parse_options(engine_argv)  # a wrong command line ends here with exit code 2, before any browser starts

    print("== scenario 1: the engine's checks 1 to 5, the click on Start ==", flush=True)
    code = engine.main(engine_argv)
    if code != engine.EXIT_PASSED:
        return code  # failed, or skipped (77): there is no page to play
    playwright_api = engine.load_playwright()
    print("== scenario 2: Start, the table stands still, a click on a hand tile ==", flush=True)
    code = run_scenario(args, playwright_api, play_table, "table scenario")
    if code != engine.EXIT_PASSED:
        return code
    print("== scenario 3: Start, a reload, Continue is on the plate, a click on it ==", flush=True)
    return run_scenario(args, playwright_api, play_reload, "reload scenario")


if __name__ == "__main__":
    sys.exit(main())
