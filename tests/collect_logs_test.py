#!/usr/bin/env python3
"""Tests of tools/collect_logs.py with a fake verifier (a script that prints the `--logs DIR --check` format) and temp folders; no build.

    python3 tests/collect_logs_test.py

The fake verifier calls a file `bad` when its text contains a word of the JSON list in the file named by the environment variable
FAKE_VERIFIER_BAD, so a test can change the verdicts between two runs. Registered as the ctest entry MahjongCollectLogs.
"""

import contextlib
import io
import json
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

TOOLS = Path(__file__).resolve().parents[1] / "tools"
sys.path.insert(0, str(TOOLS))

import collect_logs  # noqa: E402

FAKE_VERIFIER = """#!{python}
import json, os, sys
folder = sys.argv[2]
words = json.load(open(os.environ["FAKE_VERIFIER_BAD"]))
if os.environ.get("FAKE_VERIFIER_FAIL"):
    print("the fake verifier fails", file=sys.stderr)
    sys.exit(1)
names = sorted(os.path.relpath(os.path.join(root, f), folder).replace(os.sep, "/")
               for root, _, files in os.walk(folder) for f in files if f.endswith(".jsonl"))
for name in names:
    text = open(os.path.join(folder, name), encoding="utf-8").read()
    reason = next((("does not replay: event 3 (ActionRefused)" if word == "REFUSED" else "unreadable: line 1: bad " + word)
                   for word in words if word in text), None)
    print("ok\\t" + name if reason is None else "bad\\t" + name + "\\t" + reason)
"""


def run_tool(*argv: str) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        try:
            code = collect_logs.main(list(argv))
        except SystemExit as exit_request:  # argparse
            code = exit_request.code
    return code, out.getvalue(), err.getvalue()


