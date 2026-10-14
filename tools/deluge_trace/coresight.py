"""The RZ/A1L's CoreSight trace path, found from the ROM tables and programmed for PTM program-flow trace
out of the 4-bit trace port.

Everything goes through a `bus` with read_u32/write_u32, the CPU's view of the debug APB (0xFC0xxxxx), so
the sequences here run as well against a dictionary in a test as against the probe. Through the CPU's
view each component's Lock Access Register applies, so every component is unlocked before it is written.

The path (RZ/A1L hardware manual, figure 43.2): PTM-A9 -> trace funnel -> 4 KB ETF -> TPIU -> P4_2..P4_7.
Register offsets and bits are from the PTM-A9 TRM (ARM DDI 0401C), the CoreSight TMC and TPIU TRMs, and
the CoreSight architecture's component identification registers.
"""

from __future__ import annotations

import time
from dataclasses import dataclass
from typing import Protocol

LAR_KEY = 0xC5ACCE55

# Component identification, common to every CoreSight component.
LAR = 0xFB0
LSR = 0xFB4
DEVID = 0xFC8
DEVTYPE = 0xFCC
PIDR4 = 0xFD0
PIDR0 = 0xFE0
PIDR1 = 0xFE4
PIDR2 = 0xFE8
CIDR0 = 0xFF0
CIDR1 = 0xFF4
CIDR2 = 0xFF8
CIDR3 = 0xFFC
CLASS_ROM = 0x1
CLASS_CORESIGHT = 0x9

# DEVTYPE: major type in [3:0], sub type in [7:4].
DEVTYPE_TPIU = 0x11
DEVTYPE_FUNNEL = 0x12
DEVTYPE_TRACE_SOURCE = 0x13
DEVTYPE_ETB = 0x21
DEVTYPE_ETF = 0x32

# ICE registers (hardware manual 43.4): the CPU must unlock them before writing.
ICEREG_JTTRCSEL = 0xFC00F004
ICEREG_LOCKACCESS = 0xFC00FFB0
TRCMUX_SEL = 1 << 20

DAP_ROM = 0xFC000000

# PTM registers (DDI 0401C table 2.2).
ETMCR = 0x000
ETMCCR = 0x004
ETMTRIGGER = 0x008
ETMSR = 0x010
ETMTSSCR = 0x018
ETMTEEVR = 0x020
ETMTECR1 = 0x024
ETMACVR = 0x040  # + 4*(n-1), n = 1..8
ETMACTR = 0x080  # + 4*(n-1)
ETMSYNCFR = 0x1E0
ETMIDR = 0x1E4
ETMCCER = 0x1E8
ETMEXTINSELR = 0x1EC
ETMTSEVR = 0x1F8
ETMAUXCR = 0x1FC
ETMTRACEIDR = 0x200
ETMCR_POWERDOWN = 1 << 0
ETMCR_BRANCH_OUTPUT = 1 << 8
ETMCR_PROGRAMMING = 1 << 10
ETMCR_CYCLE_ACCURATE = 1 << 12
ETMCR_TIMESTAMP = 1 << 28
ETMSR_PROGBIT = 1 << 1
ETMTECR1_EXCLUDE = 1 << 24
# The event "resource A, always true" and its negation, "never".
EVENT_TRUE = 0x6F
EVENT_NEVER = 0x406F
# Address comparator access type: instruction execute.
ACTR_INSTRUCTION_EXECUTE = 0x1

# Trace funnel (CSTF).
FUNCTL = 0x000
FUNCTL_HOLD_TIME = 0x3 << 8

# TMC in ETF configuration.
TMC_STS = 0x00C
TMC_CTL = 0x020
TMC_MODE = 0x028
TMC_BUFWM = 0x034
TMC_FFSR = 0x300
TMC_FFCR = 0x304
TMC_STS_READY = 1 << 2
TMC_MODE_HARDWARE_FIFO = 0x2
TMC_FFCR_ENFT = 1 << 0
TMC_FFCR_ENTI = 1 << 1

# TPIU.
TPIU_SSPSR = 0x000
TPIU_CSPSR = 0x004
TPIU_FFSR = 0x300
TPIU_FFCR = 0x304
TPIU_FSCR = 0x308
TPIU_FFCR_ENFTC = 1 << 0
TPIU_FFCR_ENFCONT = 1 << 1
TPIU_FFCR_FONMAN = 1 << 6
TPIU_FFCR_TRIGIN = 1 << 8
TPIU_FFSR_FLINPROG = 1 << 0


