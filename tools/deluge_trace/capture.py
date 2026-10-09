#!/usr/bin/env python3
"""Capture PTM program-flow trace from the Deluge through a J-Trace PRO's trace port.

Runs on the machine the J-Trace is plugged into (its trace data needs the probe's local USB):

  1. open the probe; select the JLinkScript (a halt-only reset), R7S721020, SWD; connect;
  2. load the ELF into RAM as deluge_jlink.py does: reset (halt), SCTLR = 0x00C50078 (MMU and caches
     off), VBAR = 0x20000000, the PT_LOAD segments at their physical addresses, CPSR = 0x1DF, PC = entry;
  3. set ICEREGJTTRCSEL.TRCMUX_SEL so P4_2..P4_7 carry the trace port (until the next reset);
  4. find the PTM, funnel, ETF and TPIU from the ROM tables and program them: TPIU 4 bits, continuous
     formatting; ETF as a hardware FIFO; the PTM cycle-accurate, tracing only the given address ranges;
  5. RAWTRACE START, run until an RTT line matches --until (by default the bench's `bench: parked`, after
     which the device is dark) or the timeout, halt, stop the PTM and flush, RAWTRACE STOP, read every
     byte the probe holds;
  6. resume the core, so a bench image stays parked dark, and close.

QSPI flash is never written: the image goes to RAM only, and while TRCMUX_SEL is set the flash pins are
trace pins anyway.

Output, in --out: trace.bin (the raw port bytes), rtt.txt (the target's RTT lines), meta.json (the
probe, the components found, the PTM register values used, the ranges and every DLL return code), and
jlink.log (the DLL's own log).

--survey only connects, halts, walks the ROM tables, reads the PTM's ID registers and resumes: no load,
no trace.

Exit codes: 0 captured (or surveyed), 1 a --fail-on line matched, 2 timed out before --until, 3 probe
or target error, 4 usage error.
"""

from __future__ import annotations

import argparse
import json
import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import coresight
import jlink
from elf import Elf

SCTLR_MMU_CACHES_OFF = 0x00C50078
VBAR = 0x20000000
CPSR_SVC_MASKED = 0x1DF
# SEGGER_RTT_CB with one up and one down buffer, the "SEGGER RTT" signature first.
RTT_CONTROL_BLOCK_BYTES = 0x30
DEFAULT_UNTIL = r"^bench: parked"
DEFAULT_FAIL = (r"panicked at", r"^BENCH-ERROR")


class MemBus:
    """The probe's memory access as a coresight.Bus, logging every write."""

    def __init__(self, link: jlink.JLink) -> None:
        self.link = link
        self.writes: list[tuple[int, int]] = []

    def read_u32(self, addr: int) -> int:
        return self.link.read_u32(addr)

    def write_u32(self, addr: int, value: int) -> None:
        self.writes.append((addr, value))
        self.link.write_u32(addr, value)


def parse_range(text: str) -> tuple[int, int]:
    """START-END or START+SIZE, numbers in any Python base."""
    if "+" in text:
        a, b = text.split("+", 1)
        return int(a, 0), int(a, 0) + int(b, 0)
    a, b = text.split("-", 1)
    return int(a, 0), int(b, 0)


def load_elf(link: jlink.JLink, image: Elf, log: dict) -> None:
    link.reset()
    link.cp15_write(1, 0, 0, 0, SCTLR_MMU_CACHES_OFF)
    link.cp15_write(12, 0, 0, 0, VBAR)
    log["sctlr"] = f"0x{link.cp15_read(1, 0, 0, 0):08X}"
    log["vbar"] = f"0x{link.cp15_read(12, 0, 0, 0):08X}"
    loaded = []
    t0 = time.monotonic()
    for seg in image.segments():
        link.write_mem(seg.paddr, seg.data)
        loaded.append([f"0x{seg.paddr:08X}", len(seg.data)])
    log["segments"] = loaded
    log["load_seconds"] = round(time.monotonic() - t0, 2)
    link.write_reg("CPSR", CPSR_SVC_MASKED)
    link.write_reg("R15", image.entry)
    log["pc"] = f"0x{link.read_reg('R15'):08X}"
    log["cpsr"] = f"0x{link.read_reg('CPSR'):08X}"


def dump_path(bus: MemBus, path: coresight.TracePath) -> dict[str, str]:
    """The trace path's control registers as they stand."""
    regs = {
        "ETMCR": path.ptm + coresight.ETMCR,
        "ETMSR": path.ptm + coresight.ETMSR,
        "ETMTRACEIDR": path.ptm + coresight.ETMTRACEIDR,
        "ETMTECR1": path.ptm + coresight.ETMTECR1,
        "FUNCTL": path.funnel + coresight.FUNCTL,
        "TPIU_CSPSR": path.tpiu + coresight.TPIU_CSPSR,
        "TPIU_FFCR": path.tpiu + coresight.TPIU_FFCR,
        "TPIU_FFSR": path.tpiu + coresight.TPIU_FFSR,
    }
    if path.etf is not None:
        regs.update(
            ETF_CTL=path.etf + coresight.TMC_CTL,
            ETF_MODE=path.etf + coresight.TMC_MODE,
            ETF_STS=path.etf + coresight.TMC_STS,
            ETF_FFCR=path.etf + coresight.TMC_FFCR,
        )
    return {k: f"0x{bus.read_u32(a):08X}" for k, a in regs.items()}


