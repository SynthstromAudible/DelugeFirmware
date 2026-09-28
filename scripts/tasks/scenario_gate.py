"""Run a scenario gate: golden_vt_render with GOLDEN_SCENARIO set, and the checks its registry entry declares.

Every scenario gate is checked the same way first: the run must exit 0, each of the entry's `markers` must start a line
of its output (a stage that never ran asserts nothing, and a failure count cannot show that, because it counts only
assertions that did run), and its `result` line must appear whole. An entry's `check` module adds what is particular to
that gate. It defines one of:

    def check(gate, run):   # after the standard run and its standard checks
    def run(gate):          # instead of them, when the gate runs the binary more than once or not at all like the rest

and reports through `gate.fail(message)`. A gate passes when nothing failed. Pass or fail, the card images a gate's
runs packed into the temp dir, and its `deluge-<gate>-card` fixture dir, are removed after it.
"""

import importlib.util
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

# A marker line: an upper-case tag followed by a space, '=', ':' or a further '-TAG'.
MARKER = re.compile(r"^[A-Z][A-Z0-9_]*(-[A-Z0-9_]+)*([ =:]|$)")


@dataclass
class Run:
    """One run of the binary: its exit status and what it printed."""

    code: int
    out: str
    err: str

    @property
    def lines(self) -> list[str]:
        return self.out.splitlines()

    def starting(self, prefix: str) -> list[str]:
        """Every stdout line that starts with `prefix`."""
        return [line for line in self.lines if line.startswith(prefix)]

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
        try:
            result = subprocess.run(
                [str(self.binary)],
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

    def require_standard(self, run: Run) -> bool:
        """The checks every scenario gate makes: exit 0, each marker, the result line. False when the run failed."""
        if run.code != 0:
            self.fail(f"the run exited {run.code}")
            self.show(run)
            for line in run.err.splitlines()[:25]:
                self.say(line)
            return False
        for marker in self.entry.markers:
            if not run.has(marker + " ") and not run.has_line(marker):
                self.fail(f"no {marker} line; that stage did not run")
                self.show(run)
                return False
        if self.entry.result and not run.has_line(self.entry.result):
            self.fail("in-process failures:")
            self.show(run)
            return False
        return True

    def show(self, run: Run) -> None:
        """Print the run's marker lines."""
        for line in run.lines:
            if MARKER.match(line):
                self.say(line)

    def cleanup(self) -> None:
        shutil.rmtree(self.tmp, ignore_errors=True)


def _load_check(path: Path):
    spec = importlib.util.spec_from_file_location(f"check_{path.stem}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def _card_images() -> set[Path]:
    """The packed card images golden_vt_render caches in the temp dir, each a sparse 2.5 GB file."""
    return set(Path(tempfile.gettempdir()).glob("deluge-streaming-scenario-*.img"))


def _clean_up_cards(entry, images_before: set[Path]) -> None:
    """Remove what a gate's runs left in the temp dir: the images they packed, and the gate's fixture card dir."""
    for image in _card_images() - images_before:
        image.unlink(missing_ok=True)
    shutil.rmtree(
        Path(tempfile.gettempdir()) / f"deluge-{entry.name}-card", ignore_errors=True
    )


def run_gate(root: Path, entry, env: dict[str, str], binary: Path, log) -> bool:
    """Run one scenario gate. True when it passed."""
    if not binary.is_file():
        print(f"test: '{entry.name}': {binary} is not built", file=log)
        return False
    gate = Gate(entry.name, root, binary, env, entry, log)
    images_before = _card_images()
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
        _clean_up_cards(entry, images_before)


__all__ = ["MARKER", "Gate", "Run", "field", "int_field", "run_gate"]

if __name__ == "__main__":
    sys.exit("scenario_gate.py is a module; run gates with ./dbt test")
