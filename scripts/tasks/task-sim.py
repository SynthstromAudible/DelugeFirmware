#! /usr/bin/env python3
"""dbt sim — run the host emulator: the native firmware (deluge_host) driven by the
deluge-simulator front-panel GUI, wired over the host_link Unix socket.

deluge_host is the *brain* (real firmware compiled native via the host BSP); the simulator
(a Rust crate in the deluge-sdk repo, fetched straight from git) is the panel (OLED,
pads, encoders, buttons). Audio comes out of deluge_host directly (a forked pw-cat by
default); serial MIDI auto-bridges to any snd-virmidi device. See docs/dev/host_emulator.md.
"""

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

import util

SIM_BUILD_DIR = "build-sim"
SOCKET_DEFAULT = "/tmp/deluge_emu.sock"

# The simulator GUI is a Rust crate in the deluge-sdk repo. We build+install it directly
# from git — no local checkout required. It is built with `--no-default-features` so the
# top-panel "rack" strip (CV/gate meters + audio scopes) is compiled out: over the host_link
# protocol the panel carries no audio, so that strip would be dead. The result is the
# faceplate-only ("no-panel") simulator.
SIM_GIT_URL = "https://github.com/FirestormAudio/deluge-sdk.git"
SIM_GIT_BRANCH = "main"
SIM_PACKAGE = "deluge-simulator"


def argparser():
    parser = argparse.ArgumentParser(
        prog="sim",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        description="Run the native firmware under the desktop front-panel simulator (the host emulator).",
        epilog="""\nusage example: dbt sim
                  Build deluge_host + the simulator GUI and launch the combo. Close the
                  window (or Ctrl-C) to stop both. Add --no-audio to run muted, or
                  --midi <dev> to bridge serial MIDI to a specific device.""",
    )
    parser.group = "Development"
    parser.add_argument(
        "--no-build",
        action="store_true",
        help="Run the existing binaries; don't (re)build.",
    )
    parser.add_argument(
        "--release",
        action="store_true",
        help="Build/run the simulator GUI in release (faster window; slower first build).",
    )
    parser.add_argument(
        "--no-audio", action="store_true", help="Run muted (DELUGE_HOST_AUDIO=off)."
    )
    parser.add_argument(
        "--audio",
        metavar="CMD",
        help="Custom audio player command (reads raw S16 stereo on stdin).",
    )
    parser.add_argument(
        "--midi",
        metavar="DEV",
        help="Bridge serial MIDI to this device/FIFO (e.g. a snd-virmidi /dev/snd/midiCxDy). "
        "Default: auto-detect snd-virmidi.",
    )
    parser.add_argument(
        "--no-midi", action="store_true", help="Disable the serial MIDI bridge."
    )
    parser.add_argument(
        "--64bit",
        "--x64",
        dest="x64",
        action="store_true",
        help="Build/run the 64-bit host (DELUGE_SIM_X64) in build-sim-x64 instead of the 32-bit default.",
    )
    parser.add_argument(
        "--socket",
        default=SOCKET_DEFAULT,
        help=f"host_link Unix socket path (default: {SOCKET_DEFAULT}).",
    )
    return parser


def host_triple():
    """The Rust host target triple (the simulator GUI must be built for it — the SDK's
    default target is armv7a, so we always pass --target)."""
    out = subprocess.run(["rustc", "-vV"], capture_output=True, text=True).stdout
    for line in out.splitlines():
        if line.startswith("host:"):
            return line.split(":", 1)[1].strip()
    return "x86_64-unknown-linux-gnu"


def sim_install_root():
    """Where `cargo install` drops the simulator (binary lands in <root>/bin)."""
    return Path(SIM_BUILD_DIR) / "sim-tools"


def build_host(no_build, build_dir, cmake_defs=()):
    """Configure (if needed) and build deluge_host in build_dir with the given -D defines."""
    if no_build:
        return 0
    if not Path(build_dir, "build.ninja").is_file():
        rc = subprocess.run(
            ["cmake", "-B", build_dir, "-S", "sim", "-G", "Ninja", *cmake_defs],
            env=os.environ,
        ).returncode
        if rc != 0:
            return rc
    return subprocess.run(
        ["cmake", "--build", build_dir, "--target", "deluge_host"], env=os.environ
    ).returncode


