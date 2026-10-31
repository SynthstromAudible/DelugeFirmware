"""The report's mapping of a decoded trace, with cycle counts, onto the disassembly.

The canned decode is two calls of CalibrationLoop::block in trc_pkt_lister's format: the first runs
three loop iterations (8, 8 and 9 cycles) and returns; the second runs one (8), loses trace to an
overflow, and runs two more (7, 8).
"""

import unittest
from pathlib import Path

import report

DATA = Path(__file__).parent / "testdata"
LO, HI = 0x200F3598, 0x200F35B8
HEAD, CLOSER = 0x200F35A2, 0x200F35AC


class Fixture(unittest.TestCase):
    def setUp(self):
        self.dec = report.parse_decode((DATA / "calibration_decode.txt").read_text())
        self.insns = report.parse_objdump(
            (DATA / "calibration_objdump.txt").read_text()
        )
        self.result = report.analyse(self.dec, self.insns, LO, HI)

    def block(self, start, end):
        (row,) = [
            b for b in self.result["blocks"] if (b["start"], b["end"]) == (start, end)
        ]
        return row

    def line(self, address):
        (row,) = [i for i in self.result["instructions"] if i["address"] == address]
        return row


class ParseDecode(Fixture):
    def test_reads_ranges_with_their_cycles(self):
        ranges = [i for i in self.dec.items if isinstance(i, report.Range)]
        self.assertEqual(len(ranges), 11)
        first = ranges[0]
        self.assertEqual(
            (first.start, first.end, first.count), (0x200F3598, 0x200F359E, 3)
        )
        self.assertFalse(first.taken)
        self.assertEqual(first.cycles, 2)
        self.assertEqual(first.last, 0x200F359C)

    def test_trace_on_and_breaks_split_the_sequence(self):
        self.assertEqual(dict(self.dec.trace_on), {"begin or filter": 2, "overflow": 1})
        self.assertEqual([len(run) for run in report.segments(self.dec)], [6, 3, 2])

    def test_trace_on_keeps_its_reason_and_the_cycles_it_was_off(self):
        gaps = [i for i in self.dec.items if isinstance(i, report.TraceOn)]
        self.assertEqual(
            [(g.reason, g.cycles) for g in gaps],
            [("begin or filter", 500), ("begin or filter", 700), ("overflow", 944)],
        )

    def test_a_range_without_a_cycle_count_has_none(self):
        dec = report.parse_decode(
            "Idx:1; ID:10; OCSD_GEN_TRC_ELEM_INSTR_RANGE(exec range=0x10:[0x14] num_i(2) "
            "last_sz(2) (ISA=T32) E BR   <cond> )\n"
        )
        self.assertIsNone(dec.items[0].cycles)


class ParseObjdump(Fixture):
    def test_maps_every_instruction(self):
        self.assertEqual(len(self.insns), 14)
        self.assertEqual(min(self.insns), LO)
        self.assertIn("mul\tr1, r1, r3", self.insns[HEAD])


class Loops(Fixture):
    def test_times_every_complete_iteration(self):
        self.assertEqual(
            report.loop_iterations(self.dec, LO, HI), {HEAD: [8, 8, 9, 8, 7, 8]}
        )

    def test_summarises_the_loop(self):
        (loop,) = self.result["loops"]
        self.assertEqual(loop["head"], HEAD)
        self.assertEqual(
            loop["cycles"],
            {"n": 6, "mean": 8.0, "min": 7, "p50": 8, "p90": 9, "max": 9},
        )
        self.assertEqual(loop["histogram"], "8:67%, 9:17%, 7:17%")

    def test_iteration_missing_a_cycle_count_is_left_out(self):
        text = (
            (DATA / "calibration_decode.txt")
            .read_text()
            .replace("N BR   <cond> [CC=9]; )", "N BR   <cond> )")
        )
        dec = report.parse_decode(text)
        self.assertEqual(report.loop_iterations(dec, LO, HI), {HEAD: [8, 8, 8, 7, 8]})

    def test_ranges_outside_the_function_are_not_loops(self):
        self.assertEqual(report.loop_iterations(self.dec, 0x200F3598, 0x200F35A0), {})


