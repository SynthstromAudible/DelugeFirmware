#!/usr/bin/env python3
"""Reads the audio-timing probe's AUDIO-TIMING and USB-AUDIO-TIMING lines from stream-bench captures.

`report CAPTURE...` prints, per stream-bench phase, the DSP load, the worst render, the closest the
queue came to running dry, overruns, the render task's worst wake lateness and the drive's overruns,
then each USB audio stream's frames, late services, missed microframes and longest gap (`in` to the
host, `out` from it), and the frames lost: those sent to the host less those that came from it. That
difference means loss only while a host that follows our implicit feedback runs both streams.
`overhead --small A --large B` fits each render's fixed cost and its cost per frame from two captures
whose renders are of two sizes (the drive renders 128 frames, so one of them comes from an image whose
drive renders another size).

A line belongs to the stream-bench phase whose STREAM-BENCH-PHASE line follows it;
lines before STREAM-BENCH-LOADED are the boot's and are left out.
"""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from pathlib import Path

SAMPLE_RATE = 44_100
TIMING = re.compile(r"(?<!USB-)AUDIO-TIMING (.*)$")
USB_TIMING = re.compile(r"USB-AUDIO-TIMING (.*)$")
PHASE = re.compile(r"STREAM-BENCH-PHASE phase=(\S+)")
LOADED = re.compile(r"STREAM-BENCH-LOADED ")


@dataclass
class UsbStream:
    packets: int = 0
    frames: int = 0
    late: int = 0
    missed: int = 0
    max_gap_us: int = 0

    def add(self, f: dict[str, str], name: str) -> None:
        self.packets += int(f[f"{name}_packets"])
        self.frames += int(f[f"{name}_frames"])
        self.late += int(f[f"{name}_late"])
        self.missed += int(f[f"{name}_missed"])
        self.max_gap_us = max(self.max_gap_us, int(f[f"{name}_max_gap_us"]))


@dataclass
class Phase:
    renders: int = 0
    frames: int = 0
    us: int = 0
    max_us: int = 0
    max_us_frames: int = 0
    min_queued: int | None = None
    min_slack: int | None = None
    overruns: int = 0
    max_wake_late_us: int = 0
    # None until a line from a fixed-block drive image (which reports it) is added.
    drive_overruns: int | None = None
    # None until a line from an image that reports the USB streams is added.
    usb_in: UsbStream | None = None
    usb_out: UsbStream | None = None

    def add(self, f: dict[str, str]) -> None:
        self.renders += int(f["renders"])
        self.frames += int(f["frames"])
        self.us += int(f["us"])
        if int(f["max_us"]) > self.max_us:
            self.max_us = int(f["max_us"])
            self.max_us_frames = int(f["max_us_frames"])
        q, s = int(f["min_queued"]), int(f["min_slack"])
        self.min_queued = q if self.min_queued is None else min(self.min_queued, q)
        self.min_slack = s if self.min_slack is None else min(self.min_slack, s)
        self.overruns += int(f["overruns"])
        self.max_wake_late_us = max(self.max_wake_late_us, int(f["max_wake_late_us"]))
        if "drive_overruns" in f:
            self.drive_overruns = (self.drive_overruns or 0) + int(f["drive_overruns"])

    def add_usb(self, f: dict[str, str]) -> None:
        if self.usb_in is None or self.usb_out is None:
            self.usb_in, self.usb_out = UsbStream(), UsbStream()
        self.usb_in.add(f, "in")
        self.usb_out.add(f, "out")

    @property
    def usb_lost_frames(self) -> int | None:
        """Frames sent to the host less those that came from it (see the module docstring)."""
        if self.usb_in is None or self.usb_out is None:
            return None
        return self.usb_in.frames - self.usb_out.frames

    @property
    def load(self) -> float:
        """Render time over the real time the rendered frames last."""
        return (self.us / 1e6) / (self.frames / SAMPLE_RATE) if self.frames else 0.0

    @property
    def us_per_render(self) -> float:
        return self.us / self.renders if self.renders else 0.0

    @property
    def frames_per_render(self) -> float:
        return self.frames / self.renders if self.renders else 0.0


