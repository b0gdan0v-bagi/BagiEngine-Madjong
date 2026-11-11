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
import unittest
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


if __name__ == "__main__":
    unittest.main()
