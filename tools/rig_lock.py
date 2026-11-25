"""The rig's lock: one run at a time on the Deluge behind the probe's Pi.

Whatever opens the rig holds an flock on the Pi while it runs, so every checkout, session and machine sees it. The lock
is held by a script on the Pi that lives as long as an ssh connection from the holder: it ends when the holder exits or
dies, or when the holder has been silent for SILENCE_S, so nothing has to be cleaned up. Beside the lock, HOLDER says
who holds it, what for and since when.

A process that holds the lock names the Pi in $DELUGE_RIG_LOCK, so the tools it runs do not take it again.

    tools/rig.py status [--host HOST]    says whether the rig is free, and who holds it

A tool takes `--wait` (or $DELUGE_RIG_WAIT) to wait for the rig; without it a held rig is refused, exit EXIT_BUSY.
"""

from __future__ import annotations

import argparse
import contextlib
import getpass
import os
import shlex
import socket
import subprocess
import sys
import threading
from collections.abc import Callable, Iterator

EXIT_BUSY = os.EX_TEMPFAIL
HELD_ENV = "DELUGE_RIG_LOCK"
WAIT_ENV = "DELUGE_RIG_WAIT"
LOCK = "/tmp/deluge-rig.lock"
SILENCE_S = 30
HEARTBEAT_S = 5

# Run by bash on the Pi. Its standard input is the holder's ssh connection: a line every HEARTBEAT_S, its end when the
# holder goes. HOLDER is read only while the lock is held, so a stale one is never shown.
HOLD = """
exec 9>>{lock} || exit 1
if ! flock -n 9; then
  echo "BUSY $(cat {lock}.holder 2>/dev/null)"
  [ {wait} = 1 ] || exit 3
  flock 9 || exit 1
fi
echo {holder} "since $(date '+%Y-%m-%d %H:%M:%S')" > {lock}.holder
echo HELD
while read -r -t {silence} _; do :; done
rm -f {lock}.holder
"""
STATUS = """
exec 9>>{lock} || exit 1
if flock -n 9; then echo FREE; else echo "BUSY $(cat {lock}.holder 2>/dev/null)"; fi
"""


class RigBusy(RuntimeError):
    """Another run holds the rig; the message names it. Not a link failure: trying again at once is no use."""


class RigLockError(RuntimeError):
    """The lock could not be asked for: the Pi is out of reach, or has no flock."""


def address(host: str) -> str:
    """The Pi's address without the ssh user."""
    return host.rpartition("@")[2]


def host_of(select: str | None) -> str | None:
    """The ssh destination of the probe that J-Link's `-select` names, None for a probe on this machine. The user is
    $DELUGE_PROBE_HOST's when that is the same Pi, else pi."""
    kind, _, where = (select or "").partition("=")
    if kind.upper() != "IP" or not where:
        return None
    where = where.partition(":")[0]
    known = os.environ.get("DELUGE_PROBE_HOST", "")
    return known if address(known) == where else f"pi@{where}"


def ssh_argv(host: str, script: str) -> list[str]:
    return [
        "ssh",
        "-o", "BatchMode=yes",
        "-o", "ConnectTimeout=10",
        "-o", "ServerAliveInterval=10",
        "-o", "ServerAliveCountMax=3",
        host,
        f"bash -c {shlex.quote(script)}",
    ]  # fmt: skip


def holder_text(what: str) -> str:
    return f"{getpass.getuser()}@{socket.gethostname()} pid {os.getpid()}: {what}"


def status(host: str) -> str | None:
    """Who holds the rig, None when it is free; raises RigLockError when the Pi does not answer."""
    try:
        done = subprocess.run(
            ssh_argv(host, STATUS.format(lock=LOCK)),
            stdin=subprocess.DEVNULL,
            capture_output=True,
            text=True,
            timeout=30,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        raise RigLockError(f"{host} did not answer") from exc
    for line in done.stdout.splitlines():
        if line == "FREE":
            return None
        if line.startswith("BUSY"):
            return line[5:] or "an unnamed run"
    raise RigLockError(f"{host}: {done.stderr.strip()[-300:] or 'no answer'}")


class _Held:
    """The connection that holds the lock."""

    def __init__(self, host: str, what: str, wait: bool):
        script = HOLD.format(
            lock=LOCK,
            wait=int(wait),
            holder=shlex.quote(holder_text(what)),
            silence=SILENCE_S,
        )
        # Its own session: a Ctrl-C must not end the lock before the tool has put the rig back.
        self._proc = subprocess.Popen(
            ssh_argv(host, script),
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            start_new_session=True,
        )
        self._done = threading.Event()
        self._stop_beating = threading.Event()
        self._beat = threading.Thread(target=self._heartbeat, daemon=True)
        self._beat.start()
        try:
            self._await(host, wait)
        except BaseException:
            self.release()
            raise

    def _await(self, host: str, wait: bool) -> None:
        assert self._proc.stdout is not None
        said: list[str] = []
        for line in self._proc.stdout:
            line = line.rstrip("\n")
            if line == "HELD":
                return
            if line.startswith("BUSY"):
                who = line[5:] or "an unnamed run"
                if not wait:
                    raise RigBusy(f"the rig on {host} is in use: {who}")
                print(f"rig: waiting for {who}", file=sys.stderr, flush=True)
                continue
            said.append(line)
        raise RigLockError(
            f"cannot take the rig's lock on {host}: {' '.join(said)[-300:] or 'no answer'}"
        )

    def _heartbeat(self) -> None:
        assert self._proc.stdin is not None
        while not self._stop_beating.wait(HEARTBEAT_S):
            try:
                self._proc.stdin.write("\n")
                self._proc.stdin.flush()
            except (OSError, ValueError):
                return

    def lost(self) -> bool:
        """Whether the connection has ended, and the lock with it."""
        return self._proc.poll() is not None

    def release(self) -> None:
        self._stop_beating.set()
        self._beat.join()
        assert self._proc.stdin is not None
        with contextlib.suppress(OSError):
            self._proc.stdin.close()
        try:
            # Waited for, so that the next tool finds the rig free.
            self._proc.wait(10)
        except subprocess.TimeoutExpired:
            self._proc.kill()
            self._proc.wait()
        if self._proc.stdout is not None:
            self._proc.stdout.close()


@contextlib.contextmanager
def hold(host: str | None, what: str, wait: bool = False) -> Iterator[None]:
    """Holds the rig on `host` for the block: nothing to do for no host (a probe on this machine) or when this process
    or one that started it holds it already. Raises RigBusy when it is held and `wait` is false."""
    if host is None or os.environ.get(HELD_ENV) == address(host):
        yield
        return
    held = _Held(host, what, wait or os.environ.get(WAIT_ENV) == "1")
    os.environ[HELD_ENV] = address(host)
    try:
        yield
    finally:
        os.environ.pop(HELD_ENV, None)
        if held.lost():
            print(
                f"rig: the connection to {host} was lost: the run may not have had the rig to itself",
                file=sys.stderr,
            )
        held.release()


def guarded(host: str | None, what: str, wait: bool, run: Callable[[], int]) -> int:
    """`run()` while holding the rig, for a tool's main: EXIT_BUSY and who holds it when another run does."""
    try:
        with hold(host, what, wait):
            return run()
    except (RigBusy, RigLockError) as exc:
        print(f"rig: {exc}", file=sys.stderr)
        return EXIT_BUSY


def add_argument(ap: argparse.ArgumentParser) -> None:
    ap.add_argument(
        "--wait",
        action="store_true",
        help="wait for the rig when another run holds it (default: refuse)",
    )
