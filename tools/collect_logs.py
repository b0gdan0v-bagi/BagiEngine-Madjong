#!/usr/bin/env python3
"""Collects match logs of testers into one folder: only the logs that replay, each once, with an index.

    python3 tools/collect_logs.py --from some/folder [--from other/folder ...] [--to logs/collected] [--verifier build/macos-release/bin/BagiMahjongArena] [--recheck]
    python3 tools/collect_logs.py --store https://the-service --user NAME [--idle-hours 24] [--to logs/collected]
    python3 tools/collect_logs.py --store URL --user NAME --delete-id ID
    python3 tools/collect_logs.py --forget-nickname NAME [--store URL --user NAME]

For every `--from` folder it runs `<verifier> --logs FOLDER --check` (the output of `BagiMahjongArena`: one line per `*.jsonl` file,
`ok<TAB>name` or `bad<TAB>name<TAB>reason`; any program that prints that format will do), hashes every file (the SHA-256 of its bytes is its
identity), copies the new `ok` ones to `<to>/<first 16 hex digits of the hash>.jsonl` (never over an existing file; the same bytes from two
places are one file) and keeps `<to>/index.json`: per hash the file name (a dropped log has none), the first source it came from, the status
`kept` or `dropped` and, for a dropped one, the reason. The index is written to a temp file and renamed, after all copies.

A log that does not replay is dropped and counted, never silent: the run ends with a summary on stdout (kept, new, already known, dropped
now, dropped before) and the logs dropped in this run by kind of reason, with their names and reasons. A dropped log is not copied; if a
later run finds the same bytes replaying (the rules were fixed) it is kept then. `--recheck` verifies the kept files again, too, and moves
the ones that no longer replay to `dropped` (a change of the rules makes old logs stop replaying; the summary says how many and why). A
log moved to `dropped` is deleted from `<to>`: its bytes are still in the folder it came from, and the index remembers the first one.

`<to>` must lie outside git, or inside a work tree only where git ignores it (`logs/collected` is: `logs/` is in `.gitignore`): the tool
refuses any other folder, as it stores nothing but the logs and the index.

The store source (`--store URL`, the web service of the game): `GET /matches/` lists the matches and their piece counts; every match not
yet downloaded (or grown since) is fetched piece by piece, put together in order into `logs/downloaded/<id>.jsonl` (the raw bytes, kept
whatever the verifier says; outside git like `--to`) and the folder then goes through the flow above as one more `--from`. A match that
may still be played is kept as `<id>.part` meanwhile, which the flow does not read: a growing match must not be kept once per state; it
becomes `<id>.jsonl` when it ends, is given up or has been idle for `--idle-hours`. The credentials
never come from the command line: the environment variable `MATCH_COLLECT_TOKEN` sends `Authorization: Bearer <token>` (the sign-in
service); else `--user` with `MATCH_STORE_PASSWORD` sends HTTP Basic; with neither nothing is sent (a service without a password).
**A match is deleted on the server only after its file is on disk and read back equal to what was fetched** (`DELETE /matches/<id>`); a log
that does not replay stays in `logs/downloaded/` and is `dropped` in the index with the reason, and the server copy goes all the same.
The server keeps a match 90 days, then only the local copy is left. A match that may still be played (no `end` event and the last piece
not empty) is deleted only when its piece count did not change for `--idle-hours` (default 24; the clocks are in
`logs/downloaded/state.json`); an ended or given-up match is deleted at once. A "tester" member of a list entry (the sign-in service
names the account) is kept with the log in the index and in `state.json`; its absence is not an error. A failed request (network, 401,
5xx) ends the run with exit 1 after the files already written are in place and deletes nothing that was not saved.
`--delete-id ID` deletes one match on the server. `--forget-nickname NAME` removes from `logs/downloaded/`, `logs/collected/` and the
index every log whose `start` carries that nickname (and the server copy when `--store` is given), and says how many.

Exit code: 0 done (also when logs were dropped: read the summary), 1 the verifier is missing or fails, or a request failed, 2 a wrong
command line. Standard library only.
"""

