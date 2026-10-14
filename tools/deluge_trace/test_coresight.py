"""The CoreSight ROM walk and trace-path programming, against a modelled debug APB."""

import unittest

import coresight as cs


def ids(cls: int, part: int, devtype: int = 0, devid: int = 0) -> dict[int, int]:
    """A component's identification registers."""
    return {
        cs.CIDR0: 0x0D,
        cs.CIDR1: cls << 4,
        cs.CIDR2: 0x05,
        cs.CIDR3: 0xB1,
        cs.PIDR0: part & 0xFF,
        cs.PIDR1: 0xB0 | (part >> 8),
        cs.PIDR2: 0x0B,
        cs.PIDR4: 0x04,
        cs.DEVTYPE: devtype,
        cs.DEVID: devid,
    }


class FakeBus:
    """Memory, plus the PTM reporting its Programming bit as soon as it is written."""

    def __init__(self, ptm: int | None = None) -> None:
        self.mem: dict[int, int] = {}
        self.writes: list[tuple[int, int]] = []
        self.ptm = ptm

    def place(self, base: int, regs: dict[int, int]) -> None:
        for off, value in regs.items():
            self.mem[base + off] = value

    def read_u32(self, addr: int) -> int:
        if self.ptm is not None and addr == self.ptm + cs.ETMSR:
            return (
                cs.ETMSR_PROGBIT
                if self.mem.get(self.ptm, 0) & cs.ETMCR_PROGRAMMING
                else 0
            )
        return self.mem.get(addr, 0)

    def write_u32(self, addr: int, value: int) -> None:
        self.writes.append((addr, value))
        self.mem[addr] = value


PTM, ETF, TPIU, FUNNEL = 0xFC03C000, 0xFC021000, 0xFC023000, 0xFC024000


def rza1_bus() -> FakeBus:
    """The RZ/A1L's two ROM tables as the rig reported them, trimmed to the trace path."""
    bus = FakeBus(ptm=PTM)
    bus.place(cs.DAP_ROM, ids(cs.CLASS_ROM, 0x012))
    bus.place(cs.DAP_ROM, {0: 0x00020003, 4: 0x0003C003, 8: 0x00F00002, 12: 0})
    bus.place(0xFC020000, ids(cs.CLASS_ROM, 0x4A9))
    bus.place(
        0xFC020000, {0: 0x00001003, 4: 0x00003003, 8: 0x00004003, 12: 0xFFFE0003, 16: 0}
    )
    bus.place(ETF, ids(cs.CLASS_CORESIGHT, 0x961, cs.DEVTYPE_ETF, 0x380))
    bus.place(TPIU, ids(cs.CLASS_CORESIGHT, 0x912, cs.DEVTYPE_TPIU, 0xA0))
    bus.place(FUNNEL, ids(cs.CLASS_CORESIGHT, 0x908, cs.DEVTYPE_FUNNEL, 0x28))
    bus.place(PTM, ids(cs.CLASS_CORESIGHT, 0x950, cs.DEVTYPE_TRACE_SOURCE))
    bus.mem[TPIU + cs.TPIU_SSPSR] = 0xF
    bus.mem[ETF + cs.TMC_STS] = cs.TMC_STS_READY
    return bus


class WalkRom(unittest.TestCase):
    def test_finds_the_trace_path_through_the_nested_table(self):
        comps = cs.walk_rom(rza1_bus())
        self.assertEqual(
            [(c.base, c.part) for c in comps],
            [
                (cs.DAP_ROM, 0x012),
                (0xFC020000, 0x4A9),
                (ETF, 0x961),
                (TPIU, 0x912),
                (FUNNEL, 0x908),
                (PTM, 0x950),
            ],
        )
        self.assertEqual(comps[-1].designer, 0x23B)  # ARM's JEP106 code
        path = cs.TracePath.from_components(comps)
        self.assertEqual(
            (path.ptm, path.funnel, path.etf, path.tpiu), (PTM, FUNNEL, ETF, TPIU)
        )

    def test_missing_ptm_is_named(self):
        bus = rza1_bus()
        bus.mem[cs.DAP_ROM + 4] = 0
        with self.assertRaisesRegex(LookupError, "no PTM"):
            cs.TracePath.from_components(cs.walk_rom(bus))


