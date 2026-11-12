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


def snap(*oleds):
    return {
        "frames": [{"oled": o, "pads": [], "leds": [], "gold": []} for o in oleds],
        "midi": "",
    }


def screen_with(rows):
    """A 48-row OLED text, blank but for `rows` (index -> row text)."""
    blank = "." * 128
    return "\n".join(rows.get(i, blank) for i in range(48))


def text_rows(text, first_row=37):
    """{row: text} for `text` drawn centred in the 7 px font with its top at `first_row`."""
    line = run_parity.render_centred(text, run_parity.font_columns())
    return {first_row + i: row for i, row in enumerate(line)}


NAME = {10: "#" * 20 + "." * 108}


class SectionLineTest(unittest.TestCase):
    """D77: main draws an output name without its section line until a later redraw; ours draws it at once."""

    def test_the_font_renders_section_1_as_the_firmware_draws_it(self):
        line = run_parity.render_centred("SECTION 1", run_parity.font_columns())
        self.assertEqual(
            line[0][37:42], ".###."
        )  # S, starting at the line's centred origin
        self.assertEqual(line[6][85:90], ".###.")  # the 1's foot, in the ninth cell

    def test_a_section_line_only_ours_draws_is_d77(self):
        ref = snap(screen_with(NAME))
        test = snap(screen_with({**NAME, **text_rows("SECTION 1")}))
        self.assertEqual(run_parity.classify(ref, test, ["oled"]), "D77")

    def test_a_section_line_whose_top_rows_a_popup_covers_is_d77(self):
        box = {i: "#" * 128 for i in range(37, 40)}
        ref = snap(screen_with({**NAME, **box}))
        drawn = {i: row for i, row in text_rows("SECTION 1").items() if i >= 40}
        test = snap(screen_with({**NAME, **box, **drawn}))
        self.assertEqual(run_parity.classify(ref, test, ["oled"]), "D77")

    def test_part_of_a_section_line_with_nothing_covering_the_rest_is_not(self):
        ref = snap(screen_with(NAME))
        drawn = {i: row for i, row in text_rows("SECTION 1").items() if i >= 40}
        test = snap(screen_with({**NAME, **drawn}))
        self.assertIsNone(run_parity.classify(ref, test, ["oled"]))

    def test_other_text_in_the_section_line_rows_is_not(self):
        ref = snap(screen_with(NAME))
        test = snap(screen_with({**NAME, **text_rows("SECTOR 1")}))
        self.assertIsNone(run_parity.classify(ref, test, ["oled"]))

    def test_a_section_line_off_its_rows_is_not(self):
        ref = snap(screen_with(NAME))
        test = snap(screen_with({**NAME, **text_rows("SECTION 1", first_row=38)}))
        self.assertIsNone(run_parity.classify(ref, test, ["oled"]))

    def test_a_section_line_with_no_name_above_it_is_not(self):
        ref = snap(screen_with({}))
        test = snap(screen_with(text_rows("SECTION 1")))
        self.assertIsNone(run_parity.classify(ref, test, ["oled"]))

    def test_a_window_whose_other_frames_differ_elsewhere_is_not(self):
        name_frame = screen_with(NAME)
        ref = snap(name_frame, screen_with({**NAME, 2: "#" + "." * 127}))
        test = snap(
            screen_with({**NAME, **text_rows("SECTION 1")}),
            screen_with({**NAME, 2: ".#" + "." * 126}),
        )
        self.assertIsNone(run_parity.classify(ref, test, ["oled"]))

    def test_a_difference_where_main_draws_something_is_not(self):
        ref = snap(screen_with({**NAME, 40: "#" + "." * 127}))
        test = snap(screen_with({**NAME, 40: "##" + "." * 126}))
        self.assertIsNone(run_parity.classify(ref, test, ["oled"]))

    def test_a_difference_outside_the_section_line_is_not(self):
        ref = snap(screen_with(NAME))
        test = snap(screen_with({**NAME, 20: "#" + "." * 127}))
        self.assertIsNone(run_parity.classify(ref, test, ["oled"]))

    def test_a_difference_beyond_the_oled_is_not(self):
        ref = snap(screen_with(NAME))
        test = snap(screen_with({**NAME, **text_rows("SECTION 1")}))
        self.assertIsNone(run_parity.classify(ref, test, ["oled", "pads"]))


if __name__ == "__main__":
    unittest.main()
