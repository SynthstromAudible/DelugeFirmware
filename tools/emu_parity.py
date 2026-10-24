#!/usr/bin/env python3
"""emu_parity.py -- run two firmware images side by side in delugemu and require identical panels.

A user must not be able to tell two builds apart by looking at the Deluge. This boots a reference image and a
test image in the delugemu emulator (~/GitHub/delugemu) on the same SD card and the same starting settings flash,
drives both through the same level-3 UI scripts (tests/ui-scripts/app/*.txt, format in tests/harness/README.md),
and at every `snapshot` step compares what each shows: the OLED bitmap, every pad's colour, the button LEDs and
the gold-knob rings. Any difference is reported with both panels' screenshots, and the run exits non-zero.

Script times are milliseconds after boot settles (the panel stops changing), not the level-3 frame clock: the two
images boot at different speeds, and a snapshot is taken once both panels have settled after the step's time.

    tools/emu_parity.py --ref main.elf --test build/RelWithDebInfo/deluge.elf \\
        --card tests/fixtures/card tests/ui-scripts/app/boot.txt tests/ui-scripts/app/menu.txt

Exit codes: 0 every snapshot matched; 1 a snapshot differed; 2 usage or emulator error.
"""

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BUTTON_TABLE = REPO / "tests/harness/level3/host_script_tables.c"

# Firmware encoder index by script role: tests/harness/level3/host_script_tables.c's k_encoder_roles.
ENCODER_ROLES = ["Vertical", "Horizontal", "Tempo", "Select", "GoldUpper", "GoldLower"]

SETTLE_POLL_S = 0.2
SETTLE_QUIET_S = 0.6
SETTLE_TIMEOUT_S = 15.0
# Boot lights the pads about 0.9 s before the OLED's first frame, and takes several seconds to get there.
BOOT_QUIET_S = 1.5
BOOT_TIMEOUT_S = 60.0


def button_table() -> dict[str, tuple[int, int]]:
    """The level-3 harness's button names and matrix coordinates, read from its own C table."""
    text = BUTTON_TABLE.read_text()
    return {
        m[1]: (int(m[2]), int(m[3]))
        for m in re.finditer(r'\{"(\w+)",\s*(\d+),\s*(\d+)\}', text)
    }


def parse_script(path: Path) -> list[tuple[int, str, list[str]]]:
    """`<at_ms> <command> <args...>` lines, `#` comments, as tests/harness/ui_script.cpp parses them."""
    steps = []
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tok = line.split()
        steps.append((int(tok[0]), tok[1], tok[2:]))
    return steps


def panel(dz) -> dict:
    return {
        "oled": dz.read_oled(),
        "pads": dz.read_pads(),
        "leds": dz.read_leds(),
        "gold": dz.read_gold_knob(),
    }


def lit(p: dict) -> bool:
    return any(any(row) for row in p["oled"]) or any(
        any(any(v) for v in col) for col in p["pads"]
    )


def settle(dz, quiet_s: float = SETTLE_QUIET_S, need_lit: bool = False) -> dict:
    """The panel once it has stopped changing for `quiet_s` (and, with `need_lit`, once something is lit), or after
    SETTLE_TIMEOUT_S marked unsettled."""
    start = time.time()
    last = panel(dz)
    quiet_since = time.time()
    while time.time() - start < SETTLE_TIMEOUT_S:
        time.sleep(SETTLE_POLL_S)
        now = panel(dz)
        if now != last:
            last, quiet_since = now, time.time()
        elif time.time() - quiet_since >= quiet_s and (lit(now) or not need_lit):
            return last
    last["unsettled"] = True
    return last


def settle_boot(dz) -> dict:
    start = time.time()
    while time.time() - start < BOOT_TIMEOUT_S:
        p = settle(dz, BOOT_QUIET_S, need_lit=True)
        if not p.get("unsettled"):
            return p
    return {"unsettled": True}


