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
# packages built with different features coexist safely; the cost is that concurrent builds serialize on its lock.
SHARED_TARGET_SUBDIR = Path("build") / "cargo-harness"
# The root workspace's profile golden_vt_render builds with (Cargo.toml's [profile.harness]); cargo puts its outputs
# in a directory of that name under the target dir (and under the target triple, for a cross build).
HARNESS_PROFILE = "harness"
# cargo's arguments for golden_vt_render, from the repo root.
GOLDEN_CARGO_BUILD = [
    "cargo",
    "build",
    "-p",
    "golden_vt_render",
    "--profile",
    HARNESS_PROFILE,
]

# The C++ app's object closure for the Embassy host harnesses: the app, its libraries and the host BSP's C side,
# compiled for x86-64. Nothing links here; golden_vt_render's build script archives these objects.
HOST_APP_TREE = Path("build") / "host-app"
HOST_APP_ARM_TREE = Path("build") / "host-app-arm"
# The same closure compiled with AddressSanitizer and UBSan (DELUGE_SIM_SANITIZE), and the harness it links into.
HOST_APP_ASAN_TREE = Path("build") / "host-app-asan"
ASAN_TARGET_SUBDIR = Path("build") / "cargo-harness-asan"
# The Rust target the ARM golden_vt_render is built for: 32-bit ARM Linux, hard float, run under qemu.
ARM_TARGET = "armv7-unknown-linux-gnueabihf"
# The CPU and FPU the arm-linux host app is compiled for (cmake/arm-linux-clang.cmake), which the build scripts' C++ in
# the ARM golden_vt_render gets too. cc names the target to clang by rustc's LLVM triple, `arm-unknown-linux-gnueabihf`,
# which carries no architecture: clang would take ARMv4T without NEON, and argon's NEON code would not compile.
ARM_CPU_FLAGS = "-mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard"


