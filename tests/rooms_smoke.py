#!/usr/bin/env python3
"""Smoke test of the Mahjong room server: starts BagiMahjongServer and plays hands against it from this process, over real sockets.

    python3 tests/rooms_smoke.py --exe build/macos-debug/bin/BagiMahjongServer

Scenarios, on a server with --computer-delay-ms 0 --reconnect-grace-ms 300 (the room is `mahjong`, 8 hands, computers at level 1):
  * a host and a guest with two computers play a hand to its end (`hand_over`), send `next_hand` and see the next hand dealt; the
    room is made with `hintsAllowed` and every seat's view says so;
  * the guest drops mid-hand, a computer plays its seat once the grace has passed, the guest returns with its token and gets
    `joined`, `lobby`, `view`, and plays on to the end of a hand;
  * four people, nobody empty, play a hand to its end; the room is made without the option and every view says `hintsAllowed` false.
Every text every client receives goes through the judge below. The judge is the rule of tests/MahjongRoomsJudge.h, ported: change both
together (tests/CLAUDE.md). It has a self-check on hand-made texts, because a judge that cannot fail proves nothing.

The helpers (the server process, the player on a socket) are the engine's tools/rooms/smoke.py, loaded by path (BAGI_ENGINE_DIR, else
../engine): tools/control/tests/smoke.py has the same module name. Standard library only, Python 3.10+.
Exit code: 0 passed, 1 failed, 77 skipped (the executable does not exist; ctest reads 77 as "skipped").
"""

import argparse
import copy
import importlib.util
import json
import os
import sys
import time
from pathlib import Path
from typing import Any, Callable

EXIT_PASSED = 0
EXIT_FAILED = 1
EXIT_SKIPPED = 77

ENGINE_DIR = Path(os.environ.get("BAGI_ENGINE_DIR") or Path(__file__).resolve().parents[1] / "engine")
ENGINE_SMOKE = ENGINE_DIR / "tools" / "rooms" / "smoke.py"

GAME = "mahjong"
GAME_OPTIONS = {"hands": 8, "level": 1}
SEAT_COUNT = 4
GUEST_SEAT = 1
RECONNECT_GRACE_S = 0.3  # the grace the engine's RoomServer starts the server with (--reconnect-grace-ms 300)
GUEST_DROP_DISCARDS = 4  # the guest drops on its turn once this many tiles are discarded
HAND_LIMIT_SECONDS = 90.0  # a hand of four seats at Debug speed, polled every few milliseconds
STALE_ACTION = "stale_action"  # two people of one claim window both answer; the second one is late, which is the designed answer

# The names of the phases of a view (the control API's, MahjongStateJson)
PHASE_PLAYING = "playing"
PHASE_HAND_OVER = "hand_over"
PHASE_MATCH_OVER = "match_over"
RESULT_DRAWN = "drawn"


def load_engine_smoke() -> Any:
    spec = importlib.util.spec_from_file_location("bagi_engine_rooms_smoke", ENGINE_SMOKE)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


# ---------------------------------------------------------------------------------------------------------------------------------
# The judge (tests/MahjongRoomsJudge.h): the schema of what a client may receive. Tiles have no ids, so it checks the keys: only the
# known ones at the top, in the state, in every seat entry, meld and result; the one list of concealed tiles in a playing hand is
# `view.hand`, it is the seat's own and as long as the own `concealedCount` says; the other seats' melds are open; `result` (all four
# hands) only when the phase is not `playing`; the other messages carry no tile list.
# ---------------------------------------------------------------------------------------------------------------------------------

Offences = list[str]


def is_int(value: Any) -> bool:
    return isinstance(value, int) and not isinstance(value, bool)


def is_string(value: Any) -> bool:
    return isinstance(value, str)


def is_bool(value: Any) -> bool:
    return isinstance(value, bool)


def offend(offences: Offences, where: str, what: str) -> None:
    offences.append(f"{where}: {what}")


