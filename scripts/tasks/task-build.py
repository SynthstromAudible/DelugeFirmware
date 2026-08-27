#! /usr/bin/env python3
"""Build the firmware: C++ app under clang/ThinLTO, linked by the Rust BSP via lld.

Two steps, same shape as `dbt rust`, different toolchain:

  1. cmake builds the C++ app objects with clang. Under Release these are LLVM
     bitcode, not ELF.
  2. cargo archives them and links the Rust firmware with lld, running LTO
     across the Rust/C++ boundary.

Why this is a separate tree from `dbt rust`'s `build-gcc/`: that one is GCC,
and the two are NOT interchangeable. arm-none-eabi GCC mangles int32_t as
`long` where clang mangles it as `int`, so a GCC-built object's references do
not resolve against a clang-built one. Mixing is all-or-nothing across any
C++ interface using the fixed-width integer types, hence `build/` (this,
shipping) alongside `build-gcc/` (fallback).

`dbt rust` remains the GCC path, and builds the ARM device image by default.
Its C++ objects cannot use LTO — the Rust link cannot read GCC's slim-LTO
objects — so it builds Debug objects at -O2 (see DELUGE_DEBUG_OPT_LEVEL in
the root CMakeLists). This task has no such restriction, which is most of
the point: measured on Release, the clang image is ~315 KB smaller and
leaves 537 KB of SRAM free against 222 KB.

NOTE: CMake produces only library targets — there is no standalone C++-only
`deluge` executable target to build against.
"""

import argparse
import importlib
import os
import shutil
import subprocess
import sys
from collections.abc import Sequence
from pathlib import Path

import util

# The C++ targets the Rust build.rs archives + links (mirrors its panic message).
CPP_TARGETS = [
    "deluge_app",
    "NE10",
    "eyalroz_printf",
    "deluge_dsp",
    "deluge_scheduler",
    "deluge_foundation",
    "deluge_midi",
    "deluge_gfx_oled",
    "deluge_gfx_grid",
    "deluge_notify",
]

RUST_BSP_DIR = Path("src/bsp/rust")
BUILD_DIR = "build"
# Feature set matching clang's, so LLVM will inline across the language boundary.
RUST_TARGET = "./armv7a-deluge-eabihf.json"
MULTILIB = "thumb/v7-a+simd/hard"

BUILD_CONFIGS = {
    "release": "Release",
    "relwithdebinfo": "RelWithDebInfo",
    "debug": "Debug",
}

# Cargo profile per CMake config. RelWithDebInfo ships the optimised Rust half:
# it is the device dev build (-O2 + ThinLTO, and ENABLE_TEXT_OUTPUT is defined
# for it, so D_PRINTLN survives — see src/deluge/CMakeLists.txt).
CARGO_RELEASE_CONFIGS = {"Release", "RelWithDebInfo"}


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="build",
        description="Build the firmware (clang/ThinLTO C++ + Rust BSP, linked by lld)",
    )
    parser.group = "Building"
    parser.add_argument(
        "-v", "--verbose", help="Verbose cmake + cargo output", action="store_true"
    )
    parser.add_argument(
        "-c",
        "--clean-first",
        help="Clean the C++ app objects before rebuilding",
        action="store_true",
    )
    parser.add_argument(
        "config",
        nargs="?",
        default="release",
        choices=list(BUILD_CONFIGS.keys()) + list(BUILD_CONFIGS.values()),
        help=(
            "Build configuration (default: release). release = shipping image, "
            "no D_PRINTLN output. relwithdebinfo = device dev build, same -O2 "
            "plus debug logging. debug = -Og plus debug logging; whether that is "
            "fast enough for glitch-free audio on hardware is UNMEASURED (see "
            "DELUGE_DEBUG_OPT_LEVEL in the root CMakeLists)."
        ),
    )
    # Anything unrecognised is forwarded to `cargo build` (e.g. --features).
    return parser


