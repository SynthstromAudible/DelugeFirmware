#! /usr/bin/env python3
"""Unit tests for golden_update.py, golden_records.py and `./dbt test --update`.

Run directly with:
    python3 scripts/tasks/test_golden_update.py
"""

import contextlib
import importlib.util
import io
import json
import multiprocessing
import os
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))

import golden_records as gr
import golden_update as gu
import harness_registry as hr

_spec = importlib.util.spec_from_file_location(
    "task_test", Path(__file__).parent / "task-test.py"
)
tt = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tt)


def _env(log: Path) -> dict[str, str]:
    return {gu.SWITCH: "1", gu.LOG: str(log)}


def _args(*argv):
    return tt.argparser().parse_args(list(argv))


def _script(name, code, baseline=("base.txt",)):
    return hr.Harness(
        name=name,
        path=".",
        level="integration",
        runner="script",
        kind="regression",
        status="gate",
        gates="g",
        tier="pr",
        build=[],
        run=[sys.executable, "-c", code],
        requires=[],
        runtime="?",
        baseline=list(baseline),
    )


class UpdatingTest(unittest.TestCase):
    def test_off_unless_exactly_one(self):
        self.assertFalse(gu.updating({}))
        self.assertFalse(gu.updating({gu.SWITCH: "0", gu.LOG: "x"}))
        self.assertFalse(gu.updating({gu.SWITCH: "", gu.LOG: "x"}))
        self.assertTrue(gu.updating({gu.SWITCH: "1", gu.LOG: "x"}))

    def test_the_switch_without_the_log_is_refused(self):
        with self.assertRaises(SystemExit) as cm:
            gu.updating({gu.SWITCH: "1"})
        self.assertEqual(cm.exception.code, 2)

    def test_the_refusal_names_the_runner(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), self.assertRaises(SystemExit):
            gu.updating({gu.SWITCH: "1"})
        self.assertEqual(
            err.getvalue(),
            "DELUGE_GOLDEN_UPDATE=1 is set by ./dbt test --update <name>, which builds first and collects what changed: "
            "run that\n",
        )

    def test_reporting_without_a_log_is_refused(self):
        with (
            contextlib.redirect_stderr(io.StringIO()),
            self.assertRaises(SystemExit) as cm,
        ):
            gu.report("x", "a", "b", env={})
        self.assertEqual(cm.exception.code, 2)

    def test_without_drops_both(self):
        self.assertEqual(
            gu.without({gu.SWITCH: "1", gu.LOG: "x", "A": "b"}), {"A": "b"}
        )


class WriterTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.log = self.dir / "log"
        self.env = _env(self.log)

    def lines(self):
        return self.log.read_text().splitlines() if self.log.exists() else []

    def test_a_new_file_is_reported_from_absent(self):
        self.assertTrue(gu.write_text(self.dir / "a.txt", "x\n", "a.txt", env=self.env))
        (line,) = self.lines()
        self.assertTrue(
            line.startswith("GOLDEN-UPDATED a.txt (absent) -> sha256="), line
        )
        self.assertEqual(gu.item_file(line), "a.txt")

    def test_unchanged_bytes_are_not_written_or_reported(self):
        path = self.dir / "a.txt"
        path.write_text("x\n")
        os.utime(path, (1, 1))
        self.assertFalse(gu.write_text(path, "x\n", "a.txt", env=self.env))
        self.assertEqual(path.stat().st_mtime, 1)
        self.assertEqual(self.lines(), [])

    def test_descriptions_are_one_word(self):
        for value in (
            None,
            "abc",
            "f" * 64,
            {"sha256": "f" * 64, "frames": 3, "rms_db": -1.5},
            [1, 2],
            7,
        ):
            self.assertNotIn(" ", gu.describe_value(value))
        self.assertNotIn(" ", gu.describe_bytes(b"a b"))

    def test_an_item_with_spaces_parses(self):
        gu.report("tests/x/Bedroom Beat.txt", "a", "b", env=self.env)
        self.assertEqual(gu.item_file(self.lines()[0]), "tests/x/Bedroom Beat.txt")

    def test_remove_reports_to_absent(self):
        path = self.dir / "a.pbm"
        path.write_text("P1")
        self.assertTrue(gu.remove(path, "a.pbm", env=self.env))
        self.assertFalse(path.exists())
        self.assertTrue(self.lines()[0].endswith(" -> (absent)"))
        self.assertFalse(gu.remove(path, "a.pbm", env=self.env))

    def test_write_json_formats_and_reports_each_key(self):
        path = self.dir / "f.json"
        path.write_text(
            json.dumps({"a": "1" * 64, "b": "2" * 64}, indent=2, sort_keys=True) + "\n"
        )
        n = gu.write_json(
            path,
            {"a": "1" * 64, "b": "3" * 64, "c": "4" * 64},
            "f.json",
            indent=2,
            env=self.env,
        )
        self.assertEqual(n, 2)
        self.assertEqual(
            path.read_text(),
            json.dumps(
                {"a": "1" * 64, "b": "3" * 64, "c": "4" * 64}, indent=2, sort_keys=True
            )
            + "\n",
        )
        self.assertEqual(
            self.lines(),
            [
                f"GOLDEN-UPDATED f.json:b {'2' * 16} -> {'3' * 16}",
                f"GOLDEN-UPDATED f.json:c (absent) -> {'4' * 16}",
            ],
        )

    def test_write_json_of_the_same_object_leaves_the_file(self):
        path = self.dir / "f.json"
        path.write_text(json.dumps({"a": "x"}, indent=1, sort_keys=True) + "\n")
        os.utime(path, (1, 1))
        self.assertEqual(
            gu.write_json(path, {"a": "x"}, "f.json", indent=1, env=self.env), 0
        )
        self.assertEqual(path.stat().st_mtime, 1)

    def test_parallel_key_updates_all_land(self):
        path = self.dir / "tree" / "songs.json"
        threads = [
            threading.Thread(
                target=gu.update_json_key,
                args=(path, f"S{i}", {"sha256": str(i)}, "songs.json"),
                kwargs={"indent": 1, "env": self.env},
            )
            for i in range(16)
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(
            sorted(json.loads(path.read_text())), sorted(f"S{i}" for i in range(16))
        )
        self.assertEqual(len(self.lines()), 16)
        self.assertEqual([p.name for p in path.parent.iterdir()], ["songs.json"])
        self.assertEqual(len(list((self.dir / "locks").iterdir())), 1)


class RecordsTest(unittest.TestCase):
    HEADER = (
        "#pragma once\n"
        "inline constexpr int kA[] = {\n"
        "// clang-format off\n"
        "// GOLDEN-RECORDS BEGIN kA\n"
        "    1, // one\n"
        "// GOLDEN-RECORDS END kA\n"
        "// clang-format on\n"
        "};\n"
        "// GOLDEN-RECORDS BEGIN kB\n"
        "inline constexpr bool kB = false;\n"
        "// GOLDEN-RECORDS END kB\n"
    )

    def test_parse_keeps_indentation_and_order(self):
        got = gr.parse(
            [
                "GOLDEN-RECORD h.h kA     1, // one",
                "noise",
                "GOLDEN-RECORD h.h kA     2, // two",
            ]
        )
        self.assertEqual(got, {("h.h", "kA"): ["    1, // one", "    2, // two"]})

    def test_align_pads_code_before_trailing_comments(self):
        self.assertEqual(
            gr.align(
                ["    0x0ULL, // a", "    0x123456ULL, // b", "    // c", "    {1, 2},"]
            ),
            [
                "    0x0ULL,      // a",
                "    0x123456ULL, // b",
                "    // c",
                "    {1, 2},",
            ],
        )

    def test_rewrite_replaces_only_the_region(self):
        new, old = gr.rewrite(self.HEADER, "kA", ["    7, // seven"])
        self.assertEqual(old, ["    1, // one"])
        self.assertEqual(
            new, self.HEADER.replace("    1, // one\n", "    7, // seven\n")
        )

    def test_a_missing_marker_is_an_error(self):
        with self.assertRaisesRegex(ValueError, "kC"):
            gr.rewrite(self.HEADER, "kC", ["x"])

    def test_apply_reports_a_changed_region_and_leaves_an_unchanged_one(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        (root / "h.h").write_text(self.HEADER)
        lines = [
            "GOLDEN-RECORD h.h kA     1, // one",
            "GOLDEN-RECORD h.h kB inline constexpr bool kB = true;",
        ]
        updated = gr.apply(root, lines)
        self.assertEqual(len(updated), 1)
        self.assertTrue(
            updated[0].startswith("GOLDEN-UPDATED h.h:kB lines=1,sha256="), updated[0]
        )
        self.assertIn("inline constexpr bool kB = true;\n", (root / "h.h").read_text())
        self.assertEqual(gr.apply(root, lines), [])


def _append_lines(log: str, n: int, tag: str) -> None:
    env = {gu.SWITCH: "1", gu.LOG: log}
    for i in range(n):
        gu.report(f"{tag}-{i}", "a", "b", env=env)


class ExactOutputTest(unittest.TestCase):
    def setUp(self):
        self.dir = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.dir, True)
        self.log = self.dir / "log"
        self.env = _env(self.log)

    def test_a_bytes_change_logs_the_exact_line(self):
        path = self.dir / "a.bin"
        path.write_bytes(b"old")
        gu.write_bytes(path, b"new", "a.bin", env=self.env)
        old, new = gu.describe_bytes(b"old"), gu.describe_bytes(b"new")
        self.assertEqual(self.log.read_text(), f"GOLDEN-UPDATED a.bin {old} -> {new}\n")
        self.assertTrue(old.startswith("sha256=") and old.endswith(",bytes=3"))

    def test_a_text_change_logs_the_exact_line(self):
        path = self.dir / "a.txt"
        path.write_text("one\n")
        gu.write_text(path, "two\n", "a.txt", env=self.env)
        self.assertEqual(
            self.log.read_text(),
            f"GOLDEN-UPDATED a.txt {gu.describe_bytes(b'one\n')} -> {gu.describe_bytes(b'two\n')}\n",
        )

    def test_the_mode_survives_and_no_temp_file_is_left(self):
        path = self.dir / "a.sh"
        path.write_text("x")
        path.chmod(0o755)
        gu.write_text(path, "y", "a.sh", env=self.env)
        self.assertEqual(stat.S_IMODE(path.stat().st_mode), 0o755)
        self.assertEqual(sorted(p.name for p in self.dir.iterdir()), ["a.sh", "log"])

    def test_unchanged_json_with_other_formatting_is_left_and_silent(self):
        path = self.dir / "f.json"
        path.write_text('{"a":"x"}')
        os.utime(path, (1, 1))
        self.assertEqual(
            gu.write_json(path, {"a": "x"}, "f.json", indent=2, env=self.env), 0
        )
        self.assertEqual(path.read_text(), '{"a":"x"}')
        self.assertEqual(path.stat().st_mtime, 1)
        self.assertFalse(self.log.exists())

    def test_json_updates_without_a_log_are_refused(self):
        with contextlib.redirect_stderr(io.StringIO()), self.assertRaises(SystemExit):
            gu.write_json(self.dir / "f.json", {"a": 1}, "f.json", indent=1, env={})
        self.assertEqual(list(self.dir.iterdir()), [])

    def test_concurrent_processes_append_whole_lines(self):
        procs = [
            multiprocessing.Process(
                target=_append_lines, args=(str(self.log), 50, f"p{i}")
            )
            for i in range(6)
        ]
        for p in procs:
            p.start()
        for p in procs:
            p.join()
        want = sorted(
            f"GOLDEN-UPDATED p{i}-{k} a -> b" for i in range(6) for k in range(50)
        )
        self.assertEqual(sorted(self.log.read_text().splitlines()), want)


class RecordFileTest(unittest.TestCase):
    MULTI = (
        "// head \xe9\n"
        "// GOLDEN-RECORDS BEGIN kA\n"
        "    1,\n"
        "// GOLDEN-RECORDS END kA\n"
        "between\x0c\n\n"
        "// GOLDEN-RECORDS BEGIN kB\n"
        "    2,\n"
        "// GOLDEN-RECORDS END kB\n"
        "tail"
    )

    def test_only_the_regions_change_in_a_multi_region_header(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        (root / "h.h").write_bytes(self.MULTI.encode())
        gr.apply(root, ["GOLDEN-RECORD h.h kA     9,", "GOLDEN-RECORD h.h kB     2,"])
        self.assertEqual(
            (root / "h.h").read_bytes().decode(), self.MULTI.replace("    1,", "    9,")
        )

    def test_crlf_survives(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        crlf = self.MULTI.replace("\n", "\r\n")
        (root / "h.h").write_bytes(crlf.encode())
        updated = gr.apply(root, ["GOLDEN-RECORD h.h kA     9,"])
        self.assertEqual(len(updated), 1)
        self.assertEqual(
            (root / "h.h").read_bytes().decode(), crlf.replace("    1,", "    9,")
        )

    def test_an_unchanged_tree_logs_nothing_and_keeps_mtime(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        (root / "h.h").write_bytes(self.MULTI.encode())
        os.utime(root / "h.h", (1, 1))
        self.assertEqual(
            gr.apply(
                root, ["GOLDEN-RECORD h.h kA     1,", "GOLDEN-RECORD h.h kB     2,"]
            ),
            [],
        )
        self.assertEqual((root / "h.h").stat().st_mtime, 1)

    def test_a_duplicated_marker_is_an_error_naming_the_header(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        dup = self.MULTI + "\n// GOLDEN-RECORDS BEGIN kA\n"
        (root / "h.h").write_bytes(dup.encode())
        with self.assertRaisesRegex(ValueError, "h.h region kA"):
            gr.apply(root, ["GOLDEN-RECORD h.h kA     9,"])


class UpdateRefusalTest(unittest.TestCase):
    def test_names_alone_are_accepted(self):
        self.assertIsNone(tt.update_refusal(_args("--update", "a", "b")))
        self.assertIsNone(tt.update_refusal(_args("--update", "-j", "1", "a")))

    def test_refusals(self):
        for argv, why in (
            (["--update"], "name"),
            (["--update", "--tier", "pr", "a"], "--tier"),
            (["--update", "--level", "unit", "a"], "--level"),
            (["--update", "--prebuilt", "a"], "builds first"),
            (["--update", "-n", "a"], "builds first"),
            (["--update", "-j", "4", "a"], "one test at a time"),
            (["--update", "--list", "a"], "--list"),
            (["--update", "--capture", "x", "a"], "--capture"),
            (["--update", "--update-readme", "a"], "--update-readme"),
            (["--update", "--skip-checks", "a"], "--skip-checks"),
        ):
            with self.subTest(argv):
                self.assertIn(why, tt.update_refusal(_args(*argv)) or "")

    def test_an_entry_without_a_baseline_is_refused(self):
        why = tt.owners_refusal(
            [_script("a", "pass"), _script("b", "pass", baseline=())]
        )
        self.assertIn("b", why)
        self.assertNotIn("a,", why)
        self.assertIsNone(tt.owners_refusal([_script("a", "pass")]))


class UpdateEnvironmentTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def test_no_test_inherits_the_switch(self):
        with mock.patch.dict(os.environ, {gu.SWITCH: "1", gu.LOG: "/x"}):
            env = tt._environment(self.ROOT, _script("t", "pass"), {})
        self.assertNotIn(gu.SWITCH, env)
        self.assertNotIn(gu.LOG, env)

    def test_extra_env_reaches_the_run(self):
        code = "import os; print(os.environ.get('DELUGE_GOLDEN_UPDATE'))"
        with tempfile.TemporaryFile("w+") as log:
            tt._run_one(
                self.ROOT,
                _script("t", code),
                [],
                False,
                None,
                log,
                {},
                extra_env={gu.SWITCH: "1"},
            )
            tt._run_one(self.ROOT, _script("t", code), [], False, None, log, {})
            log.seek(0)
            self.assertEqual(
                [l for l in log.read().splitlines() if not l.startswith("==>")],
                ["1", "None"],
            )


class UpdateOneTest(unittest.TestCase):
    """`_update_one` on a scratch git worktree, with script entries standing in for owners."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, self.root, True)
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        (self.root / "base.txt").write_text("old\n")
        (self.root / "h.h").write_text(RecordsTest.HEADER)
        (self.root / "tracked.txt").write_text("committed\n")
        git = [
            "git",
            "-c",
            "user.name=t",
            "-c",
            "user.email=t@example.invalid",
            "-c",
            "commit.gpgsign=false",
        ]
        subprocess.run(["git", "add", "."], cwd=self.root, check=True)
        subprocess.run(
            [*git, "commit", "-q", "--no-verify", "-m", "x"], cwd=self.root, check=True
        )

    def _update(self, code, baseline=("base.txt",)):
        with tempfile.TemporaryFile("w+") as out, mock.patch.object(tt, "OUT", out):
            before = tt._update_one(self.root, _script("t", code, baseline), [], {})
            out.seek(0)
            return before, out.read()

    LOGS = "import os, pathlib; log = open(os.environ['DELUGE_GOLDEN_UPDATE_LOG'], 'a'); root = pathlib.Path.cwd()\n"

    def test_nothing_changed_says_none(self):
        before, out = self._update("pass")
        self.assertIsNotNone(before)
        self.assertIn("GOLDEN-UPDATED none\n", out)

    def test_a_reported_change_is_printed_relative_to_the_repository(self):
        code = self.LOGS + (
            "(root / 'base.txt').write_text('new\\n')\n"
            "log.write(f'GOLDEN-UPDATED {root}/base.txt a -> b\\n')\n"
        )
        before, out = self._update(code)
        self.assertIsNotNone(before)
        self.assertIn("GOLDEN-UPDATED base.txt a -> b\n", out)
        self.assertEqual((self.root / "base.txt").read_text(), "new\n")

    def test_a_failed_run_restores_the_baseline(self):
        before, out = self._update(
            "import pathlib, sys; pathlib.Path('base.txt').write_text('x'); sys.exit(1)"
        )
        self.assertIsNone(before)
        self.assertIn("GOLDEN-UPDATE-REVERTED base.txt", out)
        self.assertEqual((self.root / "base.txt").read_text(), "old\n")

    def test_an_unreported_change_fails_and_is_restored(self):
        before, out = self._update(
            "import pathlib; pathlib.Path('base.txt').write_text('x')"
        )
        self.assertIsNone(before)
        self.assertIn("no GOLDEN-UPDATED line says so", out)
        self.assertEqual((self.root / "base.txt").read_text(), "old\n")

    def test_a_write_outside_the_baseline_fails(self):
        before, out = self._update(
            "import pathlib; pathlib.Path('other.txt').write_text('x')"
        )
        self.assertIsNone(before)
        self.assertIn("other.txt changed, and it is not among t's baselines", out)
        self.assertIn("GOLDEN-UPDATE-REVERTED other.txt", out)
        self.assertFalse((self.root / "other.txt").exists())

    def test_a_new_file_in_a_new_directory_is_removed_with_it(self):
        before, out = self._update(
            "import pathlib; pathlib.Path('d/e').mkdir(parents=True); pathlib.Path('d/e/f').write_text('x')"
        )
        self.assertIsNone(before)
        self.assertIn("GOLDEN-UPDATE-REVERTED d/e/f", out)
        self.assertFalse((self.root / "d").exists())

    def test_a_clean_tracked_file_written_outside_the_baseline_takes_its_committed_content(
        self,
    ):
        before, out = self._update(
            "import pathlib; pathlib.Path('tracked.txt').write_text('x')"
        )
        self.assertIsNone(before)
        self.assertIn("GOLDEN-UPDATE-REVERTED tracked.txt", out)
        self.assertEqual((self.root / "tracked.txt").read_text(), "committed\n")

    def test_a_clean_tracked_file_deleted_outside_the_baseline_comes_back(self):
        before, out = self._update(
            "import pathlib; pathlib.Path('tracked.txt').unlink()"
        )
        self.assertIsNone(before)
        self.assertIn("GOLDEN-UPDATE-REVERTED tracked.txt", out)
        self.assertEqual((self.root / "tracked.txt").read_text(), "committed\n")

    def test_a_change_of_the_callers_own_is_left(self):
        (self.root / "tracked.txt").write_text("mine\n")
        (self.root / "notes.txt").write_text("mine\n")
        code = "import pathlib; pathlib.Path('tracked.txt').write_text('run'); pathlib.Path('notes.txt').write_text('run')"
        before, out = self._update(code)
        self.assertIsNone(before)
        self.assertIn("GOLDEN-UPDATE-LEFT notes.txt", out)
        self.assertIn("GOLDEN-UPDATE-LEFT tracked.txt", out)
        self.assertEqual((self.root / "tracked.txt").read_text(), "run")
        self.assertEqual((self.root / "notes.txt").read_text(), "run")

    def test_a_failed_run_discards_its_lines(self):
        code = self.LOGS + (
            "(root / 'base.txt').write_text('new\\n')\n"
            "log.write(f'GOLDEN-UPDATED {root}/base.txt a -> b\\n')\n"
            "raise SystemExit(1)\n"
        )
        before, out = self._update(code)
        self.assertIsNone(before)
        self.assertIn("GOLDEN-UPDATE-DISCARDED base.txt a -> b\n", out)
        self.assertNotRegex(out, r"(?m)^GOLDEN-UPDATED")
        self.assertEqual((self.root / "base.txt").read_text(), "old\n")

    def test_an_interrupt_mid_write_puts_everything_back(self):
        def interrupted(root, *_args, **_kwargs):
            (root / "base.txt").write_text("ha")
            (root / "other.txt").write_text("x")
            raise KeyboardInterrupt

        with (
            mock.patch.object(tt, "_run_one", interrupted),
            self.assertRaises(KeyboardInterrupt),
        ):
            self._update("pass")
        self.assertEqual((self.root / "base.txt").read_text(), "old\n")
        self.assertFalse((self.root / "other.txt").exists())

    def test_an_interrupt_whose_restore_fails_raises_the_interrupt_and_restores_the_rest(
        self,
    ):
        def interrupted(root, *_args, **_kwargs):
            (root / "base.txt").write_text("ha")
            (root / "other.txt").write_text("x")
            raise KeyboardInterrupt

        broken = OSError("disk gone")
        with (
            mock.patch.object(tt, "_run_one", interrupted),
            mock.patch.object(tt, "restore", side_effect=broken),
            tempfile.TemporaryFile("w+") as out,
            mock.patch.object(tt, "OUT", out),
        ):
            with self.assertRaises(KeyboardInterrupt) as cm:
                tt._update_one(self.root, _script("t", "pass"), [], {})
            out.seek(0)
            text = out.read()
        self.assertIs(cm.exception.__cause__, broken)
        self.assertIn("GOLDEN-UPDATE-UNRESTORED t: base.txt: disk gone", text)
        self.assertIn("GOLDEN-UPDATE-REVERTED other.txt", text)
        self.assertFalse((self.root / "other.txt").exists())

    def test_a_directory_that_existed_empty_survives_the_put_back(self):
        (self.root / "keep").mkdir()
        before, out = self._update(
            "import pathlib; pathlib.Path('keep/f').write_text('x'); pathlib.Path('n/m').mkdir(parents=True);"
            " pathlib.Path('n/m/f').write_text('x')"
        )
        self.assertIsNone(before)
        self.assertIn("GOLDEN-UPDATE-REVERTED keep/f", out)
        self.assertTrue((self.root / "keep").is_dir())
        self.assertFalse((self.root / "keep" / "f").exists())
        self.assertFalse((self.root / "n").exists())

    def test_worktree_state_names_a_rename_by_its_new_path(self):
        subprocess.run(
            ["git", "mv", "tracked.txt", "moved.txt"], cwd=self.root, check=True
        )
        state = tt.worktree_state(self.root)
        self.assertIn("moved.txt", state)
        self.assertNotIn("tracked.txt", state)

    def test_record_lines_rewrite_the_header(self):
        code = self.LOGS + "log.write('GOLDEN-RECORD h.h kA     9, // nine\\n')\n"
        before, out = self._update(code, baseline=("h.h",))
        self.assertIsNotNone(before)
        self.assertIn("    9, // nine\n", (self.root / "h.h").read_text())
        self.assertIn("GOLDEN-UPDATED h.h:kA ", out)

    def test_a_record_line_for_another_header_is_refused(self):
        code = self.LOGS + "log.write('GOLDEN-RECORD h.h kA     9, // nine\\n')\n"
        before, out = self._update(code)
        self.assertIsNone(before)
        self.assertIn("h.h is not among t's baselines", out)
        self.assertNotIn("nine", (self.root / "h.h").read_text())


class MainTest(unittest.TestCase):
    def test_a_callers_switch_is_stripped_and_reported(self):
        with mock.patch.dict(os.environ, {gu.SWITCH: "1", gu.LOG: "/x"}):
            err = io.StringIO()
            with contextlib.redirect_stderr(err):
                rc = tt.main(["--update"])
            self.assertNotIn(gu.SWITCH, os.environ)
            self.assertNotIn(gu.LOG, os.environ)
        self.assertEqual(rc, 2)
        self.assertIn(
            f"test: {gu.SWITCH} in the environment is ignored", err.getvalue()
        )
        self.assertIn(f"test: {gu.LOG} in the environment is ignored", err.getvalue())


class CmdUpdateTest(unittest.TestCase):
    def test_a_baseline_that_fails_its_second_run_is_restored(self):
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        (root / "base.txt").write_text("old\n")
        # Records a value no second run accepts: the stand-in for a render that differs run to run.
        code = (
            "import os, pathlib, sys\n"
            "p = pathlib.Path('base.txt')\n"
            "if os.environ.get('DELUGE_GOLDEN_UPDATE') == '1':\n"
            "    p.write_text('new\\n')\n"
            "    open(os.environ['DELUGE_GOLDEN_UPDATE_LOG'], 'a').write(f'GOLDEN-UPDATED {pathlib.Path.cwd()}/base.txt a -> b\\n')\n"
            "else:\n"
            "    sys.exit(0 if p.read_text() == 'never\\n' else 1)\n"
        )
        with (
            tempfile.TemporaryFile("w+") as out,
            mock.patch.object(tt, "OUT", out),
            mock.patch.object(tt.tb, "products", return_value={}),
        ):
            rc = tt.cmd_update(root, [_script("t", code)], [])
            out.seek(0)
            text = out.read()
        self.assertEqual(rc, 1)
        self.assertIn("GOLDEN-UPDATE-REVERTED base.txt", text)
        self.assertEqual((root / "base.txt").read_text(), "old\n")

    @staticmethod
    def _recorder(rel):
        """Records `rel` as 'new' under the switch; otherwise passes."""
        return (
            "import os, pathlib\n"
            "if os.environ.get('DELUGE_GOLDEN_UPDATE') == '1':\n"
            f"    pathlib.Path('{rel}').write_text('new\\n')\n"
            "    open(os.environ['DELUGE_GOLDEN_UPDATE_LOG'], 'a').write("
            f"f'GOLDEN-UPDATED {{pathlib.Path.cwd()}}/{rel} a -> b\\n')\n"
        )

    def _interrupted_update(self, names, interrupt):
        """cmd_update over one recorder per name (each owning <name>.txt), raising KeyboardInterrupt from the run
        `interrupt(h, extra_env)` picks. Returns the root."""
        root = Path(tempfile.mkdtemp())
        self.addCleanup(shutil.rmtree, root, True)
        subprocess.run(["git", "init", "-q"], cwd=root, check=True)
        for name in names:
            (root / f"{name}.txt").write_text("old\n")
        real = tt._run_one

        def run_one(root_, h, *args, extra_env=None, **kwargs):
            if interrupt(h, extra_env):
                raise KeyboardInterrupt
            return real(root_, h, *args, extra_env=extra_env, **kwargs)

        selection = [
            _script(n, self._recorder(f"{n}.txt"), (f"{n}.txt",)) for n in names
        ]
        with (
            tempfile.TemporaryFile("w+") as out,
            mock.patch.object(tt, "OUT", out),
            mock.patch.object(tt.tb, "products", return_value={}),
            mock.patch.object(tt, "_run_one", run_one),
            self.assertRaises(KeyboardInterrupt),
        ):
            tt.cmd_update(root, selection, [])
        return root

    def test_an_interrupt_in_a_later_update_puts_back_the_earlier_unconfirmed_ones(
        self,
    ):
        root = self._interrupted_update(
            ["a", "b"], lambda h, extra: h.name == "b" and extra
        )
        self.assertEqual((root / "a.txt").read_text(), "old\n")
        self.assertEqual((root / "b.txt").read_text(), "old\n")

    def test_an_interrupt_in_a_confirming_run_puts_back_only_the_unconfirmed(self):
        root = self._interrupted_update(
            ["a", "b", "c"], lambda h, extra: h.name == "b" and not extra
        )
        self.assertEqual((root / "a.txt").read_text(), "new\n")
        self.assertEqual((root / "b.txt").read_text(), "old\n")
        self.assertEqual((root / "c.txt").read_text(), "old\n")


if __name__ == "__main__":
    unittest.main()
