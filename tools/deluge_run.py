#!/usr/bin/env python3
"""deluge_run.py -- flash, run, and capture RTT from the Deluge in one shot.

Wraps `probe-rs run` for non-interactive callers (CI, scripts, LLM agents).
`probe-rs run` already flashes, resets, and streams RTT, but it streams until
Ctrl-C and never exits on its own -- which makes it unusable from a single
tool call. This adds the missing pieces:

  * a wall-clock timeout, so the call always terminates
  * --until / --fail-on regexes, so it stops as soon as the outcome is known
  * a machine-readable JSON result with distinct exit codes
  * clean child teardown (SIGINT first, so probe-rs detaches the core properly)

Target is the RZ/A1LU (R7S721020), a Cortex-A9 -- see probe-rs' RZA1L.yaml.
Note its RTT caveat: the data cache covers SRAM at 0x20000000, so a control
block there reads stale over the MEM-AP. Put _SEGGER_RTT in the non-cached
mirror at 0x60000000 if scanning finds nothing. Both ranges are scanned.

Examples:
    tools/deluge_run.py build/Debug/deluge.elf --until 'boot ok' --timeout 30
    tools/deluge_run.py firmware.elf --fail-on 'panicked at' --json
    tools/deluge_run.py firmware.elf --no-flash --timeout 10   # attach only

Exit codes:
    0  --until matched, or the run completed within the timeout with no
       --until given (a plain capture)
    1  --fail-on matched
    2  timed out before any --until matched
    3  probe/flash/attach error (probe-rs failed before or during the run)
    4  usage error (bad arguments, missing file)
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import signal
import subprocess
import sys
import threading
import time
from dataclasses import dataclass, field
from queue import Empty, Queue
from typing import Pattern

DEFAULT_CHIP = "R7S721020"
DEFAULT_TIMEOUT = 30.0
DEFAULT_SPEED = 4000

# The installed probe-rs may have been built before RZA1L.yaml was corrected
# (an older build capped InternalSRAM at 0x2009FFFF, which rejects any image
# reaching above it). Prefer the checked-out target file when we can find it.
DEFAULT_CHIP_DESC_CANDIDATES = [
    os.path.expanduser("~/GitHub/probe-rs/probe-rs/targets/RZA1L.yaml"),
]

# probe-rs emits these on stderr before RTT starts flowing. Seeing one means
# the run never got as far as target code, so we should report a probe error
# (exit 3) rather than a timeout (exit 2) -- the distinction tells the caller
# whether to check their firmware or their cable.
PROBE_ERROR_PATTERNS = [
    re.compile(p, re.IGNORECASE)
    for p in (
        r"^\s*Error",
        r"No debug probe (was )?found",
        r"Probe could not be created",
        r"The chip with the specified name .* was not found",
        r"Connecting to the chip was unsuccessful",
        r"An error with the usage of the probe occurred",
        r"Failed to attach",
        r"Unable to open probe",
    )
]

# Lines probe-rs prints about its own progress. Kept out of the captured RTT
# so `rtt` in the JSON is purely target output.
PROBE_NOISE_PATTERNS = [
    re.compile(p)
    for p in (
        r"^\s*(Erasing|Programming|Finished|Booting|Attaching|Flashing|Verifying)\b",
        r"^\s*WARN\s",
        r"^\s*INFO\s",
        r"^\s*DEBUG\s",
        r"^\s*TRACE\s",
        r"^\s*$",
    )
]


@dataclass
class Result:
    backend: str = "probe-rs"
    elf: str = ""
    chip: str = ""
    flashed: bool = False
    exit: str = ""  # pattern | fail | timeout | probe-error | completed
    rtt_address: str | None = None
    matched: str | None = None
    matched_line: str | None = None
    elapsed: float = 0.0
    returncode: int | None = None
    rtt: list[str] = field(default_factory=list)
    probe_log: list[str] = field(default_factory=list)
    error: str | None = None


def find_symbol(elf_path: str, want: str) -> int | None:
    """Return the address of `want` in a 32-bit little-endian ELF, or None.

    Parsed here rather than shelling out to nm so the tool works without the
    ARM toolchain on PATH. Only the layout we actually ship is handled
    (ELF32 LE); anything else returns None and we fall back to scanning.
    """
    try:
        with open(elf_path, "rb") as f:
            data = f.read()
    except OSError:
        return None
    if len(data) < 52 or data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        return None  # not ELF32 little-endian

    e_shoff = int.from_bytes(data[32:36], "little")
    e_shentsize = int.from_bytes(data[46:48], "little")
    e_shnum = int.from_bytes(data[48:50], "little")
    if e_shoff == 0 or e_shnum == 0:
        return None

    sections = []
    for i in range(e_shnum):
        off = e_shoff + i * e_shentsize
        if off + 40 > len(data):
            return None
        sections.append(
            {
                "type": int.from_bytes(data[off + 4 : off + 8], "little"),
                "offset": int.from_bytes(data[off + 16 : off + 20], "little"),
                "size": int.from_bytes(data[off + 20 : off + 24], "little"),
                "link": int.from_bytes(data[off + 24 : off + 28], "little"),
                "entsize": int.from_bytes(data[off + 36 : off + 40], "little"),
            }
        )

    SHT_SYMTAB = 2
    for sec in sections:
        if sec["type"] != SHT_SYMTAB or sec["entsize"] < 16:
            continue
        if sec["link"] >= len(sections):
            continue
        strtab = sections[sec["link"]]
        strs = data[strtab["offset"] : strtab["offset"] + strtab["size"]]
        for off in range(sec["offset"], sec["offset"] + sec["size"], sec["entsize"]):
            if off + 16 > len(data):
                break
            name_off = int.from_bytes(data[off : off + 4], "little")
            end = strs.find(b"\0", name_off)
            if end < 0:
                continue
            if strs[name_off:end].decode("utf-8", "replace") == want:
                return int.from_bytes(data[off + 4 : off + 8], "little")
    return None


def compile_patterns(raw: list[str], literal: bool) -> list[Pattern[str]]:
    out = []
    for p in raw:
        try:
            out.append(re.compile(re.escape(p) if literal else p))
        except re.error as e:
            sys.exit(f"deluge_run: bad pattern {p!r}: {e}")
    return out


def is_probe_noise(line: str) -> bool:
    return any(p.search(line) for p in PROBE_NOISE_PATTERNS)


def looks_like_probe_error(line: str) -> bool:
    return any(p.search(line) for p in PROBE_ERROR_PATTERNS)


def pump(stream, tag: str, q: Queue) -> None:
    """Feed one of the child's streams into the shared queue, line by line."""
    try:
        for line in iter(stream.readline, ""):
            q.put((tag, line.rstrip("\n")))
    except (ValueError, OSError):
        pass  # stream closed underneath us during teardown
    finally:
        q.put((tag, None))  # EOF sentinel
        try:
            stream.close()
        except Exception:
            pass


