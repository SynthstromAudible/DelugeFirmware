#! /usr/bin/env python3
"""Unit tests for harness_registry.py.

Run directly with:
    python3 scripts/tasks/test_harness_registry.py

Gate registry-selftest:
    the registry loader, validation, selection and probes (golden data lives only under tests/golden, and no spec
    names a golden outside GOLDEN_ROOT), the SDRAM content packer's format (the pack the boot inflates, and the
    Rust decoder's fixture as such a pack), the setting-menus checker's planted song-state writes and scene engine
    calls, the layer rules' planted scenes rows, and the per-directory support/ names that would shadow tests/support/
"""

import ast
import contextlib
import dataclasses
import hashlib
import importlib.util
import io
import json
import os
import re
import subprocess
import sys
import tempfile
import threading
import types
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from typing import ClassVar
from unittest import mock

import tomllib

sys.path.insert(0, str(Path(__file__).parent))

import corpus_gate as cg
import harness_registry as hr
import scenario_gate as sg
import test_builds as tb
import test_selection as ts

_spec = importlib.util.spec_from_file_location(
    "task_test", Path(__file__).parent / "task-test.py"
)
tt = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(tt)

MINIMAL = """
[[harness]]
name = "alpha"
path = "harness/rust/alpha"
level = "component"
runner = "cargo"
kind = "differential"
status = "gate"
gates = "alpha must equal beta"
areas = ["all"]
build = ["cargo", "build"]
run = ["cargo", "test"]
requires = ["cargo"]
tier = "pr"
runtime_s = 60
job = "storage"
"""


CTEST = """
[[harness]]
name = "beta"
level = "unit"
runner = "ctest"
kind = "unit"
status = "gate"
gates = "beta holds"
areas = ["all"]
source = "x"
tree = "build/x"
ctest_args = ["-C", "Debug"]
tier = "pr"
job = "unit"
runtime_s = 1
"""


def entry(name, level="unit", tier="pr", status="gate"):
    return hr.Harness(
        name=name,
        path=".",
        level=level,
        runner="script",
        kind="unit",
        status=status,
        gates="g",
        tier=tier,
        build=[],
        run=["true"],
        requires=[],
        runtime_s=1,
        job="unit",
    )


class SelectTest(unittest.TestCase):
    ENTRIES = (
        entry("u-pr"),
        entry("c-nightly", level="component", tier="nightly"),
        entry("i-manual", level="integration", tier="manual"),
        entry("u-gone", status="unavailable"),
    )

    def names(self, **kw):
        return [
            h.name
            for h in hr.select(
                self.ENTRIES, kw.get("names", []), kw.get("levels", []), kw["tier"]
            )
        ]

    def test_tiers_are_cumulative(self):
        self.assertEqual(self.names(tier="pr"), ["u-pr"])
        self.assertEqual(self.names(tier="nightly"), ["u-pr", "c-nightly"])
        self.assertEqual(self.names(tier="manual"), ["u-pr", "c-nightly", "i-manual"])

    def test_levels_filter(self):
        self.assertEqual(
            self.names(tier="manual", levels=["component", "integration"]),
            ["c-nightly", "i-manual"],
        )

    def test_unavailable_only_when_named(self):
        self.assertNotIn("u-gone", self.names(tier="manual"))
        self.assertEqual(self.names(tier="pr", names=["u-gone"]), ["u-gone"])

    def test_named_ignores_tier_and_keeps_order(self):
        self.assertEqual(
            self.names(tier="pr", names=["i-manual", "u-pr"]), ["i-manual", "u-pr"]
        )

    def test_unknown_name_is_rejected(self):
        with self.assertRaises(ValueError):
            self.names(tier="pr", names=["nope"])


class LoadRegistryTest(unittest.TestCase):
    def _root_with(self, text):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(text)
        return root

    def test_an_entry_without_areas_is_refused(self):
        text = MINIMAL.replace('areas = ["all"]\n', "")
        with self.assertRaises(ValueError) as caught:
            hr.load_registry(self._root_with(text))
        self.assertEqual(str(caught.exception), "alpha: missing required field 'areas'")

    def test_areas_is_a_list_of_names(self):
        text = MINIMAL.replace('areas = ["all"]', 'areas = "all"')
        with self.assertRaisesRegex(ValueError, "alpha: areas is a list of area names"):
            hr.load_registry(self._root_with(text))

    def test_the_area_tables_load(self):
        text = (
            MINIMAL
            + '\n[area.ui]\npaths = ["src/ui/**"]\n\n[no-gate]\npaths = ["docs/**"]\n\n[every-gate]\npaths = ["dbt"]\n'
        )
        root = self._root_with(text)
        self.assertEqual(hr.load_registry(root)[0].areas, ["all"])
        areas = hr.load_areas(root)
        self.assertEqual(
            (areas.areas, areas.no_gate, areas.every_gate),
            ({"ui": ["src/ui/**"]}, ["docs/**"], ["dbt"]),
        )

    def test_a_long_gates_line_is_refused(self):
        text = MINIMAL.replace(
            'gates = "alpha must equal beta"', 'gates = "alpha holds. Beta too"'
        )
        with self.assertRaises(ValueError) as caught:
            hr.load_registry(self._root_with(text))
        self.assertEqual(
            str(caught.exception),
            "alpha: gates has a sentence break ('.'): state what the test gates in one sentence, and put the rest in "
            "its explanation, a 'Gate alpha:' block in the file that judges it or a '## alpha' section in that "
            "file's notes (tests/README.md, Writing an entry)",
        )

    def test_runtime_s_is_required(self):
        with self.assertRaisesRegex(ValueError, "missing required field 'runtime_s'"):
            hr.load_registry(self._root_with(MINIMAL.replace("runtime_s = 60\n", "")))

    def test_runtime_s_is_a_whole_positive_number_of_seconds(self):
        for bad in ("0", "-5", "1.5", '"~1m"', "true"):
            with (
                self.subTest(bad),
                self.assertRaisesRegex(ValueError, "whole number of seconds"),
            ):
                hr.load_registry(
                    self._root_with(
                        MINIMAL.replace("runtime_s = 60", f"runtime_s = {bad}")
                    )
                )

    def test_runtime_text(self):
        for seconds, text in (
            (1, "~1s"),
            (45, "~45s"),
            (60, "~1m"),
            (90, "~90s"),
            (300, "~5m"),
            (1800, "~30m"),
        ):
            self.assertEqual(hr.runtime_text(seconds), text)

    def test_loads_all_fields(self):
        entries = hr.load_registry(self._root_with(MINIMAL))
        self.assertEqual(len(entries), 1)
        h = entries[0]
        self.assertEqual(h.name, "alpha")
        self.assertEqual(h.kind, "differential")
        self.assertEqual(h.run, ["cargo", "test"])
        self.assertEqual(h.requires, ["cargo"])
        self.assertEqual(h.tier, "pr")
        self.assertEqual(h.level, "component")
        self.assertEqual(h.runner, "cargo")

    def test_env_defaults_to_empty_dict(self):
        h = hr.load_registry(self._root_with(MINIMAL))[0]
        self.assertEqual(h.env, {})

    def test_missing_job_is_rejected(self):
        text = MINIMAL.replace('job = "storage"\n', "")
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("missing required field 'job'", str(cm.exception))

    def test_a_group_field_is_not_read_as_the_job(self):
        text = MINIMAL.replace('job = "storage"', 'group = "storage"')
        with self.assertRaisesRegex(ValueError, r"unknown field\(s\) group"):
            hr.load_registry(self._root_with(text))

    def test_an_unknown_field_is_refused(self):
        with self.assertRaisesRegex(ValueError, r"alpha: unknown field\(s\) colour"):
            hr.load_registry(self._root_with(MINIMAL + 'colour = "blue"\n'))

    def test_an_unknown_table_is_refused(self):
        with self.assertRaisesRegex(ValueError, r"unknown table\(s\) extras"):
            hr.load_registry(self._root_with(MINIMAL + "[extras]\nx = 1\n"))

    def test_an_unknown_kind_is_refused(self):
        with self.assertRaisesRegex(ValueError, "kind 'vibes' must be one of"):
            hr.load_registry(
                self._root_with(
                    MINIMAL.replace('kind = "differential"', 'kind = "vibes"')
                )
            )

    def test_cargo_and_script_need_a_path(self):
        script = MINIMAL.replace('runner = "cargo"', 'runner = "script"')
        for runner, text in (("cargo", MINIMAL), ("script", script)):
            for path in ('path = ""\n', ""):
                with (
                    self.subTest(runner=runner, path=path),
                    self.assertRaises(ValueError) as caught,
                ):
                    hr.load_registry(
                        self._root_with(
                            text.replace('path = "harness/rust/alpha"\n', path)
                        )
                    )
                self.assertEqual(
                    str(caught.exception),
                    f"alpha: a {runner} test runs its commands in 'path', so it needs a non-empty 'path'",
                )

    def test_run_and_build_are_lists_of_strings(self):
        script = MINIMAL.replace('runner = "cargo"', 'runner = "script"')
        for old, new, message in (
            ('run = ["cargo", "test"]', "run = [3]", "alpha: run is a list of strings"),
            (
                'run = ["cargo", "test"]',
                'run = "python3 x.py"',
                "alpha: run is a list of strings",
            ),
            (
                'run = ["cargo", "test"]',
                'run = ["python3", 3]',
                "alpha: run is a list of strings",
            ),
            (
                'build = ["cargo", "build"]',
                "build = [3]",
                "alpha: build is a list of strings",
            ),
            (
                'build = ["cargo", "build"]',
                'build = "make"',
                "alpha: build is a list of strings",
            ),
        ):
            with self.subTest(new):
                with self.assertRaises(ValueError) as caught:
                    hr.load_registry(self._root_with(script.replace(old, new)))
                self.assertEqual(str(caught.exception), message)

    def test_a_script_entry_with_a_path_loads(self):
        script = MINIMAL.replace('runner = "cargo"', 'runner = "script"')
        h = hr.load_registry(self._root_with(script))[0]
        self.assertEqual((h.runner, h.path), ("script", "harness/rust/alpha"))

    def test_harness_is_a_list_of_tables(self):
        for text in ("harness = 3\n", "harness = [3]\n"):
            with self.subTest(text), self.assertRaises(ValueError) as caught:
                hr.load_registry(self._root_with(text))
            self.assertEqual(
                str(caught.exception), "'harness' is a list of [[harness]] tables"
            )

    def test_name_and_gates_are_strings(self):
        for old, new, message in (
            ('name = "alpha"', "name = 3", "entry #0: name is a string"),
            (
                'gates = "alpha must equal beta"',
                "gates = 3",
                "alpha: gates is a string",
            ),
        ):
            with self.subTest(new), self.assertRaises(ValueError) as caught:
                hr.load_registry(self._root_with(MINIMAL.replace(old, new)))
            self.assertEqual(str(caught.exception), message)

    def test_other_runners_take_no_path(self):
        with self.assertRaisesRegex(ValueError, "takes none"):
            hr.load_registry(self._root_with(CTEST + 'path = "x"\n'))

    def test_path_defaults_to_the_root(self):
        self.assertEqual(hr.load_registry(self._root_with(CTEST))[0].path, ".")

    def test_requires_defaults_to_none(self):
        text = MINIMAL.replace('requires = ["cargo"]\n', "")
        self.assertEqual(hr.load_registry(self._root_with(text))[0].requires, [])

    def test_unknown_job_is_rejected(self):
        text = MINIMAL.replace('job = "storage"', 'job = "misc"')
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("job 'misc' must be one of", str(cm.exception))

    def test_prebuilt_job_builds_only_its_product(self):
        text = MINIMAL.replace('job = "storage"', 'job = "render"').replace(
            'requires = ["cargo"]', 'builds = ["host-app"]\nrequires = ["cargo"]'
        )
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("builds only golden-vt-render", str(cm.exception))

    def test_prebuilt_job_holds_only_scenario_gates(self):
        text = MINIMAL.replace('job = "storage"', 'job = "model"').replace(
            'requires = ["cargo"]',
            'builds = ["golden-vt-render"]\nrequires = ["cargo"]',
        )
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("holds only scenario and corpus gates", str(cm.exception))

    def test_missing_gates_is_rejected(self):
        text = MINIMAL.replace('gates = "alpha must equal beta"\n', "")
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("gates", str(cm.exception))

    def test_bad_status_is_rejected(self):
        text = MINIMAL.replace('status = "gate"', 'status = "probably fine"')
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("status", str(cm.exception))

    def test_bad_tier_is_rejected(self):
        text = MINIMAL.replace('tier = "pr"', 'tier = "sometimes"')
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("tier", str(cm.exception))

    def test_bad_level_is_rejected(self):
        text = MINIMAL.replace('level = "component"', 'level = "system"')
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("level", str(cm.exception))

    def test_bad_runner_is_rejected(self):
        text = MINIMAL.replace('runner = "cargo"', 'runner = "make"')
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("runner", str(cm.exception))

    def test_missing_run_is_rejected(self):
        text = MINIMAL.replace('run = ["cargo", "test"]\n', "")
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("run", str(cm.exception))

    def test_ctest_needs_source_and_tree(self):
        text = CTEST.replace('tree = "build/x"\n', "")
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("tree", str(cm.exception))

    def test_ctest_rejects_build_and_run(self):
        text = CTEST.replace('source = "x"', 'source = "x"\nrun = ["ctest"]')
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("ctest", str(cm.exception))

    def test_ctest_loads(self):
        h = hr.load_registry(self._root_with(CTEST))[0]
        self.assertEqual(
            (h.source, h.tree, h.ctest_args), ("x", "build/x", ["-C", "Debug"])
        )

    def test_unknown_requirement_is_rejected(self):
        text = MINIMAL.replace('requires = ["cargo"]', 'requires = ["a-pony"]')
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("a-pony", str(cm.exception))

    def test_duplicate_names_are_rejected(self):
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(MINIMAL + MINIMAL))
        self.assertIn("alpha", str(cm.exception))

    def test_baseline_defaults_to_empty_and_loads(self):
        self.assertEqual(hr.load_registry(self._root_with(MINIMAL))[0].baseline, [])
        text = MINIMAL.replace(
            'tier = "pr"',
            'baseline = ["tests/golden/x.json", "tests/golden/d"]\ntier = "pr"',
        )
        self.assertEqual(
            hr.load_registry(self._root_with(text))[0].baseline,
            ["tests/golden/x.json", "tests/golden/d"],
        )

    def test_a_baseline_outside_the_repository_is_refused(self):
        for bad in ('["/etc/x"]', '["../x"]', '["a/../../x"]', '[""]', '"tests/x"'):
            text = MINIMAL.replace('tier = "pr"', f'baseline = {bad}\ntier = "pr"')
            with (
                self.subTest(bad),
                self.assertRaisesRegex(ValueError, "relative to the repository root"),
            ):
                hr.load_registry(self._root_with(text))

    def test_a_baseline_outside_the_golden_root_is_refused(self):
        text = MINIMAL.replace(
            'tier = "pr"', 'baseline = ["tests/unit/x.json"]\ntier = "pr"'
        )
        with self.assertRaisesRegex(
            ValueError,
            r"baseline 'tests/unit/x\.json' is outside tests/golden/<consumer>/",
        ):
            hr.load_registry(self._root_with(text))


class BaselineDeclarationTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]
    # What in a check, a corpus runner or a script's Python says that it records a baseline.
    RECORDS: ClassVar[tuple[str, ...]] = ("golden_update.", "driver.baseline_diff(")
    # Entries whose binaries record a baseline from C++ (tests/support/golden_update.h). The registry cannot see what a
    # binary links, so this set stands in for it, checked both ways against `baseline`.
    CPP_OWNERS: ClassVar[set[str]] = {
        "unit",
        "unit-arm",
        "ui-scenes-spec",
        "load-policy",
        "voice-pins",
    }
    # Entries that run an owner's code but record nothing: entry -> why.
    NOT_OWNERS: ClassVar[dict[str, str]] = {
        "golden-synth-arm": "the ARM render must equal the x86 records golden-synth makes",
        "render-audio-stretch-arm": "the ARM render must equal the x86 record render-audio-stretch makes",
        "render-audio-stretch-edges-arm": "the ARM render must equal the x86 record render-audio-stretch-edges makes",
        "upgrade-differential-corpus": "its part records nothing: upgrade_run.py refuses to re-record it",
    }

    def _code(self, h: hr.Harness) -> str:
        paths = [h.check] if h.check else []
        if h.corpus_runner:
            paths.append(str(hr.CORPUS_RUNNERS / f"{h.corpus_runner}_run.py"))
        paths += [arg for arg in h.run if arg.endswith(".py")]
        return "".join(
            (self.ROOT / p).read_text() for p in paths if (self.ROOT / p).is_file()
        )

    def test_an_entry_lists_a_baseline_exactly_when_it_records_one(self):
        for h in hr.load_registry(self.ROOT):
            with self.subTest(h.name):
                if h.variant_of:
                    # A variant copy runs its base's checks on another build; the loader gives it no baseline.
                    self.assertEqual(h.baseline, [])
                    continue
                records = h.name in self.CPP_OWNERS or (
                    h.name not in self.NOT_OWNERS
                    and any(mark in self._code(h) for mark in self.RECORDS)
                )
                self.assertEqual(bool(h.baseline), records)

    def test_every_listed_baseline_is_in_the_tree(self):
        for h in hr.load_registry(self.ROOT):
            for rel in h.baseline:
                with self.subTest(h.name, path=rel):
                    self.assertTrue((self.ROOT / rel).exists())

    def test_the_lists_here_name_registry_entries(self):
        names = {h.name for h in hr.load_registry(self.ROOT)}
        self.assertLessEqual(self.CPP_OWNERS | set(self.NOT_OWNERS), names)


class GoldenRootTest(unittest.TestCase):
    def test_a_baseline_lives_under_the_golden_root(self):
        for good in (
            "tests/golden/songs/fixtures.json",
            "tests/golden/ui/app",
            "tests/unit/dsp/x_records.h",
            "tests/integration/scenarios/pitch_pin_records.h",
            "website/src/data/generated/menu-tree.json",
        ):
            with self.subTest(good):
                self.assertIsNone(hr.baseline_problem(good))

    def test_a_baseline_elsewhere_is_refused(self):
        for bad in (
            "tests/golden",
            "tests/goldenx/a.json",
            "tests/integration/checks/x.sha256",
            "tests/unit/x.h",
            "src/instruments/classic/cable_records.h",
            "build/golden/a.json",
        ):
            with self.subTest(bad):
                self.assertIn(
                    "is outside tests/golden/<consumer>/",
                    hr.baseline_problem(bad) or "",
                )


def golden_strays(paths: list[str]) -> list[str]:
    """The paths among `paths` that are golden data outside tests/golden: a file under a directory named `golden`,
    or a snapshot or hash file (.pbm, .state, .sha256)."""
    return [
        path
        for path in paths
        if Path(path).parts[:2] != ("tests", "golden")
        and (
            "golden" in Path(path).parts[:-1]
            or Path(path).suffix in (".pbm", ".state", ".sha256")
        )
    ]


# Where a spec names a golden: the arguments of a golden-reading helper, or the `/` chain after golden_root().
GOLDEN_CALL = re.compile(
    r"\b(?:expect_golden|expect_script_goldens)\s*\(|\bgolden_root\(\)"
)
STRING_LITERAL = re.compile(r'"(?:[^"\\\n]|\\.)*"')
GOLDEN_ROOT_CHAIN = re.compile(r'(?:\s*/\s*(?:"(?:[^"\\\n]|\\.)*"|[\w:.]+(?:\(\))?))*')


def golden_name_escapes(name: str) -> bool:
    """Whether `name`, a golden named relative to GOLDEN_ROOT, can leave it: an absolute path or a `..` segment."""
    return name.startswith("/") or ".." in name.split("/")


def _call_span(text: str, start: int) -> int:
    """The end of the argument list whose opening parenthesis is at `start - 1`, skipping string literals."""
    depth = 1
    i = start
    while i < len(text) and depth:
        if text[i] == '"':
            literal = STRING_LITERAL.match(text, i)
            i = literal.end() if literal else i + 1
            continue
        depth += {"(": 1, ")": -1}.get(text[i], 0)
        i += 1
    return i


def golden_escapes(source: str) -> list[tuple[int, str]]:
    """The (line, literal) pairs in C++ `source` where a golden-reading helper is handed a name that leaves
    GOLDEN_ROOT. `#include` lines are not golden names."""
    text = "\n".join(
        "" if line.lstrip().startswith("#include") else line
        for line in source.splitlines()
    )
    found = []
    for call in GOLDEN_CALL.finditer(text):
        if call.group().endswith("("):
            end = _call_span(text, call.end())
        else:
            end = GOLDEN_ROOT_CHAIN.match(text, call.end()).end()
        for literal in STRING_LITERAL.finditer(text, call.end(), end):
            if golden_name_escapes(literal.group()[1:-1]):
                found.append(
                    (text.count("\n", 0, literal.start()) + 1, literal.group()[1:-1])
                )
    return found


class GoldenEscapeTest(unittest.TestCase):
    def test_a_name_that_leaves_the_root_escapes(self):
        for bad in (
            "../ui/x.pbm",
            "../shell/x.pbm",
            "../gfx/x.pbm",
            "../render/x.pbm",
            "../../x",
            "/abs/x",
            "ui/../x",
        ):
            with self.subTest(bad):
                self.assertTrue(golden_name_escapes(bad))

    def test_a_name_under_the_root_does_not(self):
        for good in ("ui/menu/x_48.pbm", "ui/menu/", "render", "gfx/oled/a..b.pbm"):
            with self.subTest(good):
                self.assertFalse(golden_name_escapes(good))

    def test_escapes_are_found_in_every_call_shape(self):
        source = """\
#include "../ui/support/golden.h"
expect_golden(self, "ui/menu/x_48.pbm", r.oled());
expect_golden(self, std::string{"ui/menu/"} + name + "_48.pbm", r.oled());
expect_golden(self, "../render/x.pbm", r.oled());
expect_golden(self, tall ? "ui/menu/a.pbm"
              : "/abs/x", f(g("x")));
uitest::expect_script_goldens(self, "../../x", reg, Services{});
load(wavetable_fixture::golden_root() / "render" / record.file);
load(wavetable_fixture::golden_root() / "ui/../x");
return std::string{GOLDEN_ROOT} + "/ui/menu_oracle";
log("../elsewhere");
"""
        self.assertEqual(
            golden_escapes(source),
            [(4, "../render/x.pbm"), (6, "/abs/x"), (7, "../../x"), (9, "ui/../x")],
        )


class GoldenTreeTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def _tracked(self, *patterns: str) -> list[str]:
        found = subprocess.run(
            ["git", "ls-files", "-z", "--", *patterns],
            cwd=self.ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        return [p for p in found.stdout.split("\0") if p]

    def test_golden_strays_names_data_outside_the_root(self):
        self.assertEqual(
            golden_strays(
                [
                    "tests/golden/gfx/a.pbm",
                    "docs/golden/a.json",
                    "tests/integration/checks/a.sha256",
                    "src/a.state",
                    "tests/harness/golden_vt_render/src/main.rs",
                    "tests/golden/README.md",
                ]
            ),
            ["docs/golden/a.json", "tests/integration/checks/a.sha256", "src/a.state"],
        )

    def test_no_golden_data_lives_outside_the_root(self):
        self.assertEqual(golden_strays(self._tracked()), [])

    def test_no_spec_names_a_golden_outside_the_root(self):
        hits = [
            f"{path}:{n}: {name}"
            for path in self._tracked("*.cpp", "*.h")
            for n, name in golden_escapes(
                (self.ROOT / path).read_text(errors="replace")
            )
        ]
        self.assertEqual(hits, [])


class RegistryVariantTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def test_no_sanitizer_copy_is_written_by_hand(self):
        raw = tomllib.loads((self.ROOT / "tests" / "registry.toml").read_text())
        self.assertEqual(
            [h["name"] for h in raw["harness"] if h["name"].startswith("app-asan-")], []
        )

    def test_the_sanitizer_copies_are_generated(self):
        copies = {
            h.name: h.variant_of
            for h in hr.load_registry(self.ROOT)
            if h.name.startswith("app-asan-")
        }
        self.assertEqual(len(copies), 7)
        self.assertTrue(
            all(base and name == f"app-asan-{base}" for name, base in copies.items())
        )


class StemExportBaselineTest(unittest.TestCase):
    def test_the_stem_export_declares_no_baseline(self):
        """A red ST-RENDER is bisected, never re-recorded: `./dbt test --update` must refuse the entry."""
        root = Path(__file__).resolve().parents[2]
        stem = next(h for h in hr.load_registry(root) if h.name == "stem-export")
        self.assertEqual(stem.baseline, [])


class CorpusEntryTest(unittest.TestCase):
    CORPUS = """
[[harness]]
name = "delta"
level = "integration"
runner = "corpus"
kind = "regression"
status = "gate"
gates = "delta holds"
areas = ["all"]
corpus_runner = "fake"
builds = ["golden-vt-render"]
tier = "nightly"
job = "model"
runtime_s = 1
"""

    def _root_with(self, text, runners=("fake",)):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(text)
        (root / hr.CORPUS_RUNNERS).mkdir(parents=True)
        for name in runners:
            (root / hr.CORPUS_RUNNERS / f"{name}_run.py").write_text("")
        return root

    def test_loads(self):
        h = hr.load_registry(self._root_with(self.CORPUS))[0]
        self.assertEqual(
            (h.runner, h.corpus_runner, h.scenario), ("corpus", "fake", None)
        )

    def test_needs_a_runner(self):
        with self.assertRaisesRegex(ValueError, "corpus_runner"):
            hr.load_registry(
                self._root_with(self.CORPUS.replace('corpus_runner = "fake"\n', ""))
            )

    def test_the_runner_must_exist(self):
        with self.assertRaisesRegex(ValueError, "fake_run.py"):
            hr.load_registry(self._root_with(self.CORPUS, runners=()))

    def test_takes_no_scenario_fields(self):
        for extra in (
            'scenario = "x"',
            'markers = ["X"]',
            'result = "X"',
            'check = "c.py"',
            'run = ["x"]',
        ):
            with self.subTest(extra=extra), self.assertRaises(ValueError):
                hr.load_registry(self._root_with(self.CORPUS + extra + "\n"))

    def test_needs_the_harness_binary(self):
        with self.assertRaises(ValueError):
            hr.load_registry(
                self._root_with(
                    self.CORPUS.replace('builds = ["golden-vt-render"]\n', "")
                )
            )

    def test_only_a_corpus_gate_names_a_runner(self):
        with self.assertRaisesRegex(ValueError, "only a corpus gate"):
            hr.load_registry(self._root_with(MINIMAL + 'corpus_runner = "fake"\n'))

    def test_the_registry_holds_the_corpus_gates(self):
        root = Path(__file__).resolve().parents[2]
        corpus = [h for h in hr.load_registry(root) if h.runner == "corpus"]
        self.assertEqual(len(corpus), 17)
        self.assertTrue(all(h.job == "model" and h.alone for h in corpus))

    def test_the_registrys_areas_are_consistent(self):
        root = Path(__file__).resolve().parents[2]
        entries = hr.load_registry(root)
        tracked = subprocess.run(
            ["git", "ls-files"], cwd=root, capture_output=True, text=True, check=True
        ).stdout.split()
        problems = ts.consistency_problems(
            [ts.Entry(h.name, h.areas, []) for h in entries],
            hr.load_areas(root),
            tracked,
        )
        self.assertEqual(problems, [])


class RigAndParityEntryTest(unittest.TestCase):
    def test_every_rig_entry_runs_alone(self):
        # There is one rig: two entries driving it at once would load images over each other.
        root = Path(__file__).resolve().parents[2]
        rig = [h for h in hr.load_registry(root) if "rig" in h.requires]
        self.assertTrue(rig)
        for h in rig:
            with self.subTest(h.name):
                self.assertTrue(h.alone)

    def test_every_parity_entry_keeps_its_own_artifacts(self):
        # The parity tools default to one shared artifact directory; entries run one after another would overwrite
        # each other's snapshots and logs, which are read by hand.
        root = Path(__file__).resolve().parents[2]
        tools = {"tools/emu_parity.py", "tools/device_parity.py"}
        parity = [h for h in hr.load_registry(root) if tools.intersection(h.run)]
        self.assertTrue(parity)
        for h in parity:
            with self.subTest(h.name):
                self.assertIn("--out", h.run)
                self.assertEqual(
                    h.run[h.run.index("--out") + 1], f".superpowers/emu-runs/{h.name}"
                )


class ScenarioEntryTest(unittest.TestCase):
    SCENARIO = """
[[harness]]
name = "gamma"
level = "integration"
runner = "scenario"
kind = "property"
status = "gate"
gates = "gamma holds"
areas = ["all"]
scenario = "gamma"
markers = ["GA-ONE"]
result = "GA-RESULT failures=0"
builds = ["golden-vt-render"]
tier = "nightly"
job = "render"
runtime_s = 1
"""

    def _root_with(self, text):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(text)
        return root

    def test_loads(self):
        h = hr.load_registry(self._root_with(self.SCENARIO))[0]
        self.assertEqual((h.scenario, h.markers), ("gamma", ["GA-ONE"]))

    def test_needs_the_harness_binary(self):
        with self.assertRaises(ValueError):
            hr.load_registry(
                self._root_with(
                    self.SCENARIO.replace('builds = ["golden-vt-render"]\n', "")
                )
            )

    def test_an_empty_scenario_is_refused(self):
        with self.assertRaisesRegex(ValueError, "must name a row"):
            hr.load_registry(
                self._root_with(
                    self.SCENARIO.replace('scenario = "gamma"', 'scenario = ""')
                )
            )

    def test_a_scenario_entry_names_its_row(self):
        with self.assertRaisesRegex(ValueError, "must name a row"):
            hr.load_registry(
                self._root_with(self.SCENARIO.replace('scenario = "gamma"\n', ""))
            )

    def test_a_scenario_entry_names_its_markers(self):
        with self.assertRaisesRegex(ValueError, "markers"):
            hr.load_registry(
                self._root_with(self.SCENARIO.replace('markers = ["GA-ONE"]\n', ""))
            )

    def test_a_scenario_entry_names_its_result(self):
        with self.assertRaisesRegex(ValueError, "result"):
            hr.load_registry(
                self._root_with(
                    self.SCENARIO.replace('result = "GA-RESULT failures=0"\n', "")
                )
            )

    def test_rejects_run(self):
        with self.assertRaises(ValueError):
            hr.load_registry(self._root_with(self.SCENARIO + 'run = ["x"]\n'))

    def test_missing_check_module_is_rejected(self):
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(self.SCENARIO + 'check = "nope.py"\n'))
        self.assertIn("nope.py", str(cm.exception))


class CheckModuleTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    # Checks that keep `re` for text that is not a marker line, and what they parse with it.
    NON_MARKER_RE: ClassVar[dict[str, str]] = {
        "render_preset.py": "a preset's saved MIDI learn (XML and JSON)",
        "render_song.py": "a song's reverb element",
        "ui_scripts.py": "a UI script's board steps",
    }

    IMPORTS_RE = re.compile(r"^\s*(import re\b|from re import)", re.MULTILINE)

    def _checks_importing_re(self):
        checks = self.ROOT / "tests" / "integration" / "checks"
        return sorted(
            p.name
            for p in checks.rglob("*.py")
            if not p.name.startswith("test_") and self.IMPORTS_RE.search(p.read_text())
        )

    def test_checks_read_marker_lines_through_scenario_gate(self):
        own = [n for n in self._checks_importing_re() if n not in self.NON_MARKER_RE]
        self.assertEqual(
            own,
            [],
            "parse marker lines with scenario_gate's helpers (tag, fields, field, ...)",
        )

    def test_every_allowed_re_import_is_still_used(self):
        stale = sorted(set(self.NON_MARKER_RE) - set(self._checks_importing_re()))
        self.assertEqual(
            stale, [], "drop these from NON_MARKER_RE: they no longer import re"
        )

    def test_a_check_that_runs_the_binary_itself_judges_the_entrys_lines(self):
        unjudged = sorted(
            {
                h.check
                for h in hr.load_registry(self.ROOT)
                if h.runner == "scenario"
                and h.check
                and "\ndef run(" in (text := (self.ROOT / h.check).read_text())
                and not any(
                    call in text
                    for call in ("require_standard(", "require_markers(", "shortfall(")
                )
            }
        )
        self.assertEqual(
            unjudged,
            [],
            "these run(gate) checks never apply their entry's markers and result (scenario_gate.Gate.require_standard)",
        )


def _load_task_test():
    spec = importlib.util.spec_from_file_location(
        "task_test", Path(__file__).parent / "task-test.py"
    )
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class BuildsTest(unittest.TestCase):
    def _root_with(self, text):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(text)
        return root

    def test_known_product_loads(self):
        text = MINIMAL.replace(
            'requires = ["cargo"]',
            'builds = ["golden-vt-render"]\nrequires = ["cargo"]',
        )
        self.assertEqual(
            hr.load_registry(self._root_with(text))[0].builds, ["golden-vt-render"]
        )

    def test_unknown_product_is_rejected(self):
        text = MINIMAL.replace(
            'requires = ["cargo"]', 'builds = ["a-pony"]\nrequires = ["cargo"]'
        )
        with self.assertRaises(ValueError) as cm:
            hr.load_registry(self._root_with(text))
        self.assertIn("a-pony", str(cm.exception))

    def test_golden_vt_render_outputs_the_release_binary(self):
        root = Path("/repo")
        self.assertEqual(
            tb.products(root)["golden-vt-render"].outputs(root),
            [root / "build/cargo-harness/harness/golden_vt_render"],
        )

    def test_prebuilt_outputs_rejects_unprebuildable(self):
        root = Path("/repo")
        with self.assertRaisesRegex(ValueError, "has no prebuilt outputs"):
            tb.prebuilt_outputs(["host-app"], tb.products(root), root)

    def test_prebuilt_missing_output_fails(self):
        task_test = _load_task_test()
        root = Path(tempfile.mkdtemp())
        h = dataclasses.replace(entry("r"), job="render", builds=["golden-vt-render"])
        args = types.SimpleNamespace(
            junit=None,
            skip_checks=True,
            build_only=False,
            jobs=1,
            strict=False,
            prebuilt=True,
        )
        out = io.StringIO()
        with (
            mock.patch("subprocess.run") as run,
            mock.patch.object(task_test, "OUT", out),
        ):
            status = task_test.cmd_run(root, [h], args, [], named=False)
        run.assert_not_called()
        self.assertEqual(status, 1)
        self.assertIn("is not built", out.getvalue())

    def _run(self, selection, junit=None, prebuilt=False):
        task_test = _load_task_test()
        args = types.SimpleNamespace(
            junit=junit,
            skip_checks=False,
            build_only=False,
            jobs=1,
            strict=False,
            prebuilt=prebuilt,
        )
        # A real file: a test run straight through writes to the runner's output descriptor.
        with (
            tempfile.TemporaryFile("w+") as out,
            mock.patch.object(task_test, "OUT", out),
        ):
            return task_test.cmd_run(
                Path(tempfile.mkdtemp()), selection, args, [], named=False
            )

    def test_prebuilt_checks_products_before_probes(self):
        h = dataclasses.replace(
            entry("r"),
            job="render",
            builds=["golden-vt-render"],
            requires=["cargo-fuzz"],
        )
        with mock.patch.object(hr, "probe", return_value=False):
            self.assertEqual(self._run([h], prebuilt=True), 1)

    def test_junit_written_for_every_test_without_its_own(self):
        junit = Path(tempfile.mkdtemp())
        passing = entry("passes")
        failing = dataclasses.replace(entry("fails"), run=["false"])
        blocked = dataclasses.replace(
            entry("blocked"), job="render", builds=["golden-vt-render"]
        )
        self._run([passing, failing, blocked], junit=str(junit), prebuilt=True)
        cases = {
            p.stem: ET.parse(p).getroot().find(".//testcase")
            for p in junit.glob("*.xml")
        }
        self.assertEqual(sorted(cases), ["blocked", "fails", "passes"])
        self.assertIsNone(cases["passes"].find("failure"))
        self.assertIsNotNone(cases["fails"].find("failure"))
        self.assertIn("is not built", cases["blocked"].find("failure").get("message"))

    def test_order_puts_dependencies_first_and_each_once(self):
        known = tb.products(Path("/repo"))
        self.assertEqual(
            tb.order(["golden-vt-render", "host-app", "golden-vt-render"], known),
            ["host-app", "golden-vt-render"],
        )

    def test_order_rejects_a_cycle(self):
        a = tb.Product("a", ["b"], [], {})
        b = tb.Product("b", ["a"], [], {})
        with self.assertRaises(ValueError):
            tb.order(["a"], {"a": a, "b": b})

    def test_golden_vt_render_links_the_host_app_tree(self):
        known = tb.products(Path("/repo"))
        (step,) = known["golden-vt-render"].steps(Path("/repo"))
        self.assertEqual(step.env["DELUGE_HOSTAPP_BUILD_DIR"], "/repo/build/host-app")

    def test_the_arm_golden_vt_render_compiles_its_cxx_for_the_arm_host_app_cpu(self):
        # cc names the target to clang by rustc's LLVM triple, `arm-unknown-linux-gnueabihf`, which leaves the CPU at
        # ARMv4T without NEON; the build scripts' C++ needs the arm-linux host app's CPU and FPU.
        known = tb.products(Path("/repo"))
        (step,) = known["golden-vt-render-arm"].steps(Path("/repo"))
        for var in (
            "CFLAGS_armv7_unknown_linux_gnueabihf",
            "CXXFLAGS_armv7_unknown_linux_gnueabihf",
        ):
            with self.subTest(var):
                self.assertEqual(
                    step.env[var].split(),
                    ["-mcpu=cortex-a9", "-mfpu=neon", "-mfloat-abi=hard"],
                )

    TEST_TREES = ("host-app", "host-app-asan", "host-app-arm")

    def _configured_tree(self, root, tree, extra=""):
        (root / tree).mkdir(parents=True)
        (root / tree / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={root / 'src/platform/host'}\n"
            "CMAKE_CXX_COMPILER:FILEPATH=/usr/bin/clang++\n" + extra
        )

    def test_test_trees_configure_version_off(self):
        root = Path(tempfile.mkdtemp())
        known = tb.products(root)
        for name in self.TEST_TREES:
            configure = known[name].steps(root)[0]
            self.assertEqual(configure.what, "configure", name)
            self.assertIn("-DDELUGE_VERSION_FROM_GIT=OFF", configure.argv, name)

    def test_existing_tree_without_the_define_reconfigures(self):
        root = Path(tempfile.mkdtemp())
        (root / "src/platform/host").mkdir(parents=True)
        self._configured_tree(root, tb.HOST_APP_TREE)
        steps = tb.products(root)["host-app"].steps(root)
        self.assertEqual([s.what for s in steps], ["configure", "build"])
        self.assertTrue((root / tb.HOST_APP_TREE).is_dir())

    def test_tree_with_the_define_does_not_reconfigure(self):
        root = Path(tempfile.mkdtemp())
        (root / "src/platform/host").mkdir(parents=True)
        self._configured_tree(
            root, tb.HOST_APP_TREE, "DELUGE_VERSION_FROM_GIT:BOOL=OFF\n"
        )
        steps = tb.products(root)["host-app"].steps(root)
        self.assertEqual([s.what for s in steps], ["build"])


class ScenarioGateTest(unittest.TestCase):
    """The standard checks every scenario gate makes, against a stub binary that prints what a run would."""

    def _gate(
        self,
        stdout,
        code=0,
        markers=("AB-ONE", "AB-TWO"),
        result="AB-RESULT failures=0",
        check="",
    ):
        root = Path(tempfile.mkdtemp())
        binary = root / "fake"
        binary.write_text(
            "#!/bin/sh\ncat <<'END'\n" + stdout + "\nEND\nexit " + str(code) + "\n"
        )
        binary.chmod(0o755)
        if check:
            (root / "check.py").write_text(check)
        entry = hr.Harness(
            name="g",
            path=".",
            level="integration",
            runner="scenario",
            kind="property",
            status="gate",
            gates="g",
            tier="nightly",
            build=[],
            run=[],
            requires=[],
            runtime_s=1,
            job="unit",
            builds=["golden-vt-render"],
            scenario="g",
            markers=list(markers),
            result=result,
            check="check.py" if check else "",
        )
        log = io.StringIO()
        return sg.run_gate(root, entry, dict(os.environ), binary, log), log.getvalue()

    GOOD = "AB-ONE x=1\nAB-TWO y=2\nAB-RESULT failures=0"

    def test_a_clean_run_passes(self):
        passed, log = self._gate(self.GOOD)
        self.assertTrue(passed, log)

    def test_a_nonzero_exit_fails(self):
        passed, log = self._gate(self.GOOD, code=1)
        self.assertFalse(passed)
        self.assertIn("exited 1", log)

    def test_a_missing_marker_fails(self):
        passed, log = self._gate("AB-ONE x=1\nAB-RESULT failures=0")
        self.assertFalse(passed)
        self.assertIn("no AB-TWO line", log)

    def test_a_marker_must_start_the_line(self):
        passed, _ = self._gate("AB-ONE x=1\nsaw AB-TWO y=2\nAB-RESULT failures=0")
        self.assertFalse(passed)

    def test_the_result_line_must_be_whole(self):
        passed, _ = self._gate("AB-ONE x=1\nAB-TWO y=2\nAB-RESULT failures=01")
        self.assertFalse(passed)

    def test_a_check_module_runs_after_and_can_fail(self):
        check = (
            "from scenario_gate import int_field\n\ndef check(gate, run):\n"
            "    if int_field(run.first('AB-TWO'), 'y') != 3:\n        gate.fail('AB-TWO y is not 3')\n"
        )
        passed, log = self._gate(self.GOOD, check=check)
        self.assertFalse(passed)
        self.assertIn("FAIL AB-TWO y is not 3", log)

    def test_field_helpers(self):
        self.assertEqual(sg.field("AB x=1 y=two", "y"), "two")
        self.assertIsNone(sg.field("AB xx=1", "x"))
        self.assertEqual(sg.int_field("AB n=-3", "n"), -3)
        self.assertIsNone(sg.int_field("AB n=abc", "n"))

    def test_count_field_accepts_only_unsigned_digits(self):
        self.assertEqual(sg.count_field("AB n=0 m=12", "m"), 12)
        self.assertEqual(sg.count_field("AB n=0", "n"), 0)
        for bad in ("-1", "+3", "-0", "8x", "", "x", "\u00b2"):
            self.assertIsNone(sg.count_field(f"AB n={bad}", "n"), bad)
        self.assertIsNone(sg.count_field("AB m=1", "n"))
        self.assertIsNone(sg.count_field(None, "n"))


