"""The build products tests declare in tests/registry.toml's `builds`, and how to make them.

A test names what it needs; the runner builds the union of a selection's products once, in dependency order, before
running any of them. No test builds a product itself, so none can run against a stale one: `host-app` always runs
its ninja step, which is a no-op when nothing changed, before anything links its objects.

A ctest entry's own tree is not a product here: its runner configures and builds it.
"""

import re
import shutil
from dataclasses import dataclass, field
from pathlib import Path

# The cargo target dir every cargo-built test and product shares. Cargo namespaces artifacts by fingerprint, so
# packages with divergent [patch] sections coexist safely; the cost is that concurrent builds serialize on its lock.
SHARED_TARGET_SUBDIR = Path("build") / "cargo-harness"

# The C++ app's object closure for the Embassy host harnesses: the app, its libraries and the host BSP's C side,
# compiled for x86-64. Nothing links here; golden_vt_render's build.rs archives these objects.
HOST_APP_TREE = Path("build") / "host-app"


def needs_configure(tree: Path, source: Path) -> bool:
    """Whether the CMake tree at `tree` must be configured from `source`.

    A tree configured from another source directory (the source moved) cannot be reused -- CMake refuses it -- so it
    is removed first; it holds only build products.
    """
    cache = tree / "CMakeCache.txt"
    if not cache.exists():
        return True
    home = re.search(
        r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$",
        cache.read_text(errors="replace"),
        re.MULTILINE,
    )
    if home is not None and Path(home.group(1)).resolve() == source.resolve():
        return False
    shutil.rmtree(tree)
    return True


HOST_APP_TARGETS = [
    "deluge_app",
    "deluge_scenarios",
    "NE10",
    "eyalroz_printf",
    "deluge_dsp",
    "deluge_dsp_fixed",
    "deluge_dsp_float",
    "deluge_scheduler",
    "deluge_foundation",
    "deluge_midi",
]


@dataclass
class Step:
    what: str
    argv: list[str]
    cwd: Path
    env: dict[str, str] = field(default_factory=dict)


@dataclass
class Product:
    name: str
    deps: list[str]
    requires: list[str]
    # The environment every test that uses this product runs with.
    test_env: dict[str, str]

    def steps(self, root: Path) -> list[Step]:
        raise NotImplementedError


class HostApp(Product):
    def steps(self, root: Path) -> list[Step]:
        tree = root / HOST_APP_TREE
        steps = []
        if needs_configure(tree, root / "sim"):
            # -DDELUGE_SIM_X64=ON is required: the sim defaults to a -m32 configuration, which cannot link against
            # the harnesses' 64-bit host Rust.
            steps.append(
                Step(
                    "configure",
                    [
                        "cmake",
                        "-S",
                        "sim",
                        "-B",
                        str(tree),
                        "-G",
                        "Ninja",
                        "-DDELUGE_SIM_X64=ON",
                    ],
                    root,
                )
            )
        steps.append(Step("build", ["ninja", "-C", str(tree), *HOST_APP_TARGETS], root))
        return steps


class GoldenVtRender(Product):
    def steps(self, root: Path) -> list[Step]:
        return [
            Step(
                "build",
                ["cargo", "build", "--release"],
                root / "src/bsp/rust/harness/golden_vt_render",
                {"DELUGE_HOSTAPP_BUILD_DIR": str(root / HOST_APP_TREE)},
            )
        ]


def products(root: Path) -> dict[str, Product]:
    host_app_env = {
        "DELUGE_HOSTAPP_BUILD_DIR": str(root / HOST_APP_TREE),
        # The gate scripts' name for the same tree.
        "BUILD_DIR": str(root / HOST_APP_TREE),
    }
    return {
        "host-app": HostApp("host-app", [], ["cmake"], host_app_env),
        "golden-vt-render": GoldenVtRender(
            "golden-vt-render",
            ["host-app"],
            ["cargo", "deluge-sdk-sibling"],
            host_app_env,
        ),
    }


def order(wanted: list[str], known: dict[str, Product]) -> list[str]:
    """`wanted` and everything they depend on, each once, dependencies first."""
    ordered: list[str] = []

    def visit(name: str, path: tuple[str, ...]) -> None:
        if name in ordered:
            return
        if name in path:
            raise ValueError(
                f"build products depend on each other in a cycle: {' -> '.join((*path, name))}"
            )
        for dep in known[name].deps:
            visit(dep, (*path, name))
        ordered.append(name)

    for name in wanted:
        visit(name, ())
    return ordered


__all__ = [
    "HOST_APP_TREE",
    "SHARED_TARGET_SUBDIR",
    "Product",
    "Step",
    "order",
    "products",
]
