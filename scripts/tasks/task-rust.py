#! /usr/bin/env python3
"""Build the Rust/Embassy BSP firmware. Default: the real ARM device image.

By default this builds the actual firmware that runs on Deluge hardware: the
portable C++ application (Debug, `build-gcc/`) plus the Rust BSP, linked for
`armv7a-none-eabihf`. CI does not yet build this target — the armv7a BSP job
in .github/workflows/rust.yml is a commented-out TODO, pending a reproducible
device build in CI. The Rust BSP (`src/bsp/rust`) links the C++ application
as a static archive; its `build.rs` does NOT compile the C++ itself on this
path — it expects the app objects to already exist in `build-gcc/` (the GCC
fallback tree; the clang tree that ships lives in `build/`, see `dbt build`)
and panics otherwise. So the device build is a two-step flow:

  1. cmake builds the C++ app objects (Debug — Release uses GCC slim-LTO objects
     that rust's lld can't read).
  2. cargo archives those objects and links the ARM Rust firmware.

Doing step 1 explicitly each time also avoids a stale-object pitfall: `cargo
build` alone does not track the C++ sources, so editing a .cpp without a fresh
cmake build can link an out-of-date object.

Pass `--host` to instead build the platform-std host image (`cargo build`,
no `--target`) for host-side testing — no ARM C++ compile, no `build-gcc`.
See `src/bsp/rust/HOST_HARNESS.md` for how the host binary gets its C++
objects (`DELUGE_HOSTAPP_BUILD_DIR` / `build-embassy-hostapp`).
"""

import argparse
import importlib
import os
import subprocess
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
    "deluge_ui_core",
    "deluge_notify",
]

RUST_BSP_DIR = Path("src/bsp/rust")
# The GCC fallback tree. The clang/lld shipping build owns `build/` (dbt build).
BUILD_DIR = "build-gcc"
# build.rs links Debug objects (see module docstring); keep both steps in sync.
CONFIG = "Debug"


def sdk_config_args() -> list[str]:
    """`cargo --config local-sdk.toml` when the local deluge-sdk override exists.

    Cargo.toml pins the deluge-sdk crates to a revision so any checkout can
    build. local-sdk.toml (gitignored) patches them back to a sibling
    ../deluge-sdk working copy for live SDK development.
    """
    override = Path(util.get_git_root()).absolute() / RUST_BSP_DIR / "local-sdk.toml"
    return ["--config", "local-sdk.toml"] if override.is_file() else []


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="rust",
        description=(
            "Build the Rust/Embassy BSP firmware. Default: the ARM device image "
            "(C++ app in build-gcc/ + Rust, linked for armv7a-none-eabihf). "
            "--host builds the platform-std host image instead."
        ),
    )
    parser.group = "Building"
    parser.add_argument(
        "-v", "--verbose", help="Verbose cmake + cargo output", action="store_true"
    )
    parser.add_argument(
        "-c",
        "--clean-first",
        help="Clean the C++ app objects before rebuilding (device build only)",
        action="store_true",
    )
    parser.add_argument(
        "--host",
        help=(
            "Build the platform-std host image (plain `cargo build`, no "
            "--target) instead of the ARM device image. Skips the build-gcc "
            "C++ compile entirely."
        ),
        action="store_true",
    )
    # Anything after `--` is forwarded to `cargo build` (e.g. --release, --features).
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    (args, cargo_extra) = argparser().parse_known_args(argv)

    os.chdir(util.get_git_root())

    if args.host:
        # Host (platform-std) image: build.rs's host branch reads
        # DELUGE_HOSTAPP_BUILD_DIR (default build-embassy-hostapp/), not the
        # ARM CPP_TARGETS below — so skip build-gcc entirely, it would be
        # pure waste. See src/bsp/rust/HOST_HARNESS.md.
        print(
            "Building the HOST image (--host): C++ objects come from "
            "build-embassy-hostapp (DELUGE_HOSTAPP_BUILD_DIR), not build-gcc. "
            "See src/bsp/rust/HOST_HARNESS.md if build.rs panics on missing objects."
        )
        cargo_args = ["cargo", "build"]
        if args.verbose:
            cargo_args += ["--verbose"]
        cargo_args += cargo_extra
        cargo_args += sdk_config_args()
        return subprocess.run(
            cargo_args, cwd=RUST_BSP_DIR, env=os.environ, check=False
        ).returncode

    # ── Device (default): build the ARM firmware image ───────────────────────

    # Configure the GCC fallback tree if it isn't already.
    if not os.path.exists(BUILD_DIR):
        result = importlib.import_module("task-configure").main(["--toolchain", "gcc"])
        if result != 0:
            return result

    # ── Step 1: build the C++ app objects (the inputs to the Rust link) ──────
    cmake_args = [
        "cmake",
        "--build",
        BUILD_DIR,
        "--config",
        CONFIG,
        "--target",
        *CPP_TARGETS,
    ]
    if args.verbose:
        cmake_args += ["--verbose"]
    if args.clean_first:
        cmake_args += ["--clean-first"]
    result = util.run(cmake_args)
    if result != 0:
        return result

    # ── Step 2: archive the C++ objects + link the ARM Rust firmware ─────────
    # rust-toolchain.toml selects the nightly toolchain + armv7a target; the rtt
    # logging feature is on by default. Matches the `device` alias in
    # src/bsp/rust/.cargo/config.toml.
    cargo_args = [
        "cargo",
        "build",
        "--target",
        "armv7a-none-eabihf",
        "-Zbuild-std=core,alloc",
    ]
    if args.verbose:
        cargo_args += ["--verbose"]
    cargo_args += cargo_extra
    cargo_args += sdk_config_args()

    # Explicit, because build.rs's default is `<root>/build` — the clang tree.
    env = {
        **os.environ,
        "DELUGE_BUILD_DIR": str(Path(util.get_git_root()).absolute() / BUILD_DIR),
        "DELUGE_BUILD_CONFIG": CONFIG,
        "CARGO_TARGET_ARMV7A_NONE_EABIHF_LINKER": str(
            Path(util.get_git_root()).absolute()
            / "toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-g++"
        ),
    }
    return subprocess.run(cargo_args, cwd=RUST_BSP_DIR, env=env, check=False).returncode


if __name__ == "__main__":
    main()