@unittest.skipIf(sys.platform == "win32", "the fake verifier is a script with a shebang")
class CollectLogsTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = Path(tempfile.mkdtemp(prefix="collect_logs_test_"))
        self.addCleanup(shutil.rmtree, self.temp, ignore_errors=True)
        self.bad_words = self.temp / "bad.json"
        self.set_bad([])
        os.environ["FAKE_VERIFIER_BAD"] = str(self.bad_words)
        os.environ.pop("FAKE_VERIFIER_FAIL", None)
        self.verifier = self.temp / "verifier.py"
        self.verifier.write_text(FAKE_VERIFIER.format(python=sys.executable), encoding="utf-8")
        self.verifier.chmod(self.verifier.stat().st_mode | stat.S_IXUSR)
        self.to = self.temp / "collected"

    def set_bad(self, words: list[str]) -> None:
        self.bad_words.write_text(json.dumps(words), encoding="utf-8")

    def folder(self, name: str, **files: str) -> Path:
        folder = self.temp / name
        folder.mkdir(exist_ok=True)
        for file, text in files.items():
            (folder / (file + ".jsonl")).write_text(text, encoding="utf-8")
        return folder

    def collect(self, *sources: Path, extra: tuple[str, ...] = ()) -> tuple[int, str, str]:
        arguments = []
        for source in sources:
            arguments += ["--from", str(source)]
        return run_tool(*arguments, "--to", str(self.to), "--verifier", str(self.verifier), *extra)

    def index(self) -> dict:
        return json.loads((self.to / "index.json").read_text(encoding="utf-8"))["logs"]

    def kept_files(self) -> list[str]:
        return sorted(path.name for path in self.to.glob("*.jsonl"))

    def test_a_new_good_log_is_copied_and_indexed(self) -> None:
        source = self.folder("a", one="GOOD one\n")
        code, out, _ = self.collect(source)
        self.assertEqual(code, 0)
        self.assertEqual(len(self.kept_files()), 1)
        (digest, entry), = self.index().items()
        self.assertEqual(entry["file"], digest[:16] + ".jsonl")
        self.assertEqual(entry["status"], "kept")
        self.assertEqual(entry["source"], f"{source}/one.jsonl")
        self.assertEqual((self.to / entry["file"]).read_text(encoding="utf-8"), "GOOD one\n")
        self.assertIn("new: 1\n", out)
        self.assertIn("dropped now: 0\n", out)

    def test_a_second_run_copies_nothing_and_reports_it_as_known(self) -> None:
        source = self.folder("a", one="GOOD one\n")
        self.collect(source)
        before = self.index()
        code, out, _ = self.collect(source)
        self.assertEqual(code, 0)
        self.assertEqual(self.index(), before)
        self.assertIn("new: 0\n", out)
        self.assertIn("already known: 1\n", out)
        self.assertIn("kept (in", out)

    def test_a_log_that_fails_is_not_copied_is_dropped_with_its_reason_and_counted(self) -> None:
        self.set_bad(["BROKEN", "REFUSED"])
        source = self.folder("a", good="GOOD\n", broken="BROKEN\n", refused="REFUSED\n")
        code, out, _ = self.collect(source)
        self.assertEqual(code, 0)
        self.assertEqual(len(self.kept_files()), 1)
        dropped = [entry for entry in self.index().values() if entry["status"] == "dropped"]
        self.assertEqual(sorted(entry["reason"] for entry in dropped),
                         ["does not replay: event 3 (ActionRefused)", "unreadable: line 1: bad BROKEN"])
        self.assertTrue(all(entry["file"] is None for entry in dropped))
        self.assertIn("dropped now: 2\n", out)
        self.assertIn("dropped, unreadable: 1\n", out)
        self.assertIn("dropped, does not replay: 1\n", out)
        self.assertIn(f"  {source}/broken.jsonl\tunreadable: line 1: bad BROKEN\n", out)
        self.assertIn(f"  {source}/refused.jsonl\tdoes not replay: event 3 (ActionRefused)\n", out)
        _, again, _ = self.collect(source)
        self.assertIn("dropped now: 0\n", again)
        self.assertIn("dropped before: 2\n", again)

    def test_the_same_bytes_from_two_folders_are_one_file(self) -> None:
        first = self.folder("a", one="GOOD same\n")
        second = self.folder("b", other="GOOD same\n", third="GOOD different\n")
        code, out, _ = self.collect(first, second)
        self.assertEqual(code, 0)
        self.assertEqual(len(self.kept_files()), 2)
        sources = sorted(entry["source"] for entry in self.index().values())
        self.assertEqual(sources, [f"{first}/one.jsonl", f"{second}/third.jsonl"])
        self.assertIn("read 3 log files\n", out)
        self.assertIn("new: 2\n", out)
        self.assertIn("already known: 1\n", out)

    def test_subfolders_are_read_and_a_log_dropped_earlier_that_replays_now_is_kept(self) -> None:
        self.set_bad(["GOOD"])
        source = self.folder("a")
        (source / "deep").mkdir()
        (source / "deep" / "x.jsonl").write_text("GOOD x\n", encoding="utf-8")
        self.collect(source)
        self.assertEqual(self.kept_files(), [])
        self.set_bad([])
        code, out, _ = self.collect(source)
        self.assertEqual(code, 0)
        self.assertEqual(len(self.kept_files()), 1)
        self.assertIn("new: 1\n", out)
        self.assertEqual(next(iter(self.index().values()))["source"], f"{source}/deep/x.jsonl")

    def test_recheck_moves_a_log_whose_verdict_turned_bad(self) -> None:
        source = self.folder("a", one="GOOD one\n", two="GOOD two\n")
        self.collect(source)
        self.set_bad(["two"])
        code, out, _ = self.collect(self.folder("empty"), extra=("--recheck",))
        self.assertEqual(code, 0)
        self.assertIn("moved from kept to dropped by --recheck: 1\n", out)
        self.assertIn("dropped, unreadable: 1\n", out)
        self.assertEqual(len(self.kept_files()), 1)
        statuses = sorted(entry["status"] for entry in self.index().values())
        self.assertEqual(statuses, ["dropped", "kept"])

    def test_a_target_inside_a_git_work_tree_is_refused_unless_ignored(self) -> None:
        if shutil.which("git") is None:
            self.skipTest("no git")
        repo = self.temp / "repo"
        repo.mkdir()
        subprocess.run(["git", "init", "-q", str(repo)], check=True)
        (repo / ".gitignore").write_text("ignored/\n", encoding="utf-8")
        source = self.folder("a", one="GOOD one\n")
        arguments = ["--from", str(source), "--verifier", str(self.verifier)]
        code, _, err = run_tool(*arguments, "--to", str(repo / "tracked"))
        self.assertEqual(code, 2)
        self.assertIn("git work tree", err)
        self.assertFalse((repo / "tracked").exists())
        code, _, _ = run_tool(*arguments, "--to", str(repo / "ignored" / "collected"))
        self.assertEqual(code, 0)
        code, _, _ = run_tool(*arguments, "--to", str(repo / "ignored" / "collected"))  # the folder exists now
        self.assertEqual(code, 0)
        cwd = os.getcwd()
        self.addCleanup(os.chdir, cwd)
        os.chdir(repo)
        code, _, _ = run_tool(*arguments, "--to", "ignored/collected")  # a relative path
        self.assertEqual(code, 0)
        self.assertTrue((repo / "ignored" / "collected" / "index.json").exists())

    def test_a_missing_or_failing_verifier_is_exit_1(self) -> None:
        source = self.folder("a", one="GOOD one\n")
        code, _, err = run_tool("--from", str(source), "--to", str(self.to), "--verifier", str(self.temp / "nothing"))
        self.assertEqual(code, 1)
        self.assertIn("does not exist", err)
        os.environ["FAKE_VERIFIER_FAIL"] = "1"
        code, _, err = self.collect(source)
        self.assertEqual(code, 1)
        self.assertIn("the fake verifier fails", err)
        self.assertFalse(self.to.exists())  # nothing was written

    def test_bad_arguments_are_exit_2(self) -> None:
        self.assertEqual(run_tool("--to", str(self.to))[0], 2)  # no --from
        self.assertEqual(run_tool("--from", str(self.temp / "not-there"), "--to", str(self.to))[0], 2)


