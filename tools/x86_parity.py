#!/usr/bin/env python3
"""x86_parity.py -- play the emulator parity scripts on two builds of the x86 host app and list every snapshot file
the two disagree on.

    tools/x86_parity.py --ref BIN --test BIN --out DIR [-j N] [--store DIR] SCRIPT_OR_DIR...
    tools/x86_parity.py --worktree REV [--jobs N] [--store DIR]

BIN is a `golden_vt_render` binary, run with the `ui_script` scenario. The host app runs on a virtual clock, so two
runs of one binary are byte-identical, and any file two binaries disagree on is a difference in behaviour.

Each parity script (format in tests/harness/README.md) is translated into the host's UI script:
  - every time is shifted by BOOT_MS, so "ms after boot settles" lands after the host app's boot;
  - a `quit` is put after the last step when the script has none;
  - `load NAME` is expanded into the same Load / pad-keyboard / Select presses emu_parity.py makes;
  - `random_colour X Y` is dropped and that pad is masked to lit/unlit in every .state;
  - an encoder turn of more than MAX_DETENTS detents is split into turns the runner takes;
  - every MIDI input step (`midi`, `note`, `noteoff`, `cc`, `pc`, `bend`) becomes `midi HEX`.
Each script runs once per card its `# card:` header names. A FAT image (`*.img`) is extracted once with mtools into
the store's card cache, keyed by the image's mtime; a directory card is used as it stands.

Results: DIR/ref/got/<leg>/ and DIR/test/got/<leg>/ hold each side's snapshots (`<snap>.oled<n>.pbm`, `.state`, and
`.midi`, the DIN MIDI the app sent since the previous snapshot), DIR/<side>/scripts/ the translated scripts,
DIR/<side>/logs/ each leg's output, DIR/<side>/results.json each leg's status, and DIR/diff.txt every file the two
sides disagree on. The last line printed is `X86-PARITY legs=<n> ref_failed=<a> test_failed=<b> differ=<c>`, where
`differ` counts files. The exit code is 0 when all three counts are 0, 2 when a leg failed, else 1.

--worktree builds `golden_vt_render` at REV in a detached worktree and prints the binary's path as its last line.
The store (default .superpowers/ in this checkout) holds the card cache (x86-cards/), the base worktrees (x86-wt/)
and the base binaries (x86-bin/).
"""

import argparse
import concurrent.futures
import filecmp
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

import ui_script_cards
from emu_parity import midi_bytes

REPO = Path(__file__).resolve().parent.parent

# The host app's boot, in virtual ms: a parity script's times count from the moment the panel settles after boot.
BOOT_MS = 5000
# The host runner keeps an encoder's edges in an int8 accumulator, so one step turns at most this many detents.
MAX_DETENTS = 31
# Between the pieces of a split encoder turn.
SPLIT_GAP_MS = 10
# After the last step, before the appended `quit`.
QUIT_GAP_MS = 100
# `load NAME`, as emu_parity.py presses it: Load, the browser's listing, one key per character, then Select.
LOAD_OPEN_MS = 1500
LOAD_KEY_MS = 150
LOAD_KEY_HOLD_MS = 40
LOAD_SETTLE_MS = 800
BUTTON_HOLD_MS = 20
# Virtual time the app may run past the last step, and wall-clock seconds the scenario may take past it.
BUDGET_MARGIN_MS = 30_000
DEADLINE_MARGIN_S = 60

MIDI_STEPS = {"midi", "note", "noteoff", "cc", "pc", "bend"}
# The browser's pad keyboard: (row, keys from column 3).
QWERTY_ROWS = (
    (5, "1234567890-"),
    (4, "QWERTYUIOP"),
    (3, "ASDFGHJKL\0'"),
    (2, "ZXCVBNM,."),
)
KEYS = {" ": (5, 1)}
for _y, _row in QWERTY_ROWS:
    for _x, _ch in enumerate(_row):
        if _ch != "\0":
            KEYS[_ch] = (3 + _x, _y)

PAD_LINE = re.compile(r"^pad (\d+) (\d+) (\d+) (\d+) (\d+)$")


