#!/usr/bin/env python3
"""emu_parity.py -- run two firmware images side by side in delugemu and require identical panels.

A user must not be able to tell two builds apart by looking at the Deluge. This boots a reference image and a
test image in the delugemu emulator (~/GitHub/delugemu) on the same SD card and the same starting settings flash,
drives both through the same level-3 UI scripts (format in tests/harness/README.md), and at every `snapshot` step
compares what each shows: the OLED bitmap, every pad's colour, the button LEDs, the gold-knob rings, the pads the
PIC flashed, and the pad refresh time and dimmer interval (the pad-brightness setting) the firmware gave the PIC.

Runs are deterministic. Each emulator runs paused under an instruction-counted clock (-icount sleep=off), and the
harness advances it to exact virtual times: boot is the first virtual instant the panel is lit and then unchanged
for BOOT_QUIET_MS, script times are virtual milliseconds after it, and every input lands on its millisecond. A
snapshot samples the panel every SAMPLE_MS from its time until the next step (at most WINDOW_MS) and compares the
set of states each image showed, so a blinking LED compares by what it shows, not by its phase at one instant. A
PIC pad flash lasts 6 ms, too short for the samples to see reliably, so delugemu also logs every flash and the
harness compares the set of (x, y, colour) each image flashed over the whole window, and the pads part is the
firmware's pad image without the flashes on top. Against a delugemu without these readouts, flashes and PIC
settings read as empty on both images and the pads are sampled as shown.

    tools/emu_parity.py tests/ui-scripts/parity           # every script, each on the cards its header names
    tools/emu_parity.py --sd card.img tests/ui-scripts/parity/p01-views.txt

Each snapshot reads MATCH, PHASE, D77 or DIFF (see classify(); the report holds each one's verdict and differences).
PHASE is a difference only in when a value showed: every part ends the window the same on both images, or, with
--calibrate, a second run of either image differs from its first at that snapshot too. D77 is DIFFERENCES.md's
accepted section-line difference (`accepted`). Each script runs once per card its `# card:` header names
(tools/ui_script_cards.py): a directory or card manifest is imaged with delugemu's mksd.sh, a FAT image used as it
stands; --card or --sd runs every script on one card instead. A script that runs on several cards reports as
`<script>@<card>/<snapshot>`. The images' DIN MIDI output is compared at every snapshot, and a `random_colour X Y`
step makes pad (X, Y) compare as lit or unlit only.

Exit codes: 0 every snapshot matched, differed only in phase or by an accepted row; 1 a snapshot differed or a run
failed; 4 a usage error: no reference image or image under test, a script that does not exist or names no card, two
cards with one label, or a card that does not image.
"""

import argparse
import concurrent.futures
import contextlib
import functools
import hashlib
import json
import os
import re
import select
import shutil
import socket
import subprocess
import sys
import threading
import time
from collections import Counter
from pathlib import Path

import ui_script_cards

REPO = Path(__file__).resolve().parent.parent
BUTTON_TABLE = REPO / "tests/harness/level3/host_script_tables.c"

# Firmware encoder index by script role: tests/harness/level3/host_script_tables.c's k_encoder_roles.
ENCODER_ROLES = ["Vertical", "Horizontal", "Tempo", "Select", "GoldUpper", "GoldLower"]

MS = 1_000_000
# 2^2 ns of virtual time per instruction: 250 MIPS, near the Deluge's 400 MHz Cortex-A9.
ICOUNT_SHIFT = 2
# One encoder phase step every 3 ms (a detent in 12 ms): a brisk hand turn. delugemu's default, 50 us, is a turn no
# hand makes, faster than the Rust BSP's decoder samples the B pin (every 1 ms).
ENCODER_PHASE_NS = 3_000_000
BOOT_STEP_MS = 100
BOOT_QUIET_MS = 1500
BOOT_TIMEOUT_MS = 60_000
SAMPLE_MS = 25
# Long enough for a scrolling name's whole cycle; a script leaves that long before its next step where text scrolls.
WINDOW_MS = 8000
# `load NAME`: Load, the browser's listing, one key per character, then Select.
LOAD_OPEN_MS = 1500
LOAD_KEY_MS = 150
LOAD_SETTLE_MS = 800

# The OLED rows an output name's section line ("SECTION n") takes on the name screen (DIFFERENCES.md D77).
SECTION_LINE_ROWS = range(37, 44)
# The firmware's own font table; the section line is drawn in its 7 px font (`font_apple`).
FONT_SOURCE = REPO / "src/gfx/oled/fonts/fonts.c"
OLED_WIDTH = 128
# The 7 px font is monospaced: every character takes this many columns.
TEXT_SPACING_X = 6


def button_table() -> dict[str, tuple[int, int]]:
    """The level-3 harness's button names and matrix coordinates, read from its own C table."""
    text = BUTTON_TABLE.read_text()
    return {
        m[1]: (int(m[2]), int(m[3]))
        for m in re.finditer(r'\{"(\w+)",\s*(\d+),\s*(\d+)\}', text)
    }