class Blocks(Fixture):
    def test_counts_and_times_each_block(self):
        body = self.block(HEAD, 0x200F35AE)
        self.assertEqual(body["executions"], 6)
        self.assertEqual(body["instructions"], 4)
        self.assertEqual(body["cycles"]["mean"], 8.0)
        self.assertEqual(body["even_share"], 2.0)
        entry = self.block(0x200F359E, 0x200F35AE)
        self.assertEqual((entry["executions"], entry["instructions"]), (2, 6))
        self.assertEqual(entry["even_share"], 0.5)
        ret = self.block(0x200F35AE, 0x200F35B2)
        self.assertEqual(ret["cycles"]["max"], 16)

    def test_instruction_executions_sum_every_block_covering_it(self):
        self.assertEqual(self.line(HEAD)["executions"], 8)
        self.assertEqual(self.line(LO)["executions"], 2)
        self.assertEqual(self.line(0x200F35AE)["executions"], 1)
        self.assertEqual(self.line(0x200F35B2)["executions"], 0)

    def test_shares_and_waypoints_are_weighted_by_executions(self):
        # The loop's instructions are in the entry block (2 timed runs, 0.5 each) and the body
        # (6 timed runs, 2.0 each); its closing branch ends both (3 and 8 cycles).
        self.assertAlmostEqual(self.line(HEAD)["even_share"], (2 * 0.5 + 6 * 2.0) / 8)
        self.assertAlmostEqual(self.line(CLOSER)["waypoint"], (2 * 3 + 6 * 8) / 8)
        self.assertIsNone(self.line(HEAD)["waypoint"])
        self.assertIsNone(self.line(0x200F35B2)["even_share"])

    def test_render_shows_the_loop_and_the_disassembly(self):
        text = report.render("CalibrationLoop::block", LO, HI, self.result, self.dec)
        self.assertIn("| `0x200F35A2` | 6 | 8.0 | 7 | 8 | 9 | 9 |", text)
        self.assertIn("overflow: 1", text)
        self.assertRegex(text, r"200f35ac\s+8\s+1\.62\s+6\.75\s+d1f9\s+bne")


if __name__ == "__main__":
    unittest.main()


def _range(start, end, cycles, ret=False):
    kind = "iBR V7:impl ret" if ret else "BR  "
    return (
        f"Idx:1; ID:10; OCSD_GEN_TRC_ELEM_INSTR_RANGE(exec range=0x{start:x}:[0x{end:x}] num_i(2) "
        f"last_sz(4) (ISA=A32) E {kind} [CC={cycles}]; )\n"
    )


def _trace_on(reason, cycles):
    return f"Idx:1; ID:10; OCSD_GEN_TRC_ELEM_TRACE_ON( [{reason}] [CC={cycles}]; )\n"


class Calls(unittest.TestCase):
    LO, HI = 0x1000, 0x1100

    def calls(self, text):
        return report.calls(report.parse_decode(text), self.LO, self.HI)

    def test_a_call_sums_its_ranges_and_the_time_in_its_callees(self):
        text = (
            _range(self.LO, 0x1008, 3)
            + _trace_on("begin or filter", 50)  # a call out
            + _range(0x1008, 0x1010, 4, ret=True)
            + _trace_on("begin or filter", 100)  # the caller, between calls
        )
        self.assertEqual(
            self.calls(text), [{"own": 7, "outside": 50, "calls_out": 1, "total": 57}]
        )

    def test_a_call_that_lost_trace_is_dropped(self):
        text = (
            _range(self.LO, 0x1008, 3)
            + _trace_on("overflow", 900)
            + _range(0x1008, 0x1010, 4, ret=True)
            + _range(self.LO, 0x1008, 2)
            + _range(0x1008, 0x1010, 5, ret=True)
        )
        self.assertEqual(
            self.calls(text), [{"own": 7, "outside": 0, "calls_out": 0, "total": 7}]
        )

    def test_traced_code_reached_after_the_return_is_not_the_calls(self):
        text = (
            _range(self.LO, 0x1008, 3, ret=True)
            + _trace_on("begin or filter", 1000)
            + _range(
                0x2000, 0x2008, 9, ret=True
            )  # another traced function, from an untraced caller
            + _trace_on("begin or filter", 1000)
            + _range(self.LO, 0x1008, 1, ret=True)
        )
        self.assertEqual([c["total"] for c in self.calls(text)], [3, 1])

    def test_a_call_the_capture_cuts_short_is_dropped(self):
        self.assertEqual(self.calls(_range(self.LO, 0x1008, 3)), [])
