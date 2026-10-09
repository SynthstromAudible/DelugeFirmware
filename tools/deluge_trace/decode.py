#!/usr/bin/env python3
"""Decode a capture with OpenCSD: write the snapshot directory trc_pkt_lister reads, then run it.

The snapshot (OpenCSD's DS-5 snapshot format, decoder/tests/snapshot_parser_lib) describes:
  * the core: a Cortex-A9 (ARMv7-A), with the image's executable segments as its memory, so the
    decoder can follow the program between the PTM's waypoints;
  * the trace source: a PFT 1.0 PTM with the ETMCR, ETMIDR, ETMCCER and ETMTRACEIDR values the capture
    programmed and read back;
  * the buffer: the port's CoreSight-formatted frames, frame-aligned (frames.bin), which OpenCSD's
    deformatter splits by trace ID.

The J-Trace PRO's RAWTRACE buffer is the port's byte stream reversed, newest byte first: frame
synchronisations read 7F FF FF FF and each frame's auxiliary byte comes first. normalize() reverses
it, finds the FSYNCs (the TPIU's continuous mode puts one between frames every 64 frames) and drops
them.

trc_pkt_lister's -decode output lists every packet and every generic trace element. An executed range
of instructions is one element, e.g.

    OCSD_GEN_TRC_ELEM_INSTR_RANGE(exec range=0x200f35a2:[0x200f35ae] num_i(3) last_sz(2) (ISA=T32) E BR  <cond> [CC=9]; )

and with cycle-accurate trace each carries the cycles since the previous waypoint ([CC=n]).

trc_pkt_lister is OpenCSD's own test program. On pi-top it is /usr/local/bin/trc_pkt_lister, built
against the libopencsd 1.8.3 in /usr/local/lib, while the dynamic loader finds an older system copy
first; the library directory given by --opencsd-lib (default /usr/local/lib, when it exists) goes on
LD_LIBRARY_PATH to pick the matching one. Without OpenCSD here, build it:

    git clone https://github.com/Linaro/OpenCSD && cd OpenCSD/decoder/build/linux
    make -j8 && sudo make install          # trc_pkt_lister and libopencsd into /usr/local

or decode on the Pi (deluge_trace.py run --host pi-top does).
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from elf import Elf

CORE_NAME = "cpu_0"
SOURCE_NAME = "PTM_0"
BUFFER_NAME = "TPIU_0"
CORE_TYPE = "Cortex-A9"
SOURCE_TYPE = "PFT1.0"
DEFAULT_LISTER = "trc_pkt_lister"
DEFAULT_OPENCSD_LIB = "/usr/local/lib"

# The PTM registers the decoder is configured from; the register number is the PTM's (offset / 4).
FSYNC = b"\xff\xff\xff\x7f"
FRAME = 16

SNAPSHOT_REGISTERS = (
    ("ETMCR", 0x000),
    ("ETMIDR", 0x079),
    ("ETMCCER", 0x07A),
    ("ETMTRACEIDR", 0x080),
)


def normalize(raw: bytes) -> tuple[bytes, dict[str, int]]:
    """The J-Trace's RAWTRACE bytes as frame-aligned CoreSight frames in port order, and counts of what
    was dropped: bytes after the last frame synchronisation (frame alignment unknown) and ragged ends
    of segments.

    RAWTRACE_Read returns the capture newest byte first: the whole buffer reversed. Reversed back, it
    is the port's byte stream, with the TPIU's frame synchronisations (FF FF FF 7F) between frames.
    """
    stream = raw[::-1]
    stats = {"fsyncs": 0, "frames": 0, "lead_bytes": 0, "ragged_bytes": 0}
    first = stream.find(FSYNC)
    if first < 0:
        return b"", stats | {"lead_bytes": len(stream)}
    stats["lead_bytes"] = first
    segments = stream[first:].split(FSYNC)
    stats["fsyncs"] = len(segments) - 1
    out = bytearray()
    for segment in segments:
        whole = len(segment) - len(segment) % FRAME
        stats["ragged_bytes"] += len(segment) - whole
        out += segment[:whole]
        stats["frames"] += whole // FRAME
    return bytes(out), stats


def snapshot_files(
    ptm: dict[str, int],
    dumps: list[tuple[int, str]],
    trace_file: str,
    pc: int,
) -> dict[str, str]:
    """The snapshot's ini files, by name, for the PTM register values and memory dumps given."""
    snapshot = (
        "[snapshot]\nversion=1.0\n\n"
        "[device_list]\n"
        f"device0={CORE_NAME}.ini\n"
        f"device1={SOURCE_NAME}.ini\n\n"
        "[trace]\nmetadata=trace.ini\n"
    )
    core = (
        f"[device]\nname={CORE_NAME}\nclass=core\ntype={CORE_TYPE}\n\n"
        f"[regs]\nR15=0x{pc:08X}\nCPSR=0x000001DF\n"
    )
    for k, (address, name) in enumerate(dumps, start=1):
        core += f"\n[dump{k}]\nspace=S\naddress=0x{address:08X}\nfile={name}\n"
    source = f"[device]\nname={SOURCE_NAME}\nclass=trace_source\ntype={SOURCE_TYPE}\n\n[regs]\n"
    for reg, number in SNAPSHOT_REGISTERS:
        source += f"{reg}(0x{number:03X})=0x{ptm[reg]:08X}\n"
    trace = (
        "[trace_buffers]\nbuffers=buffer0\n\n"
        f"[buffer0]\nname={BUFFER_NAME}\nfile={trace_file}\nformat=coresight\n\n"
        f"[source_buffers]\n{SOURCE_NAME}={BUFFER_NAME}\n\n"
        f"[core_trace_sources]\n{CORE_NAME}={SOURCE_NAME}\n"
    )
    return {
        "snapshot.ini": snapshot,
        f"{CORE_NAME}.ini": core,
        f"{SOURCE_NAME}.ini": source,
        "trace.ini": trace,
    }


