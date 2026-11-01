#!/usr/bin/env python3
"""Run UI scripts on two firmware images in delugemu and report where their snapshots differ.

Each script runs once per image on a fresh copy of the SD image and an erased settings flash, on a deterministic
virtual clock. At every `snapshot` line the runner reads the OLED, the pads (without PIC flashes), the indicator LEDs
and the gold-knob LEDs over a window of virtual time, and the DIN MIDI bytes the image sent since the previous
snapshot; the report lists every snapshot whose windows share no state, or whose MIDI differs.

Usage:
  run_parity.py --emu <delugemu checkout> --ref <main deluge.elf> --test <deluge.elf> --sd <card.img>
                --out <dir> script.txt [script.txt ...]

Setup: delugemu (https://github.com/SynthstromAudible/delugemu), built with its scripts/build.sh (check that
`strings qemu/build/qemu-system-arm | grep wfe_halted` finds its WFE patch); the card is the Synthstrom factory card
content as a FAT32 image (delugemu's scripts/mksd.sh, or mtools' mformat and mcopy). The reference image is main's
RelWithDebInfo deluge.elf.

Script lines (`#` starts a comment):
  down NAME | up NAME | press NAME [ms]   front-panel button by Control name (raw matrix input, holdable)
  paddown X Y | padup X Y | pad X Y [ms]  grid pad, x 0..17 (16/17 the sidebar), y 0..7 from the bottom
  turn ENCODER DETENTS                    SCROLL_Y, SCROLL_X, TEMPO, SELECT, UPPER, LOWER (gold knobs)
  cc CHANNEL NUMBER VALUE                 a control change into the DIN MIDI input, channel 1..16
  wait SECONDS                            of virtual time
  random_colour X Y                       from here on compare pad (X, Y) as lit or unlit only: its colour is a
                                          random row colour, which follows the random generator's state, and that
                                          shifts with any change to the image's timing (accepted difference D11)
  snapshot LABEL
"""

import argparse
import json
import os
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

ENCODERS = {
    "SCROLL_Y": 0,
    "SCROLL_X": 1,
    "TEMPO": 2,
    "SELECT": 3,
    "UPPER": 4,
    "LOWER": 5,
}
SETTLE = 0.25  # seconds after each input, long enough for a redraw on both images
BOOT_WAIT = 10.0
# At each snapshot both images are read over the same window of virtual time, so neither run's clock moves ahead of
# the other's: scrolling text and blinking LEDs, whose phase follows each image's own timing, match when the two
# windows share a frame.
WINDOW = float(os.environ.get("PARITY_WINDOW", "1.0"))  # seconds
WINDOW_READS = 25
# Our BSP samples the encoder phases on a 1 ms poll; the emulator's default edge spacing is faster than that.
EXTRA_QEMU = ("-global", "rza1l-gpio.edge-spacing-ns=3000000")
# A deterministic virtual clock, paused at reset: the runner advances it to exact times and injects input there, so a
# script replays identically on an image (timers, popups and random colours included).
ICOUNT = ("-icount", "shift=2,sleep=off", "-S")


def parse(path):
    steps = []
    with open(path, encoding="utf-8") as f:
        for number, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if line:
                verb, *args = line.split(
                    None, 1 if raw.lstrip().startswith("snapshot") else -1
                )
                steps.append((number, verb, args))
    return steps


def free_udp_port():
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def advance(dz, seconds):
    dz.run_until(dz.vclock_ns() + int(seconds * 1e9), timeout=600.0)


class AbortWatch:
    """Reads QEMU's interrupt log through a FIFO and keeps only the count of data and prefetch aborts: the log itself
    runs to gigabytes over a long script."""

    def __init__(self, path):
        os.mkfifo(path)
        self.path = path
        self.aborts = 0
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        with open(self.path, "rb") as fifo:
            for line in fifo:
                if b"Taking exception 4 [" in line or b"Taking exception 3 [" in line:
                    self.aborts += 1


