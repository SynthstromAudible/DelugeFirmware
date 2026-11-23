"""One temporary directory per `./dbt test` run, removed when the run ends.

Every temporary file the run makes lands in it: the runner's own (`tempfile` in this process) and its children's (the
gate binaries, ctest, the Python suites, through `TMPDIR`). A test or gate that forgets its own clean-up, or is killed
before it gets there, leaves nothing behind once the run's directory goes.

A run killed outright (SIGKILL, a memory-cap kill) cannot remove its directory, so each run first removes those of
runs whose process is gone. The directory is named `dbt-test.<pid>.<random>` in the system temp directory; only this
user's directories whose pid no longer runs are removed, so concurrent runs in other clones are left alone.
"""

import contextlib
import multiprocessing.util
import os
import re
import shutil
import signal
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path

PREFIX = "dbt-test."
_NAME = re.compile(rf"^{re.escape(PREFIX)}(\d+)\.")

# The system temp directory as it was before the run's took over: where what a run keeps on purpose goes (see
# kept_root). None outside a run.
_SYSTEM_TMP: Path | None = None


def kept_root() -> Path:
    """Where to put what must outlive the run (a copy kept for the user to read): the system temp directory during a
    run, else wherever `tempfile` points."""
    return _SYSTEM_TMP if _SYSTEM_TMP is not None else Path(tempfile.gettempdir())


def _alive(pid: int) -> bool:
    try:
        os.kill(pid, 0)
    except ProcessLookupError:
        return False
    except PermissionError:
        return True
    return True


def sweep(base: Path, log=sys.stderr) -> list[Path]:
    """Remove the run directories in `base` left by runs that are no longer running. Returns those removed; one that
    cannot be removed is reported to `log`."""
    removed: list[Path] = []
    uid = os.getuid()
    try:
        entries = list(base.iterdir())
    except OSError:
        return removed
    for path in entries:
        match = _NAME.match(path.name)
        if match is None or int(match.group(1)) == os.getpid():
            continue
        try:
            st = path.lstat()
        except OSError:
            continue
        if st.st_uid != uid or not path.is_dir() or path.is_symlink():
            continue
        if _alive(int(match.group(1))):
            continue
        shutil.rmtree(path, ignore_errors=True)
        if path.exists():
            print(f"test: could not remove {path}, left by an earlier run", file=log)
        else:
            removed.append(path)
    return removed


def _raise_exit(signum, _frame):
    # SIGTERM and SIGHUP end the run as Ctrl-C does, so each `finally` and the directory's removal still run.
    raise SystemExit(128 + signum)


@contextlib.contextmanager
def run_directory(log=sys.stderr) -> Iterator[Path]:
    """Make the run's directory, point `tempfile` and `TMPDIR` at it, and remove it at the end, however the run ends
    short of SIGKILL. Earlier runs' directories are swept first."""
    global _SYSTEM_TMP
    system = Path(tempfile.gettempdir())
    sweep(system, log)
    # multiprocessing removes its own temporary directory at interpreter exit, after this one has gone: made now, it
    # sits in the system temp directory instead.
    multiprocessing.util.get_temp_dir()
    path = Path(tempfile.mkdtemp(prefix=f"{PREFIX}{os.getpid()}.", dir=system))
    saved_env = os.environ.get("TMPDIR")
    saved_tempdir = tempfile.tempdir
    saved_handlers = {}
    for signum in (signal.SIGTERM, signal.SIGHUP):
        with contextlib.suppress(ValueError):  # not the main thread
            saved_handlers[signum] = signal.signal(signum, _raise_exit)
    os.environ["TMPDIR"] = str(path)
    tempfile.tempdir = str(path)
    _SYSTEM_TMP = system
    try:
        yield path
    finally:
        _SYSTEM_TMP = None
        tempfile.tempdir = saved_tempdir
        if saved_env is None:
            os.environ.pop("TMPDIR", None)
        else:
            os.environ["TMPDIR"] = saved_env
        for signum, handler in saved_handlers.items():
            signal.signal(signum, handler)
        shutil.rmtree(path, ignore_errors=True)
        if path.exists():
            print(
                f"test: could not remove the run's temporary directory {path}", file=log
            )
