"""Re-recording a baseline: the switch, and the evidence each change leaves.

`./dbt test --update <name>...` builds, then runs each named test with DELUGE_GOLDEN_UPDATE=1 and
DELUGE_GOLDEN_UPDATE_LOG naming a fresh file. A test that owns a baseline (its registry entry lists it in `baseline`)
then writes what it would have compared with, through the writers here, and each write that changes something appends

    GOLDEN-UPDATED <file or file:key> <old> -> <new>

to the log; the runner prints the log after the run, and `GOLDEN-UPDATED none` when it is empty. Each description is
one word -- `(absent)` for an item that did not exist or no longer does -- so a line splits from the right whatever
spaces its item holds. C++ owners write the same lines through tests/support/golden_update.h.

A check reads the switch from its gate's environment (`updating(gate.env)`), never from this process's: the runner
sets it for the one test being recorded. DELUGE_GOLDEN_UPDATE=1 without the log is refused, because only the runner
sets the log, after building: a baseline recorded from a stale binary, or with no record of what changed, is what this
module exists to prevent.
"""

import fcntl
import hashlib
import json
import os
import sys
import tempfile
from collections.abc import Mapping
from pathlib import Path

SWITCH = "DELUGE_GOLDEN_UPDATE"
LOG = "DELUGE_GOLDEN_UPDATE_LOG"
TAG = "GOLDEN-UPDATED"
ABSENT = "(absent)"


def _env(env: Mapping[str, str] | None) -> Mapping[str, str]:
    return os.environ if env is None else env


def _refuse() -> None:
    print(
        f"{SWITCH}=1 is set by ./dbt test --update <name>, which builds first and collects what changed: run that",
        file=sys.stderr,
    )
    raise SystemExit(2)


def updating(env: Mapping[str, str] | None = None) -> bool:
    """Is this run re-recording? `env` is the gate's environment; None reads this process's (a runner or tool run as
    its own process). Exits with status 2 when the switch is set without the log."""
    env = _env(env)
    if env.get(SWITCH) != "1":
        return False
    if not env.get(LOG):
        _refuse()
    return True


def without(env: Mapping[str, str]) -> dict[str, str]:
    """`env` without the switch and the log: for a run inside an update that compares rather than records."""
    return {key: value for key, value in env.items() if key not in (SWITCH, LOG)}


def describe_bytes(data: bytes | None) -> str:
    """One word for a file's content: its SHA-256 prefix and size, or `(absent)`."""
    if data is None:
        return ABSENT
    return f"sha256={hashlib.sha256(data).hexdigest()[:16]},bytes={len(data)}"


def describe_value(value) -> str:
    """One word for a JSON value: a string as its first 16 characters (a hash's prefix), an object as its fields."""
    if value is None:
        return ABSENT
    if isinstance(value, str):
        return value[:16].replace(" ", "_")
    if isinstance(value, dict):
        return ",".join(f"{key}={describe_value(value[key])}" for key in sorted(value))
    return json.dumps(value, separators=(",", ":")).replace(" ", "_")


def _append(line: str, env: Mapping[str, str] | None) -> None:
    path = _env(env).get(LOG)
    if not path:
        _refuse()
    with open(path, "a") as log:
        fcntl.flock(log, fcntl.LOCK_EX)
        log.write(line + "\n")


def report(item: str, old: str, new: str, env: Mapping[str, str] | None = None) -> None:
    """Log that `item` went from `old` to `new` (both one word); nothing when they are equal. The writers call this
    after the atomic write, so a logged change is always on disk."""
    if old != new:
        _append(f"{TAG} {item} {old} -> {new}", env)


def item_file(line: str) -> str:
    """The file a `GOLDEN-UPDATED` line names: its item without a `:key`."""
    item = line[len(TAG) + 1 :].rsplit(" ", 3)[0]
    return item.split(":", 1)[0]


def atomic_write(path: Path, data: bytes) -> None:
    """Replace `path` with `data` in one rename, keeping its mode: a reader or an interrupted run sees the old file or
    the new one, never a torn one."""
    path.parent.mkdir(parents=True, exist_ok=True)
    mode = path.stat().st_mode & 0o7777 if path.exists() else 0o644
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.")
    try:
        with os.fdopen(fd, "wb") as fh:
            fh.write(data)
            fh.flush()
            os.fsync(fh.fileno())
        os.chmod(tmp, mode)
        os.replace(tmp, path)
    except BaseException:
        Path(tmp).unlink(missing_ok=True)
        raise


def write_bytes(
    path: Path, data: bytes, item: str, env: Mapping[str, str] | None = None
) -> bool:
    """Make `path` hold `data`, reporting it as `item`. Writes nothing when it already does. True when it changed."""
    old = path.read_bytes() if path.is_file() else None
    if old == data:
        return False
    atomic_write(path, data)
    report(item, describe_bytes(old), describe_bytes(data), env)
    return True


def write_text(
    path: Path, text: str, item: str, env: Mapping[str, str] | None = None
) -> bool:
    return write_bytes(path, text.encode(), item, env)


def remove(path: Path, item: str, env: Mapping[str, str] | None = None) -> bool:
    """Delete `path`, reporting it as `item`; True when there was one."""
    if not path.is_file():
        return False
    old = path.read_bytes()
    path.unlink()
    report(item, describe_bytes(old), ABSENT, env)
    return True


def _lock_path(path: Path, env: Mapping[str, str] | None) -> Path:
    """The lock for `path`'s read-modify-write: under the update log's directory (untracked, set by the runner), named
    by the baseline's resolved path, so a lock never lands beside a tracked file."""
    log = _env(env).get(LOG)
    if not log:
        _refuse()
    key = hashlib.sha1(str(path.resolve()).encode()).hexdigest()
    return Path(log).resolve().parent / "locks" / f"{key}.lock"


def _rewrite_json(path: Path, change, item: str, indent: int, env) -> int:
    """Apply `change(old) -> new` to the JSON object in `path`, write it as json.dumps(new, indent=indent,
    sort_keys=True) plus a newline, and report each key that changed. Returns how many did. Leaves the file alone when
    the object is unchanged, whatever its formatting. Gates running at once share a records file, so the read-modify-
    write holds a lock under the update log's directory (never beside the baseline): the write replaces the file's
    inode, which a lock on the file itself would not survive."""
    lock_path = _lock_path(path, env)
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    path.parent.mkdir(parents=True, exist_ok=True)
    with open(lock_path, "a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        text = path.read_text() if path.exists() else ""
        old = json.loads(text) if text else {}
        new = change(dict(old))
        if new == old:
            return 0
        atomic_write(
            path, (json.dumps(new, indent=indent, sort_keys=True) + "\n").encode()
        )
        changed = 0
        for key in sorted(set(old) | set(new)):
            if old.get(key) != new.get(key):
                report(
                    f"{item}:{key}",
                    describe_value(old.get(key)),
                    describe_value(new.get(key)),
                    env,
                )
                changed += 1
        return changed


def write_json(
    path: Path,
    new: dict,
    item: str,
    *,
    indent: int,
    env: Mapping[str, str] | None = None,
) -> int:
    """Make the JSON object file `path` exactly `new`; returns how many keys changed."""
    return _rewrite_json(path, lambda _old: new, item, indent, env)


def update_json_key(
    path: Path,
    key: str,
    value,
    item: str,
    *,
    indent: int,
    env: Mapping[str, str] | None = None,
) -> bool:
    """Set one key of the JSON object file `path`; True when it changed."""

    def change(old: dict) -> dict:
        old[key] = value
        return old

    return _rewrite_json(path, change, item, indent, env) > 0
