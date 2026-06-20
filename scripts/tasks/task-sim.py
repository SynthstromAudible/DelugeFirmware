#! /usr/bin/env python3
"""dbt sim — run the host emulator: the native firmware (deluge_host) driven by the
deluge-simulator front-panel GUI, wired over the host_link Unix socket.

deluge_host is the *brain* (real firmware compiled native via the host BSP); the simulator
(from the deluge-sdk repo) is the panel (OLED, pads, encoders, buttons). Audio comes out of
deluge_host directly (a forked pw-cat by default); serial MIDI auto-bridges to any
snd-virmidi device. See docs/dev/host_emulator.md.
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
SIM_CRATE_SUBPATH = Path("tools/deluge-simulator")


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
        "--socket",
        default=SOCKET_DEFAULT,
        help=f"host_link Unix socket path (default: {SOCKET_DEFAULT}).",
    )
    parser.add_argument(
        "--sdk-dir",
        help="Path to the deluge-sdk repo (holds the simulator GUI). "
        "Default: $DELUGE_SDK_DIR, then a sibling ../deluge-sdk.",
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


def find_sdk_dir(arg, root):
    for cand in (
        arg,
        os.environ.get("DELUGE_SDK_DIR"),
        str(Path(root).parent / "deluge-sdk"),
    ):
        if cand and (Path(cand) / SIM_CRATE_SUBPATH / "Cargo.toml").is_file():
            return Path(cand).resolve()
    return None


def build_host(no_build):
    """Configure (if needed) and build deluge_host."""
    if no_build:
        return 0
    if not Path(SIM_BUILD_DIR, "build.ninja").is_file():
        rc = subprocess.run(
            ["cmake", "-B", SIM_BUILD_DIR, "-S", "sim", "-G", "Ninja"], env=os.environ
        ).returncode
        if rc != 0:
            return rc
    return subprocess.run(
        ["cmake", "--build", SIM_BUILD_DIR, "--target", "deluge_host"], env=os.environ
    ).returncode


def build_sim(sim_crate, triple, release, no_build):
    if no_build:
        return 0
    cmd = ["cargo", "build", "--target", triple]
    if release:
        cmd += ["--release"]
    return subprocess.run(cmd, cwd=sim_crate, env=os.environ).returncode


def sim_binary(sim_crate, triple, release):
    profile = "release" if release else "debug"
    return sim_crate / "target" / triple / profile / "deluge-simulator"


def main():
    args = argparser().parse_args()
    os.chdir(util.get_git_root())
    root = os.getcwd()

    # Locate the simulator GUI (lives in the deluge-sdk repo).
    sdk = find_sdk_dir(args.sdk_dir, root)
    if sdk is None:
        util.note(
            "ERROR: could not find the deluge-sdk repo (it holds the simulator GUI under "
            "tools/deluge-simulator). Pass --sdk-dir <path> or set DELUGE_SDK_DIR."
        )
        return 1
    sim_crate = sdk / SIM_CRATE_SUBPATH
    triple = host_triple()

    if not shutil.which("cargo"):
        util.note(
            "ERROR: cargo not found — the simulator GUI is a Rust crate. Install Rust."
        )
        return 1

    # Build both halves.
    rc = build_host(args.no_build)
    if rc != 0:
        return rc
    rc = build_sim(sim_crate, triple, args.release, args.no_build)
    if rc != 0:
        return rc

    host_bin = Path(SIM_BUILD_DIR) / "deluge_host"
    sim_bin = sim_binary(sim_crate, triple, args.release)
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