def check_keys(value: Any, where: str, required: set[str], optional: set[str], offences: Offences) -> bool:
    """The object has every key of `required` and no key beyond `required` and `optional`."""
    if not isinstance(value, dict):
        offend(offences, where, "is not an object")
        return False
    good = True
    for key in value:
        if key not in required and key not in optional:
            offend(offences, where, f"has the key '{key}' that the schema does not know")
            good = False
    for key in sorted(required):
        if key not in value:
            offend(offences, where, f"lacks the key '{key}'")
            good = False
    return good


def member(obj: Any, key: str) -> Any:
    return obj.get(key) if isinstance(obj, dict) else None


def is_list_of(value: Any, is_item: Callable[[Any], bool]) -> bool:
    return isinstance(value, list) and all(is_item(item) for item in value)


def check_list(obj: Any, key: str, is_item: Callable[[Any], bool], what: str, where: str, offences: Offences) -> None:
    items = member(obj, key)
    if items is not None and not is_list_of(items, is_item):
        offend(offences, f"{where}.{key}", f"is not a list of {what}")


def check_number(obj: Any, key: str, where: str, offences: Offences) -> None:
    number = member(obj, key)
    if number is not None and not is_int(number):
        offend(offences, f"{where}.{key}", "is not an integer")


def check_meld(meld: Any, where: str, must_be_open: bool, offences: Offences) -> None:
    if not check_keys(meld, where, {"kind", "tiles", "concealed", "from", "fromDiscard", "claimed"}, set(), offences):
        return
    check_list(meld, "tiles", is_string, "tiles", where, offences)
    concealed = member(meld, "concealed")
    if not is_bool(concealed):
        offend(offences, f"{where}.concealed", "is not a boolean")
    elif must_be_open and concealed:
        offend(offences, where, "is a concealed meld of another seat")


def check_melds(owner: Any, where: str, must_be_open: bool, offences: Offences) -> None:
    melds = member(owner, "melds")
    if not isinstance(melds, list):
        offend(offences, f"{where}.melds", "is not a list")
        return
    for index, meld in enumerate(melds):
        check_meld(meld, f"{where}.melds[{index}]", must_be_open, offences)


def check_seat_entry(entry: Any, index: int, viewer: int, offences: Offences) -> None:
    where = f"view.seats[{index}]"
    if not check_keys(entry, where, {"wind", "score", "concealedCount", "melds", "concealedKongs", "bonus", "discardCount", "calling"}, set(), offences):
        return
    check_melds(entry, where, index != viewer, offences)
    check_list(entry, "bonus", is_string, "tiles", where, offences)
    for key in ("score", "concealedCount", "concealedKongs", "discardCount"):
        check_number(entry, key, where, offences)


def check_result(result: Any, offences: Offences) -> None:
    where = "view.result"
    if not check_keys(result, where, {"kind", "winner", "winningTile", "source", "from", "loose", "lastLiveTile", "seats", "scores", "deltas"}, set(), offences):
        return
    seats = result["seats"]
    if not isinstance(seats, list) or len(seats) != SEAT_COUNT:
        offend(offences, f"{where}.seats", "is not a list of four")
        return
    for index, entry in enumerate(seats):
        at = f"{where}.seats[{index}]"
        if not check_keys(entry, at, {"concealed", "melds", "bonus", "breakdown"}, set(), offences):
            continue
        check_list(entry, "concealed", is_string, "tiles", at, offences)
        check_list(entry, "bonus", is_string, "tiles", at, offences)
        check_melds(entry, at, False, offences)
        check_keys(entry["breakdown"], f"{at}.breakdown", {"won", "lines", "points", "doubles", "raw", "score", "limitHand"}, set(), offences)