def write_snapshot(capture: Path, elf: Path) -> Path:
    """Write <capture>/snapshot from the capture's meta.json and trace.bin and the ELF's code."""
    meta = json.loads((capture / "meta.json").read_text())
    ptm = {k: int(v, 16) for k, v in meta["ptm"].items()}
    snap = capture / "snapshot"
    snap.mkdir(exist_ok=True)
    image = Elf(elf)
    dumps = []
    for k, seg in enumerate(s for s in image.segments() if s.executable):
        name = f"mem_{k}.bin"
        (snap / name).write_bytes(seg.data)
        dumps.append((seg.vaddr, name))
    frames, stats = normalize((capture / "trace.bin").read_bytes())
    (snap / "frames.bin").write_bytes(frames)
    (snap / "normalize.json").write_text(json.dumps(stats, indent=2) + "\n")
    for name, text in snapshot_files(ptm, dumps, "frames.bin", image.entry).items():
        (snap / name).write_text(text)
    return snap


def lister_env(opencsd_lib: str | None) -> dict[str, str]:
    env = dict(os.environ)
    if opencsd_lib and Path(opencsd_lib).is_dir():
        env["LD_LIBRARY_PATH"] = opencsd_lib + (
            ":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else ""
        )
    return env


def run_lister(
    snap: Path, out: Path, trace_id: int, lister: str, opencsd_lib: str | None
) -> int:
    """Run trc_pkt_lister over the snapshot, its full decode into `out`; return its exit code."""
    exe = shutil.which(lister) or (lister if Path(lister).exists() else None)
    if exe is None:
        print(
            f"decode: {lister} not found. Build OpenCSD (see decode.py's docstring) or decode on the Pi.",
            file=sys.stderr,
        )
        return 3
    cmd = [
        exe,
        "-ss_dir",
        str(snap),
        "-id",
        f"0x{trace_id:02X}",
        "-decode",
        "-no_time_print",
        "-logfilename",
        str(out),
    ]
    proc = subprocess.run(
        cmd, env=lister_env(opencsd_lib), capture_output=True, text=True, check=False
    )
    if proc.returncode:
        print(proc.stdout[-2000:], proc.stderr[-2000:], file=sys.stderr)
    return proc.returncode


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Decode a PTM capture with OpenCSD.")
    ap.add_argument("capture", help="the capture directory (trace.bin, meta.json)")
    ap.add_argument("--elf", required=True, help="the image that ran")
    ap.add_argument("--lister", default=DEFAULT_LISTER)
    ap.add_argument("--opencsd-lib", default=DEFAULT_OPENCSD_LIB)
    args = ap.parse_args(argv)

    capture = Path(args.capture)
    meta = json.loads((capture / "meta.json").read_text())
    if not meta.get("trace_bytes"):
        print("decode: the capture holds no trace bytes", file=sys.stderr)
        return 2
    snap = write_snapshot(capture, Path(args.elf))
    out = capture / "decode.txt"
    out.unlink(missing_ok=True)  # trc_pkt_lister appends to its log file
    code = run_lister(snap, out, meta["trace_id"], args.lister, args.opencsd_lib)
    if code == 0:
        print(f"decode: wrote {out}", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main())
