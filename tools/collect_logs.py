#!/usr/bin/env python3
"""Collects match logs of testers into one folder: only the logs that replay, each once, with an index.

    python3 tools/collect_logs.py --from some/folder [--from other/folder ...] [--to logs/collected] [--verifier build/macos-release/bin/BagiMahjongArena] [--recheck]

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
refuses any other folder, as it stores nothing but the logs and the index. There is no downloader: a dump of the store placed in a folder
works with `--from`.

Exit code: 0 done (also when logs were dropped: read the summary), 1 the verifier is missing or fails, 2 a wrong command line.
Standard library only.
"""

import argparse
import hashlib
import json
import os
import platform
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_TO = ROOT / "logs" / "collected"
INDEX_NAME = "index.json"
INDEX_VERSION = 1
HASH_NAME_DIGITS = 16
LOG_SUFFIX = ".jsonl"
STATUS_KEPT = "kept"
STATUS_DROPPED = "dropped"
CHECK_OK = "ok"
CHECK_BAD = "bad"
EXIT_DONE = 0
EXIT_FAILED = 1
EXIT_USAGE = 2


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
    parser.add_argument("--from", dest="sources", action="append", required=True, type=Path, metavar="DIR",
                        help="a folder of *.jsonl match logs (searched recursively); repeat for more")
    parser.add_argument("--to", type=Path, default=DEFAULT_TO, help=f"the folder of the collected logs (default {DEFAULT_TO.relative_to(ROOT)}); outside git or git-ignored")
    parser.add_argument("--verifier", type=Path, default=default_verifier(),
                        help="the program that prints the `--logs DIR --check` format (default: the arena in build/)")
    parser.add_argument("--recheck", action="store_true", help="verify the kept files again and move the ones that no longer replay to dropped")
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


def collect(args: argparse.Namespace) -> None:
    to: Path = args.to
    verifier: Path = args.verifier
    if not verifier.is_file():
        raise CollectError(f"the verifier {verifier} does not exist (build BagiMahjongArena, or give --verifier)")
    verdicts = [(folder, verify_folder(verifier, folder)) for folder in args.sources]  # every verdict before the first copy
    to.mkdir(parents=True, exist_ok=True)
    logs = load_index(to / INDEX_NAME)
    tally = Tally()
    seen: set[str] = set()
    for folder, folder_verdicts in verdicts:
        for verdict in folder_verdicts:
            take_source_log(folder, verdict, to, logs, seen, tally)
    if args.recheck:
        recheck_kept(verifier, to, logs, seen, tally)
    save_index(to / INDEX_NAME, logs)
    print_summary(tally, logs, to)


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)  # a wrong command line exits 2 here
    for folder in args.sources:
        if not folder.is_dir():
            print(f"FAILED: {folder} is not a folder", file=sys.stderr)
            return EXIT_USAGE
    refusal = refused_target(args.to)
    if refusal:
        print(f"FAILED: {refusal}", file=sys.stderr)
        return EXIT_USAGE
    try:
        collect(args)
    except CollectError as error:
        print(f"FAILED: {error}", file=sys.stderr)
        return EXIT_FAILED
    return EXIT_DONE


if __name__ == "__main__":
    sys.exit(main())