def parse_script(path: Path) -> list[tuple[int, str, list[str]]]:
    """`<at_ms> <command> <args...>` lines, `#` comments, as tests/harness/ui_script.cpp parses them."""
    steps = []
    for line in path.read_text().splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tok = line.split()
        steps.append((int(tok[0]), tok[1], tok[2:]))
    return steps


# Script commands that are not panel input on a real Deluge, which the runner passes over.
NOT_INPUT = {"board", "released", "model", "notice"}


def check_steps(script: Path, steps: list[tuple[int, str, list[str]]]) -> None:
    """Refuses a command the runner does not know, which would otherwise do nothing and leave the snapshots after it
    differing with no hint why, and a snapshot name used twice, whose second window would hide the first."""
    known = {
        "button",
        "pad",
        "midi",
        *MIDI_STATUS,
        "random_colour",
        "encoder",
        "load",
        "snapshot",
        "quit",
        *NOT_INPUT,
    }
    named = set()
    for _, cmd, args in steps:
        if cmd not in known:
            raise ValueError(f"{script.name}: unknown command {cmd!r}")
        if cmd == "snapshot":
            if args[0] in named:
                raise ValueError(f"{script.name}: snapshot {args[0]!r} twice")
            named.add(args[0])


def monitor_errors() -> tuple[type[BaseException], ...]:
    """What a session raises for a readout it lacks: delugemu's QMP error, when delugemu is on the path (a rig
    session has no monitor)."""
    try:
        from delugemu.qmp import QmpError
    except ImportError:
        return ()
    return (QmpError,)


def take_flashes(dz) -> list[list[int]]:
    """The [x, y, colour] pad flashes since the last call (delugemu clears its log on each read); none from a
    delugemu without the readout."""
    try:
        return [list(f) for f in dz.read_pad_flashes()]
    except (AttributeError, *monitor_errors()):
        return []


def pic_settings(dz) -> dict:
    """The refresh time and dimmer interval the firmware last gave the PIC; empty from a delugemu without them."""
    try:
        got = dz.read_pic_settings()
    except (AttributeError, *monitor_errors()):
        return {}
    return {k: got[k] for k in ("refresh_time", "dimmer_interval")}


def pads(dz) -> list:
    """The pads without the PIC's flashes on top, which `take_flashes` reports instead; as shown, flashes and all,
    from a delugemu without that readout."""
    try:
        return dz.read_pads(flashes=False)
    except (TypeError, *monitor_errors()):
        return dz.read_pads()


def panel(dz, random_pads=()) -> dict:
    """What the panel shows now, plus the pad flashes since the previous sample. A pad in `random_pads` reads lit or
    unlit only: its colour is a random row colour, which follows the random generator, and any change to an image's
    timing moves that (DIFFERENCES.md D11)."""
    shown = pads(dz)
    for x, y in random_pads:
        shown[x][y] = [1, 1, 1] if any(shown[x][y]) else [0, 0, 0]
    return {
        "oled": dz.read_oled(),
        "pads": shown,
        "leds": dz.read_leds(),
        "gold": dz.read_gold_knob(),
        "flashes": take_flashes(dz),
        "pic": pic_settings(dz),
    }


def lit(p: dict) -> bool:
    return any(any(row) for row in p["oled"]) or any(
        any(any(v) for v in col) for col in p["pads"]
    )


def key(p: dict) -> str:
    return json.dumps(p, sort_keys=True)


def boot(dz) -> int:
    """The virtual time (ns) at which the OLED is lit and the pads, LEDs and knob rings have been unchanged for
    BOOT_QUIET_MS. The OLED itself may never settle: a long preset name scrolls for as long as it shows."""
    t = 0
    last, since = None, 0
    while t < BOOT_TIMEOUT_MS * MS:
        t += BOOT_STEP_MS * MS
        dz.run_until(t)
        now = panel(dz)
        still = key({k: v for k, v in now.items() if k not in ("oled", "flashes")})
        if still != last:
            last, since = still, t
        elif (
            any(any(row) for row in now["oled"])
            and lit(now)
            and t - since >= BOOT_QUIET_MS * MS
        ):
            return t
    raise RuntimeError("the panel never settled after boot")


def load(dz, at: int, name: str, buttons) -> int:
    """Open the browser with Load, type `name` on the pad keyboard (a prefix jumps to the entry), and press Select.
    Returns the virtual time the gesture ends."""
    from delugemu import qwerty

    x, y = buttons["Load"]
    dz.button(x, y, True)
    dz.run_until(at + 20 * MS)
    dz.button(x, y, False)
    t = at + LOAD_OPEN_MS * MS
    for ch in name:
        (px, py), shifted = qwerty.key_for(ch)
        if shifted:
            raise ValueError(
                f"`load`: {ch!r} needs shift, which the harness does not type"
            )
        dz.run_until(t)
        dz.pad_event(px, py, True)
        dz.run_until(t + 40 * MS)
        dz.pad_event(px, py, False)
        t += LOAD_KEY_MS * MS
    t += LOAD_SETTLE_MS * MS
    dz.run_until(t)
    x, y = buttons["SelectEnc"]
    dz.button(x, y, True)
    dz.run_until(t + 20 * MS)
    dz.button(x, y, False)
    return t + 20 * MS


