#!/usr/bin/env python3
"""Run UI scripts on two firmware images in delugemu and report where their snapshots differ.

Each script runs once per image on a fresh copy of the SD image and an erased settings flash, on a deterministic
virtual clock. At every `snapshot` line the runner reads the OLED, the pads (without PIC flashes), the indicator LEDs
and the gold-knob LEDs over a window of virtual time, and the DIN MIDI bytes the image sent since the previous
snapshot; the report lists every snapshot whose windows share no state, or whose MIDI differs.

Usage:
  run_parity.py --emu <delugemu checkout> --ref <main deluge.elf> --test <deluge.elf> --sd <card.img>
                --out <dir> [--jobs N] [--cache DIR | --no-cache] script.txt [script.txt ...]

A run is deterministic, so its snapshots are cached under --cache (default $XDG_CACHE_HOME/deluge-parity), keyed by the
image, the script, the card and this runner: a reference image compared again, or a script whose image did not change,
is not run again. Runs not in the cache go to --jobs emulators at once (default: half the cores, at most 16), each with
its own ports and working directory.

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
import concurrent.futures
import hashlib
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


class RunError(Exception):
    """A run that cannot give snapshots: the emulator did not start, the script is wrong, or the image aborted."""


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


class Schedule:
    """The script's own clock: every wait moves an absolute target, and the emulator runs to it. A stop lands a little
    after its target; measured from "now", those overshoots add up over a long script and the two images drift apart
    (by 14 s over `midi_follow`), so the same timeout falls in different snapshots."""

    def __init__(self, dz):
        self.dz = dz
        self.target_ns = dz.vclock_ns()

    def wait(self, seconds):
        self.target_ns += round(seconds * 1e9)
        if self.target_ns > self.dz.vclock_ns():
            self.dz.run_until(self.target_ns, timeout=600.0)


class AbortWatch:
    """Reads QEMU's interrupt log through a FIFO and keeps the count of data and prefetch aborts, and the first one's
    lines (the exception and the fault status and address QEMU prints after it): the log itself runs to gigabytes over
    a long script."""

    DETAIL_LINES = 4

    def __init__(self, path):
        os.mkfifo(path)
        self.path = path
        self.aborts = 0
        self.first = []
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        keep = 0
        with open(self.path, "rb") as fifo:
            for line in fifo:
                if keep:
                    self.first.append(line.decode(errors="replace").rstrip())
                    keep -= 1
                if b"Taking exception 4 [" in line or b"Taking exception 3 [" in line:
                    self.aborts += 1
                    if self.aborts == 1:
                        self.first.append(line.decode(errors="replace").rstrip())
                        keep = self.DETAIL_LINES


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
            raise RunError(f"run.sh did not start; see {log}")
        time.sleep(0.1)
    dz = session_cls(
        qmp_client_cls(sock, connect_timeout=15.0),
        proc=proc,
        repo_root=emu,
        run_log=log,
    )
    schedule = Schedule(dz)
    schedule.wait(BOOT_WAIT)
    return dz, emu_port, watch, schedule


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


def snapshot(dz, schedule, midi, random_pads=()):
    """The distinct states the screen shows over the window, in order, and the DIN MIDI sent since the last snapshot."""
    frames = []
    for _ in range(WINDOW_READS):
        state = screen(dz, random_pads)
        if state not in frames:
            frames.append(state)
        schedule.wait(WINDOW / WINDOW_READS)
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
    dz, emu_port, watch, schedule = launch(
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
                    schedule.wait(int(args[1]) / 1000.0 if len(args) > 1 else 0.12)
                if verb != "down":
                    dz.button(c["col"], c["row"], False)
            elif verb in ("paddown", "padup", "pad"):
                x, y = int(args[0]), int(args[1])
                if verb != "padup":
                    dz.pad_event(x, y, True)
                if verb == "pad":
                    schedule.wait(int(args[2]) / 1000.0 if len(args) > 2 else 0.12)
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
                schedule.wait(float(args[0]))
                continue
            elif verb == "snapshot":
                label = args[0]
                if label in snaps:
                    raise RunError(f"{where}: snapshot label {label!r} used twice")
                snaps[label] = snapshot(dz, schedule, midi, random_pads)
                continue
            else:
                raise RunError(f"{where}: unknown verb {verb!r}")
            schedule.wait(SETTLE)
        if watch.aborts:
            console = ""
            run_log = getattr(dz, "run_log", None)
            if run_log and os.path.exists(run_log):
                with open(run_log, errors="replace") as fh:
                    console = "".join(fh.readlines()[-20:])
            raise RunError(
                f"{os.path.basename(script)}: the image took {watch.aborts} data or prefetch abort(s); the first:\n"
                + "\n".join(watch.first)
                + f"\nthe image's console, last lines:\n{console}"
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


def file_digest(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for block in iter(lambda: f.read(1 << 20), b""):
            h.update(block)
    return h.hexdigest()


def cache_key(elf_digest, script, card, runner_digest):
    """What a run's snapshots depend on: the image, the script, the card (by size and modification time: hashing a
    card image takes longer than the run), this runner and the window it reads."""
    card_stat = os.stat(card)
    parts = [
        elf_digest,
        file_digest(script),
        f"{card_stat.st_size}:{card_stat.st_mtime_ns}",
        runner_digest,
        str(WINDOW),
    ]
    return hashlib.sha256("\n".join(parts).encode()).hexdigest()


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
    parser.add_argument(
        "--jobs", type=int, default=max(1, min(16, (os.cpu_count() or 2) // 2))
    )
    parser.add_argument(
        "--cache",
        default=os.path.join(
            os.environ.get("XDG_CACHE_HOME", os.path.expanduser("~/.cache")),
            "deluge-parity",
        ),
    )
    parser.add_argument("--no-cache", action="store_true")
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
    if not a.no_cache:
        os.makedirs(a.cache, exist_ok=True)
    runner_digest = file_digest(os.path.abspath(__file__))
    elf_digests = {elf: file_digest(elf) for elf in {a.ref, a.test}}

    results = {}  # (script, side) -> snapshots
    pending = []  # (script, side, elf, cache path)
    for script in a.scripts:
        for side, elf in (("ref", a.ref), ("test", a.test)):
            key = cache_key(elf_digests[elf], script, a.sd, runner_digest)
            cached = None if a.no_cache else os.path.join(a.cache, f"{key}.json")
            if cached and os.path.exists(cached):
                with open(cached, encoding="utf-8") as f:
                    results[(script, side)] = json.load(f)
                print(f"{os.path.basename(script)} [{side}] cached", flush=True)
            else:
                pending.append((script, side, elf, cached))

    def run(job):
        script, side, elf, cached = job
        name = os.path.splitext(os.path.basename(script))[0]
        workdir = tempfile.mkdtemp(prefix=f"parity-{name}-{side}-")
        started = time.monotonic()
        try:
            snaps = run_script(
                DelugeSession, QmpClient, CONTROLS, a.emu, elf, a.sd, script, workdir
            )
        finally:
            shutil.rmtree(workdir, ignore_errors=True)
        # In the form a cached run is read back in (lists, not tuples), so a fresh run compares equal to a cached one.
        snaps = json.loads(json.dumps(snaps))
        if cached:
            with open(cached + ".tmp", "w", encoding="utf-8") as f:
                json.dump(snaps, f)
            os.replace(cached + ".tmp", cached)
        print(
            f"{name} [{side}] {len(snaps)} snapshots in {time.monotonic() - started:.0f} s",
            flush=True,
        )
        return snaps

    with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
        futures = {pool.submit(run, job): job for job in pending}
        for future in concurrent.futures.as_completed(futures):
            script, side, _, _ = futures[future]
            try:
                results[(script, side)] = future.result()
            except RunError as e:
                for other in futures:
                    other.cancel()
                sys.exit(str(e))

    total = 0
    differing = 0
    for script in a.scripts:
        name = os.path.splitext(os.path.basename(script))[0]
        ref, test = results[(script, "ref")], results[(script, "test")]
        for side, snaps in (("ref", ref), ("test", test)):
            with open(
                os.path.join(a.out, f"{name}.{side}.json"), "w", encoding="utf-8"
            ) as f:
                json.dump(snaps, f, indent=1)
        diffs = differences(ref, test)
        total += len(ref)
        differing += len(diffs)
        for label, parts in diffs:
            print(f"DIFF {name}/{label}: {', '.join(parts)}")
            if "oled" in parts:
                ref_oled = ref[label]["frames"][0]["oled"]
                test_oled = test[label]["frames"][0]["oled"]
                for r, t in zip(ref_oled.splitlines(), test_oled.splitlines()):
                    if r != t:
                        print(f"  {r} | {t}")
            if "midi" in parts:
                print(f"  midi {ref[label]['midi']} | {test[label]['midi']}")
    print(f"PARITY {total - differing} of {total} snapshots identical")
    return 1 if differing else 0


if __name__ == "__main__":
    sys.exit(main())