def launch(session_cls, qmp_client_cls, emu, elf, card, extra, workdir, midi):
    """Start run.sh with SCIF0 (DIN MIDI) on a UDP chardev: the emulator sends to @p midi and listens on the port
    returned, and connect to its QMP socket."""
    emu_port = free_udp_port()
    sock = os.path.join(workdir, "qmp.sock")
    log = os.path.join(workdir, "run.log")
    int_log = os.path.join(workdir, "int.fifo")
    watch = AbortWatch(int_log)
    cmd = [
        os.path.join(emu, "scripts", "run.sh"),
        elf,
        "--sd",
        card,
        "--display",
        "none",
        "--audio",
        "none",
        "--midi",
        f"udp:127.0.0.1:{midi.getsockname()[1]}@127.0.0.1:{emu_port}",
        "--",
        "-qmp",
        f"unix:{sock},server,nowait",
        "-d",
        "int",
        "-D",
        int_log,
        *ICOUNT,
        *extra,
    ]
    with open(log, "w") as log_fh:  # the emulator keeps its own copy of the descriptor
        proc = subprocess.Popen(
            cmd,
            cwd=emu,
            stdout=log_fh,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
        )
    deadline = time.monotonic() + 90
    while not os.path.exists(sock):
        if proc.poll() is not None or time.monotonic() > deadline:
            proc.kill()
            sys.exit(f"run.sh did not start; see {log}")
        time.sleep(0.1)
    dz = session_cls(
        qmp_client_cls(sock, connect_timeout=15.0),
        proc=proc,
        repo_root=emu,
        run_log=log,
    )
    advance(dz, BOOT_WAIT)
    return dz, emu_port, watch


def drain(midi):
    received = b""
    while True:
        try:
            received += midi.recv(4096)
        except BlockingIOError:
            return received


def screen(dz, random_pads=()):
    pads = dz.read_pads(flashes=False)
    for x, y in random_pads:
        pads[x][y] = [1, 1, 1] if any(pads[x][y]) else [0, 0, 0]
    return {
        "oled": dz.oled_text(),
        "pads": pads,
        "leds": sorted(name for name, on in dz.read_leds().items() if on),
        "gold": dz.read_gold_knob(),
    }


def snapshot(dz, midi, random_pads=()):
    """The distinct states the screen shows over the window, in order, and the DIN MIDI sent since the last snapshot."""
    frames = []
    for _ in range(WINDOW_READS):
        state = screen(dz, random_pads)
        if state not in frames:
            frames.append(state)
        advance(dz, WINDOW / WINDOW_READS)
    return {"frames": frames, "midi": drain(midi).hex(" ")}


def run_script(session_cls, qmp_client_cls, controls, emu, elf, sd, script, workdir):
    card = os.path.join(workdir, "card.img")
    subprocess.run(["cp", "--sparse=always", sd, card], check=True)
    flash = os.path.join(workdir, "flash.bin")
    with open(flash, "wb") as f:
        f.write(b"\xff" * (4 << 20))
    extra = EXTRA_QEMU + ("-global", "rza1l-spibsc.flash-file=" + flash)
    snaps = {}
    random_pads = set()
    midi = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    midi.bind(("127.0.0.1", 0))
    midi.setblocking(False)
    dz, emu_port, watch = launch(
        session_cls, qmp_client_cls, emu, elf, card, extra, workdir, midi
    )
    try:
        drain(midi)  # boot-time output is not part of any snapshot
        for number, verb, args in parse(script):
            where = f"{os.path.basename(script)}:{number}"
            if verb in ("down", "up", "press"):
                c = controls[args[0].upper()]
                if verb != "up":
                    dz.button(c["col"], c["row"], True)
                if verb == "press":
                    advance(dz, int(args[1]) / 1000.0 if len(args) > 1 else 0.12)
                if verb != "down":
                    dz.button(c["col"], c["row"], False)
            elif verb in ("paddown", "padup", "pad"):
                x, y = int(args[0]), int(args[1])
                if verb != "padup":
                    dz.pad_event(x, y, True)
                if verb == "pad":
                    advance(dz, int(args[2]) / 1000.0 if len(args) > 2 else 0.12)
                if verb != "paddown":
                    dz.pad_event(x, y, False)
            elif verb == "turn":
                dz.encoder(ENCODERS[args[0].upper()], int(args[1]))
            elif verb == "cc":
                channel, number_, value = (int(v) for v in args)
                midi.sendto(
                    bytes([0xB0 | (channel - 1), number_, value]),
                    ("127.0.0.1", emu_port),
                )
            elif verb == "random_colour":
                random_pads.add((int(args[0]), int(args[1])))
                continue
            elif verb == "wait":
                advance(dz, float(args[0]))
                continue
            elif verb == "snapshot":
                label = args[0]
                if label in snaps:
                    sys.exit(f"{where}: snapshot label {label!r} used twice")
                snaps[label] = snapshot(dz, midi, random_pads)
                continue
            else:
                sys.exit(f"{where}: unknown verb {verb!r}")
            advance(dz, SETTLE)
        if watch.aborts:
            sys.exit(
                f"{os.path.basename(script)}: the image took {watch.aborts} data or prefetch abort(s)"
            )
    finally:
        dz.close()
        midi.close()
    return snaps