class ScenarioGateParseTest(unittest.TestCase):
    """scenario_gate's marker-line helpers, which every check parses through."""

    def test_tag(self):
        self.assertEqual(sg.tag("CL-SONGLOAD clipsReferenced=0"), "CL-SONGLOAD")
        self.assertEqual(sg.tag("TIER-SYNC-AT n=1"), "TIER-SYNC-AT")
        self.assertEqual(sg.tag("RW-RESULT"), "RW-RESULT")
        self.assertEqual(sg.tag("CL-EPOCH0 epoch=1"), "CL-EPOCH0")
        self.assertIsNone(sg.tag("saw CL-X a=1"))
        self.assertIsNone(sg.tag("Cl-X a=1"))
        self.assertIsNone(sg.tag(None))
        self.assertEqual(sg.tag("SC-SHELL-SLICE x=1"), "SC-SHELL-SLICE")

    def test_fields(self):
        self.assertEqual(
            sg.fields("PM-START stray=0 masked_alloc=0 hash=ab12 injected=1"),
            {"stray": "0", "masked_alloc": "0", "hash": "ab12", "injected": "1"},
        )
        self.assertEqual(sg.fields("A b=1 c= d=2"), {"b": "1", "c": "", "d": "2"})
        self.assertEqual(sg.fields("A label x=1"), {"x": "1"})
        self.assertEqual(sg.fields(None), {})
        self.assertEqual(sg.fields("A k=b=c x=1 x=2"), {"k": "b=c", "x": "1"})

    def test_fields_agrees_with_field(self):
        for line in ("A x=1 y= z=3", "A x=1 x=2", "UPGRADE OK=3 FAIL=0"):
            for key, value in sg.fields(line).items():
                self.assertEqual(value, sg.field(line, key), (line, key))

    def test_has_fields(self):
        line = "SL-BURST n=40 violations=0 pending=0"
        self.assertTrue(sg.has_fields(line, violations=0, pending=0))
        self.assertTrue(sg.has_fields(line, n="40"))
        self.assertFalse(sg.has_fields(line, pending=1))
        self.assertFalse(sg.has_fields(line, absent=0))
        self.assertFalse(sg.has_fields("SL-BURST pendingX=0", pending=0))
        self.assertFalse(sg.has_fields(None))

    def test_starting(self):
        lines = ["AB-CASE 1", "AB-REF 2", "x AB-CASE", "AB-CASEY 3"]
        self.assertEqual(
            sg.starting(lines, "AB-CASE ", "AB-REF "), ["AB-CASE 1", "AB-REF 2"]
        )
        run = sg.Run(0, "\n".join(lines), "")
        self.assertEqual(run.starting("AB-CASE ", "AB-REF "), ["AB-CASE 1", "AB-REF 2"])
        self.assertEqual(run.starting("AB-CASE"), ["AB-CASE 1", "AB-CASEY 3"])

    def test_after(self):
        self.assertEqual(sg.after("RS-MODEL a b", "RS-MODEL "), "a b")
        self.assertIsNone(sg.after("RS-X a", "RS-MODEL "))
        self.assertIsNone(sg.after(None, "RS-MODEL "))


class GateShortfallTest(unittest.TestCase):
    """The entry's markers and result, judged over lines from any source."""

    def _gate(self):
        h = dataclasses.replace(
            entry("g"),
            runner="scenario",
            scenario="g",
            markers=["AB-ONE", "AB-TWO"],
            result="AB-RESULT failures=0",
        )
        log = io.StringIO()
        gate = sg.Gate("g", Path(tempfile.mkdtemp()), Path("/nonexistent"), {}, h, log)
        self.addCleanup(gate.cleanup)
        return gate, log

    def test_shortfall_names_the_first_gap_and_records_nothing(self):
        gate, _ = self._gate()
        self.assertIsNone(
            gate.shortfall(["AB-ONE x=1", "AB-TWO", "AB-RESULT failures=0"])
        )
        self.assertEqual(
            gate.shortfall(["AB-ONE x=1", "AB-RESULT failures=0"]),
            "no AB-TWO line; that stage did not run",
        )
        self.assertEqual(
            gate.shortfall(["AB-ONE", "AB-TWO", "AB-RESULT failures=1"]),
            "in-process failures:",
        )
        self.assertEqual(gate.failures, [])

    def test_a_longer_tag_does_not_satisfy_a_marker(self):
        gate, _ = self._gate()
        self.assertEqual(
            gate.shortfall(["AB-ONE-X", "AB-TWO", "AB-RESULT failures=0"]),
            "no AB-ONE line; that stage did not run",
        )

    def test_require_markers_over_lines_from_two_sources(self):
        gate, _ = self._gate()
        out = ["AB-ONE x=1", "AB-RESULT failures=0"]
        err = ["AB-TWO"]
        self.assertTrue(gate.require_markers(out + err))
        self.assertEqual(gate.failures, [])

    def test_require_markers_records_and_shows_the_marker_lines(self):
        gate, log = self._gate()
        self.assertFalse(
            gate.require_markers(["AB-ONE x=1", "noise", "AB-RESULT failures=0"])
        )
        self.assertEqual(gate.failures, ["no AB-TWO line; that stage did not run"])
        self.assertIn("AB-ONE x=1\n", log.getvalue())
        self.assertNotIn("noise", log.getvalue())


class ProbeTest(unittest.TestCase):
    def test_delugemu_needs_its_launcher_and_its_qemu(self):
        for files in (
            ("scripts/run.sh", "qemu/build/qemu-system-arm"),
            ("qemu/build/qemu-system-arm", "scripts/run.sh"),
        ):
            emu = Path(tempfile.mkdtemp())
            with (
                self.subTest(files),
                mock.patch.dict(os.environ, {"DELUGEMU_ROOT": str(emu)}),
            ):
                self.assertFalse(hr.probe("delugemu", Path.cwd()))
                for f in files:
                    self.assertFalse(hr.probe("delugemu", Path.cwd()))
                    (emu / f).parent.mkdir(parents=True, exist_ok=True)
                    (emu / f).write_text("")
                self.assertTrue(hr.probe("delugemu", Path.cwd()))

    def test_parity_ref_is_mains_image(self):
        ref = Path(tempfile.mkdtemp()) / "main.elf"
        with mock.patch.dict(os.environ, {"DELUGE_PARITY_REF": str(ref.parent)}):
            self.assertFalse(hr.probe("parity-ref", Path.cwd()))
        with mock.patch.dict(os.environ, {"DELUGE_PARITY_REF": str(ref)}):
            self.assertFalse(hr.probe("parity-ref", Path.cwd()))
            ref.write_text("")
            self.assertTrue(hr.probe("parity-ref", Path.cwd()))
        root = Path(tempfile.mkdtemp())
        with mock.patch.dict(os.environ, {"DELUGE_PARITY_REF": ""}):
            self.assertFalse(hr.probe("parity-ref", root))
            (root / ".superpowers/estate/emu").mkdir(parents=True)
            (root / ".superpowers/estate/emu/main.elf").write_text("")
            self.assertTrue(hr.probe("parity-ref", root))

    def test_content_card_is_the_content_directory(self):
        cards = Path(tempfile.mkdtemp())
        (cards / "a-file").write_text("")
        with mock.patch.dict(os.environ, {"DELUGE_CONTENT_CARDS": str(cards)}):
            self.assertTrue(hr.probe("content-card", Path.cwd()))
        for path in (cards / "none", cards / "a-file"):
            with (
                self.subTest(path.name),
                mock.patch.dict(os.environ, {"DELUGE_CONTENT_CARDS": str(path)}),
            ):
                self.assertFalse(hr.probe("content-card", Path.cwd()))

    def test_rig_is_a_probe_host(self):
        with mock.patch.dict(os.environ, {"DELUGE_PROBE_HOST": "rig"}):
            self.assertTrue(hr.probe("rig", Path.cwd()))
        with mock.patch.dict(os.environ, {"DELUGE_PROBE_HOST": ""}):
            self.assertFalse(hr.probe("rig", Path.cwd()))

    @staticmethod
    def _usb_device(devices: Path, name: str, vendor: str, product: str) -> Path:
        device = devices / name
        device.mkdir()
        (device / "idVendor").write_text(f"{vendor}\n")
        (device / "idProduct").write_text(f"{product}\n")
        return device

    def _deluge_usb(self, devices: Path, modules: bool) -> bool:
        spec = object() if modules else None
        with (
            mock.patch.object(hr, "USB_DEVICES", devices),
            mock.patch.object(hr.importlib.util, "find_spec", return_value=spec),
        ):
            return hr.probe("deluge-usb", Path.cwd())

    def test_deluge_usb_needs_the_device_on_this_machine(self):
        devices = Path(tempfile.mkdtemp())
        self.assertFalse(self._deluge_usb(devices, modules=True))
        self._usb_device(devices, "1-1", "16d0", "0ce3")
        self._usb_device(devices, "1-2", "16d1", "0ce2")
        (devices / "usb1").mkdir()
        self.assertFalse(self._deluge_usb(devices, modules=True))
        self._usb_device(devices, "1-3", "16d0", "0ce2")
        self.assertTrue(self._deluge_usb(devices, modules=True))

    def test_deluge_usb_needs_the_python_modules(self):
        devices = Path(tempfile.mkdtemp())
        self._usb_device(devices, "1-1", "16d0", "0ce2")
        self.assertFalse(self._deluge_usb(devices, modules=False))

    def test_a_usb_device_that_goes_away_reads_as_no_device(self):
        devices = Path(tempfile.mkdtemp())
        self._usb_device(devices, "1-1", "16d0", "0ce2")
        with mock.patch.object(Path, "read_text", side_effect=FileNotFoundError):
            self.assertFalse(self._deluge_usb(devices, modules=True))
        gone = devices / "1-2"
        self.assertIsNone(hr.usb_device_id(gone))

    def test_legacy_corpus_needs_synths_beside_it(self):
        backup = Path(tempfile.mkdtemp())
        songs = backup / "SONGS"
        songs.mkdir()
        with mock.patch.dict(os.environ, {"DELUGE_LEGACY_CORPUS": str(songs)}):
            self.assertFalse(hr.probe("legacy-corpus", Path.cwd()))
            (backup / "SYNTHS").mkdir()
            self.assertTrue(hr.probe("legacy-corpus", Path.cwd()))
        with mock.patch.dict(os.environ, {"DELUGE_LEGACY_CORPUS": ""}):
            self.assertFalse(hr.probe("legacy-corpus", Path.cwd()))

    def test_cargo_is_present(self):
        self.assertTrue(hr.probe("cargo", Path.cwd()))

    def test_every_requirement_key_has_a_fix_hint(self):
        for key in hr.PROBES:
            self.assertTrue(hr.PROBES[key], f"{key} has no fix hint")

    def test_unknown_requirement_probes_false(self):
        self.assertFalse(hr.probe("a-pony", Path.cwd()))

    def test_song_corpus_via_deluge_backup(self):
        # Test that song-corpus probe returns True when DELUGE_BACKUP points
        # at a real directory and DELUGE_SONG_CORPUS is unset.
        backup_dir = Path(tempfile.mkdtemp())
        root = Path(__file__).resolve().parents[2]
        with mock.patch.dict(
            os.environ, {"DELUGE_SONG_CORPUS": "", "DELUGE_BACKUP": str(backup_dir)}
        ):
            self.assertTrue(hr.probe("song-corpus", root))


class CiMatrixTest(unittest.TestCase):
    def test_ci_matrix_covers_every_ci_test_once(self):
        root = Path(__file__).resolve().parents[2]
        selection = hr.select(hr.load_registry(root), [], [], "nightly")
        names = [
            n for e in hr.ci_matrix(selection)["include"] for n in e["tests"].split()
        ]
        self.assertEqual(
            sorted(names), sorted(h.name for h in selection if not hr.local_only(h))
        )
        self.assertEqual(len(names), len(set(names)))

    def test_local_only_requirements_are_probes(self):
        self.assertLessEqual(hr.LOCAL_ONLY, set(hr.PROBES))
        self.assertEqual(
            hr.LOCAL_ONLY,
            {
                "song-corpus",
                "song-xml-corpus",
                "legacy-corpus",
                "delugemu",
                "parity-ref",
                "content-card",
                "rig",
                "deluge-usb",
            },
        )

    def test_ci_matrix_omits_local_only_tests(self):
        corpus = dataclasses.replace(entry("c"), job="unit", requires=["song-corpus"])
        xml = dataclasses.replace(
            entry("x"), job="unit", requires=["cargo", "song-xml-corpus"]
        )
        plain = dataclasses.replace(entry("p"), job="unit")
        matrix = hr.ci_matrix([corpus, plain, xml])
        self.assertEqual(
            matrix["include"], [{"job": "unit", "tests": "p", "prebuilt": ""}]
        )

    def test_no_build_product_requires_a_local_only_item(self):
        root = Path(__file__).resolve().parents[2]
        for name, product in tb.products(root).items():
            with self.subTest(product=name):
                self.assertTrue(hr.LOCAL_ONLY.isdisjoint(product.requires))

    def test_list_marks_local_only_entries(self):
        root = Path(__file__).resolve().parents[2]
        corpus = dataclasses.replace(entry("c"), requires=["song-corpus"])
        out = io.StringIO()
        with (
            mock.patch.object(hr, "probe", return_value=True),
            contextlib.redirect_stdout(out),
        ):
            tt.cmd_list(root, [corpus], False, [corpus])
        self.assertIn("[local-only: not run in CI]", out.getvalue())

    def test_doctor_marks_a_missing_local_only_requirement(self):
        root = Path(__file__).resolve().parents[2]
        corpus = dataclasses.replace(entry("c"), requires=["song-corpus"])
        out = io.StringIO()
        with (
            mock.patch.object(hr, "probe", return_value=False),
            contextlib.redirect_stdout(out),
        ):
            code = tt.cmd_doctor(root, [corpus])
        self.assertEqual(code, 1)
        self.assertIn("song-corpus   (local-only: CI never has it)", out.getvalue())

    def test_local_only(self):
        self.assertTrue(
            hr.local_only(dataclasses.replace(entry("c"), requires=["song-corpus"]))
        )
        self.assertFalse(
            hr.local_only(dataclasses.replace(entry("p"), requires=["cargo"]))
        )

    def test_ci_matrix_entry_shape(self):
        selection = [
            dataclasses.replace(entry("a"), job="unit"),
            dataclasses.replace(entry("r"), job="render"),
            dataclasses.replace(entry("b"), job="unit"),
        ]
        self.assertEqual(
            hr.ci_matrix(selection),
            {
                "include": [
                    {"job": "render", "tests": "r", "prebuilt": "golden-vt-render"},
                    {"job": "unit", "tests": "a b", "prebuilt": ""},
                ],
            },
        )


