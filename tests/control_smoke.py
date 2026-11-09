#!/usr/bin/env python3
"""Smoke test of the control API of Mahjong: starts BagiMahjong with --control and plays over WebSocket.

    python3 tests/control_smoke.py --exe build/ClangmacOS/BagiMahjong

The game is started twice, once on the real clock and once with --manual-clock (game time moves only on `step`), headless (no window).
Both runs play the same scenario: the methods of the control API, one whole hand chosen at random among `list_actions`, and a match of
eight hands between four computers driven by `next_hand` alone; then the scene: the UI tree (the HUD, the seat choice closed by a reset,
the score table of a finished hand and the standings of a finished match, shown and hidden by the game), a screenshot of the table and,
on the manual clock, the screenshots of two seeds. The helpers are the engine's (tools/control/tests/smoke.py).
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
    find_ui_element,
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
TABLE_COLORS = 500  # a table of 150 tiles with antialiased marks and text shows thousands; flat felt, or felt and plain boxes, far fewer
SCENE_CANVASES = {"Hints", "HintsButton", "Actions", "ActionsCompact", "Hud", "HudMenu", "Handoff", "HandOver", "HandOverAction", "MatchOver", "Setup", "Rules", "RulesAction"}
COMING_AND_GOING_SCREENS = ("Setup", "Handoff", "HandOver", "HandOverAction", "MatchOver", "Rules", "RulesAction")
HAND_OVER_SCREENS = {"HandOver", "HandOverAction"}  # the score table and its action bar are shown together
RULES_SCREENS = {"Rules", "RulesAction"}  # the pages of the rules and their bar are shown together
SETTLE_FRAMES = 2  # frames after a change of the game before the canvases show it
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
    check(state["seats"][0]["concealedCount"] == len(state["hand"]), "concealedCount disagrees with the hand")
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


def screens_shown(client: BagiControlClient) -> tuple[set[str], dict[str, Any]]:
    """Which of the screens that come and go (the modal ones and the action bar of the score table) are shown, after a few frames in which the scene reads the game; and the whole UI tree."""
    client.call("step", {"frames": SETTLE_FRAMES})
    tree = client.call("ui_tree")
    check(tree["scene"] == "MahjongScene", f"the active scene is {tree['scene']}")
    canvases = {canvas["node"]: canvas for canvas in tree["canvases"]}
    check(SCENE_CANVASES <= set(canvases), f"the scene lacks the canvases {sorted(SCENE_CANVASES - set(canvases))}: {sorted(canvases)}")
    return {node for node in COMING_AND_GOING_SCREENS if canvases[node]["visible"]}, tree


def check_hud(tree: dict[str, Any], state: dict[str, Any]) -> None:
    """The HUD labels carry the round and the four scores of the state."""
    round_label = find_ui_element(tree, "RoundLabel")
    check(round_label is not None and round_label["text"].startswith(f"Round: {state['roundWind']}"), f"the round label is {round_label}")
    for seat in range(SEAT_COUNT):
        label = find_ui_element(tree, f"Score{seat}")
        check(label is not None and str(state["seats"][seat]["score"]) in label["text"] and f"Seat {seat + 1}" in label["text"], f"the score label of seat {seat} is {label}")


def check_screens_follow_the_game(client: BagiControlClient, rng: random.Random) -> None:
    """The seat choice is closed by a reset, the score table is shown exactly while a hand is over, the standings while the match is."""
    client.call("reset", {"seed": SEED, "seats": "PCCC", "hands": SHORT_MATCH_HANDS})
    shown, tree = screens_shown(client)
    check("Setup" not in shown, "the seat choice is still open after a reset")
    check(shown == set(), f"a table in play shows {sorted(shown)}")
    check_hud(tree, get_state(client))

    # A hand played by the person: the score table appears with the end of the hand and goes with next_hand
    over = play_one_hand(client, rng)
    check(over["phase"] == "hand_over", f"the hand ended in {over['phase']}")
    shown, tree = screens_shown(client)
    check(shown == HAND_OVER_SCREENS, f"a finished hand shows {sorted(shown)}")
    headline = find_ui_element(tree, "Headline")
    check(headline is not None and headline["text"] != "", "the score table has no headline")
    won = over["result"]["kind"] == "win"
    check(headline["text"].startswith("Seat") if won else headline["text"] == "Drawn hand", f"the headline '{headline['text']}' does not say '{over['result']['kind']}'")
    button = find_ui_element(tree, "NextHand")
    check(button is not None and button["visible"] and button["action"] == "MahjongNextHand", f"the table has no Next hand button: {button}")
    menu = next(canvas for canvas in tree["canvases"] if canvas["node"] == "HudMenu")
    check(not menu["visible"], "the New game button of the HUD is still shown over the score table")
    client.call("perform_action", {"type": "next_hand"})
    shown, _ = screens_shown(client)
    check(shown == set(), f"next_hand left {sorted(shown)} on the screen")

    # The same through the screen: four computers, the click on Next hand
    client.call("reset", {"seed": SEED, "seats": "CCCC", "hands": SHORT_MATCH_HANDS})
    shown, _ = screens_shown(client)
    check(shown == HAND_OVER_SCREENS, f"a hand of computers, over at once, shows {sorted(shown)}")
    over = get_state(client)
    played = over["handsPlayed"]
    drawn = over["result"]["kind"] == "drawn"  # a drawn hand is replayed and does not count: handsPlayed stays
    clicked = client.call("ui_click", {"name": "NextHand"})
    check(clicked["consumed"] and clicked["action"] == "MahjongNextHand", f"the click on Next hand was not taken: {clicked}")
    shown, _ = screens_shown(client)
    check(shown == set(), f"a click on Next hand left {sorted(shown)} on the screen")
    check(get_state(client)["handsPlayed"] >= played + (0 if drawn else 1), "the click on Next hand did not settle the hand")

    # The end of the match: the standings, then New game opens the seat choice, and Start begins another match
    client.call("reset", {"seed": SEED, "seats": "CCCC", "hands": SHORT_MATCH_HANDS})
    for _ in range(HAND_LIMIT_PER_MATCH):
        if get_state(client)["phase"] == "match_over":
            break
        client.call("perform_action", {"type": "next_hand"})
    shown, tree = screens_shown(client)
    check(shown == {"MatchOver"}, f"a finished match shows {sorted(shown)}")
    standing = find_ui_element(tree, "Standing0")
    check(standing is not None and standing["text"].startswith("1. Seat"), f"the first row of the standings is {standing}")
    client.call("ui_click", {"name": "NewGame", "canvas": "MatchOver"})
    shown, _ = screens_shown(client)
    check(shown == {"Setup", "MatchOver"}, f"New game on the standings shows {sorted(shown)}")  # the seat choice lies over the standings
    client.call("ui_click", {"name": "Start"})
    shown, _ = screens_shown(client)
    check(shown == set(), f"Start left {sorted(shown)} on the screen")
    check(get_state(client)["handsPlayed"] == 0, "Start did not begin a new match")


def element_text(tree: dict[str, Any], node: str, name: str) -> str:
    """The text of the element `name` of the canvas on the node `node` (the names Title and Pager are in other canvases too)."""
    canvas = next((canvas for canvas in tree["canvases"] if canvas["node"] == node), None)
    check(canvas is not None, f"the canvas {node} is not in the tree")
    found = find_ui_element({"canvases": [canvas]}, name)
    check(found is not None, f"{node} has no element {name}")
    return found["text"]


def check_rules_screen(client: BagiControlClient) -> None:
    """The Rules button of the HUD opens the pages, Next turns them, Close closes them; the match does not move meanwhile."""
    client.call("reset", {"seed": SEED, "seats": "PCCC", "hands": SHORT_MATCH_HANDS})
    shown, tree = screens_shown(client)
    check(shown == set(), f"a table in play shows {sorted(shown)}")
    version = get_state(client)["version"]
    button = find_ui_element(tree, "Rules")
    check(button is not None and button["action"] == "MahjongRules", f"the HUD has no Rules button: {button}")

    clicked = client.call("ui_click", {"name": "Rules", "canvas": "HudMenu"})
    check(clicked["consumed"] and clicked["action"] == "MahjongRules", f"the click on Rules was not taken: {clicked}")
    shown, tree = screens_shown(client)
    check(shown == RULES_SCREENS, f"the Rules button shows {sorted(shown)}")
    menu = next(canvas for canvas in tree["canvases"] if canvas["node"] == "HudMenu")
    check(not menu["visible"], "the buttons of the HUD are still shown over the pages of the rules")
    title = element_text(tree, "Rules", "Title")
    pager = element_text(tree, "Rules", "Pager")
    check(title != "" and pager != "", f"the first page has title '{title}' and pager '{pager}'")
    check(pager.startswith("Page 1 of"), f"the pager of the first page is '{pager}'")

    clicked = client.call("ui_click", {"name": "RulesNext"})
    check(clicked["consumed"] and clicked["action"] == "MahjongRulesNext", f"the click on Next was not taken: {clicked}")
    shown, tree = screens_shown(client)
    check(shown == RULES_SCREENS, f"Next left {sorted(shown)} on the screen")
    check(element_text(tree, "Rules", "Title") not in ("", title), "the title did not change with the next page")
    check(element_text(tree, "Rules", "Pager").startswith("Page 2 of"), "the pager did not move to the second page")
    check(get_state(client)["version"] == version, "the match moved while the pages of the rules were open")

    clicked = client.call("ui_click", {"name": "RulesClose"})
    check(clicked["consumed"] and clicked["action"] == "MahjongRulesClose", f"the click on Close was not taken: {clicked}")
    shown, tree = screens_shown(client)
    check(shown == set(), f"Close left {sorted(shown)} on the screen")
    menu = next(canvas for canvas in tree["canvases"] if canvas["node"] == "HudMenu")
    check(menu["visible"], "the buttons of the HUD are not back after Close")
    check(get_state(client)["version"] == version, "the match moved while the pages of the rules were open and closed")

    # An action of the control API is a controller that plays: it closes the pages
    client.call("ui_click", {"name": "Rules", "canvas": "HudMenu"})
    shown, _ = screens_shown(client)
    check(shown == RULES_SCREENS, f"the Rules button shows {sorted(shown)} the second time")
    client.call("perform_action", client.call("list_actions")["actions"][0])
    shown, _ = screens_shown(client)
    check(shown == set(), f"a control action left {sorted(shown)} on the screen")


def table_colors(client: BagiControlClient, seed: int) -> Any:
    client.call("reset", {"seed": seed, "seats": "PCCC", "hands": SHORT_MATCH_HANDS})
    client.call("step", {"frames": SETTLE_FRAMES})
    return take_screenshot(client)


def check_table_screenshot(client: BagiControlClient) -> None:
    """A screenshot of a fresh table has felt, tiles and text in it, not a flat colour."""
    image = table_colors(client, SEED)
    check(image.width > 0 and image.height > 0, "the screenshot is empty")
    check(len(image.colors()) >= TABLE_COLORS, f"the screenshot has {len(image.colors())} colours, it does not look like a table")


def check_screenshots_of_two_seeds(client: BagiControlClient) -> None:
    """The same seed looks the same twice and another seed looks different (the screenshot follows the game)."""
    first = table_colors(client, SEED)
    again = table_colors(client, SEED)
    check(again.rows == first.rows, "the same seed looked different in two screenshots")
    other = table_colors(client, SEED + 1)
    check(other.rows != first.rows, "another seed looked the same: the screenshot does not follow the game")


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

    check_screens_follow_the_game(client, random.Random(SEED + 2))
    check_rules_screen(client)
    if headless:
        check_table_screenshot(client)


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

    check_screens_follow_the_game(client, random.Random(SEED + 2))
    check_rules_screen(client)
    if headless:
        check_table_screenshot(client)
        check_screenshots_of_two_seeds(client)


SCENARIOS = [
    ("real clock", [], run_real_clock_scenario),
    ("manual clock", ["--manual-clock"], run_manual_clock_scenario),
]


def main() -> int:
    return run_smoke(__doc__, SCENARIOS)


if __name__ == "__main__":
    sys.exit(main())