def check_state(state: Any, seat: int, offences: Offences) -> None:
    where = "view"
    required = {"phase", "seat", "seatWind", "roundWind", "east", "hand", "turn", "seats", "discards", "wall", "handsPlayed", "handsTotal", "version", "ready", "format", "hintsAllowed"}
    if not check_keys(state, where, required, {"result"}, offences):
        return
    phase = state["phase"]
    playing = phase == PHASE_PLAYING
    if not (playing or phase in (PHASE_HAND_OVER, PHASE_MATCH_OVER)):
        offend(offences, "view.phase", "is not playing, hand_over or match_over")
    if playing and "result" in state:
        offend(offences, "view.result", "is sent while the hand is played (it shows all four hands)")
    if not is_int(state["seat"]) or state["seat"] != seat:
        offend(offences, "view.seat", "is not the seat of the client")
    for key in ("wall", "east", "handsPlayed", "handsTotal", "version", "format"):
        check_number(state, key, where, offences)
    check_list(state, "hand", is_string, "tiles", where, offences)
    check_keys(state["turn"], "view.turn", {"phase", "current", "pendingTile", "yourTurn"}, set(), offences)
    ready = state["ready"]
    if not is_list_of(ready, is_bool) or len(ready) != SEAT_COUNT:
        offend(offences, "view.ready", "is not four booleans")
    if not is_bool(state["hintsAllowed"]):
        offend(offences, "view.hintsAllowed", "is not a boolean")
    discards = state["discards"]
    if not isinstance(discards, list):
        offend(offences, "view.discards", "is not a list")
    else:
        for discard in discards:
            check_keys(discard, "view.discards[]", {"tile", "seat"}, set(), offences)

    seats = state["seats"]
    if not isinstance(seats, list) or len(seats) != SEAT_COUNT:
        offend(offences, "view.seats", "is not a list of four")
        return
    for index in range(SEAT_COUNT):
        check_seat_entry(seats[index], index, seat, offences)
    # the one concealed list of the playing hand is the seat's own, and it is as long as the seat's own count says
    own = member(seats[seat], "concealedCount") if 0 <= seat < SEAT_COUNT else None
    hand = state["hand"]
    if not is_int(own) or not isinstance(hand, list) or len(hand) != own:
        offend(offences, "view.hand", "does not have the size that the own concealedCount says")
    if "result" in state:
        check_result(state["result"], offences)


def check_view(root: dict[str, Any], seat: int, offences: Offences) -> None:
    if not check_keys(root, "message", {"v", "type", "room", "seat", "version", "finished", "view", "actions"}, set(), offences):
        return
    if not is_int(root["seat"]) or root["seat"] != seat:
        offend(offences, "message.seat", "is not the seat of the client")
    actions = root["actions"]
    if not isinstance(actions, list):
        offend(offences, "message.actions", "is not a list")
    else:
        for action in actions:
            check_keys(action, "message.actions[]", {"type"}, {"tile", "declareCalling", "claim", "first"}, offences)
    view = root["view"]
    if isinstance(view, dict) and not view:
        return  # the game has not started
    check_state(view, seat, offences)


def check_lobby(root: dict[str, Any], offences: Offences) -> None:
    if not check_keys(root, "lobby", {"v", "type", "room", "game", "phase", "host", "seats"}, {"options"}, offences):
        return
    # The public options of the room: only the keys the game names public, each a boolean or an integer
    if "options" in root and check_keys(root["options"], "lobby.options", set(), {"hands", "level", "hintsAllowed"}, offences):
        for key, value in root["options"].items():
            if not isinstance(value, (bool, int)):
                offend(offences, f"lobby.options.{key}", "is not a boolean or an integer")
    if not isinstance(root["seats"], list):
        offend(offences, "lobby.seats", "is not a list")
        return
    for seat in root["seats"]:
        check_keys(seat, "lobby.seats[]", {"seat", "kind"}, {"name", "connected"}, offences)


