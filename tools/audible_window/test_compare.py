"""The audible window's analyser on crafted render sets: what it reports for identical sets, a changed value, a
threshold crossing, a shorter voice and a shifted render; and its dumps' round trip."""

import json
import math
import tempfile
import unittest
from pathlib import Path

import compare
import numpy as np
import predicates
from dump import Dump, load_npz, save_npz

LOCAL, GLOBAL = predicates.LOCAL_PARAMS, 10
TIMES = np.arange(0, 128 * 20, 128, dtype="<u4")


def plane(value: int = 1000, n: int = len(TIMES)) -> np.ndarray:
    return np.full((n, LOCAL), value, dtype="<i4")


def tone(frames: int = 8192, shift: int = 0) -> np.ndarray:
    t = np.arange(frames)
    # The same waveform delayed by `shift` frames
    mono = (
        np.sin(2 * math.pi * 440 * (t - shift) / 44100)
        * (t >= 1000 + shift)
        * (1 << 28)
    ).astype("<i4")
    return np.repeat(mono, 2)


def write_set(root: Path, dump: Dump, pcm: np.ndarray) -> Path:
    (root / "presets").mkdir(parents=True)
    save_npz(dump, root / "presets" / "case.npz")
    pcm.astype("<i4").tofile(root / "presets" / "case.pcm")
    (root / "manifest.json").write_text(
        json.dumps({"commit": "x", "cases": ["case"], "songs": [], "failed": []})
    )
    return root


def a_dump(voice_plane: np.ndarray, voice_times: np.ndarray = TIMES) -> Dump:
    d = Dump(LOCAL, GLOBAL)
    d.planes[(0, 1, 0)] = (TIMES, np.zeros((len(TIMES), GLOBAL), "<i4"))
    d.planes[(0, 0, 1)] = (voice_times, voice_plane)
    return d


class Analyser(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def run_pair(
        self, ref: Dump, new: Dump, ref_pcm=None, new_pcm=None
    ) -> compare.CaseResult:
        a = write_set(self.tmp / "ref", ref, tone() if ref_pcm is None else ref_pcm)
        b = write_set(self.tmp / "new", new, tone() if new_pcm is None else new_pcm)
        (result,) = compare.compare_sets(a, b)
        return result

    def test_identical_sets_compare_as_identical(self):
        r = self.run_pair(a_dump(plane()), a_dump(plane()))
        self.assertTrue(r.identical)
        self.assertEqual((r.params, r.flips, r.lifetimes), ([], [], []))
        self.assertEqual(r.null_db, -math.inf)

    def test_a_changed_value_reports_its_delta_first_time_and_blocks(self):
        new = plane()
        new[5:7, 2] += 3
        (p,) = self.run_pair(a_dump(plane()), a_dump(new)).params
        self.assertEqual(
            (p["param"], p["max_lsb"], p["differing"], p["first_time"]),
            (2, 3, 2, int(TIMES[5])),
        )

    def test_an_attack_crossing_its_threshold_is_a_flip(self):
        ref, new = plane(245632), plane(245632)
        new[8:, predicates.ENV_ATTACK[0]] = 245633
        flips = self.run_pair(a_dump(ref), a_dump(new)).flips
        self.assertEqual(
            [(f["predicate"], f["first_time"]) for f in flips],
            [("slow_attack", int(TIMES[8]))],
        )

    def test_a_shorter_voice_is_a_lifetime_change_not_a_parameter_change(self):
        # The new voice starts 5 blocks late, its values block for block as the reference's: matched by time they agree
        ref = plane()
        ref[:, 2] = np.arange(len(TIMES)) * 10
        r = self.run_pair(a_dump(ref), a_dump(ref[5:].copy(), TIMES[5:]))
        self.assertEqual(r.params, [])
        (life,) = r.lifetimes
        self.assertEqual(
            (life["ref_first"], life["new_first"]), (int(TIMES[0]), int(TIMES[5]))
        )

    def test_a_shifted_render_aligns_before_its_null_test(self):
        r = self.run_pair(a_dump(plane()), a_dump(plane()), tone(), tone(shift=5))
        self.assertEqual(r.audio["onset"], 5)
        self.assertLess(r.null_db, -40)

    def test_records_sharing_a_time_are_matched_in_order(self):
        times = np.array([0, 0, 128, 256], "<u4")
        ref = plane(n=4)
        new = plane(n=4)
        new[1, 2] += 9  # the second record at time 0
        (p,) = self.run_pair(a_dump(ref, times), a_dump(new, times)).params
        self.assertEqual((p["max_lsb"], p["first_time"], p["differing"]), (9, 0, 1))

    def test_dumps_pack_and_unpack_losslessly(self):
        d = a_dump(plane(7))
        save_npz(d, self.tmp / "d.npz")
        back = load_npz(self.tmp / "d.npz")
        self.assertEqual(sorted(back.planes), sorted(d.planes))
        np.testing.assert_array_equal(back.planes[(0, 0, 1)][1], d.planes[(0, 0, 1)][1])


if __name__ == "__main__":
    unittest.main()