import argparse
import base64
import hashlib
import json
import os
import platform
import re
import subprocess
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TO = ROOT / "logs" / "collected"
INDEX_NAME = "index.json"
INDEX_VERSION = 1
HASH_NAME_DIGITS = 16
LOG_SUFFIX = ".jsonl"
OPEN_SUFFIX = ".part"  # a match that may still be played: not a log yet, the flow does not see it
STATUS_KEPT = "kept"
STATUS_DROPPED = "dropped"
CHECK_OK = "ok"
CHECK_BAD = "bad"
EXIT_DONE = 0
EXIT_FAILED = 1
EXIT_USAGE = 2
DEFAULT_DOWNLOADED = ROOT / "logs" / "downloaded"
STATE_NAME = "state.json"
STATE_VERSION = 1
DEFAULT_IDLE_HOURS = 24.0
SECONDS_PER_HOUR = 3600
REQUEST_TIMEOUT_SECONDS = 30
MATCH_ID_PATTERN = re.compile(r"[0-9a-f]{32}")
TOKEN_VARIABLE = "MATCH_COLLECT_TOKEN"
PASSWORD_VARIABLE = "MATCH_STORE_PASSWORD"


class CollectError(Exception):
    """A failure that ends the run with exit code 1."""


@dataclass
class Verdict:
    name: str
    ok: bool
    reason: str


def default_verifier() -> Path:
    if platform.system() == "Windows":
        return ROOT / "build" / "windows-msvc-release" / "bin" / "BagiMahjongArena.exe"
    return ROOT / "build" / "macos-release" / "bin" / "BagiMahjongArena"


# ----- command line -----

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Copies the match logs that replay from the given folders into one folder, once each, with an index.")
    parser.add_argument("--from", dest="sources", action="append", default=[], type=Path, metavar="DIR",
                        help="a folder of *.jsonl match logs (searched recursively); repeat for more")
    parser.add_argument("--to", type=Path, default=DEFAULT_TO, help=f"the folder of the collected logs (default {DEFAULT_TO.relative_to(ROOT)}); outside git or git-ignored")
    parser.add_argument("--verifier", type=Path, default=default_verifier(),
                        help="the program that prints the `--logs DIR --check` format (default: the arena in build/)")
    parser.add_argument("--recheck", action="store_true", help="verify the kept files again and move the ones that no longer replay to dropped")
    parser.add_argument("--store", metavar="URL", help="the web service that holds the matches posted by testers; its matches are downloaded to --downloaded")
    parser.add_argument("--user", default="", help=f"the user name for HTTP Basic; the password is read from ${PASSWORD_VARIABLE} (${TOKEN_VARIABLE} sends a bearer token instead)")
    parser.add_argument("--downloaded", type=Path, default=DEFAULT_DOWNLOADED,
                        help=f"the folder of the raw downloaded matches and state.json (default {DEFAULT_DOWNLOADED.relative_to(ROOT)}); outside git or git-ignored")
    parser.add_argument("--idle-hours", type=float, default=DEFAULT_IDLE_HOURS,
                        help=f"a match that may still be played is deleted on the server when its piece count did not change for this long (default {DEFAULT_IDLE_HOURS:g}); "
                             "the server itself keeps a match 90 days, then only the local copy is left")
    parser.add_argument("--delete-id", metavar="ID", help="delete this one match on the server (needs --store) and do nothing else")
    parser.add_argument("--forget-nickname", metavar="NAME", help="remove every log whose start carries this nickname from the downloaded and collected folders and the index, and do nothing else")
    return parser


# ----- git -----

def nearest_existing(path: Path) -> Path:
    parent = path
    while not parent.is_dir():
        parent = parent.parent
    return parent


def git_tracks_here(path: Path) -> bool:
    """True when the nearest existing parent of `path` is in a git work tree (false too when git cannot be run)."""
    try:
        result = subprocess.run(["git", "-C", str(nearest_existing(path)), "rev-parse", "--show-toplevel"], capture_output=True, text=True)
    except OSError:
        return False
    return result.returncode == 0


def git_ignores(path: Path) -> bool:
    result = subprocess.run(["git", "-C", str(nearest_existing(path)), "check-ignore", "-q", str(path)], capture_output=True, text=True)
    return result.returncode == 0