def judge_room_message(text: str, seat: int) -> Offences:
    """What is wrong with one text that a client received; empty when nothing. `seat` is the seat the client holds (-1 before it has one)."""
    offences: Offences = []
    try:
        root = json.loads(text)
    except ValueError:
        return ["message: is not a JSON object"]
    if not isinstance(root, dict):
        return ["message: is not a JSON object"]
    name = root.get("type")
    if not isinstance(name, str):
        return ["message: has no type"]
    if name == "view":
        check_view(root, seat, offences)
    elif name == "lobby":
        check_lobby(root, offences)
    elif name == "joined":
        check_keys(root, "joined", {"v", "type", "room", "player", "token", "seat", "host"}, set(), offences)
    elif name == "error":
        check_keys(root, "error", {"v", "type", "code", "message"}, {"ref"}, offences)
    elif name == "left":
        check_keys(root, "left", {"v", "type", "reason"}, set(), offences)
    elif name == "pong":
        check_keys(root, "pong", {"v", "type"}, set(), offences)
    else:
        offences.append(f"message: has the type '{name}' that the protocol does not send")
    return offences


# ---------------------------------------------------------------------------------------------------------------------------------
# The self-check of the judge: hand-made texts, honest and not
# ---------------------------------------------------------------------------------------------------------------------------------

def sample_breakdown() -> dict[str, Any]:
    return {"won": False, "lines": [], "points": 20, "doubles": 0, "raw": 20, "score": 20, "limitHand": None}


def sample_result() -> dict[str, Any]:
    seats = [{"concealed": ["1p", "2p"], "melds": [], "bonus": [], "breakdown": sample_breakdown()} for _ in range(SEAT_COUNT)]
    return {"kind": RESULT_DRAWN, "winner": None, "winningTile": None, "source": None, "from": None, "loose": False, "lastLiveTile": False,
            "seats": seats, "scores": [2000] * SEAT_COUNT, "deltas": [0] * SEAT_COUNT}


def sample_view(seat: int, phase: str = PHASE_PLAYING) -> dict[str, Any]:
    winds = ["East", "South", "West", "North"]
    hand = ["1p", "1p", "2p", "3p", "4p"]
    seats = [{"wind": winds[index], "score": 2000, "concealedCount": len(hand) if index == seat else 13, "melds": [], "concealedKongs": 0, "bonus": [],
              "discardCount": 0, "calling": False} for index in range(SEAT_COUNT)]
    state: dict[str, Any] = {
        "phase": phase, "seat": seat, "seatWind": winds[seat], "roundWind": "East", "east": 0, "hand": hand,
        "turn": {"phase": "discard", "current": 0, "pendingTile": None, "yourTurn": seat == 0}, "seats": seats, "discards": [{"tile": "9s", "seat": 3}],
        "wall": 67, "handsPlayed": 0, "handsTotal": 8, "version": 1, "ready": [False] * SEAT_COUNT, "format": 1, "hintsAllowed": False,
    }
    if phase != PHASE_PLAYING:
        state["result"] = sample_result()
    return {"v": 1, "type": "view", "room": "ABCDE", "seat": seat, "version": 1, "finished": False, "view": state,
            "actions": [{"type": "discard", "tile": "1p", "declareCalling": False}]}


def judged(message: Any, seat: int) -> Offences:
    return judge_room_message(message if isinstance(message, str) else json.dumps(message), seat)


def says(offences: Offences, fragment: str) -> bool:
    return any(fragment in offence for offence in offences)


def mutated(message: dict[str, Any], change: Callable[[dict[str, Any]], None]) -> dict[str, Any]:
    copied = copy.deepcopy(message)
    change(copied)
    return copied