class MarkdownTableTest(unittest.TestCase):
    def test_table_has_a_job_column_after_tier(self):
        table = hr.markdown_table(hr.load_registry(self._root()))
        header = [c.strip() for c in table.split("\n")[0].strip("|").split("|")]
        self.assertEqual(header[header.index("Tier") + 1], "Job")
        self.assertIn("| pr | storage |", table)

    def test_table_has_a_row_per_harness(self):
        entries = hr.load_registry(self._root())
        table = hr.markdown_table(entries)
        self.assertIn("| alpha |", table)
        self.assertIn("alpha must equal beta", table)
        self.assertTrue(table.startswith("|"))

    def test_table_escapes_pipes_in_gates(self):
        # Ensure pipes in gates text are escaped to preserve table structure
        entries = hr.load_registry(self._root_with_pipe())
        table = hr.markdown_table(entries)
        # The pipe in "alpha|beta" should be escaped as "alpha\|beta"
        self.assertIn("alpha\\|beta", table)
        # Count pipes in header row and data row to verify structure is preserved
        lines = table.split("\n")
        header_row = lines[0]
        data_row = lines[2]
        # One more pipe than columns: leading, one between each pair, trailing.
        self.assertEqual(header_row.count("|"), 10)
        # The escaped pipe adds one to the data row without adding a column.
        self.assertEqual(data_row.count("|"), 11)

    def _root(self):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(MINIMAL)
        return root

    def _root_with_pipe(self):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        toml_with_pipe = MINIMAL.replace(
            'gates = "alpha must equal beta"', 'gates = "alpha|beta must equal charlie"'
        )
        (root / "tests" / "registry.toml").write_text(toml_with_pipe)
        return root


class WithTableTest(unittest.TestCase):
    README = f"intro\n{hr.TABLE_BEGIN}\n| old |\n{hr.TABLE_END}\noutro\n"

    def test_replaces_only_the_table(self):
        self.assertEqual(
            hr.with_table(self.README, "| new |\n"),
            f"intro\n{hr.TABLE_BEGIN}\n| new |\n{hr.TABLE_END}\noutro\n",
        )

    def test_is_idempotent(self):
        once = hr.with_table(self.README, "| new |\n")
        self.assertEqual(hr.with_table(once, "| new |\n"), once)

    def test_missing_markers_are_an_error(self):
        with self.assertRaisesRegex(ValueError, "BEGIN GENERATED TABLE"):
            hr.with_table("no markers here\n", "| new |\n")

    def test_committed_readme_is_current(self):
        root = Path(__file__).resolve().parents[2]
        readme = (root / "tests" / "README.md").read_text()
        table = hr.markdown_table(hr.load_registry(root))
        self.assertEqual(
            hr.with_table(readme, table),
            readme,
            "tests/README.md is stale: run ./dbt test --update-readme",
        )


class UpgradeRunGuardTest(unittest.TestCase):
    @staticmethod
    def _shortfalls(*args):
        spec = importlib.util.spec_from_file_location(
            "upgrade_run",
            Path(__file__).resolve().parents[2]
            / "tests/integration/corpus/upgrade_run.py",
        )
        module = importlib.util.module_from_spec(spec)
        sys.path.insert(0, str(Path(spec.origin).parent))
        try:
            spec.loader.exec_module(module)
        finally:
            sys.path.pop(0)
        return module.shortfalls(*args)

    def test_an_empty_run_is_a_shortfall(self):
        self.assertTrue(self._shortfalls("committed", 0, 39, 0, 0))
        self.assertTrue(self._shortfalls("committed", 38, 39, 0, 0))
        self.assertTrue(self._shortfalls("corpus", 230, 0, 0, 5))
        self.assertTrue(self._shortfalls("corpus", 230, 0, 5, 0))
        self.assertTrue(self._shortfalls("all", 0, 39, 0, 0))

    def test_corpus_cases_do_not_make_up_a_committed_shortfall(self):
        self.assertTrue(self._shortfalls("all", 40, 41, 200, 30))

    def test_an_absent_corpus_original_is_a_shortfall(self):
        self.assertTrue(self._shortfalls("all", 41, 41, 200, 30, 1))
        self.assertTrue(self._shortfalls("corpus", 0, 41, 200, 30, 2))

    def test_nothing_owned_or_below_the_floor_is_a_shortfall(self):
        self.assertTrue(self._shortfalls("committed", 0, 0, 0, 0))
        self.assertTrue(self._shortfalls("all", 0, 0, 5, 5))
        self.assertTrue(self._shortfalls("committed", 40, 40, 0, 0))

    def test_a_complete_run_is_not(self):
        self.assertEqual(self._shortfalls("committed", 41, 41, 0, 0), [])
        self.assertEqual(self._shortfalls("corpus", 0, 41, 200, 30), [])
        self.assertEqual(self._shortfalls("all", 41, 41, 200, 30), [])


class CorpusDeclarationTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def _entry(self, requires):
        return dataclasses.replace(entry("t"), requires=requires)

    def test_an_entry_that_requires_the_corpus_declares_it(self):
        env = tt._environment(self.ROOT, self._entry(["song-corpus"]), {})
        self.assertEqual(env.get(hr.CORPUS_DECLARED), "1")

    def test_no_other_entry_declares_it_whatever_the_caller_set(self):
        with mock.patch.dict(os.environ, {hr.CORPUS_DECLARED: "1"}):
            env = tt._environment(self.ROOT, self._entry(["song-xml-corpus"]), {})
        self.assertNotIn(hr.CORPUS_DECLARED, env)

    def test_the_legacy_corpus_does_not_declare_the_song_corpus(self):
        with mock.patch.dict(os.environ, {hr.CORPUS_DECLARED: "1"}):
            env = tt._environment(
                self.ROOT, self._entry(["cargo", "legacy-corpus"]), {}
            )
        self.assertNotIn(hr.CORPUS_DECLARED, env)

    def test_the_upgrade_differential_runs_its_committed_cases_in_ci(self):
        by_name = {h.name: h for h in hr.load_registry(self.ROOT)}
        self.assertFalse(hr.local_only(by_name["upgrade-differential"]))
        self.assertEqual(
            by_name["upgrade-differential-corpus"].requires,
            ["cargo", "legacy-corpus"],
        )

    def test_an_entry_cannot_set_it_through_env(self):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(
            MINIMAL.replace(
                "runtime_s = 60",
                f'runtime_s = 60\nenv = {{ {hr.CORPUS_DECLARED} = "1" }}',
            )
        )
        with self.assertRaisesRegex(ValueError, "song-corpus"):
            hr.load_registry(root)

    def test_the_table_has_corpus_fixture_rows(self):
        self.assertIn("stem_export", hr.corpus_fixture_scenarios(self.ROOT))

    def test_a_comment_naming_the_corpus_fixture_does_not_mark_a_row(self):
        root = Path(tempfile.mkdtemp())
        table = root / hr.SCENARIO_TABLE
        table.parent.mkdir(parents=True)
        table.write_text(
            "kScenarios[] = {\n"
            '    {"quiet", nullptr, ScenarioPacing::Virtual},\n'
            "    // Not kCorpusFixture: the row above boots an empty card.\n"
            '    {"loud", nullptr, ScenarioPacing::Virtual, kCorpusFixture},\n'
            "};\n"
        )
        self.assertEqual(hr.corpus_fixture_scenarios(root), {"loud"})

    def test_every_scenario_entry_on_the_corpus_fixture_declares_it(self):
        rows = hr.corpus_fixture_scenarios(self.ROOT)
        undeclared = [
            h.name
            for h in hr.load_registry(self.ROOT)
            if h.runner == "scenario"
            and h.scenario is not None
            and h.scenario in rows
            and not hr.declares_corpus(h)
        ]
        self.assertEqual(
            undeclared,
            [],
            "these entries boot the corpus fixture (kCorpusFixture in scenario_table.cpp) "
            'without requires = ["song-corpus"]',
        )


if __name__ == "__main__":
    unittest.main()


class RunnerSchedulingTest(unittest.TestCase):
    def _root_with(self, text):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(text)
        return root

    def test_alone_loads_and_defaults_false(self):
        alone = MINIMAL + MINIMAL.replace('"alpha"', '"gamma"', 1).replace(
            'tier = "pr"', 'tier = "pr"\nalone = true'
        )
        entries = hr.load_registry(self._root_with(alone))
        self.assertEqual(
            [(e.name, e.alone) for e in entries], [("alpha", False), ("gamma", True)]
        )

    def test_default_jobs(self):
        cpus = os.cpu_count()
        self.assertEqual(tt.default_jobs(0, "pr", False), cpus)
        self.assertEqual(tt.default_jobs(0, "nightly", False), 1)
        self.assertEqual(tt.default_jobs(3, "pr", False), 3)
        self.assertEqual(tt.default_jobs(0, "nightly", True), cpus)

    def test_schedule_splits_alone_last(self):
        a, b, c = entry("a"), entry("b"), entry("c")
        b.alone = True
        self.assertEqual(tt.schedule([a, b, c]), ([a, c], [b]))

    def test_summary_survives_a_swapped_stdout(self):
        out = io.StringIO()
        discarded = io.StringIO()
        real = sys.stdout
        sys.stdout = discarded
        try:
            tt._summarise(
                [tt.Outcome("a", "PASS"), tt.Outcome("b", "PASS")], False, out
            )
        finally:
            sys.stdout = real
        self.assertIn("== summary", out.getvalue())
        self.assertIn("2 passed, 0 failed, 0 skipped", out.getvalue())
        self.assertEqual(discarded.getvalue(), "")


