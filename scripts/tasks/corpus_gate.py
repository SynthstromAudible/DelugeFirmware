"""Run a corpus gate: its runner, tests/golden/<corpus_runner>_run.py, against golden_vt_render.

A corpus runner drives golden_vt_render once per song through tests/golden/driver.py and judges each run itself --
a fixture hash, a stdout baseline, or the driver's own verdict -- printing a line per song and a summary, and exits 0,
1 on a failure, or 77 when it found nothing to check. This module runs it with the gate's binary (and its wrapper, for
a cross-built one) and the song corpus driver.default_corpus() finds, prints what it printed, and fails when it fails.
Exit 77 fails too: an entry that requires the corpus and finds none has a broken setup, not a skip.

The runner runs in a process of its own: `dbt test -j N` runs gates on threads of one process, and a runner that
printed to this process's stdout, or redirected it, would mix with or swallow the other gates' output.
"""

import importlib
import subprocess
import sys
from pathlib import Path

from harness_registry import CORPUS_RUNNERS
from scenario_gate import Gate

# A runner's exit status when it found no song to check.
NOTHING_TO_CHECK = 77


def run_corpus_gate(root: Path, entry, env: dict[str, str], binary: Path, log) -> bool:
    """Run one corpus gate. True when it passed."""
    if not binary.is_file():
        print(f"test: '{entry.name}': {binary} is not built", file=log)
        return False
    gate = Gate(entry.name, root, binary, env, entry, log)
    try:
        _run(gate)
        passed = not gate.failures
        gate.say(f"{entry.name}: {'PASS' if passed else 'FAIL'}")
        return passed
    finally:
        gate.cleanup()


def _run(gate: Gate) -> None:
    runners = gate.root / CORPUS_RUNNERS
    if str(runners) not in sys.path:
        sys.path.insert(0, str(runners))
    driver = importlib.import_module("driver")

    name = gate.entry.corpus_runner
    argv = [
        sys.executable,
        str(runners / f"{name}_run.py"),
        "--binary",
        str(gate.binary),
    ]
    if wrapper := gate.env.get("GOLDEN_VT_RENDER_WRAPPER", ""):
        argv += ["--wrapper", wrapper]
    if corpus := driver.default_corpus():
        argv += ["--corpus", corpus]

    result = subprocess.run(
        argv,
        cwd=gate.root,
        env=gate.env,
        capture_output=True,
        text=True,
        errors="replace",
        check=False,
    )
    for line in (result.stdout + result.stderr).splitlines():
        gate.say(line)
    if result.returncode == NOTHING_TO_CHECK:
        gate.fail(f"{name}: no corpus songs to check (set DELUGE_SONG_CORPUS)")
    elif result.returncode != 0:
        gate.fail(f"{name}: the runner exited {result.returncode}")