# Script steps that send MIDI, and their DIN status byte; channels are 1..16.
MIDI_STATUS = {"note": 0x90, "noteoff": 0x80, "cc": 0xB0, "pc": 0xC0, "bend": 0xE0}


def midi_bytes(cmd: str, args: list[str]) -> bytes:
    """The message a MIDI step sends: `midi HEX...` as written, `note CH NOTE VEL`, `noteoff CH NOTE`,
    `cc CH NUMBER VALUE`, `pc CH PROGRAM`, `bend CH VALUE14` (0..16383, 8192 centred)."""
    if cmd == "midi":
        return bytes.fromhex("".join(args))
    channel, *values = (int(a) for a in args)
    if not 1 <= channel <= 16:
        raise ValueError(f"`{cmd}`: channel {channel} is not 1..16")
    if cmd == "noteoff":
        values = [*values, 0]
    elif cmd == "bend":
        if not 0 <= values[0] <= 16383:
            raise ValueError(f"`bend`: {values[0]} is not 0..16383")
        values = [values[0] & 0x7F, values[0] >> 7]
    # A data byte above 127 would be read as a status byte and garble every message after it.
    if any(not 0 <= v <= 127 for v in values):
        raise ValueError(f"`{cmd}`: a data byte of {values} is not 0..127")
    return bytes([MIDI_STATUS[cmd] | (channel - 1), *values])


def drain(sock: socket.socket) -> bytes:
    """Everything waiting on the non-blocking `sock`."""
    got = b""
    while True:
        try:
            got += sock.recv(4096)
        except BlockingIOError:
            return got


class DinListener:
    """The DIN MIDI bytes an emulator sends to the non-blocking `sock`, received on a thread as they arrive. delugemu
    sends each byte as a datagram of its own, and a socket's receive queue holds a few hundred of those, so a long
    stretch of dense output (clock, MIDI follow's feedback) between two reads would overflow it."""

    def __init__(self, sock: socket.socket):
        self.sock = sock
        self.got = bytearray()
        self.lock = threading.Lock()
        self.done = threading.Event()
        self.thread = threading.Thread(target=self._receive, daemon=True)

    def __enter__(self):
        self.thread.start()
        return self

    def __exit__(self, *exc):
        self.done.set()
        self.thread.join()

    def _receive(self):
        while not self.done.is_set():
            select.select([self.sock], [], [], 0.05)
            with self.lock:
                self.got += drain(self.sock)

    def take(self) -> bytes:
        """Everything received since the previous call. Called with the emulator stopped, so every byte it sent before
        it stopped is here or still in the socket."""
        with self.lock:
            got = bytes(self.got + drain(self.sock))
            self.got.clear()
        return got


def qmp_socket(out: Path) -> str:
    """The QMP socket of the emulator whose artifacts go to `out`. There is one per emulator: they all run in this
    process, and delugemu's default name is per process. The name hashes `out`, which names the card, the script and
    the side, because a Unix socket's path is at most 107 bytes."""
    return f"/tmp/dz_parity_{os.getpid()}_{hashlib.sha256(str(out).encode()).hexdigest()[:16]}.sock"


def free_udp_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


def launch_emulator(
    elf: Path,
    sd: Path,
    sock: str,
    extra: list[str],
    out: Path,
    env: dict,
    audio: str,
    midi: str,
):
    """delugemu's `DelugeSession.launch`, with SCIF0 (DIN MIDI) on the chardev `midi`: `launch` has no option for it,
    and run.sh's `--midi` puts it on the first serial port, where the firmware's DIN UART is."""
    from delugemu import DelugeSession
    from delugemu.qmp import QmpClient
    from delugemu.session import find_repo_root

    root = find_repo_root()
    Path(sock).unlink(missing_ok=True)
    log = out / "run.log"
    cmd = [
        os.path.join(root, "scripts", "run.sh"),
        str(elf),
        "--sd",
        str(sd),
        "--display",
        "none",
        "--audio",
        audio,
        "--midi",
        midi,
        "--",
        "-qmp",
        f"unix:{sock},server,nowait",
        "-d",
        "int",
        "-D",
        str(out / "int.log"),
        *extra,
    ]
    with open(log, "w") as log_fh:  # the emulator keeps its own copy of the descriptor
        proc = subprocess.Popen(
            cmd,
            cwd=root,
            stdout=log_fh,
            stderr=subprocess.STDOUT,
            stdin=subprocess.DEVNULL,
            start_new_session=True,
            env=env,
        )
    deadline = time.monotonic() + 90
    while not os.path.exists(sock):
        if proc.poll() is not None or time.monotonic() > deadline:
            proc.kill()
            raise RuntimeError(f"run.sh did not start; see {log}")
        time.sleep(0.1)
    return DelugeSession(
        QmpClient(sock, connect_timeout=15.0),
        proc=proc,
        int_log=str(out / "int.log"),
        repo_root=root,
        artifacts_dir=str(out),
        run_log=str(log),
    )


