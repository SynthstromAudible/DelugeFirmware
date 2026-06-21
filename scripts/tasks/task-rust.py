#! /usr/bin/env python3
"""Build the Rust/Embassy BSP firmware (the C++ app + the Rust BSP, in one go).

The Rust BSP (`src/bsp/rust`) links the portable C++ application as a static
archive. Its `build.rs` does NOT compile the C++ itself — it expects the app
objects to already exist in `build/` and panics otherwise. So a full build is a
two-step flow:

  1. cmake builds the C++ app objects (Debug — Release uses GCC slim-LTO objects
     that rust's lld can't read).
  2. cargo archives those objects and links the Rust firmware.

Doing step 1 explicitly each time also avoids a stale-object pitfall: `cargo
build` alone does not track the C++ sources, so editing a .cpp without a fresh
cmake build can link an out-of-date object.
"""

import argparse
import importlib
import os
import subprocess
from pathlib import Path
from typing import Sequence

import util

# The C++ targets the Rust build.rs archives + links (mirrors its panic message).
CPP_TARGETS = [
    "deluge_app",
    "fatfs",
    "NE10",
    "eyalroz_printf",
    "deluge_dsp",
    "deluge_scheduler",
    "deluge_foundation",
    "deluge_midi",
]

RUST_BSP_DIR = Path("src/bsp/rust")
# build.rs links Debug objects (see module docstring); keep both steps in sync.
CONFIG = "Debug"


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="rust",
        description="Build the Rust/Embassy BSP firmware (C++ app + Rust)",
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
    # Anything after `--` is forwarded to `cargo build` (e.g. --release, --features).
    return parser


def main(argv: Sequence[str] = None) -> int:
    (args, cargo_extra) = argparser().parse_known_args(argv)

    os.chdir(util.get_git_root())

    # Configure the CMake tree if it isn't already (mirrors task-build).
    if not os.path.exists("build"):
        result = importlib.import_module("task-configure").main()
        if result != 0:
            return result

    # ── Step 1: build the C++ app objects (the inputs to the Rust link) ──────
    cmake_args = [
        "cmake",
        "--build",
        "build",
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

    # ── Step 2: archive the C++ objects + link the Rust firmware ─────────────
    # rust-toolchain.toml selects the nightly toolchain + armv7a target; the rtt
    # logging feature is on by default.
    cargo_args = ["cargo", "build"]
    if args.verbose:
        cargo_args += ["--verbose"]
    cargo_args += cargo_extra
    return subprocess.run(cargo_args, cwd=RUST_BSP_DIR, env=os.environ).returncode


if __name__ == "__main__":
    main()