def fields(text: str) -> dict[str, str]:
    return dict(word.split("=", 1) for word in text.split() if "=" in word)


def parse(lines) -> dict[str, Phase]:
    phases: dict[str, Phase] = {}
    pending: list[dict[str, str]] = []
    pending_usb: list[dict[str, str]] = []
    loaded = False
    for line in lines:
        if LOADED.search(line):
            loaded = True
            pending, pending_usb = [], []
            continue
        m = USB_TIMING.search(line)
        if m:
            if loaded:
                pending_usb.append(fields(m.group(1)))
            continue
        m = TIMING.search(line)
        if m:
            if loaded:
                pending.append(fields(m.group(1)))
            continue
        m = PHASE.search(line)
        if m and loaded:
            phase = phases.setdefault(m.group(1), Phase())
            for f in pending:
                phase.add(f)
            for f in pending_usb:
                phase.add_usb(f)
            pending, pending_usb = [], []
    return phases


def overhead(small: Phase, large: Phase) -> tuple[float, float]:
    """(fixed µs per render, µs per frame) from the mean render at two sizes."""
    per_frame = (large.us_per_render - small.us_per_render) / (
        large.frames_per_render - small.frames_per_render
    )
    return small.us_per_render - per_frame * small.frames_per_render, per_frame


def read(path: str) -> dict[str, Phase]:
    return parse(Path(path).read_text(errors="replace").splitlines())


def usb_columns(s: UsbStream | None) -> list[str]:
    if s is None:
        return ["-"] * 4
    return [str(v) for v in (s.frames, s.late, s.missed, s.max_gap_us)]


def row(capture: str, name: str, p: Phase) -> str:
    """One phase's row of the report."""
    cells = [
        capture,
        name,
        f"{p.load:.0%}",
        str(p.renders),
        f"{p.frames_per_render:.1f}",
        f"{p.us_per_render:.0f}",
        f"{p.max_us} ({p.max_us_frames})",
        str(p.min_queued),
        str(p.min_slack),
        str(p.overruns),
        str(p.max_wake_late_us),
        "-" if p.drive_overruns is None else str(p.drive_overruns),
        *usb_columns(p.usb_in),
        *usb_columns(p.usb_out),
        "-" if p.usb_lost_frames is None else str(p.usb_lost_frames),
    ]
    return f"| {' | '.join(cells)} |"


HEADERS = [
    "capture",
    "phase",
    "load",
    "renders",
    "frames/render",
    "µs/render",
    "worst µs (frames)",
    "min queued",
    "min slack",
    "overruns",
    "max wake late µs",
    "drive overruns",
    "in frames",
    "in late",
    "in missed",
    "in max gap µs",
    "out frames",
    "out late",
    "out missed",
    "out max gap µs",
    "lost frames",
]


def report(paths: list[str]) -> None:
    print(f"| {' | '.join(HEADERS)} |")
    print("|---|---|" + "---:|" * (len(HEADERS) - 2))
    for path in paths:
        for name, p in read(path).items():
            print(row(Path(path).parent.name, name, p))


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    sub = ap.add_subparsers(dest="command", required=True)
    r = sub.add_parser("report")
    r.add_argument("captures", nargs="+")
    o = sub.add_parser("overhead")
    o.add_argument("--small", required=True)
    o.add_argument("--large", required=True)
    o.add_argument("--phase", default="steady")
    args = ap.parse_args(argv)
    if args.command == "report":
        report(args.captures)
        return 0
    fixed, per_frame = overhead(
        read(args.small)[args.phase], read(args.large)[args.phase]
    )
    print(
        f"phase {args.phase}: {fixed:.0f} µs per render + {per_frame:.2f} µs per frame"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
