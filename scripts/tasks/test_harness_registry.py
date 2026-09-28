#! /usr/bin/env python3
"""Unit tests for harness_registry.py.

Run directly with:
    python3 scripts/tasks/test_harness_registry.py
"""

import os
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).parent))

import harness_registry as hr
import test_builds as tb

MINIMAL = """
[[harness]]
name = "alpha"
path = "harness/rust/alpha"
level = "component"
runner = "cargo"
kind = "differential"
status = "gate"
gates = "alpha must equal beta"
build = ["cargo", "build"]
run = ["cargo", "test"]
requires = ["cargo"]
tier = "pr"
runtime = "~1m"
"""


CTEST = """
[[harness]]
name = "beta"
path = "x"
level = "unit"
runner = "ctest"
kind = "unit"
status = "gate"
gates = "beta holds"
source = "x"
tree = "build/x"
ctest_args = ["-C", "Debug"]
tier = "pr"
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
        runtime="?",
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
        (root / "harness").mkdir()
        (root / "harness" / "registry.toml").write_text(text)
        return root

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


class BuildsTest(unittest.TestCase):
    def _root_with(self, text):
        root = Path(tempfile.mkdtemp())
        (root / "harness").mkdir()
        (root / "harness" / "registry.toml").write_text(text)
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


class ProbeTest(unittest.TestCase):
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


class MarkdownTableTest(unittest.TestCase):
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
        self.assertEqual(header_row.count("|"), 9)
        # The escaped pipe adds one to the data row without adding a column.
        self.assertEqual(data_row.count("|"), 10)

    def _root(self):
        root = Path(tempfile.mkdtemp())
        (root / "harness").mkdir()
        (root / "harness" / "registry.toml").write_text(MINIMAL)
        return root

    def _root_with_pipe(self):
        root = Path(tempfile.mkdtemp())
        (root / "harness").mkdir()
        toml_with_pipe = MINIMAL.replace(
            'gates = "alpha must equal beta"', 'gates = "alpha|beta must equal charlie"'
        )
        (root / "harness" / "registry.toml").write_text(toml_with_pipe)
        return root


if __name__ == "__main__":
    unittest.main()
