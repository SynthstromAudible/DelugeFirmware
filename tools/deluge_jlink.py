#!/usr/bin/env python3
"""deluge_jlink.py -- load, run, and capture RTT from the Deluge via SEGGER J-Link.

A backend-for-backend mirror of deluge_run.py (which wraps `probe-rs run`): same arguments, same exit
codes, same JSON shape, so the two are interchangeable in scripts and CI. Use this one when probe-rs is
suspect -- notably to A/B the boot-hang rate and the apparent slowness while a debugger is attached,
since the two backends differ in how they reset the core and how hard they poll RTT.

Mechanism (the recipe from IDE_Configs/vscode/launch.json's "deluge-rust" config, which is what the
RAM-linked Rust firmware needs):

  JLinkGDBServer -jlinkscriptfile rza1_debug.JLinkScript   (halt-only reset: asserting nRESET would
                                                            run the ROM bootloader instead of us)
  arm-none-eabi-gdb: monitor reset
                     monitor cp15 1,0,0,0 = 0x00C50078     (MMU + caches off before the load)
                     monitor cp15 12,0,0,0 = 0x20000000    (VBAR)
                     load                                  (the image is RAM-linked)
                     set $cpsr = 0x1DF                     (SVC, IRQ/FIQ masked)
                     monitor exec SetRTTAddr <_SEGGER_RTT>
                     continue

Two traps this deliberately avoids, both learned the hard way:

  * It sets NO breakpoints. A gdb that dies mid-setup leaves a HARDWARE breakpoint comparator armed in
    the Cortex-A9 debug unit, and the JLinkScript's halt-only reset never clears it -- so it fires as a
    bogus SIGTRAP at a shifted address on every subsequent load until the board is power-cycled.
  * It passes the RTT control-block address explicitly instead of relying on a scan. The block lives in
    the UNCACHED SDRAM alias (0x602Exxxx) because the D-cache covers SRAM and a cached block reads stale
    over the MEM-AP; that address is outside the ranges J-Link scans by default.

Exit codes (identical to deluge_run.py):
    0  --until matched, or the run completed within the timeout with no --until given
    1  --fail-on matched
    2  timed out before any --until matched
    3  probe/load/attach error
    4  usage error
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import tempfile
import threading
import time
from dataclasses import asdict, dataclass, field

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DEVICE = "R7S721020"
DEFAULT_INTERFACE = "swd"
DEFAULT_SPEED = "4000"
DEFAULT_GDB = os.path.join(
    REPO, "toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-gdb"
)
DEFAULT_NM = os.path.join(
    REPO, "toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-nm"
)
DEFAULT_JLINKSCRIPT = os.path.expanduser("~/GitHub/deluge-sdk/rza1_debug.JLinkScript")
# The Linux CLI binary is JLinkGDBServerCLExe; note ./dbt debug -j looks for JLinkGDBServerCL (no Exe)
# and falls back to a Windows path, so it does not work here.
SERVER_CANDIDATES = ("JLinkGDBServerCLExe", "JLinkGDBServerCL")
GDB_PORT = 3333
RTT_PORT = 19021

# Lines the GDB server prints about itself, kept out of the captured RTT so `rtt` is purely target output.
SERVER_NOISE = re.compile(
    r"^(SEGGER|Process:|Firmware:|Hardware:|S/N:|Feature|Checking|Target voltage|Listening)"
)
# The same, on the RTT telnet stream: the server greets a new reader with a banner, and reports a
# refusal there if another reader still holds the channel. Neither came from the target.
RTT_NOISE = re.compile(r"^(SEGGER J-Link|Process:|ERROR: Connection refused)")


@dataclass
class Result:
    backend: str = "jlink"
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


def find_server(explicit: str | None) -> str | None:
    if explicit:
        return shutil.which(explicit) or (
            explicit if os.path.exists(explicit) else None
        )
    for name in SERVER_CANDIDATES:
        found = shutil.which(name)
        if found:
            return found
    return None


def rtt_address(elf: str, nm: str) -> str | None:
    """Read _SEGGER_RTT out of the ELF. Explicit beats scanning -- see the module docstring."""
    try:
        out = subprocess.run(
            [nm, elf], capture_output=True, text=True, check=False
        ).stdout
    except OSError:
        return None
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "_SEGGER_RTT":
            return "0x" + parts[0]
    return None


def entry_point(elf: str, readelf: str) -> str | None:
    try:
        out = subprocess.run(
            [readelf, "-h", elf], capture_output=True, text=True, check=False
        ).stdout
    except OSError:
        return None
    for line in out.splitlines():
        if "Entry point" in line:
            return line.split()[-1]
    return None


def wait_for_port(port: int, deadline: float) -> bool:
    while time.time() < deadline:
        with socket.socket() as probe:
            probe.settimeout(0.5)
            try:
                probe.connect(("127.0.0.1", port))
                return True
            except OSError:
                time.sleep(0.2)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(
        description="Load, run and capture RTT from the Deluge via J-Link."
    )
    ap.add_argument("elf", help="path to the ELF to load and run")
    ap.add_argument(
        "--timeout",
        type=float,
        default=60.0,
        help="wall-clock seconds before giving up",
    )
    ap.add_argument(
        "--until",
        action="append",
        default=[],
        metavar="RE",
        help="stop with exit 0 as soon as an RTT line matches (repeatable)",
    )
    ap.add_argument(
        "--fail-on",
        action="append",
        default=[],
        metavar="RE",
        help="stop with exit 1 as soon as an RTT line matches (repeatable)",
    )
    ap.add_argument(
        "--max-lines",
        type=int,
        default=100000,
        help="stop capturing after this many RTT lines",
    )
    ap.add_argument(
        "--json", action="store_true", help="emit the machine-readable result on stdout"
    )
    ap.add_argument(
        "--quiet", action="store_true", help="do not stream RTT to stderr as it arrives"
    )
    ap.add_argument(
        "--no-load",
        action="store_true",
        help="attach to the running target without loading",
    )
    ap.add_argument("--device", default=DEFAULT_DEVICE)
    ap.add_argument("--interface", default=DEFAULT_INTERFACE)
    ap.add_argument("--speed", default=DEFAULT_SPEED)
    ap.add_argument("--jlinkscript", default=DEFAULT_JLINKSCRIPT)
    ap.add_argument("--gdb", default=DEFAULT_GDB)
    ap.add_argument("--server", default=None, help="path to JLinkGDBServerCLExe")
    ap.add_argument(
        "--rtt-address", default=None, help="override the _SEGGER_RTT address"
    )
    args = ap.parse_args()

    res = Result(elf=args.elf, chip=args.device)
    started = time.time()

    def finish(code: int, exit_kind: str, error: str | None = None) -> int:
        res.exit = exit_kind
        res.error = error
        res.elapsed = round(time.time() - started, 3)
        if args.json:
            print(json.dumps(asdict(res), indent=2))
        elif error:
            print(f"deluge_jlink: {exit_kind} -- {error}", file=sys.stderr)
        else:
            print(
                f"deluge_jlink: {exit_kind} in {res.elapsed}s, {len(res.rtt)} RTT line(s)",
                file=sys.stderr,
            )
        return code

    if not os.path.exists(args.elf):
        return finish(4, "usage", f"no such ELF: {args.elf}")
    server = find_server(args.server)
    if not server:
        return finish(4, "usage", "JLinkGDBServerCLExe not found on PATH")
    if not os.path.exists(args.gdb):
        return finish(4, "usage", f"gdb not found: {args.gdb}")

    addr = args.rtt_address or rtt_address(args.elf, DEFAULT_NM)
    if not addr:
        return finish(
            4, "usage", "could not read _SEGGER_RTT from the ELF; pass --rtt-address"
        )
    res.rtt_address = addr

    server_cmd = [
        server,
        "-if",
        args.interface,
        "-device",
        args.device,
        "-endian",
        "little",
        "-speed",
        args.speed,
        "-nogui",
        "-port",
        str(GDB_PORT),
        "-rtttelnetport",
        str(RTT_PORT),
    ]
    if args.jlinkscript and os.path.exists(args.jlinkscript):
        server_cmd += ["-jlinkscriptfile", args.jlinkscript]

    srv = subprocess.Popen(
        server_cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True
    )

    def drain_server() -> None:
        assert srv.stdout is not None
        for line in iter(srv.stdout.readline, ""):
            line = line.rstrip()
            if line and not SERVER_NOISE.match(line):
                res.probe_log.append(line)

    threading.Thread(target=drain_server, daemon=True).start()

    deadline = started + args.timeout
    if not wait_for_port(GDB_PORT, min(deadline, started + 30)):
        srv.kill()
        return finish(3, "probe-error", "J-Link GDB server never opened its port")

    entry = entry_point(args.elf, DEFAULT_NM.replace("-nm", "-readelf"))
    script = [
        "set pagination off",
        "set confirm off",
        f"target extended-remote :{GDB_PORT}",
    ]
    if not args.no_load:
        script += [
            "monitor reset",
            "monitor cp15 1, 0, 0, 0 = 0x00C50078",
            "monitor cp15 12, 0, 0, 0 = 0x20000000",
            "load",
            "set $cpsr = 0x1DF",
        ]
        if entry:
            script.append(f"set $pc = {entry}")
    script += [f"monitor exec SetRTTAddr {addr}", "continue"]

    with tempfile.NamedTemporaryFile("w", suffix=".gdb", delete=False) as handle:
        handle.write("\n".join(script) + "\n")
        gdb_script = handle.name

    gdb = subprocess.Popen(
        [args.gdb, "-q", "-x", gdb_script, args.elf],
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        stdin=subprocess.DEVNULL,
    )

    def drain_gdb() -> None:
        assert gdb.stdout is not None
        for line in iter(gdb.stdout.readline, ""):
            line = line.rstrip()
            if line:
                res.probe_log.append(line)
                if "Transfer rate" in line:
                    res.flashed = True

    threading.Thread(target=drain_gdb, daemon=True).start()

    # Wait for the load to finish before touching RTT. The GDB server opens port 19021 at startup
    # whether or not a control block exists, so connecting early yields the banner and an immediate
    # close -- and the block is only valid once rtt-target has initialised it.
    if not args.no_load:
        while time.time() < deadline and not res.flashed and gdb.poll() is None:
            time.sleep(0.25)
        if not res.flashed:
            gdb.kill()
            srv.kill()
            return finish(
                3, "probe-error", "load never completed (no transfer rate reported)"
            )
    if not wait_for_port(RTT_PORT, min(deadline, time.time() + 30)):
        gdb.kill()
        srv.kill()
        return finish(3, "probe-error", "J-Link RTT telnet port never opened")

    untils = [re.compile(p) for p in args.until]
    fails = [re.compile(p) for p in args.fail_on]
    outcome: tuple[int, str] | None = None

    try:
        rtt = socket.create_connection(("127.0.0.1", RTT_PORT), timeout=1.0)
        buf = ""
        while time.time() < deadline and len(res.rtt) < args.max_lines:
            try:
                chunk = rtt.recv(4096)
            except TimeoutError:
                continue
            if not chunk:
                # The server drops the connection until the control block is discoverable. Reconnect
                # instead of treating it as end-of-stream, or a slow boot looks like a dead target.
                rtt.close()
                time.sleep(0.5)
                if time.time() >= deadline:
                    break
                try:
                    rtt = socket.create_connection(("127.0.0.1", RTT_PORT), timeout=1.0)
                except OSError:
                    break
                continue
            if True:
                buf += chunk.decode("utf-8", "replace")
                while "\n" in buf:
                    line, buf = buf.split("\n", 1)
                    line = line.rstrip("\r")
                    if not line:
                        continue
                    if RTT_NOISE.match(line):
                        res.probe_log.append(line)
                        continue
                    res.rtt.append(line)
                    if not args.quiet:
                        print(line, file=sys.stderr, flush=True)
                    for pat in fails:
                        if pat.search(line):
                            res.matched, res.matched_line = pat.pattern, line
                            outcome = (1, "fail")
                            break
                    if outcome is None:
                        for pat in untils:
                            if pat.search(line):
                                res.matched, res.matched_line = pat.pattern, line
                                outcome = (0, "pattern")
                                break
                    if outcome:
                        break
                if outcome:
                    break
    except OSError as exc:
        outcome = (3, "probe-error")
        res.error = str(exc)

    # SIGINT first so gdb detaches the core cleanly, leaving the firmware running.
    for proc in (gdb, srv):
        try:
            proc.send_signal(signal.SIGINT)
        except OSError:
            pass
    time.sleep(0.5)
    for proc in (gdb, srv):
        if proc.poll() is None:
            proc.kill()
    os.unlink(gdb_script)

    if outcome:
        return finish(outcome[0], outcome[1], res.error)
    if args.until:
        return finish(2, "timeout")
    return finish(0, "completed")


if __name__ == "__main__":
    sys.exit(main())