class Bus(Protocol):
    def read_u32(self, addr: int) -> int: ...
    def write_u32(self, addr: int, value: int) -> None: ...


@dataclass(frozen=True)
class Component:
    """One CoreSight component found in a ROM table."""

    base: int
    cls: int
    part: int
    designer: int
    devtype: int
    devid: int

    def describe(self) -> str:
        names = {
            0x950: "PTM-A9",
            0xC09: "Cortex-A9 debug",
            0x9A0: "Cortex-A9 PMU",
            0x906: "CTI",
            0x908: "trace funnel",
            0x961: "TMC (ETF)",
            0x912: "TPIU",
            0x907: "ETB",
        }
        what = "ROM table" if self.cls == CLASS_ROM else names.get(self.part, "unknown")
        return (
            f"0x{self.base:08X} class=0x{self.cls:X} part=0x{self.part:03X} "
            f"devtype=0x{self.devtype:02X} devid=0x{self.devid:08X} {what}"
        )


def identify(bus: Bus, base: int) -> Component | None:
    """Read a component's identification registers; None when the preamble is not CoreSight's."""
    cidr = [bus.read_u32(base + off) & 0xFF for off in (CIDR0, CIDR1, CIDR2, CIDR3)]
    if cidr[0] != 0x0D or (cidr[1] & 0x0F) != 0x0 or cidr[2] != 0x05 or cidr[3] != 0xB1:
        return None
    pidr0 = bus.read_u32(base + PIDR0) & 0xFF
    pidr1 = bus.read_u32(base + PIDR1) & 0xFF
    pidr2 = bus.read_u32(base + PIDR2) & 0xFF
    pidr4 = bus.read_u32(base + PIDR4) & 0xFF
    cls = cidr[1] >> 4
    devtype = devid = 0
    if cls == CLASS_CORESIGHT:
        devtype = bus.read_u32(base + DEVTYPE) & 0xFF
        devid = bus.read_u32(base + DEVID)
    designer = ((pidr4 & 0xF) << 7) | ((pidr2 & 0x7) << 4) | (pidr1 >> 4)
    return Component(base, cls, ((pidr1 & 0xF) << 8) | pidr0, designer, devtype, devid)


def walk_rom(bus: Bus, rom: int = DAP_ROM, max_entries: int = 960) -> list[Component]:
    """Every component reachable from the ROM table at `rom`, nested tables included, in table order."""
    found: list[Component] = []
    seen: set[int] = set()

    def visit(table: int) -> None:
        if table in seen:
            return
        seen.add(table)
        me = identify(bus, table)
        if me is not None:
            found.append(me)
        for i in range(max_entries):
            entry = bus.read_u32(table + 4 * i)
            if entry == 0:
                break
            if not entry & 1:
                continue
            offset = entry & 0xFFFFF000
            if offset & 0x80000000:
                offset -= 1 << 32
            base = (table + offset) & 0xFFFFFFFF
            comp = identify(bus, base)
            if comp is None:
                continue
            if comp.cls == CLASS_ROM:
                visit(base)
            elif base not in seen:
                seen.add(base)
                found.append(comp)

    visit(rom)
    return found


@dataclass
class TracePath:
    """The components the trace runs through."""

    ptm: int
    funnel: int
    etf: int | None
    tpiu: int

    @classmethod
    def from_components(cls, comps: list[Component]) -> TracePath:
        def one(devtype: int, what: str) -> int:
            hits = [c.base for c in comps if c.devtype == devtype]
            if not hits:
                raise LookupError(
                    f"no {what} (devtype 0x{devtype:02X}) in the ROM tables"
                )
            return hits[0]

        etfs = [c.base for c in comps if c.devtype == DEVTYPE_ETF]
        return cls(
            ptm=one(DEVTYPE_TRACE_SOURCE, "PTM"),
            funnel=one(DEVTYPE_FUNNEL, "trace funnel"),
            etf=etfs[0] if etfs else None,
            tpiu=one(DEVTYPE_TPIU, "TPIU"),
        )