def needs_configure(tree: Path, source: Path, cxx: str | None = None) -> bool:
    """Whether the CMake tree at `tree` must be configured from `source` (with the C++ compiler `cxx`, if given, unless
    the tree's toolchain chose it).

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
    # A toolchain file sets the compiler as a plain variable, which never reaches the cache: a generated tree with no
    # compiler entry was configured by its toolchain, and reconfiguring cannot change that compiler.
    toolchain_tree = compiler is None and (tree / "build.ninja").exists()
    same_compiler = (
        cxx is None
        or toolchain_tree
        or (compiler is not None and Path(compiler.group(1)).name == cxx)
    )
    if same_source and same_compiler:
        return False
    shutil.rmtree(tree)
    return True


def cache_has(tree: Path, name: str, value: str) -> bool:
    """Whether the CMake tree at `tree` has cache entry `name` set to `value`."""
    cache = tree / "CMakeCache.txt"
    if not cache.exists():
        return False
    pattern = rf"^{re.escape(name)}:[A-Z]+={re.escape(value)}$"
    return (
        re.search(pattern, cache.read_text(errors="replace"), re.MULTILINE) is not None
    )


# The trees the runner builds embed a constant version: asking git would rebuild them whenever the work tree's state
# changes, though nothing they test depends on it.
TEST_TREE_DEFINES = ["-DDELUGE_VERSION_FROM_GIT=OFF"]


def _needs_test_tree_configure(tree: Path, source: Path, cxx: str) -> bool:
    """Whether a test tree must be configured: `needs_configure`, or a tree configured without `TEST_TREE_DEFINES`."""
    return needs_configure(tree, source, cxx) or not cache_has(
        tree, "DELUGE_VERSION_FROM_GIT", "OFF"
    )


HOST_APP_TARGETS = [
    "deluge_app",
    "deluge_scenarios",
    "eyalroz_printf",
    "deluge_dsp_fixed",
    "deluge_dsp_float",
    "deluge_synth",
    "deluge_base",
    "deluge_serialization",
    "deluge_music",
    "deluge_project",
    "deluge_ui_controls",
    "deluge_instruments_classic",
    "deluge_effects",
    "deluge_host_chains",
    "deluge_midi",
    "deluge_ui_gfx_oled",
    "deluge_ui_gfx_grid",
    "deluge_ui_widgets_core",
    "deluge_ui_widgets_oled",
    "deluge_ui_widgets_grid",
    "deluge_ui_views_oled",
    "deluge_ui_views_grid",
    "deluge_ui_shell",
    "deluge_base_notify",
    "deluge_ui_scenes",
    "deluge_l10n",
    "deluge_ui_level3",
    "deluge_ui_scenes_spec",
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

    def outputs(self, root: Path) -> list[Path]:
        """The files a test run needs from this product: what `dbt test --prebuilt` expects to find already built."""
        return []


class HostApp(Product):
    def steps(self, root: Path) -> list[Step]:
        tree = root / HOST_APP_TREE
        steps = []
        if _needs_test_tree_configure(tree, root / "src/platform/host", "clang++"):
            # clang, because the firmware ships built with it: the host renders the golden masters record must take
            # the same code generation.
            steps.append(
                Step(
                    "configure",
                    [
                        "cmake",
                        "-S",
                        "src/platform/host",
                        "-B",
                        str(tree),
                        "-G",
                        "Ninja",
                        "-DCMAKE_C_COMPILER=clang",
                        "-DCMAKE_CXX_COMPILER=clang++",
                        *TEST_TREE_DEFINES,
                    ],
                    root,
                )
            )
        steps.append(Step("build", ["ninja", "-C", str(tree), *HOST_APP_TARGETS], root))
        return steps


class HostAppAsan(Product):
    """The host app compiled with AddressSanitizer and UBSan, for the sanitizer gates."""

    def steps(self, root: Path) -> list[Step]:
        tree = root / HOST_APP_ASAN_TREE
        steps = []
        if _needs_test_tree_configure(tree, root / "src/platform/host", "clang++"):
            steps.append(
                Step(
                    "configure",
                    [
                        "cmake",
                        "-S",
                        "src/platform/host",
                        "-B",
                        str(tree),
                        "-G",
                        "Ninja",
                        "-DCMAKE_C_COMPILER=clang",
                        "-DCMAKE_CXX_COMPILER=clang++",
                        "-DDELUGE_SIM_SANITIZE=ON",
                        *TEST_TREE_DEFINES,
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
        if _needs_test_tree_configure(tree, root / "src/platform/host", "clang++"):
            steps.append(
                Step(
                    "configure",
                    [
                        "cmake",
                        "-S",
                        "src/platform/host",
                        "-B",
                        str(tree),
                        "-G",
                        "Ninja",
                        f"-DCMAKE_TOOLCHAIN_FILE={root / 'src/platform/host/arm-linux-toolchain.cmake'}",
                        *TEST_TREE_DEFINES,
                    ],
                    root,
                )
            )
        steps.append(Step("build", ["ninja", "-C", str(tree), *HOST_APP_TARGETS], root))
        return steps


class GoldenVtRenderArm(Product):
    """golden_vt_render for armv7 Linux, linked against the arm-linux host app, run under qemu."""

    def outputs(self, root: Path) -> list[Path]:
        return [
            root
            / SHARED_TARGET_SUBDIR
            / ARM_TARGET
            / HARNESS_PROFILE
            / "golden_vt_render"
        ]

    def steps(self, root: Path) -> list[Step]:
        return [
            Step(
                "build",
                [*GOLDEN_CARGO_BUILD, "--target", ARM_TARGET],
                root,
                {
                    "DELUGE_HOSTAPP_BUILD_DIR": str(root / HOST_APP_ARM_TREE),
                    # The GCC cross driver only links: the objects are clang's.
                    "CARGO_TARGET_ARMV7_UNKNOWN_LINUX_GNUEABIHF_LINKER": "arm-linux-gnueabihf-gcc",
                    "CFLAGS_armv7_unknown_linux_gnueabihf": ARM_CPU_FLAGS,
                    "CXXFLAGS_armv7_unknown_linux_gnueabihf": ARM_CPU_FLAGS,
                },
            )
        ]


class GoldenVtRender(Product):
    def outputs(self, root: Path) -> list[Path]:
        return [root / SHARED_TARGET_SUBDIR / HARNESS_PROFILE / "golden_vt_render"]

    def steps(self, root: Path) -> list[Step]:
        return [
            Step(
                "build",
                GOLDEN_CARGO_BUILD,
                root,
                {"DELUGE_HOSTAPP_BUILD_DIR": str(root / HOST_APP_TREE)},
            )
        ]


class GoldenVtRenderAsan(Product):
    """golden_vt_render linked against the AddressSanitizer host app, with the allocator's heap poisoning on."""

    def outputs(self, root: Path) -> list[Path]:
        return [root / ASAN_TARGET_SUBDIR / HARNESS_PROFILE / "golden_vt_render"]

    def steps(self, root: Path) -> list[Step]:
        return [
            Step(
                "build",
                [*GOLDEN_CARGO_BUILD, "--features", "asan"],
                root,
                {
                    "DELUGE_HOSTAPP_BUILD_DIR": str(root / HOST_APP_ASAN_TREE),
                    "CARGO_TARGET_DIR": str(root / ASAN_TARGET_SUBDIR),
                    # clang links, so -fsanitize=address takes clang's runtime, the one the objects were built for.
                    "CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER": "clang",
                },
            )
        ]


def prebuilt_outputs(
    names: list[str], known: dict[str, Product], root: Path
) -> list[Path]:
    """The outputs of the products `names` (not their dependencies), for running them prebuilt."""
    outputs: list[Path] = []
    for name in dict.fromkeys(names):
        found = known[name].outputs(root)
        if not found:
            raise ValueError(f"build product '{name}' has no prebuilt outputs")
        outputs += found
    return outputs


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
            ["cargo", "qemu-arm"],
            {
                "GOLDEN_VT_RENDER_TARGET": ARM_TARGET,
                "GOLDEN_VT_RENDER_WRAPPER": "qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9",
            },
        ),
        "golden-vt-render": GoldenVtRender(
            "golden-vt-render",
            ["host-app"],
            ["cargo"],
            host_app_env,
        ),
        "host-app-asan": HostAppAsan(
            "host-app-asan",
            [],
            ["cmake", "clang-asan"],
            {"DELUGE_HOSTAPP_BUILD_DIR": str(root / HOST_APP_ASAN_TREE)},
        ),
        "golden-vt-render-asan": GoldenVtRenderAsan(
            "golden-vt-render-asan",
            ["host-app-asan"],
            ["cargo", "clang-asan"],
            {
                "DELUGE_HOSTAPP_BUILD_DIR": str(root / HOST_APP_ASAN_TREE),
                "CARGO_TARGET_DIR": str(root / ASAN_TARGET_SUBDIR),
                "BUILD_DIR": str(root / HOST_APP_ASAN_TREE),
            },
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
    "GOLDEN_CARGO_BUILD",
    "HARNESS_PROFILE",
    "HOST_APP_TREE",
    "SHARED_TARGET_SUBDIR",
    "Product",
    "Step",
    "order",
    "products",
]