def build_sim(triple, release, no_build):
    """Build+install the faceplate-only simulator GUI straight from git.

    `cargo install` no-ops fast when the pinned branch hasn't moved, so this is cheap on
    repeat runs; it rebuilds automatically when upstream advances.
    """
    if no_build:
        return 0
    cmd = [
        "cargo",
        "install",
        "--git",
        SIM_GIT_URL,
        "--branch",
        SIM_GIT_BRANCH,
        SIM_PACKAGE,
        "--no-default-features",
        "--locked",
        "--target",
        triple,
        "--root",
        str(sim_install_root()),
    ]
    if not release:
        cmd += ["--debug"]
    return subprocess.run(cmd, env=os.environ).returncode


def sim_binary():
    return sim_install_root() / "bin" / "deluge-simulator"


def main():
    args = argparser().parse_args()
    os.chdir(util.get_git_root())

    triple = host_triple()

    if not shutil.which("cargo"):
        util.note(
            "ERROR: cargo not found — the simulator GUI is a Rust crate. Install Rust."
        )
        return 1

    # The 64-bit host lives in its own build dir so it never clashes with the 32-bit default.
    host_build_dir = "build-sim-x64" if args.x64 else SIM_BUILD_DIR
    cmake_defs = ["-DDELUGE_SIM_X64=ON"] if args.x64 else []

    # Build both halves. (The GUI panel is a native host binary — same regardless of the
    # firmware's 32/64-bit word size — so it always installs under the default build-sim.)
    rc = build_host(args.no_build, host_build_dir, cmake_defs)
    if rc != 0:
        return rc
    rc = build_sim(triple, args.release, args.no_build)
    if rc != 0:
        return rc

    host_bin = Path(host_build_dir) / "deluge_host"
    sim_bin = sim_binary()
    for label, path in (("deluge_host", host_bin), ("deluge-simulator", sim_bin)):
        if not path.is_file():
            util.note(
                f"ERROR: {label} binary not found at {path}. Build it (drop --no-build)."
            )
            return 1

    # Environment for the firmware brain.
    env = dict(os.environ)
    env["DELUGE_HOST_LINK"] = args.socket
    if args.no_audio:
        env["DELUGE_HOST_AUDIO"] = "off"
    elif args.audio:
        env["DELUGE_HOST_AUDIO"] = args.audio
    if args.no_midi:
        env["DELUGE_HOST_MIDI"] = "off"
    elif args.midi:
        env["DELUGE_HOST_MIDI"] = args.midi

    # deluge_host is the socket *server*: it binds + blocks on accept until the panel connects,
    # so start it first and wait for the socket to appear.
    if os.path.exists(args.socket):
        os.unlink(args.socket)
    print(f"Starting deluge_host (brain) on {args.socket} ...")
    host_proc = subprocess.Popen([str(host_bin)], env=env)

    try:
        for _ in range(200):  # up to ~10s
            if os.path.exists(args.socket):
                break
            if host_proc.poll() is not None:
                util.note(
                    f"ERROR: deluge_host exited early (code {host_proc.returncode})."
                )
                return host_proc.returncode or 1
            time.sleep(0.05)
        else:
            util.note("ERROR: deluge_host never created the link socket.")
            host_proc.terminate()
            return 1

        print("Starting deluge-simulator (panel) — close the window to stop.")
        # The GUI's wgpu/winit chatter is noise; keep deluge_host's logs (audio/MIDI/link) on the
        # terminal but send the panel's stdout/stderr to a logfile.
        sim_log = open(
            os.path.join(util.get_git_root(), SIM_BUILD_DIR, "deluge-simulator.log"),
            "w",
        )
        sim_proc = subprocess.Popen(
            [str(sim_bin), "--connect", args.socket],
            stdout=sim_log,
            stderr=subprocess.STDOUT,
        )

        # Wait for either to exit, then tear the other down.
        while True:
            if sim_proc.poll() is not None:
                break
            if host_proc.poll() is not None:
                util.note(f"deluge_host exited (code {host_proc.returncode}).")
                break
            time.sleep(0.1)
    except KeyboardInterrupt:
        print("\nStopping emulator ...")
    finally:
        for proc in ("sim_proc", "host_proc"):
            p = locals().get(proc)
            if p and p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    p.kill()
        if os.path.exists(args.socket):
            try:
                os.unlink(args.socket)
            except OSError:
                pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
