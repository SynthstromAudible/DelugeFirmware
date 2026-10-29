#!/usr/bin/env python3
"""Boot an image on the J-Trace rig and check it: boot timing, boot errors, memory, and pass/fail.

Usage:
    tools/device_check.py [ELF] [--host HOST] [--window 45] [--out DIR] [--no-park]
                          [--min-sdram-free BYTES] [--min-stack-headroom BYTES]

ELF defaults to build/RelWithDebInfo/deluge.elf. The rig (`--host`, `$DELUGE_PROBE_HOST` by default) is
restarted first (tools/rig.py), the image is loaded into RAM through tools/deluge_jlink.py (QSPI is never
written, and no breakpoint is set), and RTT is captured until `--window` seconds after the start (the load
takes about 15). Before the core starts, the program stack is painted with `kStackPaint`
(src/deluge/memory/memory_report.h); the image's "memory report" job sees the paint and logs a `MEM` line
every two seconds: each heap's size, free bytes and their low-water mark, and the stack's high-water mark.
Afterwards the panel is parked dark (tools/rig.py park) and the remote server stopped, unless --no-park.

The report, printed as JSON and written to DIR/device-check.json with the raw capture beside it:
    boot.main_loop_s        firmware time of "going into main loop"
    boot.first_frame_s      firmware time of the app's first OLED frame ("first OLED frame sent")
    boot.inflate_ok         the `.sdram_init` load copy unpacked ("SDRAM memory image ready", no unpack panic)
    errors                  panics, freezes, ERROR-level lines; `warnings` holds WARN-level lines
    settings.flash          the flash settings read ("flash settings: ...")
    settings.card           each card settings file's outcome (0 parsed, 1 absent, 2 unreadable), and the SD
                            card's boot state
    memory                  the last MEM line, plus the run's lowest heap free and highest stack use. The SRAM
                            heap is opportunistic (alloc_fast falls back to SDRAM), so it normally fills at
                            boot: its size (`sram`) is the number to watch, not its free bytes
    stack_guard_max_used    the deepest program stack the existing guard saw (stack_guard.cpp)
    checks / passed         each check and the verdict
    parked                  whether the park image reported the panel dark (null with --no-park)

Exit codes: 0 every check passed; 1 a check failed; 3 the probe or the rig failed; 4 usage error.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))
import rig

DEFAULT_ELF = REPO / "build" / "RelWithDebInfo" / "deluge.elf"
DEFAULT_OUT = REPO / ".superpowers" / "estate" / "device-check"
NM = REPO / "toolchain" / "current" / "arm-none-eabi-gcc" / "bin" / "arm-none-eabi-nm"
# src/deluge/memory/memory_report.h's kStackPaint.
STACK_PAINT = 0x57ACC0DE

# A C++ log line: "<seconds>: <file>:<line>: <text>".
APP_LINE = re.compile(r"^(?P<t>\d+\.\d+): (?P<where>\S+?:\d+): (?P<text>.*)$")
FATAL = re.compile(
    r"panicked at|FREEZE|freezeWithError|COLLISION|HardFault|data abort|prefetch abort|undefined instr",
    re.IGNORECASE,
)
KV = re.compile(r"(\w+)=(-?\d+)")


def symbols(elf: Path) -> dict[str, int]:
    out = subprocess.run(
        [str(NM), str(elf)], capture_output=True, text=True, check=True
    ).stdout
    found = {}
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3:
            found[parts[2]] = int(parts[0], 16)
    return found


def app_time(line: str, text: str) -> float | None:
    """The firmware time of the first C++ log line `line` whose text contains `text`."""
    m = APP_LINE.match(line)
    if m and text in m.group("text"):
        return float(m.group("t"))
    return None


def analyse(rtt: list[str]) -> dict:
    report: dict = {
        "boot": {"main_loop_s": None, "first_frame_s": None, "inflate_ok": False},
        "errors": [],
        "warnings": [],
        "settings": {"flash": None, "card": [], "sd": []},
        "memory": None,
        "stack_guard_max_used": None,
    }
    mems = []
    unpack_failed = False
    for line in rtt:
        boot = report["boot"]
        for key, text in (
            ("main_loop_s", "going into main loop"),
            ("first_frame_s", "first OLED frame sent"),
        ):
            if boot[key] is None:
                boot[key] = app_time(line, text)
        if "SDRAM memory image ready" in line:
            boot["inflate_ok"] = True
        if "did not unpack" in line:
            unpack_failed = True
        if FATAL.search(line) or line.startswith("ERROR"):
            report["errors"].append(line)
        elif line.startswith("WARN"):
            report["warnings"].append(line)
        m = APP_LINE.match(line)
        text = m.group("text") if m else line
        if text.startswith("flash settings:"):
            report["settings"]["flash"] = text
        elif text.startswith("card settings:"):
            report["settings"]["card"].append(text)
        elif (
            "sd: boot_init" in line
            or "sd: init failed" in line
            or "sd: card ready" in line
        ):
            report["settings"]["sd"].append(line.split("] ", 1)[-1])
        elif text.startswith("MEM "):
            mems.append({k: int(v) for k, v in KV.findall(text)})
        g = re.search(r"(\d+) bytes in stack", text)
        if g:
            used = int(g.group(1))
            report["stack_guard_max_used"] = max(
                report["stack_guard_max_used"] or 0, used
            )
    if unpack_failed:
        report["boot"]["inflate_ok"] = False
    if mems:
        last = dict(mems[-1])
        last["reports"] = len(mems)
        last["sram_min_seen"] = min(m.get("sram_min", 0) for m in mems)
        last["sdram_min_seen"] = min(m.get("sdram_min", 0) for m in mems)
        last["stack_hw_max"] = max(m.get("stack_hw", 0) for m in mems)
        report["memory"] = last
    return report


def verdict(report: dict, min_sdram_free: int, min_stack_headroom: int) -> dict:
    boot, mem = report["boot"], report["memory"]
    checks = {
        "reached_main_loop": boot["main_loop_s"] is not None,
        "drew_first_frame": boot["first_frame_s"] is not None,
        "sdram_init_inflated": boot["inflate_ok"],
        "no_errors": not report["errors"],
        "memory_reported": mem is not None,
    }
    if mem is not None:
        checks["sdram_free_floor"] = mem["sdram_min_seen"] >= min_sdram_free
        checks["stack_headroom"] = (
            mem["stack"] - mem["stack_hw_max"] >= min_stack_headroom
        )
    return checks


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Boot an image on the rig and check it.")
    ap.add_argument("elf", nargs="?", type=Path, default=DEFAULT_ELF)
    ap.add_argument(
        "--host",
        default=os.environ.get("DELUGE_PROBE_HOST"),
        help="the probe's ssh host (default $DELUGE_PROBE_HOST)",
    )
    ap.add_argument(
        "--window",
        type=float,
        default=45.0,
        help="seconds for the load and the RTT capture together",
    )
    ap.add_argument("--out", type=Path, default=DEFAULT_OUT)
    ap.add_argument(
        "--no-park",
        action="store_true",
        help="leave the image running and the server up",
    )
    ap.add_argument("--park-elf", type=Path, default=rig.DEFAULT_PARK)
    ap.add_argument(
        "--min-sdram-free",
        type=int,
        default=4 << 20,
        help="lowest SDRAM heap free bytes to pass",
    )
    ap.add_argument(
        "--min-stack-headroom",
        type=int,
        default=4 * 1024,
        help="least unused program stack to pass",
    )
    args = ap.parse_args(argv)
    if not args.host:
        print(
            "device_check: no --host and $DELUGE_PROBE_HOST is unset", file=sys.stderr
        )
        return 4
    if not args.elf.exists():
        print(f"device_check: no such ELF: {args.elf}", file=sys.stderr)
        return 4

    syms = symbols(args.elf)
    lo, hi = syms.get("program_stack_start"), syms.get("program_stack_end")
    if lo is None or hi is None or hi <= lo:
        print("device_check: the ELF names no program stack to paint", file=sys.stderr)
        return 4

    args.out.mkdir(parents=True, exist_ok=True)
    # The Pi's server occasionally refuses one restart; a second normally takes.
    if not any(rig.restart(args.host) for _ in range(3)):
        print(
            f"device_check: could not restart the rig on {args.host}", file=sys.stderr
        )
        return 3

    with tempfile.TemporaryDirectory() as tmp:
        paint = Path(tmp) / "stack-paint.bin"
        paint.write_bytes(struct.pack("<I", STACK_PAINT) * ((hi - lo) // 4))
        cmd = [
            sys.executable, str(REPO / "tools" / "deluge_jlink.py"), str(args.elf),
            "--select", f"IP={rig.probe_address(args.host)}",
            "--timeout", str(args.window), "--json", "--quiet",
            "--fail-on", "panicked at",
            "--before-run", f"restore {paint} binary {lo:#x}",
        ]  # fmt: skip
        proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    try:
        run = json.loads(proc.stdout)
    except json.JSONDecodeError:
        run = {"exit": "probe-error", "error": proc.stderr.strip()[-500:], "rtt": []}
    rtt = run.get("rtt", [])
    (args.out / "device-check.rtt.txt").write_text("\n".join(rtt) + "\n")

    report = {
        "elf": str(args.elf),
        "probe_exit": run.get("exit"),
        "probe_error": run.get("error"),
    }
    report.update(analyse(rtt))
    report["checks"] = verdict(report, args.min_sdram_free, args.min_stack_headroom)
    report["passed"] = run.get("exit") in ("completed", "pattern") and all(
        report["checks"].values()
    )
    report["warnings_count"] = len(report["warnings"])

    report["parked"] = None
    if not args.no_park:
        report["parked"] = rig.park(args.host, args.park_elf)
        rig.stop(args.host)

    (args.out / "device-check.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2))
    if run.get("exit") not in ("completed", "pattern", "fail"):
        return 3
    return 0 if report["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