def steps_of(script: Path) -> list[tuple[int, str, list[str]]]:
    """The (time, command, arguments) of every step of @script."""
    out = []
    for line in script.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if line:
            tok = line.split()
            out.append((int(tok[0]), tok[1], tok[2:]))
    return out


def translate(script: Path) -> tuple[str, set[tuple[int, int]], int]:
    """The host UI script for the parity script @script, the pads its `random_colour` steps mask, and the time of
    its last step before the appended `quit`."""
    lines: list[str] = []
    masked: set[tuple[int, int]] = set()
    last = 0
    for at, cmd, args in steps_of(script):
        t = at + BOOT_MS
        if cmd in MIDI_STEPS:
            lines.append(f"{t} midi {midi_bytes(cmd, args).hex()}")
            last = max(last, t)
            continue
        if cmd == "random_colour":
            masked.add((int(args[0]), int(args[1])))
            continue
        if cmd == "load":
            name = " ".join(args)
            lines += [f"{t} button Load down", f"{t + BUTTON_HOLD_MS} button Load up"]
            k = t + LOAD_OPEN_MS
            for ch in name:
                x, y = KEYS[ch.upper()]
                lines += [
                    f"{k} pad {x} {y} down",
                    f"{k + LOAD_KEY_HOLD_MS} pad {x} {y} up",
                ]
                k += LOAD_KEY_MS
            k += LOAD_SETTLE_MS
            lines += [
                f"{k} button SelectEnc down",
                f"{k + BUTTON_HOLD_MS} button SelectEnc up",
            ]
            last = max(last, k + BUTTON_HOLD_MS)
            continue
        if cmd == "encoder" and abs(int(args[1])) > MAX_DETENTS:
            n, sign, k = abs(int(args[1])), 1 if int(args[1]) > 0 else -1, t
            while n:
                c = min(n, MAX_DETENTS)
                lines.append(f"{k} encoder {args[0]} {sign * c:+d}")
                n -= c
                k += SPLIT_GAP_MS
            last = max(last, k)
            continue
        lines.append(f"{t} {cmd} {' '.join(args)}".rstrip())
        last = max(last, t)
        if cmd == "quit":
            break
    else:
        lines.append(f"{last + QUIT_GAP_MS} quit")
    # A load expansion may run past the next step's time, and the parser needs non-decreasing times.
    timed = sorted((int(line.split()[0]), i, line) for i, line in enumerate(lines))
    return "\n".join(line for _, _, line in timed) + "\n", masked, last


def cache_dir_for(image: Path, cache: Path) -> Path:
    """The extracted tree of @image in @cache: named by the image's stem and its mtime, so an image rewritten since its
    extraction is extracted again."""
    return cache / f"{image.stem}@{image.stat().st_mtime_ns}"


def card_tree(card: str, cache: Path) -> Path:
    """The directory tree the host app takes for @card: a directory card as it is, a FAT image extracted once."""
    path = ui_script_cards.resolve(card, REPO)
    if path.suffix != ".img":
        return path
    dest = cache_dir_for(path, cache)
    if not (dest / ".extracted").exists():
        for stale in cache.glob(f"{path.stem}@*"):
            shutil.rmtree(stale)
        dest.mkdir(parents=True)
        subprocess.run(
            ["mcopy", "-s", "-n", "-p", "-m", "-i", str(path), "::/*", str(dest)],
            check=True,
        )
        (dest / ".extracted").write_text(str(path))
    return dest


def mask_state(snapshots: Path, masked: set[tuple[int, int]]) -> None:
    """Rewrite each masked pad's colour in every .state under @snapshots as LIT, so a random colour compares as lit."""
    if not masked:
        return
    for f in snapshots.glob("*.state"):
        out = []
        for line in f.read_text().splitlines():
            m = PAD_LINE.match(line)
            if m and (int(m[1]), int(m[2])) in masked:
                line = f"pad {m[1]} {m[2]} LIT"
            out.append(line)
        f.write_text("\n".join(out) + "\n")


def leg_name(script: Path, card: str) -> str:
    return f"{script.stem}@{ui_script_cards.label(card)}"