def _load_check_module(name):
    path = Path(__file__).parents[2] / "tests/integration/checks" / f"{name}.py"
    spec = importlib.util.spec_from_file_location(f"check_{name}", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class CorpusGateTest(unittest.TestCase):
    """A corpus gate runs its runner in a process of its own and hands the runner's output to the gate."""

    def setUp(self):
        self.root = Path(tempfile.mkdtemp())
        runners = self.root / hr.CORPUS_RUNNERS
        runners.mkdir(parents=True)
        (runners / "driver.py").write_text("def default_corpus():\n    return None\n")
        self._runner("print('HELLO')\n    time.sleep(0.2)\n    return 0")
        self.binary = self.root / "golden_vt_render"
        self.binary.write_text("")
        self.addCleanup(
            lambda: sys.path.remove(str(runners)) if str(runners) in sys.path else None
        )
        self.addCleanup(lambda: sys.modules.pop("driver", None))

    def _runner(self, body):
        (self.root / hr.CORPUS_RUNNERS / "fake_run.py").write_text(
            "import os, sys, time\n"
            "def main(argv=None):\n"
            "    " + body + "\n"
            "if __name__ == '__main__':\n"
            "    sys.exit(main())\n"
        )

    def _run(self, env=None):
        h = dataclasses.replace(
            entry("d"),
            runner="corpus",
            corpus_runner="fake",
            builds=["golden-vt-render"],
        )
        log = io.StringIO()
        passed = cg.run_corpus_gate(
            self.root, h, dict(os.environ) if env is None else env, self.binary, log
        )
        return passed, log.getvalue()

    def test_runner_output_reaches_the_gate_without_touching_stdout(self):
        before = sys.stdout
        passed, log = self._run()
        self.assertIs(sys.stdout, before)
        self.assertTrue(passed, log)
        self.assertIn("HELLO\n", log)
        self.assertTrue(log.endswith("d: PASS\n"), log)

        results = [None, None]

        def one(i):
            results[i] = self._run()

        threads = [threading.Thread(target=one, args=(i,)) for i in range(2)]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertIs(sys.stdout, before)
        for _, log in results:
            self.assertIn("HELLO", log)

    def test_the_gate_environment_reaches_the_runner(self):
        self._runner("print('PROBE=' + os.environ.get('PROBE', 'unset'))\n    return 0")
        _, log = self._run({**os.environ, "PROBE": "from-gate"})
        self.assertIn("PROBE=from-gate", log)

    def test_finding_nothing_to_check_fails(self):
        self._runner("return 77")
        passed, log = self._run()
        self.assertFalse(passed)
        self.assertIn("no corpus songs to check", log)

    def test_a_failing_runner_fails(self):
        self._runner("return 1")
        passed, log = self._run()
        self.assertFalse(passed)
        self.assertIn("the runner exited 1", log)

    def test_an_unbuilt_binary_fails(self):
        self.binary.unlink()
        passed, log = self._run()
        self.assertFalse(passed)
        self.assertIn("is not built", log)

    def test_the_runner_dispatches_a_corpus_entry(self):
        h = dataclasses.replace(
            entry("d"),
            runner="corpus",
            corpus_runner="fake",
            builds=["golden-vt-render"],
        )
        log = io.StringIO()
        products = tb.products(Path(__file__).resolve().parents[2])
        with mock.patch.object(
            tt.corpus_gate, "run_corpus_gate", return_value=True
        ) as gate:
            self.assertTrue(tt._run_one(self.root, h, [], False, None, log, products))
        self.assertIn(
            "==> d: run: tests/integration/corpus/fake_run.py", log.getvalue()
        )
        self.assertEqual(gate.call_args.args[1], h)


class CtestStepsTest(unittest.TestCase):
    def test_a_ctest_entry_runs_in_its_source(self):
        h = hr.load_registry(LoadRegistryTest()._root_with(CTEST))[0]
        self.assertEqual(tt._workdir(Path("/r"), h), Path("/r/x"))

    def test_ctest_runs_its_tests_in_parallel(self):
        h = entry("u")
        h.runner = "ctest"
        h.source = "x"
        h.tree = "build/x"
        root = Path(tempfile.mkdtemp())
        (root / "build/x").mkdir(parents=True)
        (root / "build/x/CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={root / 'x'}\n"
        )
        (root / "x").mkdir()
        run = dict(tt._steps(root, h, [], None))["run"]
        self.assertIn("--parallel", run)
        self.assertEqual(run[run.index("--parallel") + 1], str(os.cpu_count()))


class NeedsConfigureTest(unittest.TestCase):
    def test_a_toolchain_tree_is_reused(self):
        # A toolchain file sets the compiler as a plain variable, so the cache holds no CMAKE_CXX_COMPILER entry.
        root = Path(tempfile.mkdtemp())
        source, tree = root / "src", root / "build"
        source.mkdir()
        tree.mkdir()
        (tree / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={source}\n"
        )
        (tree / "build.ninja").write_text("")
        self.assertFalse(tb.needs_configure(tree, source, "clang++"))
        self.assertTrue(tree.is_dir())

    def test_an_unfinished_tree_is_configured(self):
        root = Path(tempfile.mkdtemp())
        source, tree = root / "src", root / "build"
        source.mkdir()
        tree.mkdir()
        (tree / "CMakeCache.txt").write_text(
            f"CMAKE_HOME_DIRECTORY:INTERNAL={source}\n"
        )
        self.assertTrue(tb.needs_configure(tree, source, "clang++"))


class RenderCardCacheTest(unittest.TestCase):
    """Generated render cards come from a cache keyed by the generator, byte for byte what the generator writes."""

    REPO = Path(__file__).parents[2]
    GENERATOR = REPO / "tests/golden/render/songs/make_render_song.py"

    def setUp(self):
        self.cache = _load_check_module("render_card_cache")
        self.cache_root = Path(tempfile.mkdtemp())

    def _generated(self, song):
        out = Path(tempfile.mkdtemp()) / "card"
        subprocess.run(
            [sys.executable, str(self.GENERATOR), str(out), "--song", song], check=True
        )
        return out

    @staticmethod
    def _files(tree):
        return {
            p.relative_to(tree).as_posix(): p.read_bytes()
            for p in sorted(tree.rglob("*"))
            if p.is_file()
        }

    def test_card_matches_the_generator(self):
        for song in ("RS_STREAM", "RS_KIT_FX", "RS_AUDIO_COMP_BLEND"):
            card = self.cache.card(self.REPO, song, self.cache_root)
            self.assertEqual(
                self._files(card), self._files(self._generated(song)), song
            )

    def test_second_call_does_not_synthesise(self):
        _, generator = self.cache._generator(self.REPO)
        calls = []
        real = generator.sample_frames
        with mock.patch.object(
            generator,
            "sample_frames",
            side_effect=lambda *a: calls.append(a) or real(*a),
        ):
            self.cache.card(self.REPO, "RS_KIT_FX", self.cache_root)
            self.cache.card(self.REPO, "RS_KIT_DRY", self.cache_root)
        self.assertEqual(len(calls), 2)

    def test_generator_change_misses(self):
        root = Path(tempfile.mkdtemp())
        generator = root / "tests/golden/render/songs/make_render_song.py"
        generator.parent.mkdir(parents=True)
        generator.write_text(self.GENERATOR.read_text())
        old_key = self.cache.generator_key(root)
        old = self.cache.card(root, "RS_STREAM", self.cache_root)
        generator.write_text(self.GENERATOR.read_text() + "\n# changed\n")
        self.assertNotEqual(self.cache.generator_key(root), old_key)
        new = self.cache.card(root, "RS_STREAM", self.cache_root)
        self.assertNotEqual(new, old)
        self.assertFalse((self.cache_root / old_key).exists())

    def test_concurrent_fill_is_safe(self):
        results = []
        threads = [
            threading.Thread(
                target=lambda: results.append(
                    self.cache.card(self.REPO, "RS_KIT_FX", self.cache_root)
                )
            )
            for _ in range(8)
        ]
        for t in threads:
            t.start()
        for t in threads:
            t.join()
        self.assertEqual(len(set(results)), 1)
        self.assertEqual(
            self._files(results[0]), self._files(self._generated("RS_KIT_FX"))
        )
        self.assertEqual(list(self.cache_root.rglob("*.tmp-*")), [])

    def test_a_sample_written_twice_keeps_the_inode_cards_link(self):
        # Two writers that both found the sample missing: the second must not unlink the first's inode, which a card
        # being filled may be linking at that moment (ENOENT from os.link).
        key, generator = self.cache._generator(self.REPO)
        base = self.cache_root / key
        path = generator.samples_for("RS_KIT_FX")[0]
        first = self.cache._sample(base, generator, path)
        inode = first.stat().st_ino
        with mock.patch.object(Path, "is_file", return_value=False):
            second = self.cache._sample(base, generator, path)
        self.assertEqual(second.stat().st_ino, inode)
        self.assertEqual(list(base.rglob("*.tmp-*")), [])

    def test_a_failed_sample_write_leaves_no_temporary_sample(self):
        key, generator = self.cache._generator(self.REPO)
        base = self.cache_root / key
        path = generator.samples_for("RS_KIT_FX")[0]
        real = Path.write_bytes

        def short_write(target, data):
            real(target, data[:16])  # the file is made, then the disk fills
            raise OSError("disk full")

        with (
            mock.patch.object(Path, "write_bytes", short_write),
            self.assertRaises(OSError),
        ):
            self.cache._sample(base, generator, path)
        self.assertEqual(list(base.rglob("*.tmp-*")), [])

    def test_a_failed_fill_leaves_no_temporary_card(self):
        _, generator = self.cache._generator(self.REPO)
        with (
            mock.patch.object(
                generator, "write_song", side_effect=OSError("disk full")
            ),
            self.assertRaises(OSError),
        ):
            self.cache.card(self.REPO, "RS_KIT_FX", self.cache_root)
        self.assertEqual(list(self.cache_root.rglob("*.tmp-*")), [])

    def test_unknown_song_raises(self):
        with self.assertRaises(ValueError):
            self.cache.card(self.REPO, "RS_NO_SUCH_SONG", self.cache_root)

    def test_the_key_and_the_card_come_from_the_same_generator_bytes(self):
        # The file changes between reads (an edit mid-run): the bytes the key hashes are on disk no longer.
        root = Path(tempfile.mkdtemp())
        generator = root / "tests/golden/render/songs/make_render_song.py"
        generator.parent.mkdir(parents=True)
        read = self.GENERATOR.read_bytes()
        generator.write_bytes(read.replace(b'"RSA"', b'"RSZ"'))
        real = Path.read_bytes
        with mock.patch.object(
            Path,
            "read_bytes",
            lambda p: read if p == generator else real(p),
        ):
            card = self.cache.card(root, "RS_STREAM", self.cache_root)
        self.assertEqual(card.parent.name, hashlib.sha256(read).hexdigest()[:16])
        self.assertEqual(self._files(card), self._files(self._generated("RS_STREAM")))

    def test_a_dead_writers_leftovers_are_removed(self):
        key = self.cache.generator_key(self.REPO)
        base = self.cache_root / key
        base.mkdir(parents=True)
        dead = subprocess.Popen(["true"])
        dead.wait()
        leftover = base / f"RS_KIT_FX.tmp-{dead.pid}-1"
        live = base / f"RS_KIT_DRY.tmp-{os.getpid()}-1"
        leftover.mkdir()
        live.mkdir()
        sample = base / "samples/SAMPLES/RS" / f"A.WAV.tmp-{dead.pid}-1"
        sample.parent.mkdir(parents=True)
        sample.write_bytes(b"partial")
        self.cache.card(self.REPO, "RS_STREAM", self.cache_root)
        self.assertFalse(leftover.exists())
        self.assertFalse(sample.exists())
        self.assertTrue(live.exists())


class ChildStderrTest(unittest.TestCase):
    """A test run straight through the runner's output keeps its stderr; a buffered one folds it into its log."""

    def _run(self, buffered):
        h = entry("s")
        h.run = ["sh", "-c", "echo to-out; echo to-err >&2"]
        root = Path(tempfile.mkdtemp())
        with (
            tempfile.TemporaryFile("w+") as out,
            tempfile.TemporaryFile("w+") as buffer,
            mock.patch.object(tt, "OUT", out),
            mock.patch.object(tt, "_environment", return_value=dict(os.environ)),
        ):
            log = buffer if buffered else out
            self.assertTrue(tt._run_one(root, h, [], False, None, log, {}))
            log.seek(0)
            return log.read()

    def test_a_direct_run_inherits_stderr(self):
        text = self._run(buffered=False)
        self.assertIn("to-out", text.splitlines())
        self.assertNotIn("to-err", text.splitlines())

    def test_a_buffered_run_keeps_stderr_in_its_log(self):
        text = self._run(buffered=True)
        self.assertIn("to-out", text.splitlines())
        self.assertIn("to-err", text.splitlines())


def switch_definitions(source: str, filename: str = "<unknown>") -> list[int]:
    """The lines on which `source` calls an argparse `add_argument` naming --update or --record, however the call is
    wrapped: any positional string of the call counts, so a short alias before it hides nothing."""
    return sorted(
        node.lineno
        for node in ast.walk(ast.parse(source, filename=filename))
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Attribute)
        and node.func.attr == "add_argument"
        and any(
            isinstance(a, ast.Constant) and a.value in ("--update", "--record")
            for a in node.args
        )
    )


class SwitchDefinitionsTest(unittest.TestCase):
    def test_one_line(self):
        self.assertEqual(
            switch_definitions('p.add_argument("--update", action="store_true")\n'), [1]
        )

    def test_wrapped(self):
        source = (
            'x = 1\np.add_argument(\n    "--record",\n    action="store_true",\n)\n'
        )
        self.assertEqual(switch_definitions(source), [2])

    def test_after_a_short_alias(self):
        self.assertEqual(switch_definitions('p.add_argument("-u", "--update")\n'), [1])

    def test_a_syntax_error_names_its_file(self):
        with self.assertRaises(SyntaxError) as caught:
            switch_definitions("def (\n", "tools/broken.py")
        self.assertEqual(caught.exception.filename, "tools/broken.py")

    def test_other_switches_and_mentions_are_not_definitions(self):
        source = (
            'p.add_argument("--update-readme")\n'
            'flag = "--update"\n'
            '# p.add_argument("--update")\n'
            'p.add_argument(dest="update")\n'
        )
        self.assertEqual(switch_definitions(source), [])


class DumpExpandedTest(unittest.TestCase):
    def _root_with(self, text):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(text)
        return root

    def test_every_entry_with_every_field_sorted_by_name(self):
        rows = json.loads(
            hr.dump_expanded(hr.load_registry(self._root_with(CTEST + MINIMAL)))
        )
        self.assertEqual([r["name"] for r in rows], ["alpha", "beta"])
        self.assertEqual(set(rows[0]), {f.name for f in dataclasses.fields(hr.Harness)})

    def test_registry_order_does_not_change_it(self):
        one = hr.dump_expanded(hr.load_registry(self._root_with(MINIMAL + CTEST)))
        two = hr.dump_expanded(hr.load_registry(self._root_with(CTEST + MINIMAL)))
        self.assertEqual(one, two)

    def test_the_runner_prints_the_registrys_dump_and_nothing_else(self):
        # A child process, so a write to file descriptor 1 that bypasses sys.stdout lands in what is read too.
        root = Path(__file__).resolve().parents[2]
        run = subprocess.run(
            [
                sys.executable,
                str(Path(__file__).parent / "task-test.py"),
                "--dump-expanded",
            ],
            cwd=root,
            capture_output=True,
            text=True,
            check=False,
        )
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(
            json.loads(run.stdout),
            json.loads(hr.dump_expanded(hr.load_registry(root))),
        )


class OwnersRefusalTest(unittest.TestCase):
    def test_an_entry_without_a_baseline_is_refused(self):
        self.assertEqual(
            tt.owners_refusal([entry("plain")]),
            "--update: plain list(s) no baseline in the registry",
        )

    def test_a_variant_copy_names_its_base(self):
        copy = dataclasses.replace(entry("app-asan-x"), variant_of="x")
        self.assertEqual(
            tt.owners_refusal([copy]),
            "--update: app-asan-x is a variant of x: it checks, and ./dbt test --update x re-records",
        )

    def test_an_entry_with_a_baseline_passes(self):
        self.assertIsNone(
            tt.owners_refusal([dataclasses.replace(entry("x"), baseline=["a"])])
        )


class ModeRefusalTest(unittest.TestCase):
    def _main(self, argv):
        # A refusal returns before anything runs; were it missing, the stubs keep the test from running a selection.
        err = io.StringIO()
        with (
            mock.patch.object(tt, "cmd_run", return_value=99),
            mock.patch.object(tt, "cmd_update", return_value=99),
            mock.patch.object(tt, "cmd_list", return_value=99),
            contextlib.redirect_stdout(io.StringIO()),
            contextlib.redirect_stderr(err),
        ):
            code = tt.main(argv)
        return code, err.getvalue()

    def test_verbose_needs_list(self):
        self.assertEqual(
            self._main(["--verbose", "registry-selftest"]),
            (
                2,
                "test: --verbose adds each test's explanation to --list: ./dbt test --list --verbose <name>...\n",
            ),
        )

    def test_dump_expanded_takes_nothing_else(self):
        for argv, clash in (
            (["--dump-expanded", "--list"], "--list"),
            (["--dump-expanded", "--tier", "nightly", "x"], "test names, --tier"),
            (["--dump-expanded", "--update", "x"], "test names, --update"),
            (["--dump-expanded", "--", "-R", "x"], "arguments after --"),
        ):
            with self.subTest(argv):
                self.assertEqual(
                    self._main(argv),
                    (
                        2,
                        (
                            "test: --dump-expanded prints every entry, so it takes no other option or test name: "
                            f"{clash}\n"
                        ),
                    ),
                )


