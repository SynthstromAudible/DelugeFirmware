"""The corpus-guard gate: golden_vt_render refuses to boot the song-corpus fixture in a run that does not declare it.

A test that boots the corpus fixture must say so in tests/registry.toml (`requires = ["song-corpus"]`), which keeps it
out of CI; `dbt test` then sets DELUGE_CORPUS_DECLARED=1 for it. harness_registry.py checks that statically, from the
rows of scenario_table.cpp. This gate runs the binary itself on `stem_export`, a row whose card is the corpus fixture,
with no declaration and no card image, and requires exit code 2 and the refusal: the guard that also covers the rows
whose card comes from the environment.

Usage: python3 tests/integration/checks/corpus_guard.py   (through ./dbt test, which builds golden_vt_render first)
"""

import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
REFUSAL = "does not declare the corpus"


def main() -> int:
    target = os.environ.get("CARGO_TARGET_DIR") or str(REPO / "build" / "cargo-harness")
    binary = (
        Path(target)
        / os.environ.get("GOLDEN_VT_RENDER_TARGET", "")
        / "release"
        / "golden_vt_render"
    )
    if not binary.is_file():
        print(
            f"ERROR: '{binary}' is not built -- run this gate through ./dbt test, which builds it first"
        )
        return 2

    env = {
        k: v
        for k, v in os.environ.items()
        if k not in ("DELUGE_SD_IMAGE", "DELUGE_CORPUS_DECLARED")
    }
    env.update(GOLDEN_SCENARIO="stem_export", GOLDEN_CARD_ONLY="1")
    run = subprocess.run(
        [str(binary)], env=env, capture_output=True, text=True, timeout=120, check=False
    )
    if run.returncode != 2 or REFUSAL not in run.stderr:
        print(
            f"FAIL: the corpus fixture booted without a declaration: exit {run.returncode}, "
            f"expected 2 with '{REFUSAL}' on stderr"
        )
        print(run.stderr[-2000:])
        return 1
    print("corpus-guard: PASS (exit 2, the run refused the undeclared corpus fixture)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