def run_image(
    elf: Path,
    sd: Path,
    flash: Path | None,
    script: Path,
    out: Path,
    buttons,
    audio_wav: bool = False,
    session=None,
    din_out: bool = True,
) -> dict[str, list[dict]]:
    """Boot `elf`, play `script`, and return {snapshot name: the distinct panels it showed, first-seen order}.

    With `audio_wav`, the codec output goes to out/audio.wav (through delugemu's drift resampler) and, bit-exact as
    the firmware wrote its TX ring, to out/raw.s32 (S32LE stereo); out/times.json holds each snapshot's window in
    virtual ns.
    With `din_out` (delugemu only), every panel of a snapshot's window carries `midi`: the DIN MIDI bytes the image
    sent since the previous window ended, as hex.
    `session(out)`, when given, opens the context the script runs in instead of delugemu (tools/rig_session.py's
    rig device); `elf`, `sd`, `flash` and `audio_wav` then go unused."""
    out.mkdir(parents=True, exist_ok=True)
    # -snapshot: both images run on one card image; their writes go to a temporary overlay, not the image.
    extra = [
        "-icount",
        f"shift={ICOUNT_SHIFT},sleep=off",
        "-S",
        "-snapshot",
        "-global",
        f"rza1l-gpio.edge-spacing-ns={ENCODER_PHASE_NS}",
    ]
    if flash is not None:
        run_flash = out / "flash.bin"
        shutil.copyfile(flash, run_flash)
        extra += ["-global", f"rza1l-spibsc.flash-file={run_flash}"]
    sock = qmp_socket(out)
    steps = parse_script(script)
    check_steps(script, steps)
    snaps = {}
    random_pads: set[tuple[int, int]] = set()
    times = {}
    # QEMU keeps its -snapshot overlay in TMPDIR: on disk beside the artifacts, not in a RAM-backed /tmp.
    env = dict(os.environ, TMPDIR=str(out))
    if audio_wav:
        env["DELUGEMU_SSIF_DUMP"] = str(out / "raw.s32")

    # DIN MIDI: the emulator listens on `din_port` and sends what the image transmits to `din`.
    din = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    din.bind(("127.0.0.1", 0))
    din.setblocking(False)
    din_port = free_udp_port()
    compare_din = din_out

    def emulator(out: Path):
        nonlocal din_port
        # The port is free when picked but QEMU binds it later, so another run can take it first: pick again.
        for attempt in range(3):
            try:
                return launch_emulator(
                    elf,
                    sd,
                    sock,
                    extra,
                    out,
                    env,
                    # Nothing through the host's speakers: silence, or a WAV written on the virtual clock.
                    f"wav,path={out / 'audio.wav'}" if audio_wav else "none",
                    f"udp:127.0.0.1:{din.getsockname()[1]}@127.0.0.1:{din_port}",
                )
            except RuntimeError:
                if attempt == 2:
                    raise
                din_port = free_udp_port()

    def send_midi(dz, message: bytes):
        # The rig device takes MIDI over USB (tools/rig_session.py); the emulator over DIN.
        if hasattr(dz, "midi"):
            dz.midi(message.hex(" "))
        else:
            din.sendto(message, ("127.0.0.1", din_port))

    listener = DinListener(din) if compare_din else contextlib.nullcontext()
    with din, listener, (session or emulator)(out) as dz:
        run_until = dz.run_until
        reached = 0

        def run_until_or_record(ns, timeout=120.0):
            nonlocal reached
            # The VM is already there: a stop time in the past would only run it on to the next timer.
            if ns <= reached:
                return None
            reached = ns
            # A VM that never reaches its stop time leaves where it was and how far its clock got, in stall.txt.
            try:
                return run_until(ns, timeout)
            except TimeoutError:
                dz.q.execute("stop")
                (out / "stall.txt").write_text(
                    f"stop at {ns} ns, virtual clock {dz.vclock_ns()} ns\n"
                    + dz.q.hmp("info registers")
                )
                raise

        dz.run_until = run_until_or_record
        t0 = boot(dz)
        if compare_din:
            listener.take()  # boot's output belongs to no snapshot
        for i, (at_ms, cmd, args) in enumerate(steps):
            at = t0 + at_ms * MS
            dz.run_until(at)
            if cmd == "button":
                x, y = buttons[args[0]]
                dz.button(x, y, args[1] == "down")
            elif cmd == "pad":
                dz.pad_event(int(args[0]), int(args[1]), args[2] == "down")
            elif cmd == "midi" or cmd in MIDI_STATUS:
                send_midi(dz, midi_bytes(cmd, args))
            elif cmd == "random_colour":
                random_pads.add((int(args[0]), int(args[1])))
            elif cmd == "encoder":
                dz.encoder(ENCODER_ROLES.index(args[0]), int(args[1]))
            elif cmd == "load":
                end = load(dz, at, " ".join(args), buttons)
                if i + 1 < len(steps) and t0 + steps[i + 1][0] * MS < end:
                    raise ValueError(
                        f"{script.name}: `load` needs until {(end - t0) // MS} ms, before the next step"
                    )
            elif cmd == "snapshot":
                end = (
                    t0 + steps[i + 1][0] * MS
                    if i + 1 < len(steps)
                    else at + WINDOW_MS * MS
                )
                end = min(end, at + WINDOW_MS * MS)
                seen = {}
                take_flashes(dz)  # flashed before the window
                t = at
                while True:
                    p = panel(dz, random_pads)
                    t += SAMPLE_MS * MS
                    if t >= end:
                        # The flashes between the last sample and the window's end belong to the window too.
                        dz.run_until(end)
                        p["flashes"] += take_flashes(dz)
                        seen.setdefault(key(p), p)
                        break
                    seen.setdefault(key(p), p)
                    dz.run_until(t)
                if compare_din:
                    sent = listener.take().hex(" ")
                    for p in seen.values():
                        p["midi"] = sent
                snaps[args[0]] = list(seen.values())
                times[args[0]] = [at, end]
                dz.screenshot(str(out / f"{args[0]}.png"))
            elif cmd == "quit":
                break
        (out / "snapshots.json").write_text(json.dumps(snaps))
        (out / "times.json").write_text(json.dumps(times))
        try:
            dz.assert_no_aborts()
            # The interrupt log grows by every interrupt taken; only a faulting run's is worth keeping.
            (out / "int.log").unlink(missing_ok=True)
        except AssertionError:  # delugemu's AbortDetected
            # The firmware's fault report and log: the RTT up-buffer the device build places at 0x202EB000.
            dz.q.hmp(f'pmemsave 0x202eb000 16384 "{out / "rtt.bin"}"')
            raise
    return snaps


