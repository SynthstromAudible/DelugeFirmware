"""rig_lock: one holder at a time, the second refused with the holder's name or waiting, and a holder that exits, dies
or falls silent letting go. The Pi's script runs in a local bash, where the test's own ssh_argv sends it.

Gate rig-lock:
    one run at a time holds the rig, every tool that opens it is refused or waits, and a holder that exits, is killed
    or falls silent lets go
"""

import contextlib
import io
import os
import signal
import subprocess
import sys
import tempfile
import textwrap
import threading
import time
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parent))
import deluge_jlink
import device_check
import device_parity
import dsp_bench
import rig
import rig_lock
import stream_bench
import stream_bench_matrix
from rig_session import DeviceSession

sys.path.append(str(Path(__file__).resolve().parent / "deluge_trace"))
import deluge_trace

HOST = "pi@probe-host"


class RigLockCase(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        for target in (
            mock.patch.object(rig_lock, "LOCK", os.path.join(self.dir.name, "lock")),
            mock.patch.object(rig_lock, "ssh_argv", lambda _host, s: ["bash", "-c", s]),
            mock.patch.object(rig_lock, "HEARTBEAT_S", 0.1),
            mock.patch.dict(os.environ),
        ):
            target.start()
            self.addCleanup(target.stop)
        for name in (rig_lock.HELD_ENV, rig_lock.WAIT_ENV, "DELUGE_PROBE_HOST"):
            os.environ.pop(name, None)

    def other(self, body: str) -> subprocess.Popen:
        """Another process that has the lock as its own: `body` runs in it, its prints on stdout."""
        code = textwrap.dedent(
            f"""
            import os, sys, time
            sys.path.insert(0, {str(Path(rig_lock.__file__).parent)!r})
            import rig_lock
            rig_lock.LOCK = {rig_lock.LOCK!r}
            rig_lock.HEARTBEAT_S = 0.1
            rig_lock.SILENCE_S = {rig_lock.SILENCE_S!r}
            rig_lock.ssh_argv = lambda _host, s: ["bash", "-c", s]
            """
        ) + textwrap.dedent(body)
        return subprocess.Popen(
            [sys.executable, "-c", code], stdout=subprocess.PIPE, text=True
        )

    def holding(self) -> subprocess.Popen:
        proc = self.other(
            f"""
            with rig_lock.hold({HOST!r}, "a long bench"):
                print("in", flush=True)
                time.sleep(60)
            """
        )
        self.addCleanup(proc.stdout.close)
        self.addCleanup(proc.wait)
        self.addCleanup(proc.kill)
        self.assertEqual(proc.stdout.readline(), "in\n")
        return proc

    def free_within(self, seconds: float) -> bool:
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            if rig_lock.status(HOST) is None:
                return True
            time.sleep(0.05)
        return False


class Holding(RigLockCase):
    def test_the_rig_is_free_before_and_after_and_held_between(self):
        self.assertIsNone(rig_lock.status(HOST))
        with rig_lock.hold(HOST, "a bench"):
            self.assertIn("a bench", rig_lock.status(HOST))
            self.assertIn(f"pid {os.getpid()}", rig_lock.status(HOST))
        self.assertIsNone(rig_lock.status(HOST))

    def test_a_second_run_is_refused_and_told_who_holds_the_rig(self):
        proc = self.holding()
        with (
            self.assertRaisesRegex(
                rig_lock.RigBusy, rf"pid {proc.pid}: a long bench since 20"
            ),
            rig_lock.hold(HOST, "mine"),
        ):
            self.fail("held twice")
        self.assertNotIn(rig_lock.HELD_ENV, os.environ)

    def test_a_refusal_is_not_a_link_failure_that_a_tool_would_retry(self):
        self.assertFalse(issubclass(rig_lock.RigBusy, (ConnectionError, TimeoutError)))

    def test_a_waiting_run_starts_when_the_rig_comes_free(self):
        proc = self.holding()
        threading.Timer(0.5, proc.kill).start()
        err = io.StringIO()
        with contextlib.redirect_stderr(err), rig_lock.hold(HOST, "mine", wait=True):
            self.assertIn("mine", rig_lock.status(HOST))
        self.assertIn("waiting for", err.getvalue())

    def test_the_environment_can_ask_to_wait(self):
        proc = self.holding()
        threading.Timer(0.3, proc.kill).start()
        os.environ[rig_lock.WAIT_ENV] = "1"
        with contextlib.redirect_stderr(io.StringIO()), rig_lock.hold(HOST, "mine"):
            self.assertIn("mine", rig_lock.status(HOST))

    def test_a_killed_holder_lets_go(self):
        self.holding().send_signal(signal.SIGKILL)
        self.assertTrue(self.free_within(5))

    def test_a_silent_holder_lets_go(self):
        with mock.patch.object(rig_lock, "SILENCE_S", 1):
            proc = self.holding()
        proc.send_signal(signal.SIGSTOP)
        try:
            self.assertTrue(self.free_within(10))
        finally:
            proc.send_signal(signal.SIGCONT)

    def test_a_beating_holder_outlives_the_silence_limit(self):
        with (
            mock.patch.object(rig_lock, "SILENCE_S", 1),
            rig_lock.hold(HOST, "a bench"),
        ):
            time.sleep(2.5)
            self.assertIn("a bench", rig_lock.status(HOST))

    def test_an_error_in_the_block_lets_go(self):
        with self.assertRaises(ValueError), rig_lock.hold(HOST, "a bench"):
            raise ValueError
        self.assertIsNone(rig_lock.status(HOST))

    def test_a_tool_run_by_the_holder_does_not_take_the_lock_again(self):
        with rig_lock.hold(HOST, "outer"):
            with rig_lock.hold(HOST, "inner"):
                self.assertIn("outer", rig_lock.status(HOST))
            child = self.other("with rig_lock.hold('probe-host', 'child'): print('in')")
            self.assertEqual(child.communicate(timeout=20)[0], "in\n")
            self.assertIn("outer", rig_lock.status(HOST))

    def test_a_lost_connection_is_reported(self):
        err = io.StringIO()
        with contextlib.redirect_stderr(err), rig_lock.hold(HOST, "a bench"):
            # What holds the lock dies: the link to the Pi going
            subprocess.run(["pkill", "-9", "-f", rig_lock.LOCK], check=False)
            time.sleep(0.5)
        self.assertIn("was lost", err.getvalue())

    def test_a_pi_that_does_not_answer_is_an_error_not_a_free_rig(self):
        def unreachable(*_):
            return ["bash", "-c", "echo no route; exit 255"]

        with mock.patch.object(rig_lock, "ssh_argv", unreachable):
            with (
                self.assertRaisesRegex(rig_lock.RigLockError, "no route"),
                rig_lock.hold(HOST, "a bench"),
            ):
                self.fail("held")
            with self.assertRaises(rig_lock.RigLockError):
                rig_lock.status(HOST)


class Tools(RigLockCase):
    def test_a_local_probe_needs_no_lock(self):
        for select in (None, "", "USB=123"):
            self.assertIsNone(rig_lock.host_of(select))

    def test_a_remote_probe_is_locked_on_its_pi(self):
        self.assertEqual(rig_lock.host_of("IP=pi-top"), "pi@pi-top")
        self.assertEqual(rig_lock.host_of("IP=pi-top:19020"), "pi@pi-top")
        os.environ["DELUGE_PROBE_HOST"] = "kit@pi-top"
        self.assertEqual(rig_lock.host_of("IP=pi-top"), "kit@pi-top")
        self.assertEqual(rig_lock.host_of("IP=other"), "pi@other")

    def test_a_tool_is_refused_with_the_busy_exit_and_runs_nothing(self):
        self.holding()
        err = io.StringIO()
        with contextlib.redirect_stderr(err):
            code = rig_lock.guarded(HOST, "mine", False, lambda: self.fail("ran"))
        self.assertEqual(code, rig_lock.EXIT_BUSY)
        self.assertIn("a long bench", err.getvalue())

    def refused(self, main, *argv):
        """`main(argv)` is refused with the busy exit, having run no command and opened no session."""
        with (
            mock.patch.object(subprocess, "run", side_effect=AssertionError("ran")),
            mock.patch.object(rig, "ssh", side_effect=AssertionError("ran")),
            contextlib.redirect_stderr(io.StringIO()) as err,
        ):
            self.assertEqual(main(list(argv)), rig_lock.EXIT_BUSY)
        self.assertIn("a long bench", err.getvalue())

    def test_every_tool_that_opens_the_rig_is_refused(self):
        self.holding()
        elf = Path(self.dir.name) / "deluge.elf"
        elf.write_bytes(b"")
        self.refused(dsp_bench.main, "--elf", str(elf), "--select", "IP=probe-host")
        self.refused(stream_bench.main, "--elf", str(elf), "--select", "IP=probe-host")
        self.refused(
            stream_bench_matrix.main,
            *("run", "--image", f"a={elf}", "--host", HOST, "--out", self.dir.name),
        )
        with mock.patch.object(
            device_check,
            "symbols",
            return_value={"program_stack_start": 0, "program_stack_end": 64},
        ):
            self.refused(device_check.main, str(elf), "--host", HOST)
        self.refused(
            device_parity.main,
            "--device",
            str(elf),
            "--record-only",
            "--host",
            HOST,
            "a.txt",
        )
        script = Path(self.dir.name) / "a.JLinkScript"
        script.write_text("")
        with (
            mock.patch.object(deluge_trace, "ranges_for", return_value=[("f", 0, 4)]),
            mock.patch.object(
                deluge_trace, "run_dir", return_value=Path(self.dir.name)
            ),
        ):
            self.refused(
                deluge_trace.main,
                *("run", "--elf", str(elf), "--function", "f", "--script", str(script)),
                *("--host", "probe-host", "--out", self.dir.name),
            )
        with mock.patch.object(
            sys, "argv", ["deluge_jlink", str(elf), "--select", "IP=probe-host"]
        ):
            self.refused(lambda _: deluge_jlink.main())

    def test_a_session_holds_the_rig_until_it_closes(self):
        session = object.__new__(DeviceSession)
        session.log, session._out, session._host = [], Path(self.dir.name), HOST
        session._target, session.park_on_close = mock.Mock(), False
        with mock.patch.object(DeviceSession, "_launch", return_value=session):
            DeviceSession.launch(Path("deluge.elf"), HOST, Path(self.dir.name))
        self.assertIn("rig_session deluge.elf", rig_lock.status(HOST))
        session.close()
        self.assertIsNone(rig_lock.status(HOST))
        session.close()

    def test_a_session_that_does_not_launch_lets_go(self):
        with (
            mock.patch.object(
                DeviceSession, "_launch", side_effect=ConnectionError("link")
            ),
            self.assertRaises(ConnectionError),
        ):
            DeviceSession.launch(Path("deluge.elf"), HOST, Path(self.dir.name))
        self.assertIsNone(rig_lock.status(HOST))

    def test_rig_py_refuses_every_action_while_the_rig_is_held(self):
        self.holding()
        for action in ("restart", "park", "stop"):
            with (
                mock.patch.object(rig, action, side_effect=AssertionError("ran")),
                contextlib.redirect_stderr(io.StringIO()),
            ):
                self.assertEqual(rig.main([action, "--host", HOST]), rig_lock.EXIT_BUSY)

    def test_rig_py_status_says_who_holds_the_rig(self):
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(rig.main(["status", "--host", HOST]), 0)
        self.assertIn("free", out.getvalue())
        self.holding()
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(rig.main(["status", "--host", HOST]), rig_lock.EXIT_BUSY)
        self.assertIn("a long bench", out.getvalue())


if __name__ == "__main__":
    unittest.main()