class PtmWrites(unittest.TestCase):
    def test_include_ranges_cycle_accurate(self):
        cfg = cs.PtmConfig(ranges=[(0x200F3598, 0x200F35B8), (0x200F4D6D, 0x200F4E44)])
        writes = dict(cs.ptm_writes(cfg))
        self.assertEqual(
            writes[cs.ETMCR], cs.ETMCR_CYCLE_ACCURATE | cs.ETMCR_PROGRAMMING
        )
        self.assertEqual(writes[cs.ETMTEEVR], cs.EVENT_TRUE)
        self.assertEqual(writes[cs.ETMTRIGGER], cs.EVENT_NEVER)
        self.assertEqual(writes[cs.ETMTECR1], 0b11)
        self.assertEqual(
            [writes[cs.ETMACVR + 4 * k] for k in range(4)],
            [0x200F3598, 0x200F35B8, 0x200F4D6C, 0x200F4E44],
        )
        self.assertEqual({writes[cs.ETMACTR + 4 * k] for k in range(4)}, {1})
        self.assertEqual(writes[cs.ETMTRACEIDR], 0x10)

    def test_options_set_their_bits(self):
        cfg = cs.PtmConfig(
            ranges=[(0, 4)],
            cycle_accurate=False,
            timestamps=True,
            branch_broadcast=True,
        )
        self.assertEqual(cfg.etmcr(), cs.ETMCR_TIMESTAMP | cs.ETMCR_BRANCH_OUTPUT)

    def test_rejects_what_the_ptm_cannot_do(self):
        with self.assertRaises(ValueError):
            cs.ptm_writes(cs.PtmConfig(ranges=[(0, 4)] * 5))
        with self.assertRaises(ValueError):
            cs.ptm_writes(cs.PtmConfig(ranges=[(0, 4)], trace_id=0x70))
        with self.assertRaises(ValueError):
            cs.ptm_writes(cs.PtmConfig(ranges=[(8, 8)]))


class Program(unittest.TestCase):
    def test_programs_sink_first_and_leaves_the_ptm_running(self):
        bus = rza1_bus()
        path = cs.TracePath(PTM, FUNNEL, ETF, TPIU)
        cs.program(bus, path, cs.PtmConfig(ranges=[(0x200F3598, 0x200F35B8)]))
        first = {addr: i for i, (addr, _) in reversed(list(enumerate(bus.writes)))}
        self.assertLess(first[TPIU + cs.TPIU_CSPSR], first[PTM + cs.ETMTECR1])
        for base in (PTM, FUNNEL, ETF, TPIU):
            self.assertIn((base + cs.LAR, cs.LAR_KEY), bus.writes)
        self.assertEqual(bus.mem[TPIU + cs.TPIU_CSPSR], 0x8)
        self.assertEqual(bus.mem[TPIU + cs.TPIU_FFCR], 0x103)
        self.assertEqual(bus.mem[ETF + cs.TMC_MODE], cs.TMC_MODE_HARDWARE_FIFO)
        self.assertEqual(bus.mem[ETF + cs.TMC_CTL], 1)
        self.assertEqual(bus.mem[FUNNEL + cs.FUNCTL], 0x301)
        self.assertEqual(bus.mem[PTM + cs.ETMCR], cs.ETMCR_CYCLE_ACCURATE)

    def test_refuses_a_port_the_tpiu_lacks(self):
        bus = rza1_bus()
        bus.mem[TPIU + cs.TPIU_SSPSR] = 0x1
        with self.assertRaisesRegex(ValueError, "4-bit"):
            cs.program(
                bus, cs.TracePath(PTM, FUNNEL, ETF, TPIU), cs.PtmConfig(ranges=[(0, 4)])
            )

    def test_trace_pins_unlock_the_ice_registers_first(self):
        bus = FakeBus()
        bus.mem[cs.ICEREG_JTTRCSEL] = 0x00800000
        self.assertEqual(cs.enable_trace_pins(bus), 0x00900000)
        self.assertEqual(bus.writes[0], (cs.ICEREG_LOCKACCESS, cs.LAR_KEY))


if __name__ == "__main__":
    unittest.main()
