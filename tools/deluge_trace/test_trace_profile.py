"""Tests for trace_profile.py: `python3 -m unittest test_trace_profile`."""

import re
import unittest

import trace_profile as prof

# Function by address: main at 0x1000, load at 0x2000, open at 0x3000, the audio handler at 0x4000.
FUNCS = [(0x1000, "main"), (0x2000, "load"), (0x3000, "open"), (0x4000, "audio_irq")]


def name_of(address: int) -> str:
    for start, name in reversed(FUNCS):
        if address >= start:
            return name
    return "?"


def rng(address: int, cycles: int, end: str = "E BR  ") -> str:
    return (
        f"Idx:1; ID:10; OCSD_GEN_TRC_ELEM_INSTR_RANGE(exec range=0x{address:x}:[0x{address + 4:x}] "
        f"num_i(2) last_sz(2) (ISA=T32) {end} [CC={cycles}]; )"
    )


CALL = "E BR  b+link  "
RET = "E iBR V7:impl ret "


def exception(resume: int) -> str:
    return f"Idx:2; ID:10; OCSD_GEN_TRC_ELEM_EXCEPTION(pref ret addr:0x{resume:x}; excep num (0x0e)  [CC=0]; )"


TRACE = [
    rng(0x1000, 10, CALL),  # main calls load
    rng(0x2000, 20, CALL),  # load calls open
    rng(0x3000, 100),  # open runs ...
    exception(0x3010),  # ... and is interrupted
    rng(0x4000, 50, RET),  # the handler runs and returns
    rng(0x3010, 30, RET),  # open resumes and returns to load
    rng(0x2008, 40, RET),  # load returns to main
    rng(0x1008, 5),
]


class ProfileTests(unittest.TestCase):
    def test_self_time_belongs_to_the_innermost_function(self) -> None:
        p = prof.profile(TRACE, name_of)
        self.assertEqual(p.total, 255)
        self.assertEqual(p.self_cycles["open"], 130)
        self.assertEqual(p.self_cycles["[exception 0x0e]"], 0)
        self.assertEqual(p.self_cycles["audio_irq"], 50)

    def test_inclusive_time_counts_callees_but_not_interrupts(self) -> None:
        p = prof.profile(TRACE, name_of)
        self.assertEqual(p.inclusive["main"], 205)
        self.assertEqual(p.inclusive["load"], 190)
        self.assertEqual(p.inclusive["open"], 130)
        self.assertEqual(p.inclusive["audio_irq"], 50)
        self.assertEqual(p.inclusive["[exception 0x0e]"], 50)

    def test_focus_breaks_down_by_direct_callee(self) -> None:
        p = prof.profile(TRACE, name_of, re.compile("^load$"))
        self.assertEqual(p.focus_total, 240)
        self.assertEqual(p.focus_preempted, 50)
        self.assertEqual(p.focus_children["open"], 130)
        self.assertEqual(p.focus_children["(self)"], 60)
        self.assertNotIn("audio_irq", p.focus_self)

    def test_a_loss_of_sync_keeps_the_frames_it_resumes_in(self) -> None:
        unsync = "Idx:3; ID:10; OCSD_GEN_TRC_ELEM_TRACE_ON( [begin or filter])"
        lines = [
            rng(0x1000, 10, CALL),  # main calls load
            rng(0x2000, 20, CALL),  # load calls open
            rng(0x3000, 5),
            unsync,
            rng(0x2010, 7),  # back in load: open's frame is unwound, load's kept
            unsync,
            rng(
                0x4000, 3
            ),  # in the handler, whose entry the gap lost: behind a barrier
            unsync,
            rng(0x2020, 4),  # back in load
        ]
        p = prof.profile(lines, name_of)
        self.assertEqual(p.unsynced, 3)
        self.assertEqual(p.inclusive["main"], 46)
        self.assertEqual(p.inclusive["load"], 36)
        self.assertEqual(p.inclusive["audio_irq"], 3)
        self.assertEqual(p.inclusive["[after a gap]"], 3)

    def test_a_trace_mostly_after_gaps_says_so(self) -> None:
        unsync = "Idx:3; ID:10; OCSD_GEN_TRC_ELEM_TRACE_ON( [begin or filter])"
        lines = [rng(0x1000, 10), unsync, rng(0x4000, 90)]
        md = prof.render(prof.profile(lines, name_of), None, 5)
        self.assertIn("**90% of the traced time ran after a gap", md)

    def test_short_names_keep_anonymous_namespaces_and_drop_parameters(self) -> None:
        self.assertEqual(
            prof.short_name("ns::(anonymous namespace)::f(int) const"),
            "ns::(anonymous namespace)::f",
        )
        self.assertEqual(
            prof.short_name("decltype(auto) g<int>(int&&)"), "decltype(auto) g<int>"
        )
        self.assertEqual(prof.short_name("plain"), "plain")

    def test_recursion_is_counted_once(self) -> None:
        lines = [rng(0x2000, 10, CALL), rng(0x2000, 10, RET), rng(0x2000, 10)]
        p = prof.profile(lines, name_of)
        self.assertEqual(p.inclusive["load"], 30)

    def test_render_names_the_focus(self) -> None:
        md = prof.render(prof.profile(TRACE, name_of, re.compile("load")), "load", 5)
        self.assertIn(
            "Under the outermost frame matching `load`: 0.00 ms, of which 0.00 ms preempted",
            md,
        )
        self.assertIn("| `open` |", md)


if __name__ == "__main__":
    unittest.main()
