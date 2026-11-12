"""run_parity's schedule: each wait lands on an absolute time, so overshoot never accumulates."""

import unittest

import run_parity


class OvershootingSession:
    """A session whose run_until stops 1.3 ms late, as a stop between instructions does."""

    def __init__(self):
        self.now = 0
        self.targets = []

    def vclock_ns(self):
        return self.now

    def run_until(self, target_ns, timeout=None):
        if target_ns < self.now:
            raise AssertionError("run_until asked to go backwards")
        self.targets.append(target_ns)
        self.now = target_ns + 1_300_000


class ScheduleTest(unittest.TestCase):
    def test_a_thousand_waits_end_within_one_overshoot_of_the_script_time(self):
        dz = OvershootingSession()
        schedule = run_parity.Schedule(dz)
        for _ in range(1000):
            schedule.wait(0.04)
        self.assertEqual(dz.targets[-1], 40_000_000_000)
        self.assertLessEqual(dz.now - 40_000_000_000, 1_300_000)

    def test_a_wait_shorter_than_the_last_overshoot_does_not_go_backwards(self):
        dz = OvershootingSession()
        schedule = run_parity.Schedule(dz)
        schedule.wait(0.001)
        schedule.wait(0.0001)  # target 1.1 ms, now 2.3 ms
        self.assertEqual(len(dz.targets), 1)


def snap(oled):
    return {"frames": [{"oled": oled, "pads": [], "leds": [], "gold": []}], "midi": ""}


def screen_with(rows):
    """A 48-row OLED text, blank but for `rows` (index -> row text)."""
    blank = "." * 128
    return "\n".join(rows.get(i, blank) for i in range(48))


class SectionLineTest(unittest.TestCase):
    """D77: main draws an output name without its section line until a later redraw; ours draws it at once."""

    def test_a_section_line_only_ours_draws_is_d77(self):
        name = {10: "#" * 20 + "." * 108}
        ref = snap(screen_with(name))
        test = snap(
            screen_with(
                {**name, 40: "..###.##" + "." * 120, 42: ".#.#.#.." + "." * 120}
            )
        )
        self.assertEqual(run_parity.classify(ref, test, ["oled"]), "D77")

    def test_a_difference_where_main_draws_something_is_not(self):
        ref = snap(screen_with({40: "#" + "." * 127}))
        test = snap(screen_with({40: "##" + "." * 126}))
        self.assertIsNone(run_parity.classify(ref, test, ["oled"]))

    def test_a_difference_outside_the_section_line_is_not(self):
        ref = snap(screen_with({}))
        test = snap(screen_with({20: "#" + "." * 127}))
        self.assertIsNone(run_parity.classify(ref, test, ["oled"]))

    def test_a_difference_beyond_the_oled_is_not(self):
        ref = snap(screen_with({}))
        test = snap(screen_with({40: "#" + "." * 127}))
        self.assertIsNone(run_parity.classify(ref, test, ["oled", "pads"]))


if __name__ == "__main__":
    unittest.main()