def run_image(
    elf: Path, sd: Path, flash: Path | None, script: Path, out: Path, buttons
) -> dict[str, dict]:
    """Boot `elf`, play `script`, and return {snapshot name: panel}; screenshots go to `out`."""
    from delugemu import DelugeSession

    out.mkdir(parents=True, exist_ok=True)
    extra = []
    if flash is not None:
        run_flash = out / "flash.bin"
        shutil.copyfile(flash, run_flash)
        extra = ["-global", f"rza1l-spibsc.flash-file={run_flash}"]
    snaps = {}
    # One QMP socket per emulator: both run in this process, and the default name is per process.
    sock = f"/tmp/dz_parity_{os.getpid()}_{out.name}.sock"
    with DelugeSession.launch(
        firmware=str(elf),
        sd=str(sd),
        icount="auto",
        boot_wait=0.0,
        sock=sock,
        extra_qemu=tuple(extra),
        artifacts_dir=str(out),
        int_log=str(out / "int.log"),
    ) as dz:
        boot = settle_boot(dz)
        if boot.get("unsettled"):
            raise RuntimeError(f"{elf.name} did not finish booting")
        t0 = time.time()
        for at_ms, cmd, args in parse_script(script):
            time.sleep(max(0.0, t0 + at_ms / 1000.0 - time.time()))
            if cmd == "button":
                x, y = buttons[args[0]]
                dz.button(x, y, args[1] == "down")
            elif cmd == "pad":
                dz.pad_event(int(args[0]), int(args[1]), args[2] == "down")
            elif cmd == "encoder":
                dz.encoder(ENCODER_ROLES.index(args[0]), int(args[1]))
            elif cmd == "snapshot":
                snaps[args[0]] = settle(dz)
                dz.screenshot(str(out / f"{args[0]}.png"))
            elif cmd == "quit":
                break
            # `board`, `released`, `model`, `notice`: not panel input on a real Deluge.
        try:
            dz.assert_no_aborts()
        except AssertionError:  # delugemu's AbortDetected
            # The firmware's fault report and log: the RTT up-buffer the device build places at 0x202EB000.
            dz.q.hmp(f'pmemsave 0x202eb000 16384 "{out / "rtt.bin"}"')
            raise
    return snaps


def differences(ref: dict, test: dict) -> list[str]:
    out = []
    if ref.get("unsettled") or test.get("unsettled"):
        out.append("a panel never settled")
    if ref["oled"] != test["oled"]:
        n = sum(
            a != b for ra, rb in zip(ref["oled"], test["oled"]) for a, b in zip(ra, rb)
        )
        out.append(f"OLED: {n} pixels differ")
    pads = [
        (x, y, ref["pads"][x][y], test["pads"][x][y])
        for x in range(len(ref["pads"]))
        for y in range(len(ref["pads"][x]))
        if ref["pads"][x][y] != test["pads"][x][y]
    ]
    if pads:
        out.append(f"pads: {len(pads)} differ, first {pads[:4]}")
    leds = sorted(k for k in ref["leds"] if ref["leds"][k] != test["leds"].get(k))
    if leds:
        out.append(f"LEDs differ: {leds}")
    if ref["gold"] != test["gold"]:
        out.append(f"gold knob rings: {ref['gold']} vs {test['gold']}")
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument(
        "--ref",
        required=True,
        type=Path,
        help="reference firmware ELF (e.g. a main build)",
    )
    ap.add_argument("--test", required=True, type=Path, help="firmware ELF under test")
    card = ap.add_mutually_exclusive_group(required=True)
    card.add_argument(
        "--card",
        type=Path,
        help="SD card content directory (imaged with delugemu's mksd.sh)",
    )
    card.add_argument("--sd", type=Path, help="ready-made SD image")
    ap.add_argument(
        "--flash", type=Path, help="starting settings flash image (default: erased)"
    )
    ap.add_argument("--delugemu", type=Path, default=Path.home() / "GitHub/delugemu")
    ap.add_argument(
        "--out",
        type=Path,
        help="artifact directory (default: a temporary one, kept on failure)",
    )
    ap.add_argument("scripts", nargs="+", type=Path)
    args = ap.parse_args()

    sys.path.insert(0, str(args.delugemu / "pylib"))
    out = args.out or Path(tempfile.mkdtemp(prefix="emu-parity-"))
    sd = args.sd
    if sd is None:
        sd = out / "card.img"
        subprocess.run(
            [str(args.delugemu / "scripts/mksd.sh"), str(args.card), str(sd)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
    buttons = button_table()

    report = {}
    failed = False
    for script in args.scripts:
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            jobs = {
                side: pool.submit(
                    run_image,
                    elf,
                    sd,
                    args.flash,
                    script,
                    out / script.stem / side,
                    buttons,
                )
                for side, elf in (("ref", args.ref), ("test", args.test))
            }
            try:
                got = {side: job.result() for side, job in jobs.items()}
            except (
                AssertionError,
                RuntimeError,
                OSError,
                KeyError,
                ValueError,
            ) as e:  # a fault, the emulator, the script
                print(f"emu_parity: {script.name}: {e}", file=sys.stderr)
                return 2
        for name in got["ref"]:
            diff = differences(got["ref"][name], got["test"][name])
            report[f"{script.stem}/{name}"] = diff
            status = "MATCH" if not diff else "DIFF"
            print(
                f"{status:5} {script.stem}/{name}"
                + ("" if not diff else "  " + "; ".join(diff))
            )
            failed |= bool(diff)
    (out / "report.json").write_text(json.dumps(report, indent=1))
    print(f"emu_parity: artifacts in {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
