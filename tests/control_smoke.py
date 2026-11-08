#!/usr/bin/env python3
"""Smoke test of the control API of Mahjong: starts BagiMahjong with --control and plays over WebSocket.

    python3 tests/control_smoke.py --exe build/ClangmacOS/BagiMahjong

The game is started twice, once on the real clock and once with --manual-clock (game time moves only on `step`), headless (no window).
Both runs play the same scenario: the methods of the control API, one whole hand chosen at random among `list_actions`, and a match of
eight hands between four computers driven by `next_hand` alone. The helpers are the engine's (tools/control/tests/smoke.py).
Exit code: 0 passed, 1 failed, 77 skipped (the executable does not exist; ctest reads 77 as "skipped").
"""

import json
import os
import random
import sys
import time
from pathlib import Path
from typing import Any

ENGINE_DIR = Path(os.environ.get("BAGI_ENGINE_DIR") or Path(__file__).resolve().parents[1] / "engine")
sys.path.insert(0, str(ENGINE_DIR / "tools" / "control" / "tests"))

from smoke import (  # noqa: E402
    SmokeFailure,
    approx,
    check,
    check_state_push,
    expect_error,
    receive_response,
    run_smoke,
    take_screenshot,
)
from bagi_control_client import BagiControlClient  # noqa: E402

SEED = 1
SHORT_MATCH_HANDS = 8
SEAT_COUNT = 4
STARTING_SCORE = 2000
STEP_LIMIT_PER_HAND = 2000  # a hand has far fewer decisions of one person
HAND_LIMIT_PER_MATCH = 100  # 8 counted hands; a drawn hand is dealt again
IDLE_CHECK_SECONDS = 0.3  # how long the manual clock is watched not moving
DEFAULT_STEP_SECONDS = 1.0 / 60.0
STATE_KEYS = {"phase", "seat", "seatWind", "roundWind", "east", "hand", "turn", "seats", "discards", "wall", "handsPlayed", "handsTotal", "version"}
TURN_KEYS = {"phase", "current", "pendingTile", "yourTurn"}
SEAT_KEYS = {"kind", "wind", "score", "concealedCount", "melds", "concealedKongs", "bonus", "discardCount", "calling"}
RESULT_KEYS = {"kind", "winner", "winningTile", "source", "from", "loose", "lastLiveTile", "seats", "scores", "deltas"}
RESULT_SEAT_KEYS = {"concealed", "melds", "bonus", "breakdown"}
BREAKDOWN_KEYS = {"won", "lines", "points", "doubles", "raw", "score", "limitHand"}
# The playable tiles in the order of their ids: dots, bamboo, characters, winds, dragons
ALL_TILES = [f"{rank}{suit}" for suit in "psm" for rank in range(1, 10)] + ["Ew", "Sw", "Ww", "Nw", "Rd", "Wd", "Gd"]


def get_state(client: BagiControlClient) -> dict[str, Any]:
    return client.call("get_state")["state"]


def without_version(state: dict[str, Any]) -> dict[str, Any]:
    return {key: value for key, value in state.items() if key != "version"}


def check_keys(found: dict[str, Any], expected: set[str], what: str) -> None:
    check(expected <= set(found), f"{what} lacks the keys {sorted(expected - set(found))}: {sorted(found)}")


def check_state_shape(state: dict[str, Any]) -> None:
    """The state has the keys of the spec, four seats of their own keys, and a hand that is a list of tile names."""
    check_keys(state, STATE_KEYS, "the state")
    check(state["phase"] in ("playing", "hand_over", "match_over"), f"unknown phase {state['phase']}")
    check_keys(state["turn"], TURN_KEYS, "the turn")
    check(len(state["seats"]) == SEAT_COUNT, f"{len(state['seats'])} seats")
    for seat in state["seats"]:
        check_keys(seat, SEAT_KEYS, "a seat")
    check(all(tile in ALL_TILES for tile in state["hand"]), f"unknown tiles in the hand {state['hand']}")
    check(state["hand"] == sorted(state["hand"], key=tile_order), f"the hand is not sorted: {state['hand']}")
    if state["phase"] == "playing":
        check("result" not in state, "a result while the hand is played")
    else:
        result = state["result"]
        check_keys(result, RESULT_KEYS, "the result")
        check(len(result["seats"]) == SEAT_COUNT, "the result has not four hands")
        for seat in result["seats"]:
            check_keys(seat, RESULT_SEAT_KEYS, "a hand of the result")
            check_keys(seat["breakdown"], BREAKDOWN_KEYS, "a breakdown")