def describe(ref: dict, test: dict) -> list[str]:
    """How two single panels differ."""
    out = []
    if ref["oled"] != test["oled"]:
        n = sum(
            a != b for ra, rb in zip(ref["oled"], test["oled"]) for a, b in zip(ra, rb)
        )
        out.append(f"OLED: {n} pixels differ")
    pads = [
        (x, y, ref["pads"][x][y], test["pads"][x][y])
        for x in range(len(ref["pads"]))
        for y in range(len(ref["pads"][x]))
        if ref["pads"][x][y] != test["pads"][x][y]
    ]
    if pads:
        out.append(f"pads: {len(pads)} differ, first {pads[:4]}")
    leds = sorted(k for k in ref["leds"] if ref["leds"][k] != test["leds"].get(k))
    if leds:
        out.append(f"LEDs differ: {leds}")
    if ref["gold"] != test["gold"]:
        out.append(f"gold knob rings: {ref['gold']} vs {test['gold']}")
    return out


def parts(panels: list[dict]) -> dict[str, set[str]]:
    """The values each part of the panel showed over a window: the OLED, the pads, the knob rings, every LED on
    its own, the set of (x, y, colour) pad flashes, and each PIC setting on its own. Compared part by part, a
    blink's phase against another LED's, or a sample landing between two LED writes, is not a difference; every
    value either image showed still has to appear on both. Snapshots from an older run, without flashes or PIC
    settings, compare as having none."""
    out: dict[str, set[str]] = {}
    for p in panels:
        for name in ("oled", "pads", "gold"):
            out.setdefault(name, set()).add(json.dumps(p[name]))
        for led, value in p["leds"].items():
            out.setdefault(f"LED {led}", set()).add(json.dumps(value))
        for flash in p.get("flashes", ()):
            out.setdefault("flashes", set()).add(json.dumps(flash))
        for setting, value in p.get("pic", {}).items():
            out.setdefault(f"PIC {setting}", set()).add(json.dumps(value))
        if "midi" in p:
            out.setdefault("DIN MIDI out", set()).add(p["midi"])
    return out


def differences(ref: list[dict], test: list[dict]) -> list[str]:
    """How two snapshots' windows differ, part by part; empty when every part showed the same values on both."""
    a, b = parts(ref), parts(test)
    differing = sorted(
        name for name in a.keys() | b.keys() if a.get(name) != b.get(name)
    )
    if not differing:
        return []
    # Describe by the first panel of each window that shows a value the other image never showed.
    only_ref = next(
        (
            p
            for p in ref
            if any(
                json.dumps(p[n]) not in b.get(n, set())
                for n in ("oled", "pads", "gold")
            )
        ),
        ref[0],
    )
    only_test = next(
        (
            p
            for p in test
            if any(
                json.dumps(p[n]) not in a.get(n, set())
                for n in ("oled", "pads", "gold")
            )
        ),
        test[0],
    )
    out = [d for d in describe(only_ref, only_test) if not d.startswith("LEDs differ")]
    leds = [n for n in differing if n.startswith("LED ")]
    for name in leds:
        out.append(
            f"{name}: {sorted(a.get(name, set()))} vs {sorted(b.get(name, set()))}"
        )
    if "flashes" in differing:
        fa, fb = a.get("flashes", set()), b.get("flashes", set())
        out.append(
            "pad flashes (x, y, colour): "
            f"only ref {sorted(json.loads(f) for f in fa - fb)}, only test {sorted(json.loads(f) for f in fb - fa)}"
        )
    if "DIN MIDI out" in differing:
        out.append(
            f"DIN MIDI out: {sorted(a.get('DIN MIDI out', set()))} vs {sorted(b.get('DIN MIDI out', set()))}"
        )
    for name in (n for n in differing if n.startswith("PIC ")):
        out.append(
            f"{name}: {sorted(a.get(name, set()))} vs {sorted(b.get(name, set()))}"
        )
    for name in ("oled", "pads", "gold"):
        if name in differing and len(a.get(name, ())) != len(b.get(name, ())):
            out.append(
                f"{name}: {len(a.get(name, ()))} value(s) vs {len(b.get(name, ()))} over the window"
            )
    return out or ["the same values on both, in different panels"]