def terminate(proc: subprocess.Popen) -> None:
    """Stop probe-rs, preferring SIGINT so it detaches the core cleanly.

    A hard SIGKILL can leave the Cortex-A9 halted with the debug interface
    claimed, which makes the *next* run fail to attach. Escalate only if it
    refuses to go.
    """
    if proc.poll() is not None:
        return
    for sig, grace in (
        (signal.SIGINT, 3.0),
        (signal.SIGTERM, 2.0),
        (signal.SIGKILL, 1.0),
    ):
        try:
            proc.send_signal(sig)
        except ProcessLookupError:
            return
        deadline = time.monotonic() + grace
        while time.monotonic() < deadline:
            if proc.poll() is not None:
                return
            time.sleep(0.05)


def build_command(args: argparse.Namespace) -> list[str]:
    probe_rs = shutil.which(args.probe_rs) or args.probe_rs
    cmd = [
        probe_rs,
        "run",
        "--chip",
        args.chip,
        "--non-interactive",
        "--disable-progressbars",
    ]
    if args.protocol:
        cmd += ["--protocol", args.protocol]
    if args.speed:
        cmd += ["--speed", str(args.speed)]
    if args.probe:
        cmd += ["--probe", args.probe]
    if args.connect_under_reset:
        cmd.append("--connect-under-reset")
    if args.verify:
        cmd.append("--verify")
    if args.chip_description_path:
        cmd += ["--chip-description-path", args.chip_description_path]
    if args.scan_region:
        cmd += ["--scan-region", args.scan_region]
    if args.no_catch:
        # This firmware runs in SVC mode with its own vector table; the default
        # reset/SVC vector catches halt it the moment it starts.
        cmd += [
            "--no-catch-reset",
            "--no-catch-hardfault",
            "--no-catch-svc",
            "--no-catch-hlt",
        ]
    cmd += args.probe_rs_arg
    cmd.append(args.elf)
    return cmd