def tile_order(tile: str) -> int:
    """The position of a tile in the order of the tile ids (suits, winds, dragons), the order of `hand`."""
    return ALL_TILES.index(tile) if tile in ALL_TILES else len(ALL_TILES)


def play_one_hand(client: BagiControlClient, rng: random.Random) -> dict[str, Any]:
    """Chooses at random among `list_actions` for the person seat until the hand is over; returns the state then."""
    for _ in range(STEP_LIMIT_PER_HAND):
        state = get_state(client)
        if state["phase"] != "playing":
            return state
        actions = client.call("list_actions")["actions"]
        check(len(actions) > 0, f"the person has to act but has no action: {state['turn']}")
        check(state["turn"]["yourTurn"] is True, "actions are listed but it is not the turn of the seat")
        outcome = client.call("perform_action", rng.choice(actions))
        check(outcome["outcome"]["phase"] in ("playing", "hand_over", "match_over"), f"unknown phase in {outcome}")
    raise SmokeFailure(f"the hand did not end in {STEP_LIMIT_PER_HAND} actions")


def check_hand_over(client: BagiControlClient, state: dict[str, Any]) -> None:
    """A finished hand shows all four hands; `next_hand` is the only action and settles it."""
    check_state_shape(state)
    result = state["result"]
    check(state["phase"] == "hand_over", f"expected hand_over, got {state['phase']}")
    check(result["kind"] in ("win", "drawn"), f"unknown result {result['kind']}")
    check(sum(result["deltas"]) == 0, f"the payments do not add up to zero: {result['deltas']}")
    check(all(score % 10 == 0 for score in result["scores"]), f"a score is not a multiple of 10: {result['scores']}")
    check(all(len(seat["concealed"]) > 0 or seat["melds"] for seat in result["seats"]), "a hand of the result is empty")
    check(client.call("list_actions")["actions"] == [{"type": "next_hand"}], "next_hand is not the only action when the hand is over")
    expect_error("illegal_action", lambda: client.call("perform_action", {"type": "pass"}), "a pass when the hand is over")

    version = client.call("get_state")["version"]
    outcome = client.call("perform_action", {"type": "next_hand"})
    settlement = outcome["outcome"]["settlement"]
    check(settlement["deltas"] == result["deltas"] and settlement["scores"] == result["scores"], "next_hand settled another hand than the result shows")
    check(outcome["version"] > version, "the version did not grow after next_hand")
    expect_error("illegal_action", lambda: client.call("perform_action", {"type": "next_hand"}), "next_hand while a hand is played")