def differences(ref, test):
    found = []
    for label in ref:
        if label not in test:
            found.append((label, ["missing"]))
            continue
        r, t = ref[label], test[label]
        if (
            any(frame in r["frames"] for frame in t["frames"])
            and r["midi"] == t["midi"]
        ):
            continue
        # name what differs between the windows' first frames
        parts = [
            key
            for key in ("oled", "pads", "leds", "gold")
            if r["frames"][0][key] != t["frames"][0][key]
        ]
        if r["midi"] != t["midi"]:
            parts.append("midi")
        found.append((label, parts or ["frames"]))
    return found


def main():
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument(
        "--emu", default=os.environ.get("DELUGEMU_ROOT"), help="delugemu checkout"
    )
    parser.add_argument("--ref", required=True)
    parser.add_argument("--test", required=True)
    parser.add_argument("--sd", required=True)
    parser.add_argument("--out", required=True)
    parser.add_argument("scripts", nargs="+")
    a = parser.parse_args()
    if not a.emu:
        sys.exit("--emu or DELUGEMU_ROOT must name a delugemu checkout")
    # The emulator's launcher runs in its own checkout, so every path it is handed must be absolute.
    a.emu, a.ref, a.test, a.sd = (
        os.path.abspath(p) for p in (a.emu, a.ref, a.test, a.sd)
    )
    sys.path.insert(0, os.path.join(a.emu, "pylib"))
    from delugemu import DelugeSession
    from delugemu._controls import CONTROLS
    from delugemu.qmp import QmpClient

    os.makedirs(a.out, exist_ok=True)
    total = 0
    differing = 0
    for script in a.scripts:
        name = os.path.splitext(os.path.basename(script))[0]
        results = {}
        for side, elf in (("ref", a.ref), ("test", a.test)):
            workdir = tempfile.mkdtemp(prefix=f"parity-{name}-{side}-")
            started = time.monotonic()
            results[side] = run_script(
                DelugeSession, QmpClient, CONTROLS, a.emu, elf, a.sd, script, workdir
            )
            with open(
                os.path.join(a.out, f"{name}.{side}.json"), "w", encoding="utf-8"
            ) as f:
                json.dump(results[side], f, indent=1)
            print(
                f"{name} [{side}] {len(results[side])} snapshots in {time.monotonic() - started:.0f} s",
                flush=True,
            )
            shutil.rmtree(workdir, ignore_errors=True)
        diffs = differences(results["ref"], results["test"])
        total += len(results["ref"])
        differing += len(diffs)
        for label, parts in diffs:
            print(f"DIFF {name}/{label}: {', '.join(parts)}")
            if "oled" in parts:
                ref_oled = results["ref"][label]["frames"][0]["oled"]
                test_oled = results["test"][label]["frames"][0]["oled"]
                for r, t in zip(ref_oled.splitlines(), test_oled.splitlines()):
                    if r != t:
                        print(f"  {r} | {t}")
            if "midi" in parts:
                print(
                    f"  midi {results['ref'][label]['midi']} | {results['test'][label]['midi']}"
                )
    print(f"PARITY {total - differing} of {total} snapshots identical")
    return 1 if differing else 0


if __name__ == "__main__":
    sys.exit(main())