def self_check_judge(smoke: Any) -> None:
    check = smoke.check
    honest = sample_view(0)
    check(not judged(honest, 0), f"the judge refuses an honest view: {judged(honest, 0)}")
    over = sample_view(0, PHASE_HAND_OVER)
    check(not judged(over, 0), f"the judge refuses an honest view of a finished hand: {judged(over, 0)}")
    check(not judged(mutated(honest, lambda m: m.update(view={})), 0), "the judge refuses the view before the start")

    def cannot_miss(what: str, message: Any, seat: int, fragment: str) -> None:
        found = judged(message, seat)
        check(says(found, fragment), f"the judge missed {what}: {found}")

    cannot_miss("a `hands` key", mutated(honest, lambda m: m["view"].update(hands=[])), 0, "'hands'")
    cannot_miss("another seat's concealed list", mutated(honest, lambda m: m["view"]["seats"][1].update(concealed=["1p", "2p"])), 0, "'concealed'")
    cannot_miss("a wall of tiles", mutated(honest, lambda m: m["view"].update(wall=["1p"])), 0, "view.wall")
    cannot_miss("a hand at the top of the message", mutated(honest, lambda m: m.update(hand=["1p"])), 0, "'hand'")
    cannot_miss("a hand shorter than its count", mutated(honest, lambda m: m["view"]["hand"].pop(0)), 0, "view.hand")
    cannot_miss("a concealed meld of another seat", mutated(honest, lambda m: m["view"]["seats"][2].update(
        melds=[{"kind": "kong", "tiles": ["1p"] * 4, "concealed": True, "from": None, "fromDiscard": False, "claimed": None}])), 0, "concealed meld of another seat")
    cannot_miss("a view without the format number", mutated(honest, lambda m: m["view"].pop("format")), 0, "'format'")
    cannot_miss("a format number that is a string", mutated(honest, lambda m: m["view"].update(format="1")), 0, "view.format")
    cannot_miss("a view without the hints setting", mutated(honest, lambda m: m["view"].pop("hintsAllowed")), 0, "'hintsAllowed'")
    cannot_miss("a hints setting that is a string", mutated(honest, lambda m: m["view"].update(hintsAllowed="true")), 0, "view.hintsAllowed")
    cannot_miss("a view of another seat", honest, 1, "seat of the client")
    cannot_miss("a result while the hand is played", mutated(honest, lambda m: m["view"].update(result=sample_result())), 0, "view.result")
    cannot_miss("a result that is not four hands", mutated(over, lambda m: m["view"]["result"].update(seats=[])), 0, "view.result.seats")
    cannot_miss("an extra key in the result", mutated(over, lambda m: m["view"]["result"].update(extra=1)), 0, "'extra'")
    cannot_miss("a hand list that is not tiles in the result", mutated(over, lambda m: m["view"]["result"]["seats"][1].update(concealed=5)), 0, "concealed")
    cannot_miss("a tile list in a joined", '{"v":1,"type":"joined","room":"A","player":1,"token":"x","seat":0,"host":true,"hand":["1p"]}', 0, "'hand'")
    cannot_miss("an option the game does not make public", '{"v":1,"type":"lobby","room":"A","game":"mahjong","phase":"lobby","host":0,"seats":[],"options":{"seed":1}}', 0, "'seed'")
    cannot_miss("a public option that is text", '{"v":1,"type":"lobby","room":"A","game":"mahjong","phase":"lobby","host":0,"seats":[],"options":{"hands":"8"}}', 0, "lobby.options.hands")
    cannot_miss("a tile list in a lobby", '{"v":1,"type":"lobby","room":"A","game":"mahjong","phase":"playing","host":0,"seats":[],"hands":[]}', 0, "'hands'")
    cannot_miss("a tile list in an error", '{"v":1,"type":"error","code":"x","message":"y","hand":["1p"]}', 0, "'hand'")
    cannot_miss("a tile list in a pong", '{"v":1,"type":"pong","hand":["1p"]}', 0, "'hand'")
    cannot_miss("a text that is not JSON", "not json", 0, "not a JSON object")
    cannot_miss("a type the protocol does not send", '{"v":1,"type":"cheat"}', 0, "'cheat'")
    check(not judged('{"v":1,"type":"pong"}', 0), "the judge refuses an honest pong")


# ---------------------------------------------------------------------------------------------------------------------------------
# The scenarios
# ---------------------------------------------------------------------------------------------------------------------------------