def play_mahjong(client: BagiControlClient) -> None:
    """The part of the scenario that does not depend on the clock: the methods, one whole hand, a match, and the garbage handling."""
    pong = client.call("ping")
    check(pong.get("pong") is True and isinstance(pong.get("protocol"), int), f"ping answered {pong}")

    games = client.call("list_games")
    check(games["active"] is not None, "no game is active")
    check(any(game["name"] == "Mahjong" and game["controllable"] for game in games["games"]), f"Mahjong is not a controllable game in {games}")

    # The same seed, seats and hands have to give the same table (the game may have been started with a computer East: reset first)
    options = {"seed": SEED, "seats": "PCCC", "hands": SHORT_MATCH_HANDS}
    client.call("reset", options)
    first = without_version(get_state(client))
    client.call("reset", options)
    snapshot = client.call("get_state")
    check(without_version(snapshot["state"]) == first, "the same seed gave two different tables")
    state = snapshot["state"]
    check_state_shape(state)
    check(state["phase"] == "playing", f"a fresh match is {state['phase']}")
    check(state["handsTotal"] == SHORT_MATCH_HANDS and state["handsPlayed"] == 0, f"unexpected match counts {state}")
    check(state["seat"] == 0 and [seat["kind"] for seat in state["seats"]] == ["person", "computer", "computer", "computer"], "the seats are not PCCC")
    check(all(seat["score"] == STARTING_SCORE for seat in state["seats"]), "the scores do not start at 2000")
    check(len(state["hand"]) in (13, 14), f"the person holds {len(state['hand'])} tiles")
    check([seat["concealedCount"] for seat in state["seats"]][0] == len(state["hand"]), "concealedCount disagrees with the hand")
    version = snapshot["version"]

    actions = client.call("list_actions")["actions"]
    check(len(actions) > 0, "the person has no legal action at the start")
    outcome = client.call("perform_action", actions[0])
    check(outcome["version"] > version, "the state version did not grow after an action")
    check(client.call("get_state")["version"] == outcome["version"], "get_state disagrees with perform_action about the version")

    # An action that is not legal changes nothing; a malformed one is another error
    state = get_state(client)
    version = client.call("get_state")["version"]
    check(state["phase"] == "playing", f"the match is {state['phase']} after one action")
    foreign = next(tile for tile in ALL_TILES if tile not in state["hand"])
    expect_error("illegal_action", lambda: client.call("perform_action", {"type": "discard", "tile": foreign}), "a discard of a tile the seat does not hold")
    expect_error("illegal_action", lambda: client.call("perform_action", {"type": "next_hand"}), "next_hand while a hand is played")
    for bad in ({}, {"type": "no_such_action"}, {"type": "discard"}, {"type": "discard", "tile": "zz"}, {"type": "discard", "tile": 5}):
        expect_error("invalid_params", lambda bad=bad: client.call("perform_action", bad), f"the malformed action {bad}")
    expect_error("invalid_params", lambda: client.call("reset", {"seats": "PCC"}), "a table of three seats")
    check(client.call("get_state")["version"] == version, "a refused action changed the state")

    expect_error("unknown_method", lambda: client.call("no_such_method"), "an unknown method")

    # What is not a request is answered, and the connection stays usable
    client.send_text("this is not json")
    reply = receive_response(client)
    check(reply.get("ok") is False and reply["error"]["code"] == "parse_error", f"garbage was answered with {reply}")
    check(reply.get("id") is None, "a request that cannot be parsed has no id to answer with")
    check(client.call("ping").get("pong") is True, "the connection broke after garbage")

    # One whole hand, chosen at random among the listed actions (a seeded generator: the same hand every run)
    client.call("reset", options)
    rng = random.Random(SEED)
    over = play_one_hand(client, rng)
    check_hand_over(client, over)

    # A whole match of computers: next_hand is the only call, the points stay 4 x 2000
    client.call("reset", {"seed": SEED, "seats": "CCCC", "hands": SHORT_MATCH_HANDS})
    for _ in range(HAND_LIMIT_PER_MATCH):
        state = get_state(client)
        check_state_shape(state)
        check(state["seat"] is None and state["hand"] == [], "a table without a person shows a hand")
        check(sum(seat["score"] for seat in state["seats"]) == SEAT_COUNT * STARTING_SCORE, "the points of the table do not add up")
        if state["phase"] == "match_over":
            break
        check(state["phase"] == "hand_over", f"a table of computers is {state['phase']} after a reset or a next_hand")
        client.call("perform_action", {"type": "next_hand"})
    else:
        raise SmokeFailure(f"the match did not end in {HAND_LIMIT_PER_MATCH} hands")
    check(state["handsPlayed"] == SHORT_MATCH_HANDS, f"{state['handsPlayed']} hands played at the end of the match")
    check(client.call("list_actions")["actions"] == [], "a finished match lists actions")
    expect_error("illegal_action", lambda: client.call("perform_action", {"type": "next_hand"}), "next_hand after the last hand")


