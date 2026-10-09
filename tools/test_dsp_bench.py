"""Tests for dsp_bench.py, run against canned captures of the device's RTT stream."""

import io
import json
import re
import subprocess
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest import mock

import dsp_bench

DATA = Path(__file__).parent / "testdata"
COMPLETE = DATA / "bench_capture_complete.txt"
TRUNCATED = DATA / "bench_capture_truncated.txt"
MISMATCH = DATA / "bench_capture_count_mismatch.txt"
EXTREMES = DATA / "bench_capture_extremes.txt"
SANITY_INST = DATA / "bench_capture_sanity_inst.txt"
SANITY_CPS = DATA / "bench_capture_sanity_cps.txt"
SANITY_SNE = DATA / "bench_capture_sanity_sne.txt"

GAIN_32 = dsp_bench.Record(
    case="gain",
    n=32,
    k=64,
    cycles=9876,
    cps="4.82",
    l1d_refill=3,
    inst=14336,
    dc_stall=40,
    issue_empty=2100,
    neon=4096,
)

HEADER = (
    "| case | n | cps | cycles | l1d_refill/sample | inst/sample | dc_stall/sample "
    "| issue_empty/sample | neon/sample |\n"
    "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |\n"
)


def run_main(*args):
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = dsp_bench.main(list(args))
    return code, out.getvalue(), err.getvalue()


class ParseTests(unittest.TestCase):
    def parse(self, path):
        return dsp_bench.parse_capture(path.read_text().splitlines())

    def test_complete_capture_records_are_exact(self):
        cap = self.parse(COMPLETE)
        self.assertEqual(cap.errors, [])
        self.assertEqual(cap.done_count, 12)
        self.assertEqual(len(cap.records), 12)
        self.assertEqual(cap.records[0].case, "calibration")
        self.assertEqual(cap.records[1], GAIN_32)
        self.assertEqual(
            [(r.case, r.n) for r in cap.records[-4:]],
            [
                ("calibration", 128),
                ("gain", 128),
                ("duck", 128),
                ("calibration_loop", 128),
            ],
        )

    def test_non_bench_lines_are_ignored(self):
        cap = self.parse(COMPLETE)
        self.assertEqual(sum(1 for r in cap.records if r.case == "going"), 0)
        self.assertNotIn("bench:", " ".join(r.case for r in cap.records))
        self.assertEqual(cap.errors, [])

    def test_truncated_has_no_done(self):
        cap = self.parse(TRUNCATED)
        self.assertIsNone(cap.done_count)
        self.assertEqual(len(cap.records), 7)

    def test_info_lines_are_recorded_and_not_counted(self):
        cap = self.parse(COMPLETE)
        self.assertEqual(cap.info, {"dbgauthstatus": "0x000000ff"})
        self.assertEqual(cap.done_count, len(cap.records))

    def test_malformed_info_line_is_an_error(self):
        cap = dsp_bench.parse_capture(
            ["BENCH-INFO dbgauthstatus", "BENCH-DONE cases=0"]
        )
        self.assertEqual(len(cap.errors), 1)
        self.assertIn("line 1", cap.errors[0])
        self.assertEqual(cap.info, {})

    def test_malformed_line_reports_its_line_number(self):
        lines = COMPLETE.read_text().splitlines()
        self.assertTrue(lines[4].startswith("BENCH case=gain"))
        lines[4] = lines[4].replace(" neon=4096", "")
        cap = dsp_bench.parse_capture(lines)
        self.assertEqual(len(cap.errors), 1)
        self.assertIn("line 5", cap.errors[0])
        self.assertIn("neon", cap.errors[0])

    def test_bench_error_lines_are_errors(self):
        cap = dsp_bench.parse_capture(
            ["BENCH-ERROR line-overflow", "BENCH-DONE cases=1"]
        )
        self.assertEqual(len(cap.errors), 1)
        self.assertIn("line 1", cap.errors[0])
        self.assertIn("line-overflow", cap.errors[0])

    def test_irq_enabled_is_an_error(self):
        cap = dsp_bench.parse_capture(["BENCH-ERROR irq-enabled"])
        self.assertIn("irq-enabled", cap.errors[0])


