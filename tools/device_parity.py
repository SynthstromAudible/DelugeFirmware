#!/usr/bin/env python3
"""Run level-3 UI scripts on the real Deluge on the J-Trace rig and compare what its panel shows with delugemu.

Usage:
    tools/device_parity.py --emu-ref ELF --device HARNESS_ELF (--card DIR | --sd IMG) [--host HOST]
                           [--flash FILE] [--out DIR] [-j N] scripts...

The device side boots HARNESS_ELF (a `rig-harness` build: tools/bench_image.py --features rig-harness) once per
script through tools/rig_session.py and drives it in real time; the reference side runs --emu-ref in delugemu,
on the card --card/--sd describes, which must hold what the rig's SD card holds. Both go through
tools/emu_parity.py's script runner and its comparison, so the output is emu_parity's: one MATCH/DIFF line per
snapshot and DIR/report.json.

Settings: unless --flash names a flash image, the rig's own settings sector is read (read only: the core is
halted for the copy, and QSPI is never written) and delugemu starts from it, so a difference in saved settings
does not show as a UI difference. Panel state the device cannot report the same way as delugemu: the pads
during a PIC scroll animation (the PIC moves them itself; tools/rig_session.py records when it scrolled).

Exit codes: 0 every snapshot matched; 1 a snapshot differed or a run failed; 3 the rig failed; 4 usage.
"""

from __future__ import annotations

import argparse
import base64
import concurrent.futures
import json
import os
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools"))

import emu_parity
import rig
from rig_session import DeviceSession

# libdeluge's settings sector: the flash offset (flash::DELUGE_SETTINGS_OFFSET), its size, and the uncached
# window the flash is memory-mapped at.
SETTINGS_OFFSET = 0x7F000
SETTINGS_BYTES = 0x1000
UNCACHED_FLASH = 0x5800_0000


def settings_flash_image(sector: bytes) -> bytes:
    """A delugemu flash file (the array by flash address, erased elsewhere) holding `sector` as the settings."""
    if len(sector) != SETTINGS_BYTES:
        raise ValueError(
            f"settings read gave {len(sector)} bytes, not {SETTINGS_BYTES}"
        )
    if not any(sector):
        raise ValueError("settings read all zero: the flash window was not readable")
    return b"\xff" * SETTINGS_OFFSET + sector


def read_device_settings(host: str, elf: Path, out: Path) -> bytes:
    """Boots `elf` so the firmware puts the flash window back in memory-mapped mode, then copies the settings
    sector through JLinkExe on the probe host with the core halted, and lets it run on."""
    with DeviceSession.launch(elf, host, out / "settings-boot") as dz:
        dz.close(park=False)
    addr = UNCACHED_FLASH + SETTINGS_OFFSET
    commands = f"connect\\nh\\nsavebin /tmp/dz_settings.bin 0x{addr:x} 0x{SETTINGS_BYTES:x}\\ng\\nexit\\n"
    script = (
        f"{rig.STOP}; printf '{commands}' | timeout 30 JLinkExe -device {rig.DEVICE} -if SWD -speed 4000 "
        "-autoconnect 1 > /tmp/jlinkexe-settings.log 2>&1; base64 -w0 /tmp/dz_settings.bin"
    )
    got = rig.ssh(host, script)
    if got.returncode != 0:
        raise ConnectionError(
            f"device_parity: settings read failed: {got.stderr.strip()[-300:]}"
        )
    return base64.b64decode(got.stdout.strip())


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument(
        "--emu-ref", required=True, type=Path, help="the image delugemu runs"
    )
    ap.add_argument(
        "--device", required=True, type=Path, help="the rig-harness image the rig runs"
    )
    card = ap.add_mutually_exclusive_group(required=True)
    card.add_argument(
        "--card", type=Path, help="SD content directory matching the rig's card"
    )
    card.add_argument("--sd", type=Path, help="SD image matching the rig's card")
    ap.add_argument(
        "--host",
        default=os.environ.get("DELUGE_PROBE_HOST"),
        help="probe host (ssh destination)",
    )
    ap.add_argument(
        "--flash",
        type=Path,
        help="delugemu's starting flash (default: the rig's settings)",
    )
    ap.add_argument("--delugemu", type=Path, default=Path.home() / "GitHub/delugemu")
    ap.add_argument(
        "--out", type=Path, default=REPO / ".superpowers/emu-runs/device-latest"
    )
    ap.add_argument("-j", "--jobs", type=int, default=4, help="delugemu runs at once")
    ap.add_argument("scripts", nargs="+", type=Path)
    args = ap.parse_args(argv)
    if not args.host:
        print(
            "device_parity: no --host and $DELUGE_PROBE_HOST is unset", file=sys.stderr
        )
        return 4
    for name in ("emu_ref", "device", "card", "sd", "flash", "out"):
        if getattr(args, name) is not None:
            setattr(args, name, getattr(args, name).resolve())
    sys.path.insert(0, str(args.delugemu / "pylib"))
    out = args.out
    out.mkdir(parents=True, exist_ok=True)

    flash = args.flash
    if flash is None:
        try:
            image = settings_flash_image(
                read_device_settings(args.host, args.device, out)
            )
        except (ConnectionError, TimeoutError, ValueError) as exc:
            print(f"device_parity: {exc}", file=sys.stderr)
            return 3
        flash = out / "flash.bin"
        flash.write_bytes(image)

    sd = args.sd
    if sd is None:
        sd = out / "card.img"
        subprocess.run(
            [str(args.delugemu / "scripts/mksd.sh"), str(args.card), str(sd)],
            check=True,
            stdout=subprocess.DEVNULL,
        )
    buttons = emu_parity.button_table()

    def reference(script: Path):
        return emu_parity.run_image(
            args.emu_ref, sd, flash, script, out / script.stem / "ref", buttons
        )

    def device(script: Path, park: bool):
        def session(o: Path):
            dz = DeviceSession.launch(args.device, args.host, o)
            dz.park_on_close = park
            return dz

        return emu_parity.run_image(
            None,
            None,
            None,
            script,
            out / script.stem / "test",
            buttons,
            session=session,
        )

    report, failed = {}, False
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as pool:
        refs = {s: pool.submit(reference, s) for s in args.scripts}
        for i, script in enumerate(args.scripts):
            got = {}
            try:
                got["test"] = device(script, park=i == len(args.scripts) - 1)
            except (RuntimeError, TimeoutError, ConnectionError, AssertionError) as exc:
                got["test"] = exc
            try:
                got["ref"] = refs[script].result()
            except (
                AssertionError,
                RuntimeError,
                OSError,
                KeyError,
                ValueError,
                TypeError,
            ) as exc:
                got["ref"] = exc
            errors = [
                f"{side}: {r}"
                for side, r in got.items()
                if isinstance(r, BaseException)
            ]
            snaps = (
                {"run": errors}
                if errors
                else {
                    name: emu_parity.differences(got["ref"][name], got["test"][name])
                    for name in got["ref"]
                }
            )
            for name, diff in snaps.items():
                report[f"{script.stem}/{name}"] = diff
                status = "MATCH" if not diff else "DIFF"
                print(
                    f"{status:5} {script.stem}/{name}"
                    + ("" if not diff else "  " + "; ".join(diff)),
                    flush=True,
                )
                failed |= bool(diff)
    if args.sd is None:
        sd.unlink()
    (out / "report.json").write_text(json.dumps(report, indent=1))
    print(f"device_parity: artifacts in {out}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
