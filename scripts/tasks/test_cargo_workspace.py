"""The root Cargo workspace's members are each covered where a list of them is kept: the rustfmt pre-commit hook's
`files` pattern, and the registry's cargo gates."""

import glob
import itertools
import re
import unittest
from pathlib import Path

import harness_registry as hr
import tomllib
import yaml

ROOT = Path(__file__).resolve().parents[2]

# Members no cargo gate tests by `-p`: golden_vt_render runs as every scenario gate's binary, and the host link helper
# is a build dependency of the BSP and golden_vt_render, built by both.
UNTESTED_BY_CARGO = {"golden_vt_render", "deluge_platform_host_link"}


def members(root: Path = ROOT) -> dict[str, str]:
    """Every root-workspace member's directory (repo-relative) mapped to its package name."""
    workspace = tomllib.loads((root / "Cargo.toml").read_text())["workspace"]
    found = {}
    for pattern in workspace["members"]:
        for directory in sorted(glob.glob(pattern, root_dir=root)):
            manifest = root / directory / "Cargo.toml"
            if manifest.is_file():
                found[directory] = tomllib.loads(manifest.read_text())["package"][
                    "name"
                ]
    return found


def rustfmt_pattern(config: dict) -> re.Pattern[str]:
    """The `files` pattern of the pre-commit hook that runs `cargo fmt`."""
    for repo in config["repos"]:
        for hook in repo["hooks"]:
            if hook.get("entry", "").startswith("cargo fmt"):
                return re.compile(hook["files"])
    raise AssertionError("no cargo fmt hook in .pre-commit-config.yaml")


def uncovered_by(pattern: re.Pattern[str], directories) -> list[str]:
    """The member directories whose Rust sources `pattern` would not pass to the hook."""
    return [d for d in directories if not pattern.search(f"{d}/src/lib.rs")]


def cargo_gate_packages(root: Path = ROOT) -> set[str]:
    """Every package a registry cargo gate names with `-p`, in its build or run command."""
    named = set()
    for h in hr.load_registry(root):
        if h.runner != "cargo":
            continue
        for command in (h.build, h.run):
            named.update(b for a, b in itertools.pairwise(command) if a == "-p")
    return named


class RustfmtHookTest(unittest.TestCase):
    def setUp(self):
        self.config = yaml.safe_load((ROOT / ".pre-commit-config.yaml").read_text())

    def test_the_hook_covers_every_member(self):
        self.assertEqual(uncovered_by(rustfmt_pattern(self.config), members()), [])

    def test_a_narrowed_pattern_is_caught(self):
        narrowed = re.compile(r"^(src/storage/).*\.rs$")
        self.assertIn("src/base/alloc", uncovered_by(narrowed, members()))


class CargoGateTest(unittest.TestCase):
    def test_every_member_is_tested_by_a_cargo_gate(self):
        missing = set(members().values()) - cargo_gate_packages() - UNTESTED_BY_CARGO
        self.assertEqual(missing, set())

    def test_the_exemptions_are_members(self):
        self.assertLessEqual(UNTESTED_BY_CARGO, set(members().values()))


if __name__ == "__main__":
    unittest.main()