def make_player_class(smoke: Any) -> type:
    class MahjongHuman(smoke.Human):
        """A player on its own socket: judges every text it receives, plays the first listed action, and remembers the end of a hand."""

        def __init__(self, port: int):
            super().__init__(port)
            self.texts = 0
            self.offences: Offences = []
            self.saw_hand_over = False
            self.result_kind = ""

        def _take(self, text: str) -> None:
            # Judged with the seat from `joined`, before the view can say another one
            self.texts += 1
            self.offences += judge_room_message(text, self.seat)
            super()._take(text)
            message = self.messages[-1]
            state = message.get("view") if message.get("type") == "view" else None
            if state and state.get("phase") == PHASE_HAND_OVER:
                self.saw_hand_over = True
                self.result_kind = state["result"]["kind"]

        def state(self) -> dict[str, Any]:
            return self.view["view"] if self.view else {}

        def phase(self) -> str:
            return self.state().get("phase", "")

    return MahjongHuman


def open_table(smoke: Any, player: type, port: int, people: int, options: dict[str, Any] = GAME_OPTIONS) -> list[Any]:
    """The host creates the room (with @p options) and `people - 1` guests join by its code; the host has not started it."""
    host = player(port)
    host.send({"type": "create_room", "game": GAME, "name": "Host", "options": options})
    host.wait_for("joined")
    host.wait_for("lobby")
    table = [host]
    for index in range(1, people):
        guest = player(port)
        guest.send({"type": "join_room", "code": host.room, "name": f"Guest{index}"})
        guest.wait_for("joined")
        guest.wait_for("lobby")
        table.append(guest)
    return table


def play(smoke: Any, humans: list[Any], done: Callable[[], bool], what: str) -> None:
    def step() -> None:
        for human in humans:
            human.poll()
            human.act_if_its_turn()

    smoke.run_until(done, step, what, HAND_LIMIT_SECONDS)


def check_clean(smoke: Any, humans: list[Any]) -> None:
    """No error but a late answer in a claim window, and not one offence of the judge on any text of any client."""
    for human in humans:
        errors = [message for message in human.messages if message.get("type") == "error" and message.get("code") != STALE_ACTION]
        if errors:
            raise smoke.SmokeFailure(f"the server answered an error: {errors[0]}")
        smoke.check(human.texts > 0, f"seat {human.seat} received nothing to judge")
        smoke.check(not human.offences, f"seat {human.seat}: the judge found {human.offences[:5]}")


def check_hints_setting(smoke: Any, humans: list[Any], allowed: bool) -> None:
    """Every seat's last view carries the host's choice, whatever the seat."""
    for human in humans:
        smoke.check(human.state().get("hintsAllowed") is allowed, f"seat {human.seat}: hintsAllowed is {human.state().get('hintsAllowed')}, the host chose {allowed}")


def close_all(humans: list[Any]) -> None:
    for human in humans:
        human.close()


def scenario_hand_and_next(smoke: Any, player: type, port: int) -> None:
    humans = open_table(smoke, player, port, 2, {**GAME_OPTIONS, "hintsAllowed": True})
    humans[0].send({"type": "start"})
    play(smoke, humans, lambda: all(human.saw_hand_over for human in humans), "the hand did not end")
    result_kinds = {human.result_kind for human in humans}
    smoke.check(len(result_kinds) == 1, f"the two people read different results: {result_kinds}")
    # A drawn hand is replayed and does not count; any other counts
    counted = 0 if result_kinds.pop() == RESULT_DRAWN else 1

    def dealt(human: Any) -> bool:
        state = human.state()
        return state.get("phase") == PHASE_PLAYING and state.get("handsPlayed") == counted and human.view["version"] > 0 and human.saw_hand_over

    play(smoke, humans, lambda: all(dealt(human) for human in humans), "the next hand was not dealt after the next_hand of both people")
    for human in humans:
        smoke.check(human.state()["ready"] == [False] * SEAT_COUNT, f"seat {human.seat}: the ready flags were not cleared by the deal")
        smoke.check("result" not in human.state(), f"seat {human.seat}: the new hand still shows a result")
    check_hints_setting(smoke, humans, True)
    check_clean(smoke, humans)
    close_all(humans)