START = '{"event":"start","version":6,"seed":1%s}\n'
HAND = '{"event":"hand","n":%d}\n'
END = '{"event":"end"}\n'


class FakeStore:
    """The four calls of the match service on localhost: list, fetch a piece, delete; optional Basic or Bearer check."""

    def __init__(self) -> None:
        self.matches: dict[str, list[bytes]] = {}
        self.testers: dict[str, dict] = {}
        self.expect_auth: str | None = None
        self.fail_piece_after: int | None = None  # fetches allowed before a 500
        self.fetches = 0
        self.requests: list[str] = []
        self.auth_seen: list[str | None] = []
        store = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *args) -> None:
                pass

            def answer(self, code: int, body: bytes = b"") -> None:
                self.send_response(code)
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                self.wfile.write(body)

            def allowed(self) -> bool:
                store.requests.append(f"{self.command} {self.path}")
                store.auth_seen.append(self.headers.get("Authorization"))
                if store.expect_auth is not None and self.headers.get("Authorization") != store.expect_auth:
                    self.answer(401)
                    return False
                return True

            def do_GET(self) -> None:
                if not self.allowed():
                    return
                if self.path == "/matches/":
                    entries = [{"id": key, "pieces": len(value), **({"tester": store.testers[key]} if key in store.testers else {})}
                               for key, value in sorted(store.matches.items())]
                    self.answer(200, json.dumps({"matches": entries}).encode())
                    return
                parts = self.path.split("/")
                if len(parts) == 4 and parts[2] in store.matches and parts[3].endswith(".jsonl"):
                    store.fetches += 1
                    if store.fail_piece_after is not None and store.fetches > store.fail_piece_after:
                        self.answer(500)
                        return
                    self.answer(200, store.matches[parts[2]][int(parts[3][:-6])])
                    return
                self.answer(404)

            def do_DELETE(self) -> None:
                if not self.allowed():
                    return
                match_id = self.path.rsplit("/", 1)[-1]
                self.answer(200 if store.matches.pop(match_id, None) is not None else 404)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.url = f"http://127.0.0.1:{self.server.server_address[1]}"

    def close(self) -> None:
        self.server.shutdown()
        self.server.server_close()