def device_cxx_env(root: Path, config: str) -> dict[str, str]:
    """Env for the pieces of the build that shell out to a C++ compiler themselves.

    sample_convert's build.rs compiles a device shim directly rather than through
    cmake, so it needs the same compiler and flags — a GCC-built shim would not
    link against a clang-built app (see the int32_t mangling note above).
    """
    gcc = root / "toolchain/current/arm-none-eabi-gcc"
    sysroot = gcc / "arm-none-eabi"
    # The GCC version dir moves with toolchain bumps; discover it.
    cxx_inc = sorted((sysroot / "include/c++").glob("*"))
    if not cxx_inc:
        raise SystemExit(f"no libstdc++ headers under {sysroot / 'include/c++'}")

    flags = [
        "--target=armv7a-none-eabihf",
        "-mcpu=cortex-a9",
        # neon-fp16, not neon: -mfpu=neon DISABLES fp16, which rustc leaves on by
        # the cortex-a9 default, and that mismatch blocks cross-language inlining.
        "-mfpu=neon-fp16",
        "-mfloat-abi=hard",
        "-mthumb",
        "-mlittle-endian",
        "-funsafe-math-optimizations",
        "-stdlib=libstdc++",
        f"--sysroot={sysroot}",
        f"--gcc-toolchain={gcc}",
        # bits/c++config.h is per-multilib, and the fallback copy is soft-float.
        f"-isystem{cxx_inc[0]}/arm-none-eabi/{MULTILIB}",
    ]
    # Absolute paths, not bare names: the build scripts check is_file() on these.
    clangxx = shutil.which("clang++")
    if clangxx is None:
        raise SystemExit("clang++ not found on PATH (needed for the device shim)")
    # GNU ar cannot index LLVM bitcode: it would write an archive whose symbol
    # index is empty for every member, and the linker would pull in none of them.
    llvm_ar = shutil.which("llvm-ar")
    if llvm_ar is None:
        raise SystemExit("llvm-ar not found on PATH (GNU ar cannot index bitcode)")

    # The clang/lld device link's flags, emitted here rather than hardcoded in
    # .cargo/config.toml, because the toolchain/sysroot paths are per-checkout
    # (derived from the repo root, not one developer's absolute paths).
    # A per-target RUSTFLAGS env var REPLACES the config's rustflags array, so
    # this list must be complete.
    gcc_lib = sorted((gcc / "lib/gcc/arm-none-eabi").glob("*"))
    if not gcc_lib:
        raise SystemExit(f"no libgcc under {gcc / 'lib/gcc/arm-none-eabi'}")
    link_args = [
        "--target=armv7a-none-eabihf",
        "-mcpu=cortex-a9",
        # neon-fp16, not neon: clang's -mfpu=neon DISABLES fp16, which rustc
        # leaves on by the cortex-a9 default, and that one callee-only feature
        # mismatch blocks every cross-language inline.
        "-mfpu=neon-fp16",
        "-mfloat-abi=hard",
        "-nostartfiles",
        "-fuse-ld=lld",
        "-flto=thin",
        f"--sysroot={sysroot}",
        f"--gcc-toolchain={gcc}",
        "-stdlib=libstdc++",
        # libgcc, and unwindlib=none because a bare-metal GCC sysroot has no
        # libgcc_eh: the unwinder lives inside libgcc.a itself.
        "--rtlib=libgcc",
        "--unwindlib=none",
        # Clang does not implement GCC multilib selection for Arm, so the
        # hard-float NEON multilib is named explicitly. Getting this wrong is
        # the std::lround()-returns-0 failure.
        f"-L{sysroot}/lib/{MULTILIB}",
        f"-L{gcc_lib[0]}/{MULTILIB}",
    ]
    rustflags = ["-Clinker-plugin-lto", "-Clinker-flavor=gcc"]
    for arg in link_args:
        rustflags.append(f"-Clink-arg={arg}")

    return {
        "DELUGE_BUILD_DIR": str(root / BUILD_DIR),
        "DELUGE_BUILD_CONFIG": config,
        "DELUGE_DEVICE_CXX": clangxx,
        "DELUGE_DEVICE_CXXFLAGS": " ".join(flags),
        "DELUGE_DEVICE_AR": llvm_ar,
        "CARGO_TARGET_ARMV7A_DELUGE_EABIHF_LINKER": clangxx,
        "CARGO_TARGET_ARMV7A_DELUGE_EABIHF_RUSTFLAGS": " ".join(rustflags),
    }


def stage_artifacts(root: Path, config: str) -> int:
    """Copy the cargo-linked ELF into build/<config>/ and derive .bin/.hex/.nmdump.

    cargo owns the real output path (target/<triple>/<profile>/deluge-rust), but
    dbt loadfw, dbt sizediff and the VS Code launch configs all expect the
    build/<Config>/deluge.* layout. Staging keeps that contract without
    teaching every consumer about cargo's directory scheme.
    """
    profile = "release" if config in CARGO_RELEASE_CONFIGS else "debug"
    triple = Path(RUST_TARGET).stem  # armv7a-deluge-eabihf
    src_elf = root / RUST_BSP_DIR / "target" / triple / profile / "deluge-rust"
    if not src_elf.is_file():
        print(f"stage: no ELF at {src_elf}", file=sys.stderr)
        return 1

    out_dir = root / BUILD_DIR / config
    out_dir.mkdir(parents=True, exist_ok=True)
    elf = out_dir / "deluge.elf"
    shutil.copy2(src_elf, elf)

    gcc_bin = root / "toolchain/current/arm-none-eabi-gcc/bin"
    objcopy = gcc_bin / "arm-none-eabi-objcopy"
    nm = gcc_bin / "arm-none-eabi-nm"

    for fmt, suffix in (("binary", ".bin"), ("ihex", ".hex")):
        result = util.run(
            [str(objcopy), "-O", fmt, "-S", str(elf), str(out_dir / f"deluge{suffix}")]
        )
        if result != 0:
            return result

    with open(out_dir / "deluge.nmdump", "w") as f:
        result = subprocess.run(
            [
                str(nm),
                "-t",
                "d",
                "-S",
                "-C",
                "-r",
                "--special-syms",
                "--size-sort",
                str(elf),
            ],
            stdout=f,
            check=False,
        ).returncode
        if result != 0:
            return result

    print(f"Staged {elf.relative_to(root)} (+ .bin/.hex/.nmdump)")
    return 0


