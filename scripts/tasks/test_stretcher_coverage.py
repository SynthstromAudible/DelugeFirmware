#! /usr/bin/env python3
"""Unit tests for stretcher_coverage.py: the harness seams it patches and calls, and the report's missing-file error.

Run directly with:
    python3 scripts/tasks/test_stretcher_coverage.py
"""

import sys
import tempfile
import types
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))

import scenario_gate
import stretcher_coverage as sc


def _gate_module(run):
    return types.SimpleNamespace(Gate=type("Gate", (), {"run": run}))


def _task_test_module(environment):
    module = types.SimpleNamespace()
    if environment is not None:
        module._environment = environment
    return module


def _run(self, scenario=None, env=None, timeout=None):
    pass


def _environment(root, h, products):
    pass


# The rewrite's files the coverage list measures: the stretcher, the time-stretch and sample-read components, the port
# and the read lease whole; the app's functions that drive them by prefix.
WHOLE = {
    "src/synth/sources/sample/sample_stretcher.h",
    "src/synth/sources/sample/sample_stretcher.cpp",
    "src/synth/sources/sample/hop_clock.h",
    "src/synth/sources/sample/stretch_position.h",
    "src/synth/sources/sample/stretch_mix.h",
    "src/synth/sources/sample/stretch_context.h",
    "src/synth/sources/sample/stretch_leases.h",
    "src/synth/sources/sample/stretch_ratio.h",
    "src/synth/sources/sample/perc_fill.h",
    "src/deluge/model/voice/voice_sample_stretch_port.cpp",
    "src/deluge/model/sample/sample_read_lease.cpp",
    "src/dsp/fixed/sample_read/stage.cpp",
}
BY_PREFIX = {
    "src/deluge/model/clip/audio_clip.cpp",
    "src/deluge/model/sample/sample.cpp",
    "src/deluge/model/voice/voice_sample.cpp",
    "src/deluge/processing/engines/audio_engine.cpp",
    "src/synth/sources/sample/sample_source.cpp",
}


class ScopeTest(unittest.TestCase):
    def test_the_scope_is_the_rewrite_s_files(self):
        root = Path(__file__).resolve().parents[2]
        components = {
            str(path.relative_to(root))
            for directory in ("src/dsp/fixed/timestretch", "src/dsp/fixed/sample_read")
            for path in (root / directory).glob("*.h")
        }
        whole = {key for key, prefixes in sc.SCOPE.items() if prefixes is None}
        self.assertEqual(whole, WHOLE | components)
        self.assertEqual(
            {key for key, prefixes in sc.SCOPE.items() if prefixes is not None},
            BY_PREFIX,
        )
        self.assertIn(
            "SampleSource::stretch_to_undo_pitch(",
            sc.SCOPE["src/synth/sources/sample/sample_source.cpp"],
        )
        for key in sc.SCOPE:
            with self.subTest(key=key):
                self.assertTrue((root / key).is_file())


class SeamsTest(unittest.TestCase):
    def test_the_harness_as_it_is_passes(self):
        sc.check_seams(scenario_gate, sc.load_task_test())

    def test_a_gate_run_with_another_signature_fails_naming_it(self):
        def run(self, scenario=None, env=None):
            pass

        with self.assertRaisesRegex(RuntimeError, r"scenario_gate\.Gate\.run"):
            sc.check_seams(_gate_module(run), _task_test_module(_environment))

    def test_a_missing_gate_run_fails_naming_it(self):
        with self.assertRaisesRegex(RuntimeError, r"scenario_gate\.Gate\.run"):
            sc.check_seams(
                types.SimpleNamespace(Gate=type("Gate", (), {})),
                _task_test_module(_environment),
            )

    def test_a_missing_environment_fails_naming_it(self):
        with self.assertRaisesRegex(RuntimeError, r"task-test\.py's _environment"):
            sc.check_seams(_gate_module(_run), _task_test_module(None))

    def test_an_environment_with_another_signature_fails_naming_it(self):
        def environment(root, h):
            pass

        with self.assertRaisesRegex(RuntimeError, r"task-test\.py's _environment"):
            sc.check_seams(_gate_module(_run), _task_test_module(environment))


class ShownTest(unittest.TestCase):
    def test_a_file_llvm_cov_did_not_write_fails_naming_it(self):
        with tempfile.TemporaryDirectory() as show:
            counts = sc._shown(Path(show))
            with self.assertRaisesRegex(FileNotFoundError, r"src/deluge/a\.cpp"):
                counts("src/deluge/a.cpp")


if __name__ == "__main__":
    unittest.main()
