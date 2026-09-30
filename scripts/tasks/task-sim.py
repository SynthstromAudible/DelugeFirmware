#! /usr/bin/env python3
"""dbt sim — run the emulator: the firmware on the Embassy host, as the brain of deluge-sdk's desktop simulator.

One process: the threaded host app (the C++ app on the Rust BSP's host executors) drives the simulator window's
OLED, pads and LEDs, reads its buttons, pads and encoders, plays through its audio device, and carries DIN MIDI over
its virtual ports. The card is an image file, a directory (written back when the window closes), or an empty
formatted card. See docs/dev/host_emulator.md.
"""

import argparse
import importlib
import os
import subprocess
import sys
from pathlib import Path

import util

test_builds = importlib.import_module("test_builds")

RUST_BSP_DIR = Path("src/bsp/rust")
BINARY = RUST_BSP_DIR / "target" / "release" / "deluge-rust"


def argparser():
    parser = argparse.ArgumentParser(
        prog="sim",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Run the firmware in the desktop front-panel simulator (the emulator).",
        epilog="""\nusage examples:
  dbt sim                          an empty card
  dbt sim --card ~/Deluge/card     a card directory, written back when the window closes
  dbt sim --card card.img          a FAT image, used in place
  dbt sim --headless script.txt --out snapshots/   scripted, without a window""",
    )
    parser.group = "Development"
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Run the existing binary; don't (re)build.",
    )
    parser.add_argument(
        "--card",
        metavar="PATH",
        help="The SD card: a FAT image file, or a directory loaded at boot and written back on exit.",
    )
    parser.add_argument(
        "--audio-in",
        metavar="WAV",
        help="Feed this WAV (looped) as the audio input instead of the input device.",
    )
    parser.add_argument(
        "--audio-out",
        metavar="WAV",
        help="Record the audio output to this WAV.",
    )
    parser.add_argument(
        "--hardware",
        nargs="?",
        const="auto",
        metavar="PORT",
        help="Mirror the panel onto a Deluge connected over USB (auto-detected, or PORT).",
    )
    parser.add_argument(
        "--headless",
        metavar="SCRIPT",
        help="Run without a window, replaying SCRIPT (timed input, MIDI and snapshot steps).",
    )
    parser.add_argument(
        "--out",
        metavar="DIR",
        help="Where --headless writes its snapshots (default: the current directory).",
    )
    return parser


def sdk_config_args(root: Path) -> list[str]:
    """`cargo --config local-sdk.toml` when the local deluge-sdk override exists (see task-rust.py)."""
    return (
        ["--config", "local-sdk.toml"]
        if (root / RUST_BSP_DIR / "local-sdk.toml").is_file()
        else []
    )


def build(root: Path) -> int:
    """Build the C++ app's host tree, as the gates build it, then the emulator binary on top of it."""
    host_app = test_builds.products(root)["host-app"]
    for step in host_app.steps(root):
        print(f"==> sim: host app: {step.what}", flush=True)
        rc = subprocess.run(
            step.argv, cwd=step.cwd, env=dict(os.environ, **step.env), check=False
        ).returncode
        if rc != 0:
            return rc

    config = sdk_config_args(root)
    lock = root / RUST_BSP_DIR / "Cargo.lock"
    # A build through local-sdk.toml rewrites the lockfile's deluge-sdk pins to local paths; put it back as it was.
    held = lock.read_bytes() if config else None
    print("==> sim: emulator", flush=True)
    try:
        # cargo does not see C++ changes: touching the build script makes it archive the freshly built objects again.
        (root / RUST_BSP_DIR / "build" / "main.rs").touch()
        return subprocess.run(
            ["cargo", "build", "--release", "--features", "sim", *config],
            cwd=root / RUST_BSP_DIR,
            env=dict(
                os.environ,
                DELUGE_HOSTAPP_BUILD_DIR=str(root / test_builds.HOST_APP_TREE),
            ),
            check=False,
        ).returncode
    finally:
        if held is not None:
            lock.write_bytes(held)


def main():
    args = argparser().parse_args()
    root = Path(util.get_git_root()).absolute()

    if not args.no_build:
        rc = build(root)
        if rc != 0:
            return rc
    binary = root / BINARY
    if not binary.is_file():
        util.note(f"ERROR: the emulator is not built at {binary}. Drop --no-build.")
        return 1

    env = dict(os.environ)

    def absolute(path: str) -> str:
        return str(Path(path).expanduser().absolute())

    if args.card:
        env["DELUGE_SIM_CARD"] = absolute(args.card)
    if args.audio_in:
        env["DELUGE_SIM_AUDIO_IN"] = absolute(args.audio_in)
    if args.audio_out:
        env["DELUGE_SIM_AUDIO_OUT"] = absolute(args.audio_out)
    if args.hardware:
        env["DELUGE_SIM_HARDWARE"] = "" if args.hardware == "auto" else args.hardware
    if args.headless:
        env["DELUGE_HEADLESS"] = "1"
        env["DELUGE_SIM_SCRIPT"] = absolute(args.headless)
        if args.out:
            env["DELUGE_SIM_OUT"] = absolute(args.out)
    # The emulator replaces this process, so Ctrl-C and its exit status are its own.
    sys.stdout.flush()
    os.execve(binary, [str(binary)], env)


if __name__ == "__main__":
    sys.exit(main())