def check_tree_is_clang(build_dir: str) -> int:
    """Fail loudly if an existing build_dir was configured with a non-clang compiler.

    A checkout that ran `dbt configure`/`dbt build` before build/ and build-gcc/
    swapped trees may still have a GCC-configured build/ on disk. Reusing it
    would archive GCC-mangled objects into what this task treats as the clang
    tree -- and because Debug objects are plain ELF, the link SUCCEEDS with a
    corrupt ABI instead of failing here, surfacing later as baffling
    "undefined reference to ...(long)" errors. Refuse instead of silently
    deleting the developer's tree for them.
    """
    cache = Path(build_dir) / "CMakeCache.txt"
    if not cache.is_file():
        return 0
    compiler_line = next(
        (
            line
            for line in cache.read_text().splitlines()
            if line.startswith("CMAKE_CXX_COMPILER:")
        ),
        "",
    )
    if "clang" in compiler_line:
        return 0
    print(
        f"{build_dir}/ exists but was not configured with clang "
        f"({compiler_line or 'CMAKE_CXX_COMPILER not found in CMakeCache.txt'}). "
        f"This tree must be clang's -- its objects cannot be mixed with a "
        f"GCC-configured tree. Run `rm -rf {build_dir}` and retry.",
        file=sys.stderr,
    )
    return 1


def sdk_config_args(root: Path) -> list[str]:
    """`cargo --config local-sdk.toml` when the local deluge-sdk override exists.

    Cargo.toml pins the deluge-sdk crates to a revision so any checkout can
    build. local-sdk.toml (gitignored) patches them back to a sibling
    ../deluge-sdk working copy for live SDK development; absent it, the build
    uses exactly what the pinned revision gives everyone else.
    """
    override = root / RUST_BSP_DIR / "local-sdk.toml"
    return ["--config", "local-sdk.toml"] if override.is_file() else []


def main(argv: Sequence[str] | None = None) -> int:
    (args, cargo_extra) = argparser().parse_known_args(argv)

    root = util.get_git_root()
    os.chdir(root)
    config = BUILD_CONFIGS.get(args.config, args.config)

    # ── Configure the clang tree if it isn't already ─────────────────────────
    # Via task-configure so the generator, cross-configs and compile_commands
    # export stay identical to `dbt configure` (which clang-tidy CI relies on).
    if not os.path.exists(BUILD_DIR):
        result = importlib.import_module("task-configure").main(
            ["--toolchain", "clang"]
        )
        if result != 0:
            return result
    else:
        result = check_tree_is_clang(BUILD_DIR)
        if result != 0:
            return result

    # ── Step 1: build the C++ app objects (bitcode under Release) ────────────
    cmake_args = [
        "cmake",
        "--build",
        BUILD_DIR,
        "--config",
        config,
        "--target",
        *CPP_TARGETS,
    ]
    if args.verbose:
        cmake_args += ["--verbose"]
    if args.clean_first:
        cmake_args += ["--clean-first"]
    else:
        os.environ["NINJA_STATUS"] = "[%s/%t %p :: %e] "
    result = util.run(cmake_args)
    if result != 0:
        return result

    # ── Step 2: archive the C++ objects + link the Rust firmware with lld ────
    # -Zbuild-std because the target is a JSON spec with no prebuilt core; the
    # spec exists to match clang's target features (see armv7a-deluge-eabihf.json).
    cargo_args = [
        "cargo",
        "build",
        "--target",
        RUST_TARGET,
        "-Zbuild-std=core,alloc",
        "-Zjson-target-spec",
    ]
    if config in CARGO_RELEASE_CONFIGS:
        cargo_args += ["--release"]
    if args.verbose:
        cargo_args += ["--verbose"]
    cargo_args += cargo_extra
    cargo_args += sdk_config_args(root)

    env = {**os.environ, **device_cxx_env(root, config)}
    result = subprocess.run(
        cargo_args, cwd=RUST_BSP_DIR, env=env, check=False
    ).returncode
    if result != 0:
        return result

    return stage_artifacts(root, config)


if __name__ == "__main__":
    main()