def scenario_reconnect(smoke: Any, player: type, port: int) -> None:
    host, guest = open_table(smoke, player, port, 2)
    smoke.check(guest.seat == GUEST_SEAT, f"the guest sits at seat {guest.seat}")
    host.send({"type": "start"})

    def guest_may_drop() -> bool:
        return len(guest.state().get("discards", [])) >= GUEST_DROP_DISCARDS and guest.has_actions()

    def step() -> None:
        host.poll()
        host.act_if_its_turn()
        guest.poll()
        if not guest_may_drop():
            guest.act_if_its_turn()

    smoke.run_until(guest_may_drop, step, "the guest never got a turn after the first discards", HAND_LIMIT_SECONDS)

    # The guest has actions and drops without using them: nothing can move on before a computer takes the seat
    dropped_discards = len(host.state().get("discards", []))
    guest.close()
    dropped_at = len(host.messages)
    smoke.run_until(lambda: any(smoke.shows_away_human(message) for message in host.messages[dropped_at:]), host.poll,
                    "the host was never told that the guest is away", HAND_LIMIT_SECONDS)
    time.sleep(RECONNECT_GRACE_S)
    smoke.run_until(lambda: len(host.state().get("discards", [])) > dropped_discards or host.saw_hand_over,
                    lambda: (host.poll(), host.act_if_its_turn()), "no computer took the seat after the grace", HAND_LIMIT_SECONDS)

    back = player(port)
    back.send({"type": "join_room", "code": guest.room, "token": guest.token})
    back.wait_for("view")
    kinds = [message["type"] for message in back.messages[:3]]
    smoke.check(kinds == ["joined", "lobby", "view"], f"the returning human got {[m['type'] for m in back.messages]}")
    smoke.check(back.seat == GUEST_SEAT, f"the human came back to seat {back.seat}")
    play(smoke, [host, back], lambda: host.saw_hand_over and back.saw_hand_over, "the returned human did not play on to the end of the hand")
    check_clean(smoke, [host, guest, back])
    close_all([host, guest, back])


def scenario_four_people(smoke: Any, player: type, port: int) -> None:
    humans = open_table(smoke, player, port, SEAT_COUNT)
    humans[0].send({"type": "start"})
    play(smoke, humans, lambda: all(human.saw_hand_over for human in humans), "the hand of four people did not end")
    check_hints_setting(smoke, humans, False)
    for human in humans:
        result = human.state()["result"] if human.phase() == PHASE_HAND_OVER else None
        if result is not None:
            smoke.check(len(result["seats"]) == SEAT_COUNT, f"seat {human.seat}: the end of the hand shows {len(result['seats'])} hands")
    check_clean(smoke, humans)
    close_all(humans)


def main() -> int:
    parser = argparse.ArgumentParser(description="Smoke test of BagiMahjongServer")
    parser.add_argument("--exe", required=True, help="the built BagiMahjongServer")
    args = parser.parse_args()
    exe = Path(args.exe).resolve()
    if not exe.exists():
        print(f"SKIPPED: {exe} does not exist (build the target BagiMahjongServer first)")
        return EXIT_SKIPPED
    if not ENGINE_SMOKE.exists():
        print(f"FAILED: {ENGINE_SMOKE} does not exist (BAGI_ENGINE_DIR={ENGINE_DIR})")
        return EXIT_FAILED

    smoke = load_engine_smoke()
    player = make_player_class(smoke)
    scenarios = [("hand and next hand", scenario_hand_and_next), ("reconnect", scenario_reconnect), ("four people", scenario_four_people)]
    try:
        self_check_judge(smoke)
        print("PASSED: the judge catches what it is meant to")
        for name, scenario in scenarios:
            started = time.monotonic()
            with smoke.RoomServer(exe, []) as server:
                scenario(smoke, player, server.port)
            print(f"PASSED: {name} ({time.monotonic() - started:.1f} s)")
    except (smoke.SmokeFailure, smoke.ConnectionClosed, smoke.HandshakeError, TimeoutError) as failure:
        print(f"FAILED: {failure}")
        return EXIT_FAILED
    return EXIT_PASSED


if __name__ == "__main__":
    sys.exit(main())
