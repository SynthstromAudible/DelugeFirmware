"""Run a scenario gate: golden_vt_render with GOLDEN_SCENARIO set, and the checks its registry entry declares.

Every scenario gate is checked the same way first: the run must exit 0, each of the entry's `markers` must start a line
of its output (a stage that never ran asserts nothing, and a failure count cannot show that, because it counts only
assertions that did run), and its `result` line must appear whole. An entry's `check` module adds what is particular to
that gate. It defines one of:

    def check(gate, run):   # after the standard run and its standard checks
    def run(gate):          # instead of them, when the gate runs the binary more than once or not at all like the rest

and reports through `gate.fail(message)`. A gate passes when nothing failed. Pass or fail, the `deluge-<gate>-card`
fixture tree a gate's runs staged in the temp dir is removed after it.

A `run(gate)` check judges the entry's markers and result itself: `gate.require_standard(run)` on a run, or
`gate.require_markers(lines)` over several runs' output or stdout and stderr together, or `gate.shortfall(lines)` where
a run is expected to fall short. Checks read marker lines through the helpers here (`tag`, `fields`, `field`,
`int_field`, `has_fields`, `starting`, `after`); a registry unit test refuses a check that parses them with `re`.
"""

import importlib.util
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

# A marker line: an upper-case tag followed by a space, '=', ':' or a further '-TAG'.
MARKER = re.compile(r"^[A-Z][A-Z0-9_]*(-[A-Z0-9_]+)*([ =:]|$)")
# One `key=value` on a marker line: the key runs from a space (or the line's start) to '='; the value ends at whitespace.
_FIELD = re.compile(r"(?:^| )([^\s=]+)=(\S*)")


@dataclass
class Run:
    """One run of the binary: its exit status and what it printed."""

    code: int
    out: str
    err: str

    @property
    def lines(self) -> list[str]:
        return self.out.splitlines()

    def starting(self, *prefixes: str) -> list[str]:
        """Every stdout line that starts with one of `prefixes`."""
        return starting(self.lines, *prefixes)

    def first(self, prefix: str) -> str | None:
        found = self.starting(prefix)
        return found[0] if found else None

    def last(self, prefix: str) -> str | None:
        found = self.starting(prefix)
        return found[-1] if found else None

    def has(self, prefix: str) -> bool:
        return any(line.startswith(prefix) for line in self.lines)

    def has_line(self, whole: str) -> bool:
        return whole in self.lines


def field(line: str | None, key: str) -> str | None:
    """The value of `key=value` in `line`, or None. Values end at whitespace."""
    if line is None:
        return None
    m = re.search(r"(?:^| )" + re.escape(key) + r"=(\S*)", line)
    return m.group(1) if m else None


def int_field(line: str | None, key: str) -> int | None:
    value = field(line, key)
    try:
        return int(value) if value is not None else None
    except ValueError:
        return None


def count_field(line: str | None, key: str) -> int | None:
    """The counter `key=<digits>` in `line`; None when it is absent or anything but unsigned ASCII digits."""
    value = field(line, key)
    return (
        int(value)
        if value is not None and value.isascii() and value.isdigit()
        else None
    )


def starting(lines: Iterable[str], *prefixes: str) -> list[str]:
    """Every line of `lines` that starts with one of `prefixes`, in order."""
    return [line for line in lines if line.startswith(prefixes)]


def tag(line: str | None) -> str | None:
    """The marker tag `line` starts with (`CL-SONGLOAD` in `CL-SONGLOAD clipsReferenced=0`), or None."""
    m = MARKER.match(line) if line is not None else None
    return line[: m.start(2)] if m else None


def after(line: str | None, prefix: str) -> str | None:
    """The text of `line` after `prefix`, or None when `line` does not start with it."""
    return line[len(prefix) :] if line is not None and line.startswith(prefix) else None


def fields(line: str | None) -> dict[str, str]:
    """Every `key=value` in `line`, in order; a key given twice keeps its first value, as `field` reads it."""
    found: dict[str, str] = {}
    for key, value in _FIELD.findall(line or ""):
        found.setdefault(key, value)
    return found


def has_fields(line: str | None, **want: object) -> bool:
    """Does `line` carry `key=value` for each of `want`? Values are compared as text (`pending=0` matches `"0"`)."""
    got = fields(line)
    return line is not None and all(
        got.get(key) == str(value) for key, value in want.items()
    )