@dataclass
class PtmConfig:
    """What the PTM traces: up to four instruction address ranges, [start, end)."""

    ranges: list[tuple[int, int]]
    trace_id: int = 0x10
    cycle_accurate: bool = True
    timestamps: bool = False
    branch_broadcast: bool = False
    sync_frequency: int = 0x400

    def etmcr(self) -> int:
        value = 0
        if self.cycle_accurate:
            value |= ETMCR_CYCLE_ACCURATE
        if self.timestamps:
            value |= ETMCR_TIMESTAMP
        if self.branch_broadcast:
            value |= ETMCR_BRANCH_OUTPUT
        return value


def ptm_writes(cfg: PtmConfig) -> list[tuple[int, int]]:
    """The PTM register writes (offset, value) made while the Programming bit is set.

    TraceEnable is the event "always" gated by include ranges: the ranges' address comparator pairs
    1/2, 3/4, ... select where trace is generated, and the start/stop block is off. The PFT has no
    separate ViewInst: TraceEnable is the filter.
    """
    if not 1 <= len(cfg.ranges) <= 4:
        raise ValueError(
            "the PTM-A9 has four address range comparators: give 1 to 4 ranges"
        )
    if not 0 < cfg.trace_id < 0x70:
        raise ValueError("trace IDs 0x00 and 0x70-0x7F are reserved")
    writes = [
        (ETMCR, cfg.etmcr() | ETMCR_PROGRAMMING),
        (ETMTRIGGER, EVENT_NEVER),
        (ETMTSSCR, 0),
        (ETMTEEVR, EVENT_TRUE),
    ]
    tecr1 = 0
    for k, (start, end) in enumerate(cfg.ranges):
        if end <= start:
            raise ValueError(f"empty range 0x{start:08X}-0x{end:08X}")
        lo, hi = 2 * k, 2 * k + 1
        writes += [
            (ETMACVR + 4 * lo, start & ~1),
            (ETMACTR + 4 * lo, ACTR_INSTRUCTION_EXECUTE),
            (ETMACVR + 4 * hi, end & ~1),
            (ETMACTR + 4 * hi, ACTR_INSTRUCTION_EXECUTE),
        ]
        tecr1 |= 1 << k
    writes += [
        (ETMTECR1, tecr1),
        (ETMSYNCFR, cfg.sync_frequency),
        (ETMEXTINSELR, 0),
        (ETMTSEVR, EVENT_NEVER),
        (ETMAUXCR, 0),
        (ETMTRACEIDR, cfg.trace_id),
    ]
    return writes


def _wait(bus: Bus, addr: int, mask: int, want: int, timeout: float = 1.0) -> bool:
    deadline = time.monotonic() + timeout
    while True:
        if (bus.read_u32(addr) & mask) == want:
            return True
        if time.monotonic() > deadline:
            return False
        time.sleep(0.001)


def unlock(bus: Bus, base: int) -> None:
    bus.write_u32(base + LAR, LAR_KEY)


def enable_trace_pins(bus: Bus) -> int:
    """Set TRCMUX_SEL so P4_2..P4_7 carry the trace port; return the register's new value.

    It stays set until reset, and while it is set the SPI flash pins are trace pins.
    """
    bus.write_u32(ICEREG_LOCKACCESS, LAR_KEY)
    value = bus.read_u32(ICEREG_JTTRCSEL) | TRCMUX_SEL
    bus.write_u32(ICEREG_JTTRCSEL, value)
    return bus.read_u32(ICEREG_JTTRCSEL)