class RetiredSwitchTest(unittest.TestCase):
    """`./dbt test --update <name>` is the only way the tree re-records a baseline: no other switch is named."""

    ROOT = Path(__file__).resolve().parents[2]
    RETIRED = (
        "GFX_GOLDEN_UPDATE",
        "RENDER_SONG_UPDATE",
        "RENDER_PRESET_UPDATE",
        "ARRANGE_TAKE_UPDATE",
        "LOAD_POLICY_RECORD",
        "DELUGE_DX7_RECORD",
        "HOP_SCHEDULER_PRINT_RECORDS",
        "PITCH_SHIFT_PRINT_RECORDS",
        "menu_dump_check",
        "_run.py --update",
        "_run.py --record",
    )
    # A runner's own argparse switch for re-recording. The hygiene checkers keep their --update (it rewrites their
    # allowlists, not a test baseline), and `./dbt test --update` itself is the one switch the tree keeps.
    SWITCH_PATHS = (
        "tests/*.py",
        "tools/*.py",
        "scripts/*.py",
        ":!scripts/check_*",
        ":!scripts/tasks/task-test.py",
    )

    def tracked_python(self) -> list[str]:
        found = subprocess.run(
            ["git", "ls-files", "-z", "--", *self.SWITCH_PATHS],
            cwd=self.ROOT,
            capture_output=True,
            text=True,
            check=True,
        )
        return [p for p in found.stdout.split("\0") if p]

    def git_grep(self, *args):
        return subprocess.run(
            ["git", "grep", "-n", *args],
            cwd=self.ROOT,
            capture_output=True,
            text=True,
            check=False,
        )

    def assert_no_match(self, found):
        # git grep exits 1 for no match; anything else (a match, or git failing) fails the test.
        self.assertEqual((found.returncode, found.stdout), (1, ""), found.stderr)

    def test_no_tracked_file_names_a_retired_switch(self):
        words = [arg for word in self.RETIRED for arg in ("-e", word)]
        self.assert_no_match(
            self.git_grep(
                "-F", *words, "--", ":!scripts/tasks/test_harness_registry.py"
            )
        )

    def test_the_guard_reads_the_runners(self):
        self.assertIn("tests/integration/corpus/songs_run.py", self.tracked_python())

    def test_no_runner_defines_its_own_re_record_switch(self):
        hits = [
            f"{path}:{line}"
            for path in self.tracked_python()
            for line in switch_definitions((self.ROOT / path).read_text(), path)
        ]
        self.assertEqual(hits, [])


class GatesRuleTest(unittest.TestCase):
    def test_one_short_sentence_passes(self):
        self.assertIsNone(
            hr.gates_problem("the serializers agree (every corpus song, every output)")
        )

    def test_what_breaks_it(self):
        for text, why in (
            ("a" * (hr.GATES_MAX + 1), "characters"),
            ("one claim. Another claim", "sentence break"),
            ("one claim; another", "sentence break"),
            ('a "quoted." Then more', "sentence break"),
            ("a claim.", "full stop"),
            ("two\nlines", "line"),
        ):
            with self.subTest(text):
                self.assertIn(why, hr.gates_problem(text))

    def test_numbers_and_paths_are_not_breaks(self):
        self.assertIsNone(
            hr.gates_problem("settles in 2.5 s through tests/golden/x.json")
        )


class ExplanationTest(unittest.TestCase):
    ROOT = Path(__file__).resolve().parents[2]

    def _file(self, name, text):
        path = Path(tempfile.mkdtemp()) / name
        path.write_text(text)
        return path

    @staticmethod
    def unittest_runs(h, module):
        """Whether the entry's run is `python3 -m unittest` naming `module`, or discovering the directory's."""
        return h.run[:3] == ["python3", "-m", "unittest"] and (
            module in h.run[3:] or "discover" in h.run[3:]
        )

    def test_every_tools_test_module_runs_under_an_entry(self):
        entries = [h for h in hr.load_registry(self.ROOT) if h.runner == "script"]
        unrun = [
            str(p.relative_to(self.ROOT))
            for p in sorted((self.ROOT / "tools").rglob("test_*.py"))
            if not any(
                Path(h.path) == p.parent.relative_to(self.ROOT)
                and self.unittest_runs(h, p.stem)
                for h in entries
            )
        ]
        self.assertEqual(unrun, [])

    def test_python_docstring_block(self):
        path = self._file(
            "c.py",
            '"""The check.\n\nGate alpha:\n    the long\n    story\n\nGate beta:\n    other\n"""\n\n# Gate gamma:\n#     not docstring\n',
        )
        self.assertEqual(
            hr.explanation_blocks(path), {"alpha": "the long story", "beta": "other"}
        )

    def test_comment_blocks_in_every_prefix(self):
        for name, prefix in (
            ("s.cpp", "///"),
            ("d.cpp", "//"),
            ("r.sh", "#"),
            ("Cargo.toml", "#"),
        ):
            with self.subTest(name):
                path = self._file(
                    name,
                    f"{prefix} Top.\n{prefix}\n{prefix} Gate alpha:\n{prefix}     a b\n{prefix}     c\ncode\n",
                )
                self.assertEqual(hr.explanation_blocks(path), {"alpha": "a b c"})

    def test_a_block_ends_at_a_line_that_is_not_indented(self):
        path = self._file("d.cpp", "// Gate alpha:\n//     a\n// b\n")
        self.assertEqual(hr.explanation_blocks(path), {"alpha": "a"})

    def test_a_second_block_for_one_entry_is_refused(self):
        path = self._file(
            "d.cpp", "// Gate alpha:\n//     a\n//\n// Gate alpha:\n//     b\n"
        )
        with self.assertRaisesRegex(ValueError, "alpha"):
            hr.explanation_blocks(path)

    def test_an_empty_block_is_refused(self):
        with self.assertRaisesRegex(ValueError, "empty"):
            hr.explanation_blocks(self._file("d.cpp", "// Gate alpha:\n//\n"))

    def test_markdown_notes_sections(self):
        path = self._file(
            "n.md",
            "# Notes\n\nWhat this file holds.\n\n## alpha\n\nthe long\nstory\n\nmore\n\n## beta\n\nother\n",
        )
        self.assertEqual(
            hr.explanation_blocks(path),
            {"alpha": "the long story more", "beta": "other"},
        )

    def test_a_markdown_section_ends_only_at_a_heading(self):
        path = self._file(
            "n.md",
            "## alpha\n\nfixed by\n#5263, and\n#not-a-heading either\n\n### Aside\n\nnot alpha's\n",
        )
        self.assertEqual(
            hr.explanation_blocks(path),
            {"alpha": "fixed by #5263, and #not-a-heading either"},
        )

    def test_markdown_refuses_a_repeated_or_empty_section(self):
        with self.assertRaisesRegex(ValueError, "alpha"):
            hr.explanation_blocks(
                self._file("n.md", "## alpha\n\na\n\n## alpha\n\nb\n")
            )
        with self.assertRaisesRegex(ValueError, "empty"):
            hr.explanation_blocks(self._file("n.md", "## alpha\n\n## beta\n\nb\n"))

    def test_a_python_syntax_error_names_its_file(self):
        path = self._file("c.py", '"""Gate alpha:\n    a\n"""\ndef (\n')
        with self.assertRaises(SyntaxError) as caught:
            hr.explanation_blocks(path)
        self.assertEqual(caught.exception.filename, str(path))

    def _script(self, run, path="tools/probe"):
        return dataclasses.replace(entry("probe"), path=path, run=run)

    def test_script_sources_by_command(self):
        for run, source in (
            (["./run.sh", "10"], "tools/probe/run.sh"),
            (["python3", "probe.py", "x"], "tools/probe/probe.py"),
            (["python3", "-m", "unittest"], "tools/probe/probe.py"),
            (
                ["python3", "-m", "unittest", "discover", "-p", "test_*.py"],
                "tools/probe/probe.py",
            ),
            (
                ["python3", "-m", "unittest", "test_a", "test_b"],
                "tools/probe/test_a.py",
            ),
            (["python3", "-m", "probe_main", "--fast"], "tools/probe/probe_main.py"),
            (
                [
                    "python3",
                    "-m",
                    "pytest",
                    "-q",
                    "test_usb_config.py",
                    "test_midi_config.py",
                ],
                "tools/probe/test_usb_config.py",
            ),
            (["python3", "-m", "pytest", "test_a.py::test_x"], "tools/probe/test_a.py"),
            (
                ["python3", "-m", "pytest", "--ignore=test_b.py", "test_a.py"],
                "tools/probe/test_a.py",
            ),
            (
                ["python3", "-m", "pytest", "--config-file=conf.py", "test_a.py"],
                "tools/probe/test_a.py",
            ),
            (
                ["python3", "-m", "pytest", "-k", "usb", "test_a.py"],
                "tools/probe/test_a.py",
            ),
        ):
            with self.subTest(run):
                self.assertEqual(
                    hr.explanation_source(self.ROOT, self._script(run)), Path(source)
                )

    def test_a_script_command_that_names_no_file_is_refused(self):
        for run, message in (
            (["python3"], "probe: run names no script after python3"),
            (["python3", "-u", "probe.py"], "probe: run names no script after python3"),
            (["python3", "-m"], "probe: run names no module after python3 -m"),
            *(
                (
                    ["python3", "-m", "pytest", *args],
                    (
                        "probe: run names no test file after python3 -m pytest; name a .py file (not a directory), "
                        "since the explanation is read from the first"
                    ),
                )
                for args in (["-q"], ["."])
            ),
            (
                ["python3", "-m", "unittest", "-v", "test_a"],
                (
                    "probe: run names an option before the first module after -m unittest; name the modules first, "
                    "since the explanation is read from the first"
                ),
            ),
        ):
            with self.subTest(run), self.assertRaises(ValueError) as caught:
                hr.explanation_source(self.ROOT, self._script(run))
            self.assertEqual(str(caught.exception), message)

    def test_the_loader_refuses_a_script_command_that_names_no_file(self):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(
            MINIMAL.replace('runner = "cargo"', 'runner = "script"').replace(
                'run = ["cargo", "test"]', 'run = ["python3", "-m"]'
            )
        )
        with self.assertRaises(ValueError) as caught:
            hr.load_registry(root)
        self.assertEqual(
            str(caught.exception), "alpha: run names no module after python3 -m"
        )

    def test_sources_by_runner(self):
        cases = {
            "render-kit-fx": "tests/integration/checks/render_song_cases.md",
            "effect-owners": "tests/integration/scenarios/drivers/effect_owners.cpp",
            "stale-ui": "tests/integration/scenarios/stale_ui_scenario.cpp",
            "song-roundtrip": "tests/integration/corpus/songs_run.py",
            "upgrade-differential": "tests/integration/corpus/upgrade_run.py",
            "registry-selftest": "scripts/tasks/test_harness_registry.py",
            "ptm-trace-tool": "tools/deluge_trace/deluge_trace.py",
            "snapshot-primitive-tsan": "tests/component/stress/run.sh",
            "crates": "Cargo.toml",
            "unit": "tests/trees/host/CMakeLists.txt",
        }
        by_name = {h.name: h for h in hr.load_registry(self.ROOT)}
        for name, path in cases.items():
            with self.subTest(name):
                self.assertEqual(
                    hr.explanation_source(self.ROOT, by_name[name]), Path(path)
                )

    def test_every_entry_has_a_source_in_the_tree(self):
        for h in hr.load_registry(self.ROOT):
            with self.subTest(h.name):
                source = hr.explanation_source(self.ROOT, h)
                self.assertIsNotNone(source)
                self.assertTrue((self.ROOT / source).is_file())

    # The entries whose gates line says everything. Any other entry with no explanation lost its block. A variant copy
    # is no exception: it has its own block, under its own name, in its base's file.
    NO_EXPLANATION = frozenset(
        {
            "fs-differential",
            "region-fill-differential",
            "region-read-differential",
            "region-source-differential",
            "recorder-roundtrip",
            "snapshot-primitive-tsan",
            "dsp-diff",
        }
    )

    def test_every_entry_but_the_listed_has_an_explanation(self):
        entries = hr.load_registry(self.ROOT)
        self.assertLessEqual(self.NO_EXPLANATION, {h.name for h in entries})
        for h in entries:
            with self.subTest(h.name):
                if h.name in self.NO_EXPLANATION:
                    self.assertEqual(hr.explanation(self.ROOT, h), "")
                else:
                    self.assertNotEqual(hr.explanation(self.ROOT, h), "")

    def test_every_block_names_an_entry_of_its_file(self):
        names_by_source = {}
        for h in hr.load_registry(self.ROOT):
            names_by_source.setdefault(hr.explanation_source(self.ROOT, h), set()).add(
                h.name
            )
        for source, names in names_by_source.items():
            with self.subTest(str(source)):
                self.assertLessEqual(
                    set(hr.explanation_blocks(self.ROOT / source)), names
                )

    def test_list_verbose_prints_the_explanation(self):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(MINIMAL)
        (root / "harness/rust/alpha").mkdir(parents=True)
        (root / "harness/rust/alpha/Cargo.toml").write_text(
            "# Gate alpha:\n#     beta, explained\n[package]\n"
        )
        h = hr.load_registry(root)[0]
        out = io.StringIO()
        with (
            mock.patch.object(hr, "probe", return_value=True),
            contextlib.redirect_stdout(out),
        ):
            tt.cmd_list(root, [h], False, [h], verbose=True)
        self.assertIn("beta, explained", out.getvalue())