@functools.cache
def font_columns(source: Path = FONT_SOURCE):
    """{character: its glyph's columns} for `font_apple`; a column's bit n is pixel row n from the glyph's top."""
    with open(source, encoding="utf-8") as f:
        text = f.read()
    data = text[text.index("const uint8_t font_apple[]") :]
    data = data[: data.index("};")]
    columns = [int(bits, 2) for bits in re.findall(r"0b([01]+)", data)]
    desc = text[text.index("font_apple_desc[]") :]
    desc = desc[: desc.index("};")]
    entries = re.findall(r"\.w_px\s*=\s*(\d+),\s*\.glyph_index\s*=\s*(\d+)", desc)
    return {
        chr(code): columns[int(index) : int(index) + int(width)]
        for code, (width, index) in enumerate(entries, 0x20)
    }


def render_centred(text, font):
    """The 7 rows `Canvas::drawStringCentred` draws `text` in at the 7 px font: each character centred in its own
    `TEXT_SPACING_X` cell, the line centred on the panel."""
    rows = [["."] * OLED_WIDTH for _ in range(7)]
    x = OLED_WIDTH // 2 - (len(text) * TEXT_SPACING_X >> 1)
    for c in text:
        glyph = font[c] if c != " " else []
        left = x + ((TEXT_SPACING_X - len(glyph)) >> 1)
        for dx, column in enumerate(glyph):
            for row in range(7):
                if column >> row & 1 and 0 <= left + dx < OLED_WIDTH:
                    rows[row][left + dx] = "#"
        x += TEXT_SPACING_X
    return ["".join(row) for row in rows]


def oled_rows(oled: list) -> list[str]:
    """delugemu's OLED as text rows, '#' lit and '.' dark."""
    return ["".join("#" if v else "." for v in row) for row in oled]


def section_line_only(ref: list[str], test: list[str], font) -> bool:
    """Whether two OLED frames differ only as D77 describes: in the section line's rows alone, under the same name
    screen, every differing row blank on `main` and on ours that row of "SECTION n" as the firmware draws it. A row of
    the line that does not differ is one a popup covers on both sides."""
    if len(ref) != len(test) or len(ref) <= SECTION_LINE_ROWS[-1]:
        return False
    rows = [i for i, (a, b) in enumerate(zip(ref, test)) if a != b]
    if not rows or any(i not in SECTION_LINE_ROWS for i in rows):
        return False
    if not any(set(ref[i]) - {"."} for i in range(SECTION_LINE_ROWS[0])):
        return False  # no name screen above the line
    top = SECTION_LINE_ROWS[0]
    for section in range(1, 100):
        line = render_centred(f"SECTION {section}", font)
        if all(set(ref[i]) <= {"."} and test[i] == line[i - top] for i in rows) and all(
            set(ref[i]) - {"."} or set(line[i - top]) <= {"."}
            for i in SECTION_LINE_ROWS
            if i not in rows
        ):
            return True
    return False


def phase(ref: list[dict], test: list[dict]) -> bool:
    """Whether two windows end the same: every part of the panel (the OLED, the pads, the knob rings, each LED, each
    PIC setting and the DIN MIDI out) is the same in both last panels, so any difference is in the panels before the
    last (a scroll frame or a blink caught at another moment), and both images flashed the same pads, as a flash is an
    event rather than a state the window ends in."""
    last_ref, last_test = ref[-1], test[-1]
    return all(
        json.dumps(last_ref.get(name)) == json.dumps(last_test.get(name))
        for name in ("oled", "pads", "gold", "leds", "pic", "midi")
    ) and parts(ref).get("flashes") == parts(test).get("flashes")