class Gate:
    """What a check module works with."""

    def __init__(
        self, name: str, root: Path, binary: Path, env: dict[str, str], entry, log
    ):
        self.name = name
        self.root = root
        self.binary = binary
        self.env = env
        self.entry = entry
        self.failures: list[str] = []
        self._log = log
        self.tmp = Path(tempfile.mkdtemp(prefix=f"{name}."))

    def say(self, line: str = "") -> None:
        print(line, file=self._log, flush=True)

    def fail(self, message: str) -> None:
        """Record a failure and print it as the scripts did: FAIL, a dash, and what went wrong."""
        self.failures.append(message)
        self.say(
            f"FAIL — {message}"
            if not re.match(r"^[A-Z][A-Z0-9-]* ", message)
            else f"FAIL {message}"
        )

    def run(
        self,
        scenario: str | None = None,
        env: dict[str, str] | None = None,
        timeout: float | None = None,
    ) -> Run:
        """Run the binary with GOLDEN_SCENARIO=`scenario` (the entry's, by default) and `env` over the gate's own."""
        full = dict(self.env)
        full["GOLDEN_SCENARIO"] = self.entry.scenario if scenario is None else scenario
        full.update(env or {})
        # A cross-built binary runs under an emulator (golden-vt-render-arm's qemu).
        wrapper = shlex.split(full.get("GOLDEN_VT_RENDER_WRAPPER", ""))
        try:
            result = subprocess.run(
                [*wrapper, str(self.binary)],
                cwd=self.root,
                env=full,
                capture_output=True,
                text=True,
                errors="replace",
                timeout=timeout,
                check=False,
            )
        except subprocess.TimeoutExpired as exc:
            out = (
                exc.stdout.decode(errors="replace")
                if isinstance(exc.stdout, bytes)
                else (exc.stdout or "")
            )
            err = (
                exc.stderr.decode(errors="replace")
                if isinstance(exc.stderr, bytes)
                else (exc.stderr or "")
            )
            return Run(124, out, err)
        return Run(result.returncode, result.stdout, result.stderr)

    def shortfall(self, lines: list[str]) -> str | None:
        """Why `lines` lack one of the entry's markers or its result line, or None. Records nothing: a check that
        expects a run to fall short (a runner self-test) asks this."""
        for marker in self.entry.markers:
            if not any(
                line == marker or line.startswith(marker + " ") for line in lines
            ):
                return f"no {marker} line; that stage did not run"
        if self.entry.result and self.entry.result not in lines:
            return "in-process failures:"
        return None

    def require_markers(self, lines: list[str]) -> bool:
        """The entry's markers and result line, judged over `lines`: several runs' output, or stdout and stderr
        together. False when one is missing; the failure is recorded and the marker lines shown."""
        why = self.shortfall(lines)
        if why is None:
            return True
        self.fail(why)
        self.show(lines)
        return False

    def require_standard(self, run: Run) -> bool:
        """The checks every scenario gate makes: exit 0, each marker, the result line. False when the run failed."""
        if run.code != 0:
            self.fail(f"the run exited {run.code}")
            self.show(run)
            for line in run.err.splitlines()[:25]:
                self.say(line)
            return False
        return self.require_markers(run.lines)

    def show(self, run: Run | list[str]) -> None:
        """Print the marker lines of a run, or of `lines`."""
        for line in run.lines if isinstance(run, Run) else run:
            if MARKER.match(line):
                self.say(line)

    def cleanup(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)


def _load_check(path: Path):
    spec = importlib.util.spec_from_file_location(f"check_{path.stem}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _clean_up_fixture(entry) -> None:
    """Remove the fixture tree a gate's runs staged in the temp dir to fill the card from."""
    shutil.rmtree(
        Path(tempfile.gettempdir()) / f"deluge-{entry.name}-card", ignore_errors=True
    )


def run_gate(root: Path, entry, env: dict[str, str], binary: Path, log) -> bool:
    """Run one scenario gate. True when it passed."""
    if not binary.is_file():
        print(f"test: '{entry.name}': {binary} is not built", file=log)
        return False
    gate = Gate(entry.name, root, binary, env, entry, log)
    try:
        module = _load_check(root / entry.check) if entry.check else None
        if module is not None and hasattr(module, "run"):
            module.run(gate)
        else:
            run = gate.run()
            if gate.require_standard(run):
                gate.show(run)
                if module is not None:
                    module.check(gate, run)
        passed = not gate.failures
        gate.say(f"{entry.name}: {'PASS' if passed else 'FAIL'}")
        return passed
    finally:
        gate.cleanup()
        _clean_up_fixture(entry)


__all__ = [
    "MARKER",
    "Gate",
    "Run",
    "after",
    "field",
    "fields",
    "has_fields",
    "int_field",
    "run_gate",
    "starting",
    "tag",
]

if __name__ == "__main__":
    sys.exit("scenario_gate.py is a module; run gates with ./dbt test")
