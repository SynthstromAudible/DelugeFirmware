#! /usr/bin/env python3
"""Unit tests for run_scratch.py: a `./dbt test` run's temporary directory and the sweep of dead runs' ones.

Run directly with:
    python3 scripts/tasks/test_run_scratch.py
"""

import io
import multiprocessing.util
import os
import signal
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))

import run_scratch as rs


def setUpModule():
    # run_directory makes multiprocessing's temporary directory if it is not made yet; made first, it is not inside a
    # test's directory, which goes before multiprocessing removes its own at exit.
    multiprocessing.util.get_temp_dir()


def _dead_pid() -> int:
    """A pid that has just exited (and so, briefly, names no process)."""
    child = subprocess.Popen([sys.executable, "-c", "pass"])
    child.wait()
    return child.pid


class RunScratchTest(unittest.TestCase):
    def setUp(self):
        self._base = tempfile.TemporaryDirectory()
        self.base = Path(self._base.name)
        patcher = mock.patch.object(tempfile, "tempdir", str(self.base))
        patcher.start()
        self.addCleanup(patcher.stop)
        self.addCleanup(self._base.cleanup)
        env = mock.patch.dict(os.environ, {"TMPDIR": str(self.base)})
        env.start()
        self.addCleanup(env.stop)

    def test_the_run_and_its_children_write_into_its_directory_and_it_goes_at_the_end(
        self,
    ):
        with rs.run_directory(io.StringIO()) as path:
            self.assertEqual(path.parent, self.base)
            self.assertTrue(path.name.startswith(f"{rs.PREFIX}{os.getpid()}."))
            mine = Path(tempfile.mkdtemp())
            child = subprocess.run(
                [sys.executable, "-c", "import tempfile; print(tempfile.mkdtemp())"],
                capture_output=True,
                text=True,
                check=True,
            )
            self.assertEqual(mine.parent, path)
            self.assertEqual(Path(child.stdout.strip()).parent, path)
        self.assertFalse(path.exists())
        self.assertEqual(os.environ["TMPDIR"], str(self.base))
        self.assertEqual(tempfile.gettempdir(), str(self.base))

    def test_an_exception_still_removes_the_directory(self):
        with (
            self.assertRaises(RuntimeError),
            rs.run_directory(io.StringIO()) as path,
        ):
            (path / "f").write_text("x")
            raise RuntimeError
        self.assertFalse(path.exists())

    def test_sigterm_ends_the_run_through_its_finally(self):
        with (
            self.assertRaises(SystemExit) as caught,
            rs.run_directory(io.StringIO()) as path,
        ):
            os.kill(os.getpid(), signal.SIGTERM)
        self.assertEqual(caught.exception.code, 128 + signal.SIGTERM)
        self.assertFalse(path.exists())
        self.assertIs(signal.getsignal(signal.SIGTERM), signal.SIG_DFL)

    def test_the_kept_root_is_the_system_temp_directory_not_the_runs(self):
        self.assertEqual(rs.kept_root(), self.base)
        with rs.run_directory(io.StringIO()):
            self.assertEqual(rs.kept_root(), self.base)
        self.assertEqual(rs.kept_root(), self.base)

    def test_the_sweep_removes_only_dead_runs_directories(self):
        dead = self.base / f"{rs.PREFIX}{_dead_pid()}.abc"
        (dead / "sub").mkdir(parents=True)
        live = self.base / f"{rs.PREFIX}{os.getppid()}.abc"
        live.mkdir()
        other = self.base / "golden-synth.abc"
        other.mkdir()
        not_a_dir = self.base / f"{rs.PREFIX}{_dead_pid()}.file"
        not_a_dir.write_text("x")
        removed = rs.sweep(self.base, io.StringIO())
        self.assertEqual(removed, [dead])
        self.assertFalse(dead.exists())
        for kept in (live, other, not_a_dir):
            self.assertTrue(kept.exists(), kept)

    def test_a_run_sweeps_before_it_starts(self):
        dead = self.base / f"{rs.PREFIX}{_dead_pid()}.abc"
        dead.mkdir()
        with rs.run_directory(io.StringIO()):
            self.assertFalse(dead.exists())

    def test_a_directory_that_cannot_be_removed_is_reported(self):
        dead = self.base / f"{rs.PREFIX}{_dead_pid()}.abc"
        dead.mkdir()
        log = io.StringIO()
        with mock.patch.object(rs.shutil, "rmtree"):
            self.assertEqual(rs.sweep(self.base, log), [])
        self.assertIn(str(dead), log.getvalue())


if __name__ == "__main__":
    unittest.main()