def accepted(ref: list[dict], test: list[dict]) -> str | None:
    """The accepted DIFFERENCES.md row a snapshot's difference is, or None.

    D77: `main`'s `View::drawOutputNameFromDetails` draws the section line only when its caller passes a clip, and
    several callers pass none, so its first frame of a kit's name lacks the line ours draws from the model. A
    snapshot is D77 when, with the section line taken out of ours, it reads MATCH or PHASE (`phase`): a panel of ours
    that shows one of `main`'s OLED frames with only `SECTION n` added in the line's rows, over the same pads, LEDs
    and knob rings, reads as that frame. A window of ours with no such panel is not D77, whatever else differs."""
    font = font_columns()
    oleds = [r["oled"] for r in ref]
    stripped, any_stripped = [], False
    for p in test:
        under = (
            None
            if p["oled"] in oleds
            else next(
                (
                    r
                    for r in ref
                    if all(p[k] == r[k] for k in ("pads", "leds", "gold"))
                    and section_line_only(
                        oled_rows(r["oled"]), oled_rows(p["oled"]), font
                    )
                ),
                None,
            )
        )
        if under is None:
            stripped.append(p)
        else:
            stripped.append({**p, "oled": under["oled"]})
            any_stripped = True
    if any_stripped and (not differences(ref, stripped) or phase(ref, stripped)):
        return "D77"
    return None


def classify(
    ref: list[dict],
    test: list[dict],
    ref2: list[dict] | None,
    test2: list[dict] | None = None,
) -> str:
    """A snapshot's verdict: MATCH when the windows show the same values; PHASE when they differ only in when they
    showed them, which needs one of two things to be true: the windows end the same (`phase`); or a second run of
    either image (@p ref2, @p test2) differs from its first here too, so that image cannot repeat itself at this
    snapshot. A difference DIFFERENCES.md accepts by rule (`accepted`: D77) reads as that row; it is not a failure.
    Anything else is DIFF."""
    if not differences(ref, test):
        return "MATCH"
    if row := accepted(ref, test):
        return row
    if phase(ref, test):
        return "PHASE"
    if ref2 is not None and differences(ref, ref2):
        return "PHASE"
    if test2 is not None and differences(test, test2):
        return "PHASE"
    return "DIFF"


def expand(paths: list[Path]) -> list[Path]:
    """The scripts `paths` name: each file, and each directory's `*.txt` in name order."""
    found = []
    for path in paths:
        found += sorted(path.glob("*.txt")) if path.is_dir() else [path]
    return found


def legs(
    scripts: list[Path], card: Path | None, sd: Path | None
) -> list[tuple[str, Path, list[Path]]]:
    """(label, card, scripts) for each card the scripts run on, in first-seen order: `card` or `sd` for every script
    when given, else each script's `# card:` header (tools/ui_script_cards.py). A script with no header, and two
    cards with one label, are errors."""
    if card or sd:
        chosen = card or sd
        return [(ui_script_cards.label(str(chosen)), chosen, list(scripts))]
    grouped: dict[Path, tuple[str, list[Path]]] = {}
    # The images and the artifacts are named by label, so two cards with one label would run on one image.
    paths: dict[str, Path] = {}
    for script in scripts:
        named = ui_script_cards.cards(script)
        if not named:
            raise ValueError(f"{script}: no `# card:` header; give --card or --sd")
        for text in named:
            path, name = ui_script_cards.resolve(text), ui_script_cards.label(text)
            if paths.setdefault(name, path) != path:
                raise ValueError(f"{name}: one label for {paths[name]} and {path}")
            grouped.setdefault(path, (name, []))[1].append(script)
    return [(label, path, group) for path, (label, group) in grouped.items()]


def sd_image(card: Path, at: Path, delugemu: Path) -> tuple[Path, bool]:
    """An SD image of `card`, and whether it was made here (to delete after): a FAT image as it stands; a directory,
    or a card manifest built into one first, imaged at `at` by delugemu's mksd.sh."""
    if card.suffix == ".img":
        return card, False
    tree = ui_script_cards.tree(card, at.with_suffix(".tree"))
    at.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [str(delugemu / "scripts/mksd.sh"), str(tree), str(at)],
        check=True,
        stdout=subprocess.DEVNULL,
    )
    return at, True