def program(
    bus: Bus,
    path: TracePath,
    cfg: PtmConfig,
    port_size: int = 4,
    continuous: bool = True,
    funnel_ports: int = 0x1,
) -> dict[str, int]:
    """Program the sink end first (TPIU, ETF, funnel), then the PTM; return the registers read back."""
    for base in (path.tpiu, path.etf, path.funnel, path.ptm):
        if base is not None:
            unlock(bus, base)

    # TPIU: the port width, then the formatter. Continuous formatting puts frame syncs on the port, which
    # is how a capture without TRACECTL finds the frame boundaries.
    supported = bus.read_u32(path.tpiu + TPIU_SSPSR)
    width_bit = 1 << (port_size - 1)
    if not supported & width_bit:
        raise ValueError(
            f"TPIU does not support a {port_size}-bit port (SSPSR 0x{supported:08X})"
        )
    bus.write_u32(path.tpiu + TPIU_CSPSR, width_bit)
    ffcr = TPIU_FFCR_ENFTC | TPIU_FFCR_TRIGIN
    if continuous:
        ffcr |= TPIU_FFCR_ENFCONT
    bus.write_u32(path.tpiu + TPIU_FFCR, ffcr)

    # ETF as a hardware FIFO between the funnel and the TPIU, formatting in and out.
    if path.etf is not None:
        bus.write_u32(path.etf + TMC_CTL, 0)
        _wait(bus, path.etf + TMC_STS, TMC_STS_READY, TMC_STS_READY)
        bus.write_u32(path.etf + TMC_MODE, TMC_MODE_HARDWARE_FIFO)
        bus.write_u32(path.etf + TMC_FFCR, TMC_FFCR_ENFT | TMC_FFCR_ENTI)
        bus.write_u32(path.etf + TMC_BUFWM, 0)
        bus.write_u32(path.etf + TMC_CTL, 1)

    # Funnel: only the PTM's port. An enabled port with nothing behind it never answers a flush, and the
    # ETF's flush (which the DLL issues at RAWTRACE START) then hangs the whole path.
    bus.write_u32(path.funnel + FUNCTL, FUNCTL_HOLD_TIME | funnel_ports)

    # PTM: power up and set the Programming bit, wait for the PTM to report it, program, then clear it.
    p = path.ptm
    bus.write_u32(
        p + ETMCR, (bus.read_u32(p + ETMCR) & ~ETMCR_POWERDOWN) | ETMCR_PROGRAMMING
    )
    if not _wait(bus, p + ETMSR, ETMSR_PROGBIT, ETMSR_PROGBIT):
        raise TimeoutError("the PTM never reported its Programming bit set")
    for off, value in ptm_writes(cfg):
        bus.write_u32(p + off, value)
    bus.write_u32(p + ETMCR, cfg.etmcr())
    if not _wait(bus, p + ETMSR, ETMSR_PROGBIT, 0):
        raise TimeoutError("the PTM never left programming")

    regs = {
        "ETMCR": bus.read_u32(p + ETMCR),
        "ETMCCR": bus.read_u32(p + ETMCCR),
        "ETMSR": bus.read_u32(p + ETMSR),
        "ETMIDR": bus.read_u32(p + ETMIDR),
        "ETMCCER": bus.read_u32(p + ETMCCER),
        "ETMTRACEIDR": bus.read_u32(p + ETMTRACEIDR),
        "ETMTECR1": bus.read_u32(p + ETMTECR1),
        "ETMTEEVR": bus.read_u32(p + ETMTEEVR),
        "TPIU_CSPSR": bus.read_u32(path.tpiu + TPIU_CSPSR),
        "TPIU_FFCR": bus.read_u32(path.tpiu + TPIU_FFCR),
        "TPIU_FFSR": bus.read_u32(path.tpiu + TPIU_FFSR),
        "FUNCTL": bus.read_u32(path.funnel + FUNCTL),
    }
    for k in range(2 * len(cfg.ranges)):
        regs[f"ETMACVR{k + 1}"] = bus.read_u32(p + ETMACVR + 4 * k)
        regs[f"ETMACTR{k + 1}"] = bus.read_u32(p + ETMACTR + 4 * k)
    if path.etf is not None:
        regs["ETF_MODE"] = bus.read_u32(path.etf + TMC_MODE)
        regs["ETF_CTL"] = bus.read_u32(path.etf + TMC_CTL)
        regs["ETF_STS"] = bus.read_u32(path.etf + TMC_STS)
    return regs


def stop(bus: Bus, path: TracePath) -> None:
    """Stop the PTM (its Programming bit drains its FIFO), flush the ETF and TPIU, and power the PTM down."""
    p = path.ptm
    bus.write_u32(p + ETMCR, bus.read_u32(p + ETMCR) | ETMCR_PROGRAMMING)
    _wait(bus, p + ETMSR, ETMSR_PROGBIT, ETMSR_PROGBIT)
    ffcr = bus.read_u32(path.tpiu + TPIU_FFCR)
    bus.write_u32(path.tpiu + TPIU_FFCR, ffcr | TPIU_FFCR_FONMAN)
    _wait(bus, path.tpiu + TPIU_FFSR, TPIU_FFSR_FLINPROG, 0)
    bus.write_u32(p + ETMCR, bus.read_u32(p + ETMCR) | ETMCR_POWERDOWN)