def run_leg(
    binary: Path, script: Path, card: str, side_dir: Path, cache: Path
) -> tuple[str, dict]:
    """Run @script on @card with @binary, writing its snapshots to @side_dir/got/<leg>/; the leg's name and result."""
    name = leg_name(script, card)
    got = side_dir / "got" / name
    text, masked, last = translate(script)
    host = side_dir / "scripts" / f"{name}.txt"
    host.parent.mkdir(parents=True, exist_ok=True)
    host.write_text(text)
    env = dict(
        os.environ,
        UI_SNAPSHOT_MIDI="1",
        GOLDEN_SCENARIO="ui_script",
        UI_SCRIPT=str(host),
        UI_SNAPSHOT_DIR=str(got),
        UI_CARD=str(card_tree(card, cache)),
        GOLDEN_VIRTUAL_BUDGET_MS=str(last + BUDGET_MARGIN_MS),
        GOLDEN_SCENARIO_DEADLINE_S=str(last // 1000 + DEADLINE_MARGIN_S),
    )
    t0 = time.monotonic()
    r = subprocess.run(
        [str(binary)],
        cwd=REPO,
        env=env,
        check=False,  # a failed leg is a result, read from its exit code below
        capture_output=True,
        text=True,
        errors="replace",
    )
    wall = time.monotonic() - t0
    (side_dir / "logs").mkdir(parents=True, exist_ok=True)
    (side_dir / "logs" / f"{name}.log").write_text(
        f"exit {r.returncode}\n--- stdout\n{r.stdout}\n--- stderr\n{r.stderr}"
    )
    mask_state(got, masked)
    ok = r.returncode == 0 and "UI-SCRIPT-RESULT failures=0" in r.stdout
    return name, {
        "status": "ok" if ok else f"exit {r.returncode}",
        "wall_s": round(wall, 2),
        "snapshots": len(list(got.glob("*.state"))) if got.exists() else 0,
    }


def compare(a: Path, b: Path) -> list[str]:
    """Every file under @a or @b that is missing from the other or differs from it, byte for byte, as relative paths."""
    fa = {p.relative_to(a) for p in a.rglob("*") if p.is_file()}
    fb = {p.relative_to(b) for p in b.rglob("*") if p.is_file()}
    out = sorted(str(p) for p in fa ^ fb)
    out += sorted(
        str(p) for p in sorted(fa & fb) if not filecmp.cmp(a / p, b / p, shallow=False)
    )
    return out


def build_worktree(rev: str, jobs: int, store: Path) -> Path:
    """Build golden_vt_render at @rev in a detached worktree under @store, with the toolchain DBT_TOOLCHAIN_PATH names
    (else this checkout's), and copy it to @store/x86-bin/<sha>/. The worktree's own scripts/tasks/test_builds.py names
    the targets, so a base builds what its own tree builds."""
    sha = subprocess.run(
        ["git", "-C", str(REPO), "rev-parse", "--verify", f"{rev}^{{commit}}"],
        check=True,
        capture_output=True,
        text=True,
    ).stdout.strip()
    root = store / "x86-wt" / sha[:12]
    if not root.exists():
        subprocess.run(
            ["git", "-C", str(REPO), "worktree", "add", "--detach", str(root), sha],
            check=True,
        )
    toolchain = (
        Path(os.environ.get("DBT_TOOLCHAIN_PATH", str(REPO))) / "toolchain" / "current"
    )
    env = dict(os.environ)
    env["PATH"] = os.pathsep.join(
        [
            str(toolchain / "cmake" / "bin"),
            str(toolchain / "ninja-build" / "bin"),
            env["PATH"],
        ]
    )
    sys.path.insert(0, str(root / "scripts" / "tasks"))
    import test_builds

    tree = root / "build" / "host-app"
    if not (tree / "build.ninja").exists():
        subprocess.run(
            [
                "cmake",
                "-S",
                "sim",
                "-B",
                str(tree),
                "-G",
                "Ninja",
                "-DCMAKE_C_COMPILER=clang",
                "-DCMAKE_CXX_COMPILER=clang++",
                *test_builds.TEST_TREE_DEFINES,
            ],
            cwd=root,
            env=env,
            check=True,
        )
    subprocess.run(
        ["ninja", "-j", str(jobs), "-C", str(tree), *test_builds.HOST_APP_TARGETS],
        cwd=root,
        env=env,
        check=True,
    )
    subprocess.run(
        ["cargo", "build", "--release", "--locked"],
        cwd=root / "src/bsp/rust/harness/golden_vt_render",
        env=dict(
            env,
            DELUGE_HOSTAPP_BUILD_DIR=str(tree),
            CARGO_TARGET_DIR=str(root / "build" / "cargo-harness"),
            CARGO_BUILD_JOBS=str(jobs),
        ),
        check=True,
    )
    locks = subprocess.run(
        ["git", "-C", str(root), "ls-files", "*Cargo.lock"],
        capture_output=True,
        text=True,
        check=True,
    ).stdout.split()
    if locks:
        subprocess.run(["git", "-C", str(root), "checkout", "--", *locks], check=True)
    dest = store / "x86-bin" / sha[:12] / "golden_vt_render"
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(
        root / "build" / "cargo-harness" / "release" / "golden_vt_render", dest
    )
    return dest


def run_side(
    binary: Path, legs: list[tuple[Path, str]], side_dir: Path, cache: Path, jobs: int
) -> dict[str, dict]:
    """Every leg on @binary into @side_dir, with @side_dir/results.json written; each leg's result by name."""
    t0 = time.monotonic()
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        results = dict(
            pool.map(lambda leg: run_leg(binary, leg[0], leg[1], side_dir, cache), legs)
        )
    side_dir.mkdir(parents=True, exist_ok=True)
    (side_dir / "results.json").write_text(
        json.dumps(
            {"total_wall_s": round(time.monotonic() - t0, 1), "legs": results}, indent=1
        )
    )
    return results


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ref", type=Path, help="the reference golden_vt_render")
    ap.add_argument("--test", type=Path, help="the golden_vt_render under test")
    ap.add_argument("--out", type=Path, help="where the results go")
    ap.add_argument("-j", type=int, default=8, help="legs run at once")
    ap.add_argument(
        "--store",
        type=Path,
        default=REPO / ".superpowers",
        help="the card cache, base worktrees and base binaries",
    )
    ap.add_argument(
        "--worktree",
        metavar="REV",
        help="build golden_vt_render at REV and print its path",
    )
    ap.add_argument("--jobs", type=int, default=8, help="build jobs for --worktree")
    ap.add_argument("scripts", nargs="*", type=Path)
    a = ap.parse_args()
    store = a.store.resolve()

    if a.worktree:
        print(build_worktree(a.worktree, a.jobs, store))
        return 0
    if not (a.ref and a.test and a.out and a.scripts):
        ap.error("--ref, --test, --out and at least one script are required")

    scripts: list[Path] = []
    for s in a.scripts:
        scripts += sorted(s.glob("*.txt")) if s.is_dir() else [s]
    legs = [(s, c) for s in scripts for c in ui_script_cards.cards(s)]
    cache = store / "x86-cards"
    cache.mkdir(parents=True, exist_ok=True)
    # Extract every image once, before the parallel runs.
    for c in sorted({c for _, c in legs}):
        card_tree(c, cache)

    out = a.out.resolve()
    ref = run_side(a.ref.resolve(), legs, out / "ref", cache, a.j)
    test = run_side(a.test.resolve(), legs, out / "test", cache, a.j)
    (out / "ref" / "got").mkdir(parents=True, exist_ok=True)
    (out / "test" / "got").mkdir(parents=True, exist_ok=True)
    differ = compare(out / "ref" / "got", out / "test" / "got")
    (out / "diff.txt").write_text("".join(f"{p}\n" for p in differ))

    for s, c in legs:
        name = leg_name(s, c)
        print(f"{name} ref={ref[name]['status']} test={test[name]['status']}")
    ref_failed = sum(r["status"] != "ok" for r in ref.values())
    test_failed = sum(r["status"] != "ok" for r in test.values())
    print(
        f"X86-PARITY legs={len(legs)} ref_failed={ref_failed} test_failed={test_failed} differ={len(differ)}"
    )
    if ref_failed or test_failed:
        return 2
    return 1 if differ else 0


if __name__ == "__main__":
    sys.exit(main())