def remove_card(sd: Path) -> None:
    """An SD image sd_image made, and the card tree it built the image from."""
    sd.unlink(missing_ok=True)
    shutil.rmtree(sd.with_suffix(".tree"), ignore_errors=True)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument(
        "--ref",
        type=Path,
        default=Path(
            os.environ.get("DELUGE_PARITY_REF")
            or REPO / ".superpowers/estate/emu/main.elf"
        ),
        help="reference firmware ELF, a main build (default: $DELUGE_PARITY_REF, else %(default)s)",
    )
    ap.add_argument(
        "--test",
        type=Path,
        default=REPO / "build/RelWithDebInfo/deluge.elf",
        help="firmware ELF under test (default: %(default)s)",
    )
    card = ap.add_mutually_exclusive_group()
    card.add_argument(
        "--card",
        type=Path,
        help="run every script on this card directory or card manifest instead of its header's cards",
    )
    card.add_argument(
        "--sd",
        type=Path,
        help="run every script on this SD image instead of its header's cards",
    )
    ap.add_argument(
        "--flash", type=Path, help="starting settings flash image (default: erased)"
    )
    ap.add_argument(
        "--delugemu",
        type=Path,
        default=Path(
            os.environ.get("DELUGEMU_ROOT") or Path.home() / "GitHub/delugemu"
        ),
    )
    ap.add_argument(
        "--out",
        type=Path,
        default=REPO / ".superpowers/emu-runs/latest",
        help="artifact directory, on disk: runs keep card overlays and logs there (default: %(default)s)",
    )
    ap.add_argument(
        "-j",
        "--jobs",
        type=int,
        default=1,
        help="scripts to run at once (each runs two emulators)",
    )
    ap.add_argument(
        "--audio-wav",
        action="store_true",
        help="write each image's audio to raw.s32 (bit-exact, as its TX ring held it) and audio.wav beside its "
        "snapshots",
    )
    ap.add_argument(
        "--keep-card",
        action="store_true",
        help="keep the SD images built from directories and manifests in the artifact directory (4 GB each; deleted "
        "after the run by default)",
    )
    ap.add_argument(
        "--calibrate",
        action="store_true",
        help="run each image a second time beside the two (four emulators a script), so a difference either "
        "image shows against itself reads PHASE",
    )
    ap.add_argument(
        "scripts",
        nargs="+",
        type=Path,
        help="scripts, or directories of them (each *.txt)",
    )
    args = ap.parse_args()
    # delugemu's run.sh runs from its own directory, so every path it is given is absolute.
    for name in ("ref", "test", "card", "sd", "flash", "out"):
        if getattr(args, name) is not None:
            setattr(args, name, getattr(args, name).resolve())

    if not args.ref.is_file():
        print(
            f"emu_parity: no reference image at {args.ref} (DELUGE_PARITY_REF or --ref)",
            file=sys.stderr,
        )
        return 4
    if not args.test.is_file():
        print(f"emu_parity: no image under test at {args.test}", file=sys.stderr)
        return 4
    sys.path.insert(0, str(args.delugemu / "pylib"))
    args.out.mkdir(parents=True, exist_ok=True)
    scripts = expand(args.scripts)
    missing = [str(s) for s in scripts if not s.is_file()]
    if missing:
        print(f"emu_parity: no such script {', '.join(missing)}", file=sys.stderr)
        return 4
    try:
        plan = legs(scripts, args.card, args.sd)
    except ValueError as e:
        print(f"emu_parity: {e}", file=sys.stderr)
        return 4
    images: dict[str, tuple[Path, bool]] = {}
    try:
        return run_legs(args, plan, images)
    finally:
        if not args.keep_card:
            for sd, made in images.values():
                if made:
                    remove_card(sd)


def run_legs(args, plan, images: dict[str, tuple[Path, bool]]) -> int:
    """Images each card of `plan` into `images` (the caller removes those made here) and compares every script on
    it; the exit code."""
    out = args.out
    for label, card, _ in plan:
        at = out / "cards" / f"{label}.img"
        try:
            images[label] = (
                (args.sd, False) if args.sd else sd_image(card, at, args.delugemu)
            )
        except subprocess.CalledProcessError as e:
            remove_card(at)
            print(
                f"emu_parity: card {label} ({card}) did not image: {e}", file=sys.stderr
            )
            return 4
    buttons = button_table()

    sides = [("ref", args.ref), ("test", args.test)] + (
        [("ref2", args.ref), ("test2", args.test)] if args.calibrate else []
    )

    def compare(job: tuple[str, Path]) -> dict[str, dict]:
        label, script = job
        sd = images[label][0]
        with concurrent.futures.ThreadPoolExecutor(len(sides)) as pool:
            pending = {
                side: pool.submit(
                    run_image,
                    elf,
                    sd,
                    args.flash,
                    script,
                    out / label / script.stem / side,
                    buttons,
                    args.audio_wav,
                )
                for side, elf in sides
            }
            got = {}
            for side, future in pending.items():
                try:
                    got[side] = future.result()
                except (
                    AssertionError,
                    RuntimeError,
                    OSError,
                    KeyError,
                    ValueError,
                    TypeError,
                ) as e:
                    got[side] = e  # a fault, the emulator, the script
        errors = [
            f"{side}: {r}" for side, r in got.items() if isinstance(r, BaseException)
        ]
        if errors:
            return {"run": {"verdict": "DIFF", "diff": errors}}
        return {
            name: {
                "verdict": classify(
                    got["ref"][name],
                    got["test"][name],
                    got.get("ref2", {}).get(name),
                    got.get("test2", {}).get(name),
                ),
                "diff": differences(got["ref"][name], got["test"][name]),
            }
            for name in got["ref"]
        }

    jobs = [(label, script) for label, _, group in plan for script in group]
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = dict(zip(jobs, pool.map(compare, jobs)))
    runs = Counter(script for _, script in jobs)
    report, failed = {}, False
    for (label, script), snaps in results.items():
        stem = script.stem if runs[script] == 1 else f"{script.stem}@{label}"
        for name, result in snaps.items():
            report[f"{stem}/{name}"] = result
            diff = result["diff"]
            print(
                f"{result['verdict']:5} {stem}/{name}"
                + ("" if not diff else "  " + "; ".join(diff))
            )
            failed |= result["verdict"] == "DIFF"
    (out / "report.json").write_text(json.dumps(report, indent=1))
    print(f"emu_parity: artifacts in {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
