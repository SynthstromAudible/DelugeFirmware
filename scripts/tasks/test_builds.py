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
HOST_APP_ARM_TREE = Path("build") / "host-app-arm"
# The Rust target the ARM golden_vt_render is built for: 32-bit ARM Linux, hard float, run under qemu.
ARM_TARGET = "armv7-unknown-linux-gnueabihf"


def needs_configure(tree: Path, source: Path, cxx: str | None = None) -> bool:
    """Whether the CMake tree at `tree` must be configured from `source` (with the C++ compiler `cxx`, if given).

    A tree configured from another source directory (the source moved), or with another compiler, cannot be reused --
    CMake refuses the first and keeps the old compiler for the second -- so it is removed first; it holds only build
    products.
    """
    cache = tree / "CMakeCache.txt"
    if not cache.exists():
        return True
    text = cache.read_text(errors="replace")
    home = re.search(r"^CMAKE_HOME_DIRECTORY:INTERNAL=(.*)$", text, re.MULTILINE)
    compiler = re.search(r"^CMAKE_CXX_COMPILER:[A-Z]+=(.*)$", text, re.MULTILINE)
    same_source = home is not None and Path(home.group(1)).resolve() == source.resolve()
    same_compiler = cxx is None or (
        compiler is not None and Path(compiler.group(1)).name == cxx
    )
    if same_source and same_compiler:
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
        if needs_configure(tree, root / "sim", "clang++"):
            # -DDELUGE_SIM_X64=ON is required: the sim defaults to a -m32 configuration, which cannot link against
            # the harnesses' 64-bit host Rust. clang, because the firmware ships built with it: the host renders the
            # golden masters record must take the same code generation.
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
                        "-DCMAKE_C_COMPILER=clang",
                        "-DCMAKE_CXX_COMPILER=clang++",
                    ],
                    root,
                )
            )
        steps.append(Step("build", ["ninja", "-C", str(tree), *HOST_APP_TARGETS], root))
        return steps


class HostAppArm(Product):
    """The host app cross-built for arm-linux (Cortex-A9, NEON) with clang, for the ARM golden_vt_render."""

    def steps(self, root: Path) -> list[Step]:
        tree = root / HOST_APP_ARM_TREE
        steps = []
        if needs_configure(tree, root / "sim", "clang++"):
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
                        f"-DCMAKE_TOOLCHAIN_FILE={root / 'sim/arm-linux-toolchain.cmake'}",
                    ],
                    root,
                )
            )
        steps.append(Step("build", ["ninja", "-C", str(tree), *HOST_APP_TARGETS], root))
        return steps


class GoldenVtRenderArm(Product):
    """golden_vt_render for armv7 Linux, linked against the arm-linux host app, run under qemu."""

    def steps(self, root: Path) -> list[Step]:
        return [
            Step(
                "build",
                ["cargo", "build", "--release", "--target", ARM_TARGET],
                root / "src/bsp/rust/harness/golden_vt_render",
                {
                    "DELUGE_HOSTAPP_BUILD_DIR": str(root / HOST_APP_ARM_TREE),
                    # The GCC cross driver only links: the objects are clang's.
                    "CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER": "arm-linux-gnueabihf-gcc",
                },
            )
        ]


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
        "host-app": HostApp("host-app", [], ["cmake", "clang"], host_app_env),
        "host-app-arm": HostAppArm(
            "host-app-arm", [], ["cmake", "clang", "arm-linux-gnueabihf-gcc"], {}
        ),
        "golden-vt-render-arm": GoldenVtRenderArm(
            "golden-vt-render-arm",
            ["host-app-arm"],
            ["cargo", "deluge-sdk-sibling", "qemu-arm"],
            {
                "GOLDEN_VT_RENDER_TARGET": ARM_TARGET,
                "GOLDEN_VT_RENDER_WRAPPER": "qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9",
            },
        ),
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