TEMPLATED = """
[template.t]
level = "unit"
runner = "cargo"
kind = "unit"
status = "gate"
tier = "pr"
job = "storage"
runtime_s = 5
requires = ["cargo"]

[[harness]]
name = "one"
template = "t"
path = "a"
gates = "one holds"
areas = ["all"]
run = ["cargo", "test"]
"""


class TemplateTest(unittest.TestCase):
    def _load(self, text):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(text)
        return hr.load_registry(root)

    def test_an_entry_takes_its_templates_fields(self):
        h = self._load(TEMPLATED)[0]
        self.assertEqual(
            (h.level, h.runner, h.job, h.runtime_s, h.requires),
            ("unit", "cargo", "storage", 5, ["cargo"]),
        )

    def test_a_declared_override_wins(self):
        h = self._load(TEMPLATED + 'runtime_s = 9\noverride = ["runtime_s"]\n')[0]
        self.assertEqual(h.runtime_s, 9)

    def test_an_undeclared_override_is_refused(self):
        with self.assertRaisesRegex(
            ValueError, "one: sets runtime_s, which template 't' sets too"
        ):
            self._load(TEMPLATED + "runtime_s = 9\n")

    def test_an_override_the_entry_does_not_make_is_refused(self):
        with self.assertRaisesRegex(ValueError, "'override' lists tier"):
            self._load(TEMPLATED + 'override = ["tier"]\n')

    def test_an_override_to_the_templates_own_value_is_refused(self):
        with self.assertRaises(ValueError) as caught:
            self._load(TEMPLATED + 'runtime_s = 5\noverride = ["runtime_s"]\n')
        self.assertEqual(
            str(caught.exception),
            "one: sets runtime_s to the value template 't' already sets",
        )

    def test_an_unknown_template_is_refused(self):
        with self.assertRaisesRegex(ValueError, "unknown template 'u'"):
            self._load(TEMPLATED.replace('template = "t"', 'template = "u"'))

    def test_an_override_without_a_template_is_refused(self):
        with self.assertRaisesRegex(ValueError, "has no template"):
            self._load(MINIMAL + 'override = ["tier"]\n')

    def test_templates_do_not_nest(self):
        with self.assertRaisesRegex(ValueError, "templates do not nest"):
            self._load(
                TEMPLATED.replace("[template.t]\n", '[template.t]\ntemplate = "s"\n')
            )

    def test_a_template_does_not_name_or_claim(self):
        for field_ in ('name = "x"', 'gates = "x"', 'override = ["x"]'):
            with (
                self.subTest(field_),
                self.assertRaisesRegex(ValueError, "a template cannot set"),
            ):
                self._load(
                    TEMPLATED.replace("[template.t]\n", f"[template.t]\n{field_}\n")
                )

    def test_a_template_field_must_exist(self):
        with self.assertRaisesRegex(
            ValueError, r"template 't': unknown field\(s\) colour"
        ):
            self._load(
                TEMPLATED.replace("[template.t]\n", '[template.t]\ncolour = "x"\n')
            )

    def test_the_expanded_entry_is_validated(self):
        with self.assertRaisesRegex(ValueError, "one: missing required field 'tier'"):
            self._load(TEMPLATED.replace('tier = "pr"\n', ""))

    def test_template_must_be_a_table_of_templates(self):
        with self.assertRaises(ValueError) as caught:
            self._load('template = "t"\n' + MINIMAL)
        self.assertEqual(
            str(caught.exception),
            "'template' is a table of [template.<name>] tables",
        )

    def test_each_template_is_a_table(self):
        with self.assertRaises(ValueError) as caught:
            self._load("template = { t = 1 }\n" + MINIMAL)
        self.assertEqual(str(caught.exception), "template 't' is a table of fields")

    def test_an_entrys_template_is_a_name(self):
        for value in ("3", '["t"]'):
            with self.subTest(value), self.assertRaises(ValueError) as caught:
                self._load(TEMPLATED.replace('template = "t"', f"template = {value}"))
            self.assertEqual(
                str(caught.exception),
                "one: template is the name of a [template.<name>] table",
            )

    def test_override_is_a_list(self):
        for value in ('"runtime_s"', "[1]"):
            with self.subTest(value), self.assertRaises(ValueError) as caught:
                self._load(TEMPLATED + f"runtime_s = 9\noverride = {value}\n")
            self.assertEqual(
                str(caught.exception), "one: override is a list of field names"
            )

    def test_an_unknown_table_is_refused(self):
        with self.assertRaisesRegex(ValueError, r"unknown table\(s\) templates"):
            self._load(MINIMAL + '[templates.x]\nlevel = "unit"\n')


VARIANTED = """
[variant.asan]
name = "app-asan-{base}"
gates = "the {base} gate's checks under ASan"
kind = "sanitizer"
builds = ["golden-vt-render-asan"]
requires = ["clang-asan"]
env_default = { ASAN_OPTIONS = "x" }
tier = "nightly"
job = "unit"
runtime_s = 300

[[harness]]
name = "gamma"
level = "integration"
runner = "scenario"
kind = "property"
status = "gate"
gates = "gamma holds"
areas = ["all"]
scenario = "gamma"
markers = ["GA-ONE"]
result = "GA-RESULT failures=0"
check = "c.py"
builds = ["golden-vt-render"]
requires = ["cargo"]
env = { BUDGET = "1" }
env_default = { KEEP = "1" }
baseline = ["tests/golden/g"]
tier = "pr"
job = "render"
runtime_s = 5
variants = ["asan"]

[[harness]]
name = "delta"
level = "unit"
runner = "script"
kind = "unit"
status = "gate"
gates = "delta holds"
areas = ["all"]
path = "."
run = ["true"]
tier = "pr"
job = "tooling"
runtime_s = 1
"""


class VariantTest(unittest.TestCase):
    def _load(self, text):
        root = Path(tempfile.mkdtemp())
        (root / "tests").mkdir()
        (root / "tests" / "registry.toml").write_text(text)
        (root / "c.py").write_text("")
        return hr.load_registry(root)

    def test_a_copy_follows_every_written_entry(self):
        self.assertEqual(
            [h.name for h in self._load(VARIANTED)],
            ["gamma", "delta", "app-asan-gamma"],
        )

    def test_the_copy_runs_its_bases_checks_on_the_variants_build(self):
        base, _, copy = self._load(VARIANTED)
        self.assertEqual(
            (
                copy.scenario,
                copy.markers,
                copy.result,
                copy.check,
                copy.env,
                copy.level,
                copy.status,
            ),
            (
                base.scenario,
                base.markers,
                base.result,
                base.check,
                base.env,
                base.level,
                base.status,
            ),
        )
        self.assertEqual(
            (
                copy.gates,
                copy.kind,
                copy.builds,
                copy.tier,
                copy.job,
                copy.runtime_s,
                copy.variant_of,
            ),
            (
                "the gamma gate's checks under ASan",
                "sanitizer",
                ["golden-vt-render-asan"],
                "nightly",
                "unit",
                300,
                "gamma",
            ),
        )

    def test_requires_and_env_default_add_to_the_bases(self):
        copy = self._load(VARIANTED)[2]
        self.assertEqual(copy.requires, ["cargo", "clang-asan"])
        self.assertEqual(copy.env_default, {"KEEP": "1", "ASAN_OPTIONS": "x"})

    def test_a_copy_owns_no_baseline(self):
        base, _, copy = self._load(VARIANTED)
        self.assertEqual(
            (base.baseline, copy.baseline, base.variant_of),
            (["tests/golden/g"], [], ""),
        )

    def test_a_base_sets_its_copys_runtime(self):
        text = VARIANTED.replace(
            'variants = ["asan"]',
            'variants = ["asan"]\nvariant_fields = { asan = { runtime_s = 900 } }',
        )
        self.assertEqual(self._load(text)[2].runtime_s, 900)

    def test_a_base_sets_nothing_else_of_its_copy(self):
        text = VARIANTED.replace(
            'variants = ["asan"]',
            'variants = ["asan"]\nvariant_fields = { asan = { tier = "pr" } }',
        )
        with self.assertRaisesRegex(ValueError, "may set only runtime_s"):
            self._load(text)

    def test_variant_fields_for_an_unlisted_variant_are_refused(self):
        text = VARIANTED.replace(
            'variants = ["asan"]', "variant_fields = { asan = { runtime_s = 9 } }"
        )
        with self.assertRaisesRegex(ValueError, "which 'variants' does not list"):
            self._load(text)

    def test_an_unknown_variant_is_refused(self):
        with self.assertRaisesRegex(ValueError, "unknown variant 'tsan'"):
            self._load(VARIANTED.replace('variants = ["asan"]', 'variants = ["tsan"]'))

    def test_a_variant_table_field_must_be_one_it_may_set(self):
        with self.assertRaisesRegex(
            ValueError, r"variant 'asan': unknown field\(s\) markers"
        ):
            self._load(
                VARIANTED.replace(
                    "[variant.asan]\n", '[variant.asan]\nmarkers = ["X"]\n'
                )
            )

    def test_name_and_gates_name_the_base(self):
        with self.assertRaisesRegex(
            ValueError, "variant 'asan': name must contain {base}"
        ):
            self._load(
                VARIANTED.replace('name = "app-asan-{base}"', 'name = "app-asan"')
            )
        with self.assertRaisesRegex(
            ValueError, "variant 'asan': gates must contain {base}"
        ):
            self._load(
                VARIANTED.replace(
                    'gates = "the {base} gate\'s checks under ASan"',
                    'gates = "the checks under ASan"',
                )
            )

    def test_other_braces_in_name_and_gates_are_kept(self):
        text = VARIANTED.replace(
            'gates = "the {base} gate\'s checks under ASan"',
            'gates = "the {base} gate\'s {0} {x} checks under ASan"',
        )
        self.assertEqual(
            self._load(text)[-1].gates, "the gamma gate's {0} {x} checks under ASan"
        )

    def test_a_variant_applies_to_scenario_entries(self):
        text = VARIANTED.replace(
            "runtime_s = 1\n", 'runtime_s = 1\nvariants = ["asan"]\n'
        )
        with self.assertRaises(ValueError) as caught:
            self._load(text)
        self.assertEqual(
            str(caught.exception),
            "delta: variant 'asan' swaps the render build, so it applies to scenario entries; this is a script entry",
        )

    def test_variants_is_a_list_of_names(self):
        for value in ('"asan"', "[1]", "{ asan = 1 }"):
            with self.subTest(value), self.assertRaises(ValueError) as caught:
                self._load(
                    VARIANTED.replace('variants = ["asan"]', f"variants = {value}")
                )
            self.assertEqual(
                str(caught.exception), "gamma: variants is a list of variant names"
            )

    def test_variant_fields_is_a_table_of_tables(self):
        for value, message in (
            (
                "3",
                "gamma: variant_fields is a table of [variant_fields.<variant>] tables",
            ),
            ("{ asan = 3 }", "gamma: variant_fields.asan is a table"),
        ):
            with self.subTest(value), self.assertRaises(ValueError) as caught:
                self._load(
                    VARIANTED.replace(
                        'variants = ["asan"]',
                        f'variants = ["asan"]\nvariant_fields = {value}',
                    )
                )
            self.assertEqual(str(caught.exception), message)

    def test_variant_is_a_table_of_tables(self):
        for text, message in (
            ("variant = 3\n", "'variant' is a table of [variant.<name>] tables"),
            ("variant = { asan = 1 }\n", "variant 'asan' is a table of fields"),
        ):
            with self.subTest(text), self.assertRaises(ValueError) as caught:
                self._load(text + VARIANTED.split("[variant.asan]")[0])
            self.assertEqual(str(caught.exception), message)

    def test_a_variants_name_and_gates_are_strings(self):
        for old, new, key in (
            ('name = "app-asan-{base}"', "name = 3", "name"),
            ('gates = "the {base} gate\'s checks under ASan"', "gates = 3", "gates"),
        ):
            with self.subTest(key), self.assertRaises(ValueError) as caught:
                self._load(VARIANTED.replace(old, new))
            self.assertEqual(
                str(caught.exception),
                f"variant 'asan': {key} is a string containing {{base}}",
            )

    def test_a_base_without_a_name_is_refused(self):
        with self.assertRaises(ValueError) as caught:
            self._load(VARIANTED.replace('name = "gamma"\n', ""))
        self.assertEqual(
            str(caught.exception), "entry #0: missing required field 'name'"
        )

    def test_a_copy_is_validated_like_any_entry(self):
        text = VARIANTED.replace(
            'job = "unit"\nruntime_s = 300', 'job = "render"\nruntime_s = 300'
        )
        with self.assertRaisesRegex(
            ValueError, "app-asan-gamma: job 'render' runs prebuilt"
        ):
            self._load(text)

    def test_a_copys_name_cannot_collide(self):
        clash = (
            '\n[[harness]]\nname = "app-asan-gamma"\nlevel = "unit"\nrunner = "script"\nkind = "unit"\n'
            'status = "gate"\ngates = "clash"\nareas = ["all"]\npath = "."\nrun = ["true"]\ntier = "pr"\njob = "tooling"\nruntime_s = 1\n'
        )
        with self.assertRaisesRegex(ValueError, "duplicate test name 'app-asan-gamma'"):
            self._load(VARIANTED + clash)

    def test_variant_of_is_not_written(self):
        with self.assertRaisesRegex(ValueError, r"unknown field\(s\) variant_of"):
            self._load(VARIANTED.replace('variants = ["asan"]', 'variant_of = "x"'))

    def test_the_gates_rule_points_at_writing_an_entry(self):
        with self.assertRaisesRegex(ValueError, r"tests/README\.md, Writing an entry"):
            self._load(
                VARIANTED.replace('gates = "delta holds"', 'gates = "delta holds."')
            )

    def test_an_entry_over_a_template_has_its_copy(self):
        text = VARIANTED.replace(
            '[[harness]]\nname = "gamma"\nlevel = "integration"\n',
            '[template.s]\nlevel = "integration"\n\n[[harness]]\nname = "gamma"\ntemplate = "s"\n',
        )
        self.assertEqual(
            [(h.name, h.level) for h in self._load(text)],
            [
                ("gamma", "integration"),
                ("delta", "unit"),
                ("app-asan-gamma", "integration"),
            ],
        )
