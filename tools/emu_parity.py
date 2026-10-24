#!/usr/bin/env python3
"""emu_parity.py -- run two firmware images side by side in delugemu and require identical panels.

A user must not be able to tell two builds apart by looking at the Deluge. This boots a reference image and a
test image in the delugemu emulator (~/GitHub/delugemu) on the same SD card and the same starting settings flash,
drives both through the same level-3 UI scripts (format in tests/harness/README.md), and at every `snapshot` step
compares what each shows: the OLED bitmap, every pad's colour, the button LEDs and the gold-knob rings.

Runs are deterministic. Each emulator runs paused under an instruction-counted clock (-icount sleep=off), and the
harness advances it to exact virtual times: boot is the first virtual instant the panel is lit and then unchanged
for BOOT_QUIET_MS, script times are virtual milliseconds after it, and every input lands on its millisecond. A
snapshot samples the panel every SAMPLE_MS from its time until the next step (at most WINDOW_MS) and compares the
set of states each image showed, so a blinking LED compares by what it shows, not by its phase at one instant.

    tools/emu_parity.py --ref main.elf --test build/RelWithDebInfo/deluge.elf \\
        --card tests/fixtures/card -j 4 tests/ui-scripts/parity/*.txt

Exit codes: 0 every snapshot matched; 1 a snapshot differed or a run failed.
"""

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

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


def panel(dz) -> dict:
    return {
        "oled": dz.read_oled(),
        "pads": dz.read_pads(),
        "leds": dz.read_leds(),
        "gold": dz.read_gold_knob(),
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
        still = key({k: v for k, v in now.items() if k != "oled"})
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


def run_image(
    elf: Path,
    sd: Path,
    flash: Path | None,
    script: Path,
    out: Path,
    buttons,
    audio_wav: bool = False,
) -> dict[str, list[dict]]:
    """Boot `elf`, play `script`, and return {snapshot name: the distinct panels it showed, first-seen order}.

    With `audio_wav`, the codec output goes to out/audio.wav (through delugemu's drift resampler) and, bit-exact as
    the firmware wrote its TX ring, to out/raw.s32 (S32LE stereo); out/times.json holds each snapshot's window in
    virtual ns."""
    from delugemu import DelugeSession

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
    # One QMP socket per emulator: they all run in this process, and the default name is per process.
    sock = f"/tmp/dz_parity_{os.getpid()}_{out.parent.name}_{out.name}.sock"
    steps = parse_script(script)
    snaps = {}
    times = {}
    # QEMU keeps its -snapshot overlay in TMPDIR: on disk beside the artifacts, not in a RAM-backed /tmp.
    env = dict(os.environ, TMPDIR=str(out))
    if audio_wav:
        env["DELUGEMU_SSIF_DUMP"] = str(out / "raw.s32")
    with DelugeSession.launch(
        firmware=str(elf),
        sd=str(sd),
        boot_wait=0.0,
        sock=sock,
        extra_qemu=tuple(extra),
        artifacts_dir=str(out),
        int_log=str(out / "int.log"),
        env=env,
        # Nothing through the host's speakers: silence, or a WAV written on the virtual clock.
        audio=f"wav,path={out / 'audio.wav'}" if audio_wav else "none",
    ) as dz:
        run_until = dz.run_until

        def run_until_or_record(ns, timeout=120.0):
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
        for i, (at_ms, cmd, args) in enumerate(steps):
            at = t0 + at_ms * MS
            dz.run_until(at)
            if cmd == "button":
                x, y = buttons[args[0]]
                dz.button(x, y, args[1] == "down")
            elif cmd == "pad":
                dz.pad_event(int(args[0]), int(args[1]), args[2] == "down")
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
                t = at
                while True:
                    p = panel(dz)
                    seen.setdefault(key(p), p)
                    t += SAMPLE_MS * MS
                    if t >= end:
                        break
                    dz.run_until(t)
                snaps[args[0]] = list(seen.values())
                times[args[0]] = [at, end]
                dz.screenshot(str(out / f"{args[0]}.png"))
            elif cmd == "quit":
                break
            # `board`, `released`, `model`, `notice`: not panel input on a real Deluge.
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
    """The values each part of the panel showed over a window: the OLED, the pads, the knob rings, and every LED
    on its own. Compared part by part, a blink's phase against another LED's, or a sample landing between two LED
    writes, is not a difference; every value either image showed still has to appear on both."""
    out: dict[str, set[str]] = {}
    for p in panels:
        for name in ("oled", "pads", "gold"):
            out.setdefault(name, set()).add(json.dumps(p[name]))
        for led, value in p["leds"].items():
            out.setdefault(f"LED {led}", set()).add(json.dumps(value))
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
    for name in ("oled", "pads", "gold"):
        if name in differing and len(a.get(name, ())) != len(b.get(name, ())):
            out.append(
                f"{name}: {len(a.get(name, ()))} value(s) vs {len(b.get(name, ()))} over the window"
            )
    return out or ["the same values on both, in different panels"]


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument(
        "--ref",
        required=True,
        type=Path,
        help="reference firmware ELF (e.g. a main build)",
    )
    ap.add_argument("--test", required=True, type=Path, help="firmware ELF under test")
    card = ap.add_mutually_exclusive_group(required=True)
    card.add_argument(
        "--card",
        type=Path,
        help="SD card content directory (imaged with delugemu's mksd.sh)",
    )
    card.add_argument("--sd", type=Path, help="ready-made SD image")
    ap.add_argument(
        "--flash", type=Path, help="starting settings flash image (default: erased)"
    )
    ap.add_argument("--delugemu", type=Path, default=Path.home() / "GitHub/delugemu")
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
        help="write each image's audio to raw.s32 (bit-exact, as its TX ring held it) and audio.wav beside its snapshots",
    )
    ap.add_argument("scripts", nargs="+", type=Path)
    args = ap.parse_args()
    # delugemu's run.sh runs from its own directory, so every path it is given is absolute.
    for name in ("ref", "test", "card", "sd", "flash", "out"):
        if getattr(args, name) is not None:
            setattr(args, name, getattr(args, name).resolve())

    sys.path.insert(0, str(args.delugemu / "pylib"))
    out = args.out
    out.mkdir(parents=True, exist_ok=True)
    sd = args.sd
    if sd is None:
        sd = out / "card.img"
        subprocess.run(
            [str(args.delugemu / "scripts/mksd.sh"), str(args.card), str(sd)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
    buttons = button_table()

    def compare(script: Path) -> dict[str, list[str]]:
        with concurrent.futures.ThreadPoolExecutor(2) as pool:
            jobs = {
                side: pool.submit(
                    run_image,
                    elf,
                    sd,
                    args.flash,
                    script,
                    out / script.stem / side,
                    buttons,
                    args.audio_wav,
                )
                for side, elf in (("ref", args.ref), ("test", args.test))
            }
            got = {}
            for side, job in jobs.items():
                try:
                    got[side] = job.result()
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
            return {"run": errors}
        return {
            name: differences(got["ref"][name], got["test"][name])
            for name in got["ref"]
        }

    report = {}
    failed = False
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        results = dict(zip(args.scripts, pool.map(compare, args.scripts)))
    for script, snaps in results.items():
        for name, diff in snaps.items():
            report[f"{script.stem}/{name}"] = diff
            status = "MATCH" if not diff else "DIFF"
            print(
                f"{status:5} {script.stem}/{name}"
                + ("" if not diff else "  " + "; ".join(diff))
            )
            failed |= bool(diff)
    (out / "report.json").write_text(json.dumps(report, indent=1))
    print(f"emu_parity: artifacts in {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