class TableTests(unittest.TestCase):
    def test_table_text_is_exact(self):
        cap = dsp_bench.parse_capture(COMPLETE.read_text().splitlines())
        table = dsp_bench.render_table(cap.records)
        lines = table.splitlines(keepends=True)
        self.assertEqual("".join(lines[:2]), HEADER)
        self.assertEqual(len(lines), 2 + 12)
        self.assertEqual(
            lines[3],
            "| gain | 32 | 4.82 | 9876 | 0.00 | 7.00 | 0.02 | 1.03 | 2.00 |\n",
        )
        self.assertEqual(
            lines[2],
            "| calibration | 32 | 0.59 | 1210 | 0.00 | 0.50 | 0.00 | 0.15 | 0.00 |\n",
        )
        self.assertEqual(
            lines[-2],
            "| duck | 128 | 15.24 | 124873 | 0.01 | 20.00 | 0.15 | 4.36 | 0.00 |\n",
        )

    def test_per_sample_rounds_half_up(self):
        self.assertEqual(dsp_bench.per_sample(1, 64, 32), "0.00")
        self.assertEqual(dsp_bench.per_sample(1024, 64, 32), "0.50")
        self.assertEqual(dsp_bench.per_sample(10, 4, 5), "0.50")
        self.assertEqual(dsp_bench.per_sample(1, 8, 2), "0.06")

    def test_per_sample_of_no_samples_is_zero(self):
        self.assertEqual(dsp_bench.per_sample(5, 0, 32), "0.00")
        self.assertEqual(dsp_bench.per_sample(5, 64, 0), "0.00")


class SanityTests(unittest.TestCase):
    def sanity(self, lines):
        return dsp_bench.sanity(dsp_bench.parse_capture(lines))

    def test_complete_capture_is_sane(self):
        self.assertEqual(self.sanity(COMPLETE.read_text().splitlines()), [])

    def test_inst_outside_the_band_fails(self):
        found = self.sanity(SANITY_INST.read_text().splitlines())
        self.assertEqual(len(found), 1)
        self.assertIn("calibration_loop n=64", found[0])
        self.assertIn("inst=0", found[0])
        self.assertIn("[65536, 67584]", found[0])

    def test_inst_above_the_band_fails_and_its_edges_pass(self):
        lines = COMPLETE.read_text().splitlines()
        self.assertTrue(lines[6].startswith("BENCH case=calibration_loop n=32"))
        for inst, sane in (
            (32768, True),
            (34816, True),
            (32767, False),
            (34817, False),
        ):
            lines[6] = re.sub(r"inst=\d+", f"inst={inst}", lines[6])
            self.assertEqual(self.sanity(lines) == [], sane, inst)

    def test_low_cps_fails(self):
        found = self.sanity(SANITY_CPS.read_text().splitlines())
        self.assertEqual(len(found), 1)
        self.assertIn("calibration_loop n=128", found[0])
        self.assertIn("cps=31.99", found[0])

    def test_sne_clear_fails(self):
        found = self.sanity(SANITY_SNE.read_text().splitlines())
        self.assertEqual(len(found), 1)
        self.assertIn("0x000000bf", found[0])
        self.assertIn("SNE", found[0])

    def test_a_missing_calibration_loop_fails(self):
        lines = [
            line
            for line in COMPLETE.read_text().splitlines()
            if "calibration_loop n=64" not in line
        ]
        found = self.sanity(lines)
        self.assertEqual(found, ["no calibration_loop line at n=64"])

    def test_a_missing_dbgauthstatus_fails(self):
        lines = [
            line
            for line in COMPLETE.read_text().splitlines()
            if not line.startswith("BENCH-INFO")
        ]
        found = self.sanity(lines)
        self.assertEqual(len(found), 1)
        self.assertIn("dbgauthstatus", found[0])


class ExtremeValueTests(unittest.TestCase):
    def test_small_cps_and_large_cycles_parse_and_tabulate(self):
        cap = dsp_bench.parse_capture(EXTREMES.read_text().splitlines())
        self.assertEqual(cap.errors, [])
        self.assertEqual(cap.done_count, 2)
        calibration, slow = cap.records
        self.assertEqual((calibration.cps, calibration.cycles), ("0.05", 410))
        self.assertEqual((slow.cps, slow.cycles), ("488281.25", 4_000_000_000))
        self.assertEqual(slow.inst, 2**32 - 1)
        lines = dsp_bench.render_table(cap.records).splitlines()
        self.assertEqual(
            lines[2],
            "| calibration | 128 | 0.05 | 410 | 0.00 | 0.13 | 0.00 | 0.00 | 0.00 |",
        )
        self.assertEqual(
            lines[3],
            "| slow | 128 | 488281.25 | 4000000000 | 488281.25 | 524288.00 | 0.00 | 0.00 | 0.00 |",
        )


class CaptureCliTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.out = Path(tmp.name) / "bench"
        for name, value in (("git_sha", "abc1234"), ("today", "2026-10-08")):
            patcher = mock.patch.object(dsp_bench, name, return_value=value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_complete_capture_writes_md_and_json(self):
        code, _, _ = run_main("--from-capture", str(COMPLETE), "--out", str(self.out))
        self.assertEqual(code, 0)
        md = self.out / "2026-10-08-abc1234.md"
        js = self.out / "2026-10-08-abc1234.json"
        self.assertTrue(md.read_text().startswith(HEADER))
        doc = json.loads(js.read_text())
        self.assertEqual(doc["meta"]["sha"], "abc1234")
        self.assertEqual(doc["meta"]["date"], "2026-10-08")
        self.assertEqual(doc["meta"]["probe"], "capture")
        self.assertEqual(doc["meta"]["info"], {"dbgauthstatus": "0x000000ff"})
        self.assertEqual(doc["meta"]["sanity"], {"passed": True, "failures": []})
        self.assertEqual(len(doc["records"]), 12)
        self.assertEqual(doc["records"][1]["case"], "gain")
        self.assertEqual(doc["records"][1]["cycles"], 9876)

    def test_rerun_same_sha_overwrites(self):
        run_main("--from-capture", str(COMPLETE), "--out", str(self.out))
        md = self.out / "2026-10-08-abc1234.md"
        md.write_text("stale")
        code, _, _ = run_main("--from-capture", str(COMPLETE), "--out", str(self.out))
        self.assertEqual(code, 0)
        self.assertTrue(md.read_text().startswith(HEADER))
        self.assertEqual(len(list(self.out.iterdir())), 2)

    def test_truncated_exits_2_and_writes_nothing(self):
        code, _, err = run_main(
            "--from-capture", str(TRUNCATED), "--out", str(self.out)
        )
        self.assertEqual(code, 2)
        self.assertIn("BENCH-DONE", err)
        self.assertFalse(self.out.exists())

    def test_count_mismatch_exits_2_and_writes_nothing(self):
        code, _, err = run_main("--from-capture", str(MISMATCH), "--out", str(self.out))
        self.assertEqual(code, 2)
        self.assertIn("BENCH-DONE counts 12 lines", err)
        self.assertIn("9 BENCH lines and 0 errors make 9", err)
        self.assertFalse(self.out.exists())

    def assert_insane_run(self, capture, failure):
        code, out, err = run_main(
            "--from-capture", str(capture), "--out", str(self.out)
        )
        self.assertEqual(code, 4)
        self.assertIn(failure, err)
        md = (self.out / "2026-10-08-abc1234.md").read_text()
        banner, rest = md.split("\n", 1)
        self.assertTrue(banner.startswith("**SANITY CHECK FAILED**"), banner)
        self.assertIn(failure, banner)
        self.assertTrue(rest.lstrip("\n").startswith(HEADER))
        doc = json.loads((self.out / "2026-10-08-abc1234.json").read_text())
        self.assertFalse(doc["meta"]["sanity"]["passed"])
        self.assertEqual(len(doc["meta"]["sanity"]["failures"]), 1)
        self.assertIn(failure, doc["meta"]["sanity"]["failures"][0])
        self.assertEqual(len(doc["records"]), 12)
        self.assertIn("SANITY CHECK FAILED", out)

    def test_inst_out_of_band_writes_a_marked_table_and_exits_4(self):
        self.assert_insane_run(SANITY_INST, "inst=0")

    def test_low_cps_writes_a_marked_table_and_exits_4(self):
        self.assert_insane_run(SANITY_CPS, "cps=31.99")

    def test_sne_clear_writes_a_marked_table_and_exits_4(self):
        self.assert_insane_run(SANITY_SNE, "SNE")

    def test_malformed_line_exits_2_naming_the_line(self):
        lines = COMPLETE.read_text().splitlines()
        lines[4] = lines[4].replace(" neon=4096", "")
        bad = Path(self.out.parent) / "bad.txt"
        bad.write_text("\n".join(lines) + "\n")
        code, _, err = run_main("--from-capture", str(bad), "--out", str(self.out))
        self.assertEqual(code, 2)
        self.assertIn("line 5", err)
        self.assertFalse(self.out.exists())

    def test_overflow_line_makes_the_run_incomplete(self):
        lines = COMPLETE.read_text().splitlines()
        lines[4] = "BENCH-ERROR line-overflow"
        bad = Path(self.out.parent) / "overflow.txt"
        bad.write_text("\n".join(lines) + "\n")
        code, _, err = run_main("--from-capture", str(bad), "--out", str(self.out))
        self.assertEqual(code, 2)
        self.assertIn("line-overflow", err)
        self.assertFalse(self.out.exists())


class RunPathTests(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.out = Path(tmp.name) / "bench"
        for name, value in (("git_sha", "abc1234"), ("today", "2026-10-08")):
            patcher = mock.patch.object(dsp_bench, name, return_value=value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def completed(self, rtt, code=0):
        return subprocess.CompletedProcess(
            args=[],
            returncode=code,
            stdout=json.dumps({"rtt": rtt, "exit": "pattern"}),
            stderr="",
        )

    def test_jlink_arguments_are_exact(self):
        rtt = COMPLETE.read_text().splitlines()
        with mock.patch.object(
            dsp_bench.subprocess, "run", return_value=self.completed(rtt)
        ) as run:
            code, _, _ = run_main(
                "--elf", "x/deluge.elf", "--timeout", "90", "--out", str(self.out)
            )
        self.assertEqual(code, 0)
        script = str(Path(dsp_bench.__file__).resolve().parent / "deluge_jlink.py")
        run.assert_called_once()
        self.assertEqual(
            run.call_args.args[0],
            [
                dsp_bench.sys.executable,
                script,
                "x/deluge.elf",
                "--until",
                "^BENCH-DONE",
                "--fail-on",
                "panicked at",
                "--fail-on",
                "^BENCH-ERROR",
                "--json",
                "--quiet",
                "--timeout",
                "90",
            ],
        )
        doc = json.loads((self.out / "2026-10-08-abc1234.json").read_text())
        self.assertEqual(doc["meta"]["probe"], "jlink")
        self.assertEqual(len(doc["records"]), 12)

    def test_probe_rs_uses_deluge_run(self):
        rtt = COMPLETE.read_text().splitlines()
        with mock.patch.object(
            dsp_bench.subprocess, "run", return_value=self.completed(rtt)
        ) as run:
            run_main("--probe", "probe-rs", "--out", str(self.out))
        self.assertTrue(run.call_args.args[0][1].endswith("deluge_run.py"))
        self.assertEqual(run.call_args.args[0][2], "build/RelWithDebInfo/deluge.elf")
        self.assertEqual(run.call_args.args[0][-1], "120")

    def test_probe_error_code_is_passed_through(self):
        proc = subprocess.CompletedProcess(
            args=[], returncode=3, stdout="", stderr="no probe"
        )
        with mock.patch.object(dsp_bench.subprocess, "run", return_value=proc):
            code, _, err = run_main("--out", str(self.out))
        self.assertEqual(code, 3)
        self.assertIn("no probe", err)
        self.assertFalse(self.out.exists())

    def test_probe_usage_error_exits_3_not_the_sanity_code(self):
        proc = subprocess.CompletedProcess(
            args=[], returncode=4, stdout="", stderr="no such file"
        )
        with mock.patch.object(dsp_bench.subprocess, "run", return_value=proc):
            code, _, err = run_main("--out", str(self.out))
        self.assertEqual(code, 3)
        self.assertIn("(4)", err)
        self.assertIn("no such file", err)
        self.assertFalse(self.out.exists())

    def test_timeout_with_partial_rtt_exits_2(self):
        rtt = TRUNCATED.read_text().splitlines()
        with mock.patch.object(
            dsp_bench.subprocess, "run", return_value=self.completed(rtt, 2)
        ):
            code, _, err = run_main("--out", str(self.out))
        self.assertEqual(code, 2)
        self.assertIn("BENCH-DONE", err)
        self.assertFalse(self.out.exists())

    def test_fail_on_bench_error_exits_2(self):
        rtt = [*TRUNCATED.read_text().splitlines()[:4], "BENCH-ERROR irq-enabled"]
        with mock.patch.object(
            dsp_bench.subprocess, "run", return_value=self.completed(rtt, 1)
        ):
            code, _, err = run_main("--out", str(self.out))
        self.assertEqual(code, 2)
        self.assertIn("irq-enabled", err)
        self.assertFalse(self.out.exists())


if __name__ == "__main__":
    unittest.main()