def check_state_is_pushed(client: BagiControlClient) -> None:
    """The push that follows an action: the same version and the same state that get_state gives."""
    client.call("reset", {"seed": SEED + 1, "seats": "PCCC", "hands": SHORT_MATCH_HANDS})
    outcome = client.call("perform_action", client.call("list_actions")["actions"][0])
    check_state_push(client, outcome)


def check_screenshot(client: BagiControlClient) -> None:
    client.call("reset", {"seed": SEED, "seats": "PCCC", "hands": SHORT_MATCH_HANDS})
    image = take_screenshot(client)
    check(image.width > 0 and image.height > 0, "the screenshot is empty")


def run_real_clock_scenario(client: BagiControlClient, headless: bool) -> None:
    status = client.call("status")
    check(status["frame"] >= 1, f"status reports frame {status['frame']}")
    check(status["manualClock"] is False, "the manual clock is on without --manual-clock")

    play_mahjong(client)
    check_state_is_pushed(client)

    # On the real clock a step is a wait for frames that run anyway
    before = client.call("status")["frame"]
    after = client.call("step", {"frames": 3})["frame"]
    check(after >= before + 3, f"step 3 moved the frame from {before} to {after}")

    if headless:
        check_screenshot(client)


def run_manual_clock_scenario(client: BagiControlClient, headless: bool) -> None:
    status = client.call("status")
    check(status["manualClock"] is True, f"--manual-clock did not turn the manual clock on: {status}")
    check(status["frame"] == 0 and status["totalTime"] == 0, f"a frame ran before the first step: {status}")

    # Nothing moves on its own, and the requests are still answered
    time.sleep(IDLE_CHECK_SECONDS)
    check(client.call("status")["frame"] == 0, "the manual clock ticked without a step")

    play_mahjong(client)
    check(client.call("status")["frame"] == 0, "a frame ran without a step")

    stepped = client.call("step", {"frames": 3, "dt": 0.1})
    check(stepped["frame"] == 3, f"step 3 gave frame {stepped['frame']}")
    check(approx(stepped["totalTime"], 0.3), f"step 3 of 0.1 s gave the time {stepped['totalTime']}")
    stepped = client.call("step")
    check(approx(stepped["deltaTime"], DEFAULT_STEP_SECONDS), f"a step without dt lasted {stepped['deltaTime']} s")

    # A step holds back what follows it, so a pipelined status sees the frames
    client.send_text(json.dumps({"id": 900, "method": "step", "params": {"frames": 5}}))
    client.send_text(json.dumps({"id": 901, "method": "status"}))
    step_reply = receive_response(client)
    status_reply = receive_response(client)
    check(step_reply["id"] == 900 and step_reply["result"]["frame"] == 9, f"the pipelined step answered {step_reply}")
    check(status_reply["id"] == 901 and status_reply["result"]["frame"] == 9, f"the status behind the step answered {status_reply}")

    # The push of a state change comes at the end of the next frame, before the answer of the step that ran it
    client.call("reset", {"seed": SEED + 1, "seats": "PCCC", "hands": SHORT_MATCH_HANDS})
    outcome = client.call("perform_action", client.call("list_actions")["actions"][0])
    client.events.clear()
    client.call("step")
    check_state_push(client, outcome)
    check(not client.events or all(event["data"].get("version") != outcome["version"] for event in client.events), "the same change was pushed twice")

    if headless:
        check_screenshot(client)


SCENARIOS = [
    ("real clock", [], run_real_clock_scenario),
    ("manual clock", ["--manual-clock"], run_manual_clock_scenario),
]


def main() -> int:
    return run_smoke(__doc__, SCENARIOS)


if __name__ == "__main__":
    sys.exit(main())