def survey(link: jlink.JLink, meta: dict) -> None:
    link.halt()
    bus = MemBus(link)
    comps = coresight.walk_rom(bus)
    meta["components"] = [c.describe() for c in comps]
    for line in meta["components"]:
        print(f"capture: {line}", file=sys.stderr)
    path = coresight.TracePath.from_components(comps)
    meta["path"] = {
        k: (None if v is None else f"0x{v:08X}") for k, v in vars(path).items()
    }
    coresight.unlock(bus, path.ptm)
    meta["ptm_id"] = {
        name: f"0x{bus.read_u32(path.ptm + off):08X}"
        for name, off in (
            ("ETMIDR", coresight.ETMIDR),
            ("ETMCCR", coresight.ETMCCR),
            ("ETMCCER", coresight.ETMCCER),
            ("ETMCR", coresight.ETMCR),
            ("ETMSR", coresight.ETMSR),
            ("LSR", coresight.LSR),
        )
    }
    meta["tpiu_sspsr"] = f"0x{bus.read_u32(path.tpiu + coresight.TPIU_SSPSR):08X}"
    meta["jttrcsel"] = f"0x{bus.read_u32(coresight.ICEREG_JTTRCSEL):08X}"
    print(f"capture: PTM {meta['ptm_id']}", file=sys.stderr)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Capture PTM trace through a J-Trace PRO.")
    ap.add_argument("--elf", help="the RAM-linked image to load and run")
    ap.add_argument(
        "--range",
        action="append",
        default=[],
        metavar="START-END",
        help="an address range to trace, [START, END) or START+SIZE (1 to 4)",
    )
    ap.add_argument("--out", default="trace-out", help="the directory to write into")
    ap.add_argument("--until", action="append", default=[], metavar="RE")
    ap.add_argument("--fail-on", action="append", default=[], metavar="RE")
    ap.add_argument("--timeout", type=float, default=120.0)
    ap.add_argument(
        "--rtt-address", help="the _SEGGER_RTT address (default: from the ELF)"
    )
    ap.add_argument("--device", default="R7S721020")
    ap.add_argument("--speed", type=int, default=4000, help="SWD clock, kHz")
    ap.add_argument("--script", help="the JLinkScript (rza1_debug.JLinkScript)")
    ap.add_argument("--serial", type=int, help="the probe's USB serial number")
    ap.add_argument(
        "--library", help="the J-Link DLL (default $JLINK_LIBRARY or /opt/SEGGER)"
    )
    ap.add_argument("--trace-id", type=lambda s: int(s, 0), default=0x10)
    ap.add_argument("--timestamps", action="store_true")
    ap.add_argument("--no-cycle-accurate", action="store_true")
    ap.add_argument("--branch-broadcast", action="store_true")
    ap.add_argument(
        "--exec",
        action="append",
        default=[],
        metavar="CMD",
        help="a J-Link command string run before RAWTRACE START, e.g. 'TraceSampleAdjust TD = 1000'",
    )
    ap.add_argument(
        "--funnel-ports",
        type=lambda s: int(s, 0),
        default=0x1,
        help="the trace funnel's enabled slave ports, a bit mask (the PTM's is port 0)",
    )
    ap.add_argument("--survey", action="store_true", help="walk the ROM tables only")
    args = ap.parse_args(argv)

    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    meta: dict = {"argv": sys.argv[1:], "dll_calls": {}}
    calls = meta["dll_calls"]

    def finish(code: int, outcome: str, error: str | None = None) -> int:
        meta["outcome"] = outcome
        meta["exit"] = code
        if error:
            meta["error"] = error
            print(f"capture: {outcome}: {error}", file=sys.stderr)
        (out / "meta.json").write_text(json.dumps(meta, indent=2) + "\n")
        return code

    if not args.survey:
        if not args.elf or not args.range:
            return finish(4, "usage", "--elf and at least one --range are needed")
        if len(args.range) > 4:
            return finish(4, "usage", "at most four ranges")
    try:
        ranges = [parse_range(r) for r in args.range]
    except ValueError as e:
        return finish(4, "usage", f"bad --range: {e}")
    meta["ranges"] = [[f"0x{a:08X}", f"0x{b:08X}"] for a, b in ranges]

    image = Elf(args.elf) if args.elf else None
    rtt_addr = None
    if image is not None:
        rtt_addr = (
            int(args.rtt_address, 0)
            if args.rtt_address
            else image.symbol("_SEGGER_RTT")
        )
        if rtt_addr is None:
            return finish(4, "usage", "no _SEGGER_RTT in the ELF; pass --rtt-address")
        meta["rtt_address"] = f"0x{rtt_addr:08X}"

    try:
        link = jlink.JLink(args.library)
    except (OSError, jlink.JLinkError) as e:
        return finish(3, "probe-error", str(e))
    meta["library"] = str(link.path)

    try:
        link.open(args.serial, str((out / "jlink.log").resolve()))
        meta["probe"] = {
            "serial": link.serial(),
            "dll_version": link.dll_version(),
            "firmware": link.firmware(),
        }
        link.connect(args.device, args.speed, args.script)
        if args.survey:
            survey(link, meta)
            link.go()
            return finish(0, "surveyed")
        return run(link, args, image, ranges, rtt_addr, out, meta, calls, finish)
    except (jlink.JLinkError, LookupError, TimeoutError, ValueError) as e:
        return finish(3, "probe-error", str(e))
    finally:
        link.close()