def refused_target(to: Path) -> str | None:
    """Why `to` may not hold the logs, or None. Everything the tool writes there must be ignored by git when it is in a work tree."""
    to = to.resolve()  # git is run in another directory: a relative path would mean another file
    if not git_tracks_here(to):
        return None
    for probe in (to / INDEX_NAME, to / ("x" + LOG_SUFFIX)):
        if not git_ignores(probe):
            return f"{to} lies inside a git work tree and git does not ignore {probe.name} there; match logs are never committed"
    return None


# ----- the verifier -----

def parse_check_output(text: str) -> list[Verdict]:
    verdicts = []
    for line in text.splitlines():
        parts = line.split("\t", 2)
        if len(parts) == 2 and parts[0] == CHECK_OK:
            verdicts.append(Verdict(parts[1], True, ""))
        elif len(parts) == 3 and parts[0] == CHECK_BAD:
            verdicts.append(Verdict(parts[1], False, parts[2]))
        else:
            raise CollectError(f"the verifier printed a line that is neither `ok<TAB>name` nor `bad<TAB>name<TAB>reason`: {line!r}")
    return verdicts


def verify_folder(verifier: Path, folder: Path) -> list[Verdict]:
    try:
        result = subprocess.run([str(verifier), "--logs", str(folder), "--check"], capture_output=True, text=True)
    except OSError as error:
        raise CollectError(f"the verifier {verifier} cannot be run: {error}") from error
    if result.returncode != 0:
        detail = (result.stderr or result.stdout).strip()
        raise CollectError(f"the verifier failed on {folder} (exit {result.returncode}): {detail}")
    return parse_check_output(result.stdout)


# ----- the index -----