ID_A = "a" * 32
ID_B = "b" * 32
ID_C = "c" * 32


@unittest.skipIf(sys.platform == "win32", "the fake verifier is a script with a shebang")
class CollectStoreTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = Path(tempfile.mkdtemp(prefix="collect_store_test_"))
        self.addCleanup(shutil.rmtree, self.temp, ignore_errors=True)
        self.bad_words = self.temp / "bad.json"
        self.bad_words.write_text("[]", encoding="utf-8")
        os.environ["FAKE_VERIFIER_BAD"] = str(self.bad_words)
        os.environ.pop("FAKE_VERIFIER_FAIL", None)
        os.environ.pop("MATCH_COLLECT_TOKEN", None)
        os.environ["MATCH_STORE_PASSWORD"] = "s3cret-word"
        self.verifier = self.temp / "verifier.py"
        self.verifier.write_text(FAKE_VERIFIER.format(python=sys.executable), encoding="utf-8")
        self.verifier.chmod(self.verifier.stat().st_mode | stat.S_IXUSR)
        self.to = self.temp / "collected"
        self.downloaded = self.temp / "downloaded"
        self.store = FakeStore()
        self.addCleanup(self.store.close)
        self.clock = 1000.0
        original = collect_logs.now
        collect_logs.now = lambda: self.clock
        self.addCleanup(setattr, collect_logs, "now", original)

    def set_bad(self, words: list[str]) -> None:
        self.bad_words.write_text(json.dumps(words), encoding="utf-8")

    def run_store(self, *extra: str, user: str = "tester") -> tuple[int, str, str]:
        return run_tool("--store", self.store.url, "--user", user, "--to", str(self.to), "--downloaded", str(self.downloaded),
                        "--verifier", str(self.verifier), *extra)

    def index(self) -> dict:
        return json.loads((self.to / "index.json").read_text(encoding="utf-8"))["logs"]

    def test_a_match_in_three_pieces_is_one_file_and_deleted_and_a_second_run_downloads_nothing(self) -> None:
        pieces = [(START % "").encode(), (HAND % 1).encode(), (HAND % 2 + END).encode()]
        self.store.matches[ID_A] = list(pieces)
        code, out, _ = self.run_store()
        self.assertEqual(code, 0)
        self.assertEqual((self.downloaded / (ID_A + ".jsonl")).read_bytes(), b"".join(pieces))
        self.assertEqual(self.store.matches, {})
        self.assertIn("downloaded: 1; deleted on the server: 1", out)
        self.assertEqual(len(list(self.to.glob("*.jsonl"))), 1)
        self.store.requests.clear()
        code, out, _ = self.run_store()
        self.assertEqual(code, 0)
        self.assertIn("downloaded: 0", out)
        self.assertEqual(self.store.requests, ["GET /matches/"])

    def test_a_log_that_does_not_replay_stays_downloaded_is_dropped_and_is_deleted_on_the_server(self) -> None:
        self.set_bad(["BROKEN"])
        self.store.matches[ID_A] = [(START % "").encode() + b'{"event":"end","BROKEN":1}\n']
        code, out, _ = self.run_store()
        self.assertEqual(code, 0)
        self.assertTrue((self.downloaded / (ID_A + ".jsonl")).exists())
        self.assertEqual(self.store.matches, {})
        self.assertEqual([entry["status"] for entry in self.index().values()], ["dropped"])
        self.assertIn("dropped now: 1\n", out)
        self.assertIn("unreadable: line 1: bad BROKEN", out)

    def test_an_incomplete_match_waits_grows_and_is_deleted_after_the_idle_time(self) -> None:
        part, log = self.downloaded / (ID_A + ".part"), self.downloaded / (ID_A + ".jsonl")
        self.store.matches[ID_A] = [(START % "").encode(), (HAND % 1).encode()]
        code, out, _ = self.run_store("--idle-hours", "2")
        self.assertEqual(code, 0)
        self.assertIn("deleted on the server: 0; kept on the server (may still be played): 1", out)
        self.assertIn(ID_A, self.store.matches)
        self.assertTrue(part.exists())
        self.assertFalse(log.exists())
        self.assertEqual(self.index(), {})  # an open match is not handed to the flow
        self.clock += 3600  # one hour: still waiting
        self.store.matches[ID_A].append((HAND % 2).encode())  # it grew: fetched again, the clock restarts
        _, out, _ = self.run_store("--idle-hours", "2")
        self.assertIn("downloaded: 1", out)
        self.assertEqual(part.read_bytes(), b"".join(self.store.matches[ID_A]))
        self.assertEqual(self.index(), {})
        self.clock += 3600
        _, out, _ = self.run_store("--idle-hours", "2")
        self.assertIn("downloaded: 0; deleted on the server: 0", out)
        self.clock += 3600  # two hours since the last change
        _, out, _ = self.run_store("--idle-hours", "2")
        self.assertIn("deleted on the server: 1", out)
        self.assertEqual(self.store.matches, {})
        self.assertFalse(part.exists())
        self.assertEqual(log.read_bytes(), b"".join([(START % "").encode(), (HAND % 1).encode(), (HAND % 2).encode()]))
        self.assertEqual([entry["status"] for entry in self.index().values()], ["kept"])  # one log for the id, not one per state
        self.assertEqual(len(list(self.to.glob("*.jsonl"))), 1)

    def test_an_open_match_that_ends_later_becomes_one_log(self) -> None:
        self.store.matches[ID_A] = [(START % "").encode(), (HAND % 1).encode()]
        self.run_store()
        self.store.matches[ID_A].append((HAND % 2 + END).encode())
        _, out, _ = self.run_store()
        self.assertIn("deleted on the server: 1", out)
        self.assertEqual(sorted(path.name for path in self.downloaded.glob("*.*")), sorted(["state.json", ID_A + ".jsonl"]))
        self.assertEqual(len(list(self.to.glob("*.jsonl"))), 1)
        self.assertEqual(len(self.index()), 1)

    def test_an_empty_last_piece_is_a_match_given_up_and_is_deleted_at_once(self) -> None:
        self.store.matches[ID_A] = [(START % "").encode(), b""]
        self.run_store()
        self.assertEqual(self.store.matches, {})

    def test_a_server_failing_halfway_deletes_nothing_unsaved(self) -> None:
        self.store.matches[ID_A] = [(START % "").encode(), (HAND % 1 + END).encode()]
        self.store.matches[ID_B] = [(START % "").encode(), (HAND % 1 + END).encode()]
        self.store.fail_piece_after = 3  # the first match is complete, the second fails on its second piece
        code, _, err = self.run_store()
        self.assertEqual(code, 1)
        self.assertIn("500", err)
        self.assertNotIn(ID_A, self.store.matches)
        self.assertTrue((self.downloaded / (ID_A + ".jsonl")).exists())
        self.assertIn(ID_B, self.store.matches)
        self.assertFalse((self.downloaded / (ID_B + ".jsonl")).exists())
        self.store.fail_piece_after = None
        self.assertEqual(self.run_store()[0], 0)
        self.assertEqual(self.store.matches, {})
        self.assertTrue((self.downloaded / (ID_B + ".jsonl")).exists())

    def test_a_wrong_password_is_exit_1_writes_nothing_and_prints_no_password(self) -> None:
        self.store.matches[ID_A] = [(START % "").encode(), b""]
        self.store.expect_auth = collect_logs.auth_header("tester").replace("Basic ", "Basic x")
        code, out, err = self.run_store()
        self.assertEqual(code, 1)
        self.assertIn("401", err)
        self.assertFalse(self.downloaded.exists())
        self.assertFalse(self.to.exists())
        self.assertNotIn("s3cret-word", out + err)
        self.assertIn(ID_A, self.store.matches)

    def test_basic_is_sent_with_the_password_and_a_token_is_sent_as_bearer(self) -> None:
        self.store.expect_auth = collect_logs.auth_header("tester")
        self.assertTrue(self.store.expect_auth.startswith("Basic "))
        self.assertEqual(self.run_store()[0], 0)
        os.environ["MATCH_COLLECT_TOKEN"] = "tok-123"
        self.addCleanup(os.environ.pop, "MATCH_COLLECT_TOKEN", None)
        self.store.expect_auth = "Bearer tok-123"
        self.assertEqual(self.run_store()[0], 0)
        self.assertEqual(self.store.auth_seen[-1], "Bearer tok-123")
        os.environ.pop("MATCH_COLLECT_TOKEN")
        self.store.expect_auth = None
        self.store.auth_seen.clear()
        self.assertEqual(run_tool("--store", self.store.url, "--to", str(self.to), "--downloaded", str(self.downloaded),
                                  "--verifier", str(self.verifier))[0], 0)
        self.assertEqual(self.store.auth_seen, [None])  # no user, no token: no header

    def test_the_tester_of_a_list_entry_is_kept_in_the_index_and_its_absence_is_fine(self) -> None:
        self.store.matches[ID_A] = [(START % "").encode(), (HAND % 1 + END).encode()]
        self.store.matches[ID_B] = [(START % "").encode(), (HAND % 2 + END).encode()]
        self.store.testers[ID_A] = {"provider": "google", "email": "a@example.org"}
        self.assertEqual(self.run_store()[0], 0)
        entries = sorted(self.index().values(), key=lambda entry: "tester" in entry)
        self.assertNotIn("tester", entries[0])
        self.assertEqual(entries[1]["tester"], {"provider": "google", "email": "a@example.org"})

    def test_delete_id(self) -> None:
        self.store.matches[ID_A] = [b""]
        self.store.matches[ID_B] = [b""]
        code, out, _ = run_tool("--store", self.store.url, "--user", "tester", "--delete-id", ID_A)
        self.assertEqual(code, 0)
        self.assertEqual(list(self.store.matches), [ID_B])
        self.assertEqual(run_tool("--store", self.store.url, "--delete-id", "nope")[0], 1)
        self.assertEqual(run_tool("--delete-id", ID_A)[0], 2)

    def test_forget_nickname_removes_that_nickname_everywhere_and_no_other(self) -> None:
        self.store.matches[ID_A] = [(START % ',"nickname":"Anna"').encode() + (END).encode()]
        self.store.matches[ID_B] = [(START % ',"nickname":"Boris"').encode() + (END).encode()]
        self.store.matches[ID_C] = [(START % "").encode() + (END).encode()]
        self.assertEqual(self.run_store()[0], 0)
        self.assertEqual(len(self.index()), 3)
        code, out, _ = run_tool("--forget-nickname", "Anna", "--to", str(self.to), "--downloaded", str(self.downloaded))
        self.assertEqual(code, 0)
        self.assertIn("removed 2 log files", out)  # the raw copy and the collected one
        self.assertFalse((self.downloaded / (ID_A + ".jsonl")).exists())
        self.assertTrue((self.downloaded / (ID_B + ".jsonl")).exists())
        self.assertTrue((self.downloaded / (ID_C + ".jsonl")).exists())
        self.assertEqual(len(self.index()), 2)
        self.assertEqual(len(list(self.to.glob("*.jsonl"))), 2)
        self.assertNotIn("Anna", "".join(path.read_text(encoding="utf-8") for path in self.to.iterdir()))

    def test_no_source_is_exit_2(self) -> None:
        self.assertEqual(run_tool("--to", str(self.to))[0], 2)


if __name__ == "__main__":
    unittest.main()