def run(link, args, image, ranges, rtt_addr, out, meta, calls, finish) -> int:
    load_elf(link, image, meta.setdefault("load", {}))
    bus = MemBus(link)
    meta["jttrcsel"] = f"0x{coresight.enable_trace_pins(bus):08X}"
    comps = coresight.walk_rom(bus)
    meta["components"] = [c.describe() for c in comps]
    path = coresight.TracePath.from_components(comps)
    meta["path"] = {
        k: (None if v is None else f"0x{v:08X}") for k, v in vars(path).items()
    }
    cfg = coresight.PtmConfig(
        ranges=ranges,
        trace_id=args.trace_id,
        cycle_accurate=not args.no_cycle_accurate,
        timestamps=args.timestamps,
        branch_broadcast=args.branch_broadcast,
    )
    meta["ptm"] = {
        k: f"0x{v:08X}"
        for k, v in coresight.program(
            bus, path, cfg, funnel_ports=args.funnel_ports
        ).items()
    }
    meta["trace_id"] = cfg.trace_id
    print(f"capture: PTM {meta['ptm']}", file=sys.stderr)

    for cmd in args.exec:
        calls[f"exec {cmd}"] = link.exec_command(cmd) or "ok"
    calls["RAWTRACE_GET_CAPS"] = link.rawtrace(jlink.RAWTRACE_GET_CAPS)
    calls["RAWTRACE_GET_TRACE_FREQ"] = link.rawtrace(jlink.RAWTRACE_GET_TRACE_FREQ)
    calls["RAWTRACE_START"] = link.rawtrace(jlink.RAWTRACE_START)[0]
    meta["after_start"] = dump_path(bus, path)

    untils = [re.compile(p) for p in (args.until or [DEFAULT_UNTIL])]
    fails = [re.compile(p) for p in (args.fail_on or list(DEFAULT_FAIL))]
    rtt_lines: list[str] = []
    trace = bytearray()
    reads = []
    # The RAM load leaves SDRAM as the last run left it, its RTT control block and text included. Erase
    # the block so the DLL finds it only once the new image has set it up, not the old run's last lines.
    link.write_mem(rtt_addr, bytes(RTT_CONTROL_BLOCK_BYTES))
    link.go()
    started = time.monotonic()
    link.rtt_start(rtt_addr)
    outcome: tuple[int, str] | None = None
    pending = ""
    while outcome is None and time.monotonic() - started < args.timeout:
        chunk = link.rtt_read()
        if chunk:
            pending += chunk.decode("utf-8", "replace")
            while "\n" in pending:
                line, pending = pending.split("\n", 1)
                line = line.rstrip("\r")
                rtt_lines.append(line)
                if any(p.search(line) for p in fails):
                    outcome = (1, "fail")
                elif any(p.search(line) for p in untils):
                    outcome = (0, "pattern")
                if outcome:
                    break
        else:
            time.sleep(0.02)
    meta["run_seconds"] = round(time.monotonic() - started, 2)
    if outcome is None:
        outcome = (2, "timeout")

    link.halt()
    coresight.stop(bus, path)
    calls["RAWTRACE_STOP"] = link.rawtrace(jlink.RAWTRACE_STOP)[0]
    while True:
        piece = link.rawtrace_read()
        reads.append(len(piece))
        if not piece:
            break
        trace += piece
    calls["RAWTRACE_Read"] = reads
    link.rtt_stop()
    # Resume: a bench image that reached its parked loop stays there, dark.
    link.go()

    (out / "trace.bin").write_bytes(bytes(trace))
    (out / "rtt.txt").write_text("\n".join(rtt_lines) + ("\n" if rtt_lines else ""))
    meta["trace_bytes"] = len(trace)
    meta["rtt_lines"] = len(rtt_lines)
    print(
        f"capture: {outcome[1]}, {len(trace)} trace bytes, {len(rtt_lines)} RTT lines",
        file=sys.stderr,
    )
    return finish(*outcome)


if __name__ == "__main__":
    sys.exit(main())