def load_index(path: Path) -> dict[str, dict]:
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        logs = data["logs"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise CollectError(f"{path} cannot be read ({error}); move it away to start a new index") from error
    if data.get("version") != INDEX_VERSION or not isinstance(logs, dict):
        raise CollectError(f"{path} is not an index of version {INDEX_VERSION}")
    return logs


def write_atomically(path: Path, data: bytes) -> None:
    temp = path.with_name(path.name + ".tmp")
    temp.write_bytes(data)
    os.replace(temp, path)


def save_index(path: Path, logs: dict[str, dict]) -> None:
    text = json.dumps({"version": INDEX_VERSION, "logs": logs}, indent=2, sort_keys=True) + "\n"
    write_atomically(path, text.encode("utf-8"))


def file_name_of(digest: str) -> str:
    return digest[:HASH_NAME_DIGITS] + LOG_SUFFIX


# ----- one run -----

@dataclass
class Dropped:
    source: str
    reason: str


@dataclass
class Tally:
    read: int = 0
    new: int = 0
    known: int = 0
    dropped_now: list[Dropped] = field(default_factory=list)  # the logs dropped for the first time in this run
    dropped_before: int = 0  # dropped by an earlier run and still dropped
    rechecked: int = 0
    missing: int = 0  # kept in the index, the file is gone and no source brought it back


def store_new(destination: Path, digest: str, data: bytes) -> None:
    """Writes the bytes under their name; an existing file of the same bytes (a run that stopped before the index) is left as it is."""
    if destination.exists():
        if hashlib.sha256(destination.read_bytes()).hexdigest() != digest:
            raise CollectError(f"{destination} exists and holds other bytes; the first {HASH_NAME_DIGITS} digits of a hash are taken")
        return
    write_atomically(destination, data)


def take_source_log(folder: Path, verdict: Verdict, to: Path, logs: dict[str, dict], seen: set[str], tally: Tally) -> None:
    source = f"{folder}/{verdict.name}"
    try:
        data = (folder / verdict.name).read_bytes()
    except OSError as error:
        raise CollectError(f"{source} cannot be read: {error}") from error
    tally.read += 1
    digest = hashlib.sha256(data).hexdigest()
    if digest in seen:
        tally.known += 1  # the same bytes twice in one run
        return
    seen.add(digest)
    entry = logs.get(digest)
    if verdict.ok:
        if entry and entry["status"] == STATUS_KEPT and (to / entry["file"]).exists():
            tally.known += 1
            return
        store_new(to / file_name_of(digest), digest, data)
        logs[digest] = {"file": file_name_of(digest), "source": entry["source"] if entry else source, "status": STATUS_KEPT}
        tally.new += 1
        return
    if entry and entry["status"] == STATUS_DROPPED:
        entry["reason"] = verdict.reason
        tally.dropped_before += 1
        return
    if entry:  # kept before, the file is still there or not: the verdict of this run decides
        (to / entry["file"]).unlink(missing_ok=True)
    logs[digest] = {"file": None, "source": entry["source"] if entry else source, "status": STATUS_DROPPED, "reason": verdict.reason}
    tally.dropped_now.append(Dropped(source, verdict.reason))


def recheck_kept(verifier: Path, to: Path, logs: dict[str, dict], seen: set[str], tally: Tally) -> None:
    """Verifies the kept files that this run did not just see again, and drops the ones that no longer replay."""
    by_file = {entry["file"]: digest for digest, entry in logs.items() if entry["status"] == STATUS_KEPT and digest not in seen}
    if not by_file:
        return
    for verdict in verify_folder(verifier, to):
        digest = by_file.pop(verdict.name, None)
        if digest is None or verdict.ok:
            continue
        (to / verdict.name).unlink()
        logs[digest].update({"file": None, "status": STATUS_DROPPED, "reason": verdict.reason})
        tally.dropped_now.append(Dropped(logs[digest]["source"], verdict.reason))
        tally.rechecked += 1
    tally.missing += len(by_file)  # kept in the index, not on disk


def reason_kind(reason: str) -> str:
    return reason.split(":", 1)[0]


def print_summary(tally: Tally, logs: dict[str, dict], to: Path) -> None:
    kept = sum(1 for entry in logs.values() if entry["status"] == STATUS_KEPT)
    print(f"read {tally.read} log files")
    print(f"kept (in {to}): {kept}")
    print(f"new: {tally.new}")
    print(f"already known: {tally.known}")
    print(f"dropped now: {len(tally.dropped_now)}")
    print(f"dropped before: {tally.dropped_before}")
    if tally.rechecked:
        print(f"moved from kept to dropped by --recheck: {tally.rechecked}")
    if tally.missing:
        print(f"kept in the index but not in the folder: {tally.missing}")
    kinds: dict[str, list[Dropped]] = {}
    for dropped in tally.dropped_now:
        kinds.setdefault(reason_kind(dropped.reason), []).append(dropped)
    for kind in sorted(kinds):
        print(f"dropped, {kind}: {len(kinds[kind])}")
        for dropped in kinds[kind]:
            print(f"  {dropped.source}\t{dropped.reason}")


def collect(args: argparse.Namespace, sources: list[Path], testers: dict[str, dict] | None = None) -> None:
    to: Path = args.to
    verifier: Path = args.verifier
    if not verifier.is_file():
        raise CollectError(f"the verifier {verifier} does not exist (build BagiMahjongArena, or give --verifier)")
    verdicts = [(folder, verify_folder(verifier, folder)) for folder in sources]  # every verdict before the first copy
    to.mkdir(parents=True, exist_ok=True)
    logs = load_index(to / INDEX_NAME)
    tally = Tally()
    seen: set[str] = set()
    for folder, folder_verdicts in verdicts:
        for verdict in folder_verdicts:
            take_source_log(folder, verdict, to, logs, seen, tally)
    if args.recheck:
        recheck_kept(verifier, to, logs, seen, tally)
    for digest, tester in (testers or {}).items():
        if digest in logs:
            logs[digest]["tester"] = tester
    save_index(to / INDEX_NAME, logs)
    print_summary(tally, logs, to)


# ----- the store -----

def auth_header(user: str) -> str | None:
    token = os.environ.get(TOKEN_VARIABLE)
    if token:
        return "Bearer " + token
    if user:
        credentials = f"{user}:{os.environ.get(PASSWORD_VARIABLE, '')}"
        return "Basic " + base64.b64encode(credentials.encode("utf-8")).decode("ascii")
    return None


def now() -> float:
    return time.time()


class Store:
    def __init__(self, url: str, user: str) -> None:
        self.base = url.rstrip("/")
        self.auth = auth_header(user)

    def request(self, method: str, path: str) -> bytes:
        request = urllib.request.Request(self.base + path, method=method)
        if self.auth:
            request.add_header("Authorization", self.auth)
        try:
            with urllib.request.urlopen(request, timeout=REQUEST_TIMEOUT_SECONDS) as response:
                return response.read()
        except urllib.error.HTTPError as error:
            raise CollectError(f"{method} {path}: the service answered {error.code}") from error
        except (urllib.error.URLError, OSError) as error:
            raise CollectError(f"{method} {path}: {error}") from error

    def list(self) -> list[dict]:
        try:
            matches = json.loads(self.request("GET", "/matches/"))["matches"]
            return [entry for entry in matches if MATCH_ID_PATTERN.fullmatch(str(entry["id"])) and int(entry["pieces"]) >= 0]
        except (ValueError, KeyError, TypeError) as error:
            raise CollectError(f"GET /matches/: the answer is not a list of matches ({error})") from error

    def piece(self, match_id: str, index: int) -> bytes:
        return self.request("GET", f"/matches/{match_id}/{index}.jsonl")

    def delete(self, match_id: str) -> None:
        try:
            self.request("DELETE", f"/matches/{match_id}")
        except CollectError as error:
            if "answered 404" not in str(error):  # already gone is done
                raise


def is_complete(pieces: list[bytes]) -> bool:
    """A match is over when its last piece is empty (given up) or holds the `end` event."""
    if not pieces[-1]:
        return True
    for line in pieces[-1].splitlines():
        try:
            if json.loads(line).get("event") == "end":
                return True
        except (ValueError, AttributeError):
            continue
    return False


def is_final(known: dict, idle_hours: float) -> bool:
    """The match will not grow any more as far as the tool can tell: it ended or was given up, or its piece count did not change for `idle_hours`."""
    return known["complete"] or now() - known["changed"] >= idle_hours * SECONDS_PER_HOUR


def load_state(path: Path) -> dict[str, dict]:
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
        matches = data["matches"]
    except (OSError, ValueError, KeyError, TypeError) as error:
        raise CollectError(f"{path} cannot be read ({error}); move it away to start again") from error
    if data.get("version") != STATE_VERSION or not isinstance(matches, dict):
        raise CollectError(f"{path} is not a state of version {STATE_VERSION}")
    return matches


def save_state(path: Path, matches: dict[str, dict]) -> None:
    write_atomically(path, (json.dumps({"version": STATE_VERSION, "matches": matches}, indent=2, sort_keys=True) + "\n").encode("utf-8"))


@dataclass
class DownloadTally:
    downloaded: int = 0
    deleted: int = 0
    waiting: int = 0  # kept on the server: it may still be played


def download_store(store: Store, downloaded: Path, idle_hours: float) -> tuple[DownloadTally, dict[str, dict]]:
    """Downloads what is new or grown, deletes on the server what is safe. Returns the tally and the tester of each file by hash."""
    tally = DownloadTally()
    testers: dict[str, dict] = {}
    state_path = downloaded / STATE_NAME
    listing = store.list()
    state = load_state(state_path)
    for entry in listing:
        match_id, count = entry["id"], int(entry["pieces"])
        known = state.get(match_id)
        if count == 0 or (known and known.get("deleted")):
            continue
        log_file, open_file = downloaded / (match_id + LOG_SUFFIX), downloaded / (match_id + OPEN_SUFFIX)
        if known is None or known["pieces"] != count or not (log_file.exists() or open_file.exists()):
            pieces = [store.piece(match_id, index) for index in range(count)]
            data = b"".join(pieces)
            changed = now() if known is None or known["pieces"] != count else known["changed"]
            known = {"pieces": count, "changed": changed, "complete": is_complete(pieces), "deleted": False}
            file = log_file if is_final(known, idle_hours) else open_file
            downloaded.mkdir(parents=True, exist_ok=True)
            write_atomically(file, data)
            if file.read_bytes() != data:
                raise CollectError(f"{file} does not hold the bytes fetched; nothing is deleted on the server")
            (open_file if file == log_file else log_file).unlink(missing_ok=True)  # one file per match
            state[match_id] = known
            tally.downloaded += 1
            save_state(state_path, state)  # the file is safe before the first request that deletes
        if is_final(known, idle_hours) and open_file.exists():
            os.replace(open_file, log_file)  # the match is over or given up: now it is a log, once
        if isinstance(entry.get("tester"), dict):
            known["tester"] = entry["tester"]
            save_state(state_path, state)
        if "tester" in known and log_file.exists():
            testers[hashlib.sha256(log_file.read_bytes()).hexdigest()] = known["tester"]
        if is_final(known, idle_hours):
            store.delete(match_id)
            known["deleted"] = True
            tally.deleted += 1
            save_state(state_path, state)
        else:
            tally.waiting += 1
    return tally, testers


def delete_one(store: Store, match_id: str) -> None:
    if not MATCH_ID_PATTERN.fullmatch(match_id):
        raise CollectError(f"{match_id!r} is not a match id (32 lower-case hex digits)")
    store.delete(match_id)
    print(f"deleted {match_id} on the server")


def nickname_of(path: Path) -> str | None:
    try:
        with path.open(encoding="utf-8") as lines:
            first = json.loads(lines.readline())
    except (OSError, ValueError):
        return None
    if isinstance(first, dict) and first.get("event") == "start" and isinstance(first.get("nickname"), str):
        return first["nickname"]
    return None


def forget_nickname(args: argparse.Namespace, store: Store | None) -> None:
    removed, server_ids = 0, []
    logs = load_index(args.to / INDEX_NAME) if (args.to / INDEX_NAME).exists() else {}
    for folder in (args.downloaded, args.to):
        suffixes = (LOG_SUFFIX, OPEN_SUFFIX) if folder == args.downloaded else (LOG_SUFFIX,)
        for path in sorted(p for suffix in suffixes for p in folder.glob("*" + suffix)) if folder.is_dir() else []:
            if nickname_of(path) != args.forget_nickname:
                continue
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            logs.pop(digest, None)
            for key in [key for key, entry in logs.items() if entry.get("file") == path.name]:
                del logs[key]
            if folder == args.downloaded and MATCH_ID_PATTERN.fullmatch(path.stem):
                server_ids.append(path.stem)
            path.unlink()
            removed += 1
    if (args.to / INDEX_NAME).exists() or logs:
        args.to.mkdir(parents=True, exist_ok=True)
        save_index(args.to / INDEX_NAME, logs)
    state_path = args.downloaded / STATE_NAME
    if server_ids and state_path.exists():
        state = load_state(state_path)
        for match_id in server_ids:
            if match_id in state:
                state[match_id].pop("tester", None)
                state[match_id]["deleted"] = True  # never fetched again
        save_state(state_path, state)
    if store:
        for match_id in server_ids:
            store.delete(match_id)
    print(f"removed {removed} log files of the nickname {args.forget_nickname!r}")


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)  # a wrong command line exits 2 here
    if not (args.sources or args.store or args.forget_nickname):
        parser.error("give --from, --store or --forget-nickname")
    if args.delete_id and not args.store:
        parser.error("--delete-id needs --store")
    for folder in args.sources:
        if not folder.is_dir():
            print(f"FAILED: {folder} is not a folder", file=sys.stderr)
            return EXIT_USAGE
    for target in (args.to, args.downloaded):
        refusal = refused_target(target)
        if refusal:
            print(f"FAILED: {refusal}", file=sys.stderr)
            return EXIT_USAGE
    try:
        store = Store(args.store, args.user) if args.store else None
        if args.delete_id:
            delete_one(store, args.delete_id)
            return EXIT_DONE
        if args.forget_nickname:
            forget_nickname(args, store)
            return EXIT_DONE
        sources, testers = list(args.sources), {}
        if store:
            if not args.verifier.is_file():  # before the first deletion on the server
                raise CollectError(f"the verifier {args.verifier} does not exist (build BagiMahjongArena, or give --verifier)")
            tally, testers = download_store(store, args.downloaded, args.idle_hours)
            print(f"downloaded: {tally.downloaded}; deleted on the server: {tally.deleted}; kept on the server (may still be played): {tally.waiting}")
            if args.downloaded.is_dir():
                sources.append(args.downloaded)
        if sources:
            collect(args, sources, testers)
    except CollectError as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return EXIT_FAILED
    return EXIT_DONE


if __name__ == "__main__":
    sys.exit(main())