def main() -> int:
    ap = argparse.ArgumentParser(
        prog="deluge_run.py",
        description="Flash, run, and capture RTT from the Deluge via probe-rs.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__.split("Examples:")[1] if "Examples:" in __doc__ else None,
    )
    ap.add_argument("elf", help="path to the ELF to flash and run")
    ap.add_argument(
        "--until",
        action="append",
        default=[],
        metavar="REGEX",
        help="stop with exit 0 as soon as an RTT line matches (repeatable)",
    )
    ap.add_argument(
        "--fail-on",
        action="append",
        default=[],
        metavar="REGEX",
        help="stop with exit 1 as soon as an RTT line matches (repeatable)",
    )
    ap.add_argument(
        "--literal",
        action="store_true",
        help="treat --until/--fail-on as plain substrings, not regexes",
    )
    ap.add_argument(
        "--timeout",
        type=float,
        default=DEFAULT_TIMEOUT,
        metavar="SEC",
        help=f"wall-clock limit for the whole run (default {DEFAULT_TIMEOUT:g}s)",
    )
    ap.add_argument(
        "--settle",
        type=float,
        default=0.0,
        metavar="SEC",
        help="after --until matches, keep capturing this long for trailing output",
    )
    ap.add_argument(
        "--chip", default=DEFAULT_CHIP, help=f"probe-rs chip (default {DEFAULT_CHIP})"
    )
    ap.add_argument(
        "--protocol", default=None, choices=["swd", "jtag"], help="wire protocol"
    )
    ap.add_argument(
        "--speed", type=int, default=DEFAULT_SPEED, help="probe speed in kHz"
    )
    ap.add_argument("--probe", default=None, help="probe selector, VID:PID[:Serial]")
    ap.add_argument(
        "--connect-under-reset",
        action="store_true",
        help="assert nreset while attaching",
    )
    ap.add_argument(
        "--verify", action="store_true", help="read back flashed data to verify"
    )
    ap.add_argument("--probe-rs", default="probe-rs", help="probe-rs binary to invoke")
    ap.add_argument(
        "--chip-description-path",
        default=None,
        metavar="YAML",
        help="target description override; defaults to the checked-out RZA1L.yaml if present",
    )
    ap.add_argument(
        "--scan-region",
        default=None,
        metavar="ADDR",
        help="RTT control block location. Default: the ELF's _SEGGER_RTT address. "
        "probe-rs does NOT poll RTT at all when this is unset, and its RZA1L "
        "rtt_scan_ranges stop at 0x6009FFFF, so relying on autoscan finds nothing.",
    )
    ap.add_argument(
        "--no-scan-region",
        action="store_true",
        help="do not pass --scan-region (disables RTT entirely)",
    )
    ap.add_argument(
        "--no-catch",
        action="store_true",
        default=True,
        help="disable reset/hardfault/SVC/HLT vector catch (default; see --catch)",
    )
    ap.add_argument(
        "--catch",
        dest="no_catch",
        action="store_false",
        help="leave probe-rs vector catches enabled (halts SVC-mode firmware at reset)",
    )
    ap.add_argument(
        "--probe-rs-arg",
        action="append",
        default=[],
        metavar="ARG",
        help="extra raw argument passed through to probe-rs (repeatable)",
    )
    ap.add_argument(
        "--json", action="store_true", help="emit the result as JSON on stdout"
    )
    ap.add_argument(
        "--quiet", action="store_true", help="do not mirror RTT to stdout as it arrives"
    )
    ap.add_argument(
        "--max-lines",
        type=int,
        default=10000,
        metavar="N",
        help="cap captured RTT lines to bound memory (default 10000)",
    )
    args = ap.parse_args()

    if not os.path.isfile(args.elf):
        print(f"deluge_run: no such ELF: {args.elf}", file=sys.stderr)
        return 4
    if shutil.which(args.probe_rs) is None and not os.path.isfile(args.probe_rs):
        print(f"deluge_run: probe-rs not found: {args.probe_rs}", file=sys.stderr)
        return 4

    until = compile_patterns(args.until, args.literal)
    fail_on = compile_patterns(args.fail_on, args.literal)

    if args.chip_description_path is None:
        for cand in DEFAULT_CHIP_DESC_CANDIDATES:
            if os.path.isfile(cand):
                args.chip_description_path = cand
                break

    res = Result(elf=os.path.abspath(args.elf), chip=args.chip)

    if args.no_scan_region:
        args.scan_region = None
    elif args.scan_region is None:
        addr = find_symbol(args.elf, "_SEGGER_RTT")
        if addr is not None:
            args.scan_region = f"{addr:#x}"
            res.rtt_address = args.scan_region
        else:
            res.probe_log.append(
                "deluge_run: no _SEGGER_RTT symbol in ELF; falling back to 'ram' scan"
            )
            args.scan_region = "ram"
    else:
        res.rtt_address = args.scan_region

    cmd = build_command(args)

    if not args.quiet:
        print(f"deluge_run: {' '.join(cmd)}", file=sys.stderr)

    started = time.monotonic()
    try:
        proc = subprocess.Popen(
            cmd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,  # line buffered, so --until fires promptly
            # Own process group: our SIGINT reaches probe-rs alone, and a
            # Ctrl-C aimed at us does not race the child's own handler.
            start_new_session=True,
        )
    except OSError as e:
        res.exit, res.error = "probe-error", f"could not start probe-rs: {e}"
        emit(res, args)
        return 3

    q: Queue = Queue()
    threads = [
        threading.Thread(target=pump, args=(proc.stdout, "out", q), daemon=True),
        threading.Thread(target=pump, args=(proc.stderr, "err", q), daemon=True),
    ]
    for t in threads:
        t.start()

    open_streams = 2
    deadline = started + args.timeout
    settle_until: float | None = None
    truncated = False

    while True:
        now = time.monotonic()
        if settle_until is not None and now >= settle_until:
            break
        if res.exit == "" and now >= deadline:
            res.exit = "timeout"
            break
        if open_streams == 0:
            # probe-rs exited on its own.
            if res.exit == "":
                res.exit = "completed"
            break

        # Wake often enough to honour the deadline even while the target is
        # silent (a hung target produces no lines at all).
        horizon = settle_until if settle_until is not None else deadline
        try:
            tag, line = q.get(timeout=max(0.05, min(0.25, horizon - now)))
        except Empty:
            continue

        if line is None:
            open_streams -= 1
            continue

        if tag == "err" or is_probe_noise(line):
            res.probe_log.append(line)
            if not args.quiet:
                print(line, file=sys.stderr)
            if (
                re.search(r"^\s*(Finished|Flashing succeeded)", line)
                or "Erasing" in line
            ):
                res.flashed = True
            if res.exit == "" and looks_like_probe_error(line):
                res.exit = "probe-error"
                res.error = line.strip()
                break
            continue

        # Target RTT output.
        if len(res.rtt) < args.max_lines:
            res.rtt.append(line)
        elif not truncated:
            truncated = True
            res.probe_log.append(f"deluge_run: RTT truncated at {args.max_lines} lines")
        if not args.quiet:
            print(line, flush=True)

        if res.exit != "":
            continue  # already decided; we are just draining during settle

        for p in fail_on:
            if p.search(line):
                res.exit, res.matched, res.matched_line = "fail", p.pattern, line
                break
        else:
            for p in until:
                if p.search(line):
                    res.exit, res.matched, res.matched_line = "pattern", p.pattern, line
                    break
        if res.exit == "fail":
            break
        if res.exit == "pattern":
            if args.settle > 0:
                settle_until = time.monotonic() + args.settle
            else:
                break

    # Measured before teardown so it reflects the run, not the SIGINT grace.
    res.elapsed = round(time.monotonic() - started, 3)
    terminate(proc)
    res.returncode = proc.poll()

    if res.exit == "":
        res.exit = "completed"
    # probe-rs failing before any RTT appeared is a probe error, not a clean run.
    if res.exit == "completed" and res.returncode not in (0, None) and not res.rtt:
        res.exit = "probe-error"
        res.error = (
            res.error or f"probe-rs exited {res.returncode} with no target output"
        )
    # An --until that never matched is a timeout, even if probe-rs exited first.
    if res.exit == "completed" and until:
        res.exit = "timeout"

    emit(res, args)
    return {"pattern": 0, "completed": 0, "fail": 1, "timeout": 2, "probe-error": 3}[
        res.exit
    ]


def emit(res: Result, args: argparse.Namespace) -> None:
    if args.json:
        print(json.dumps(res.__dict__, indent=2))
    elif not args.quiet:
        summary = (
            f"deluge_run: {res.exit} in {res.elapsed:g}s, {len(res.rtt)} RTT line(s)"
        )
        if res.matched:
            summary += f", matched {res.matched!r}"
        if res.error:
            summary += f" -- {res.error}"
        print(summary, file=sys.stderr)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
