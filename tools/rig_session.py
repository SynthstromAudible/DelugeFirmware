#!/usr/bin/env python3
"""Drive a rig-harness image on the J-Trace rig as if it were a delugemu session.

`DeviceSession` speaks the rig harness's RTT protocol (src/bsp/rust/src/peripherals/rig_harness/: `wire.rs`
for the `@` lines the device sends, `input.rs` for the command lines it takes) and offers the part of
delugemu's `DelugeSession` that tools/emu_parity.py's `run_image` uses, so a level-3 script runs on the
device unchanged. The device runs in real time: `run_until` waits for its clock, and times are counted
from the moment the session attached, as delugemu counts them from power-on.

    with DeviceSession.launch(elf, host, out) as dz:
        dz.run_until(2_000_000_000)
        dz.button(6, 1, True)
"""

from __future__ import annotations

import base64
import binascii
import os
import re
import struct
import sys
import time
import zlib
from pathlib import Path
from typing import Self

REPO = Path(__file__).resolve().parent.parent

# A detent is four quadrature edges, as the firmware's encoder lowering counts them.
EDGES_PER_DETENT = 4
# The device sends a heartbeat every 10 ms, but the app's synchronous work (a song load from the card) holds
# the executor the stream runs on; this long without a line means the link is dead.
SILENCE_S = 10.0
PAD_COLS, PAD_ROWS = 18, 8


def default_led_names() -> dict[int, str]:
    """delugemu's LED names by the firmware's LED index (`col + 9 * row`), from its control table."""
    sys.path.insert(0, str(Path.home() / "GitHub/delugemu/pylib"))
    from delugemu import geometry

    return {
        c["col"] + 9 * c["row"]: name
        for name, c in geometry.CONTROLS.items()
        if c["has_led"] and c["col"] >= 0
    }


class _NoMonitor:
    """Stands in for delugemu's QMP handle: a device has no monitor to stop or query."""

    def execute(self, _cmd: str) -> None:
        return None

    def hmp(self, _cmd: str) -> str:
        return ""


class DeviceSession:
    def __init__(
        self,
        link,
        led_names: dict[int, str] | None = None,
        attach_timeout: float = 10.0,
        ready_log: str | None = None,
        reconnect=None,
    ):
        """Attaches over `link` (a socket-like recv/sendall). With `ready_log`, waits first for a log line
        containing it, so the session starts once the app runs: the harness says ready as the executor
        starts, before the app's start-up holds that executor for seconds. `reconnect` returns a new link
        when the old one closes before the session attached (the J-Link server drops RTT clients until it
        finds the control block); after attaching, a closed link is an error."""
        self.link = link
        self._reconnect = reconnect
        self.led_names = led_names if led_names is not None else default_led_names()
        self.q = _NoMonitor()
        self.log: list[str] = []
        self.scrolls_us: list[int] = []
        self._buf = b""
        self._seq: int | None = None
        self._now_us = 0
        self._origin_us: int | None = None
        self._oled = bytes(768)
        self._oled_size = (128, 48)
        self._pads = [[(0, 0, 0)] * PAD_ROWS for _ in range(PAD_COLS)]
        self._leds: dict[int, bool] = {}
        self._rings = [[0] * 4, [0] * 4]
        self._flashes: list[tuple[int, int, int]] = []
        self._pic = {"refresh_time": -1, "dimmer_interval": -1}
        self._target = None
        # Leaving the session parks the panel dark; a run of several scripts parks only after the last.
        self.park_on_close = True
        # The probe host's ALSA port for the Deluge's USB-MIDI (`amidi -l`), for `midi`.
        self.midi_port: str | None = os.environ.get("DELUGE_MIDI_PORT")
        self._host: str | None = None
        self._ssh = None
        self._awaiting_ready = False
        deadline = time.monotonic() + attach_timeout
        if ready_log is not None:
            while not any(ready_log in line for line in self.log):
                self._read_once(deadline, silence=False)
        # Ask for the whole panel; the ready line after it starts the session's clock.
        self._send("S")
        self._awaiting_ready = True
        while self._origin_us is None:
            self._read_once(deadline)

    # The link

    def _send(self, line: str) -> None:
        self.link.sendall((line + "\n").encode())

    def _read_once(self, deadline: float, silence: bool = True) -> None:
        """Reads what the link has and applies every whole line; raises once `deadline` passes, or (with
        `silence`) when the link has said nothing for SILENCE_S."""
        silent_since = time.monotonic()
        while True:
            now = time.monotonic()
            if now > deadline or (silence and now - silent_since > SILENCE_S):
                raise TimeoutError(
                    f"rig harness: device clock stuck at {self._now_us} us"
                )
            try:
                chunk = self.link.recv(65536)
            except TimeoutError:
                continue
            if not chunk:
                if self._reconnect is None or self._origin_us is not None:
                    raise ConnectionError("rig harness: the RTT link closed")
                # Lines cut off by the drop are lost; numbering restarts with what the new link carries.
                self.link = self._reconnect()
                self._buf = b""
                self._seq = None
                continue
            break
        self._buf += chunk
        *lines, self._buf = self._buf.split(b"\n")
        for raw in lines:
            line = raw.decode("utf-8", "replace").rstrip("\r")
            if line.startswith("@"):
                try:
                    self._apply(line.split())
                except (ValueError, IndexError, binascii.Error) as exc:
                    raise RuntimeError(
                        f"rig harness: malformed panel line ({exc}): {line[:120]}...{line[-200:]}"
                    ) from exc
            elif line:
                self.log.append(line)

    def _apply(self, f: list[str]) -> None:
        kind, seq, us = f[0], int(f[1]), int(f[2])
        if self._seq is not None and seq != self._seq + 1:
            raise RuntimeError(
                f"rig harness: lost {seq - self._seq - 1} panel line(s) before line {seq}"
            )
        self._seq = seq
        self._now_us = max(self._now_us, us)
        if kind == "@O":
            w, h = int(f[3]), int(f[4])
            self._oled, self._oled_size = (
                base64.b64decode(f[5], validate=True) if len(f) > 5 else b"",
                (w, h),
            )
        elif kind == "@C":
            idx, rgb = int(f[3]), base64.b64decode(f[4], validate=True)
            for i in range(16):
                col = idx * 2 + i // 8
                if col < PAD_COLS:
                    self._pads[col][i % 8] = tuple(rgb[i * 3 : i * 3 + 3])
        elif kind == "@L":
            self._leds[int(f[3])] = f[4] == "1"
        elif kind == "@G":
            self._rings[int(f[3]) != 0] = [int(v) for v in f[4:8]]
        elif kind == "@F":
            idx = int(f[3])
            self._flashes.append((idx // 8, idx % 8, int(f[4])))
        elif kind == "@M":
            self._pic = {"refresh_time": int(f[4]), "dimmer_interval": int(f[3])}
        elif kind == "@X":
            self.scrolls_us.append(us)
        elif kind == "@D":
            raise RuntimeError(f"rig harness: the device dropped {f[3]} pad flash(es)")
        elif kind == "@E":
            raise RuntimeError(f"rig harness: the device refused {' '.join(f[3:])!r}")
        elif kind == "@R":
            if self._awaiting_ready and self._origin_us is None:
                self._origin_us = us

    # The DelugeSession subset emu_parity uses

    def vclock_ns(self) -> int:
        return (self._now_us - self._origin_us) * 1000

    def run_until(self, ns: int, timeout: float = 120.0) -> None:
        deadline = time.monotonic() + timeout
        while self.vclock_ns() < ns:
            self._read_once(deadline)

    def button(self, x: int, y: int, down: bool) -> None:
        self._send(f"B {x} {y} {int(down)}")

    def pad_event(self, x: int, y: int, down: bool) -> None:
        self._send(f"P {x} {y} {int(down)}")

    def encoder(self, index: int, detents: int) -> None:
        self._send(f"E {index} {detents * EDGES_PER_DETENT}")

    def midi(self, hex_bytes: str) -> None:
        """Sends MIDI bytes (hex, e.g. "90 3c 64") to the Deluge's USB-MIDI from the probe host, with amidi."""
        if not re.fullmatch(r"[0-9a-fA-F]{2}( [0-9a-fA-F]{2})*", hex_bytes):
            raise ValueError(f"rig harness: not MIDI hex bytes: {hex_bytes!r}")
        if not self.midi_port:
            raise RuntimeError(
                "rig harness: no MIDI port (--midi-port or $DELUGE_MIDI_PORT)"
            )
        if self._ssh is None:
            import rig

            self._ssh = rig.ssh
        done = self._ssh(
            self._host, f"amidi -p {self.midi_port} -S '{hex_bytes}'", timeout=20
        )
        if done.returncode != 0:
            raise RuntimeError(
                f"rig harness: amidi failed: {done.stderr.strip()[-200:]}"
            )

    def read_oled(self) -> list[list[bool]]:
        """Rows of pixels, decoded from the SSD1309 page layout as delugemu decodes its framebuffer."""
        w, h = self._oled_size
        return [
            [bool((self._oled[(y >> 3) * w + x] >> (y & 7)) & 1) for x in range(w)]
            for y in range(h)
        ]

    def read_pads(self, flashes: bool = True) -> list[list[tuple[int, int, int]]]:
        """The pad colours the app last sent; the PIC's flashes are never drawn in (see read_pad_flashes)."""
        del flashes
        return [list(col) for col in self._pads]

    def read_leds(self) -> dict[str, bool]:
        return {name: self._leds.get(i, False) for i, name in self.led_names.items()}

    def read_gold_knob(self) -> list[list[int]]:
        return [list(r) for r in self._rings]

    def read_pad_flashes(self) -> list[tuple[int, int, int]]:
        got, self._flashes = self._flashes, []
        return got

    def read_pic_settings(self) -> dict[str, int]:
        return dict(self._pic)

    def screenshot(self, path: str) -> str:
        """Writes the OLED as a greyscale PNG, 4x scaled."""
        rows = self.read_oled()
        scale = 4
        raw = b"".join(
            b"\0" + bytes(255 if p else 0 for p in row for _ in range(scale))
            for row in rows
            for _ in range(scale)
        )

        def chunk(kind: bytes, data: bytes) -> bytes:
            return (
                struct.pack(">I", len(data))
                + kind
                + data
                + struct.pack(">I", zlib.crc32(kind + data))
            )

        w, h = len(rows[0]) * scale, len(rows) * scale
        png = (
            b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 0, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw))
            + chunk(b"IEND", b"")
        )
        Path(path).write_bytes(png)
        return path

    # Launching on the rig

    @classmethod
    def launch(
        cls, elf: Path, host: str, out: Path, boot_s: float = 60.0
    ) -> DeviceSession:
        """Restarts the rig, loads `elf` into RAM (no breakpoints, QSPI untouched), connects to its RTT
        and attaches. Use as a context manager: leaving it parks the panel dark and stops the server."""
        sys.path.insert(0, str(REPO / "tools"))
        import deluge_jlink
        import rig

        out.mkdir(parents=True, exist_ok=True)
        if not rig.restart(host):
            raise ConnectionError(
                f"rig harness: could not restart the probe server on {host}"
            )
        args = deluge_jlink.build_parser().parse_args(
            [
                str(elf),
                "--select",
                f"IP={rig.probe_address(host)}",
                "--timeout",
                str(boot_s + 60),
            ]
        )
        target = deluge_jlink.start_target(
            args, deluge_jlink.Result(elf=str(elf), chip=args.device), time.time()
        )
        try:
            session = cls(
                target.connect_rtt(time.time() + boot_s),
                attach_timeout=boot_s,
                ready_log="going into main loop",
                reconnect=lambda: target.connect_rtt(time.time() + boot_s),
            )
        except BaseException:
            target.close()
            raise
        session._target = target
        session._host = host
        session._out = out
        (out / "device.log").write_text("")
        return session

    def __enter__(self) -> Self:
        return self

    def __exit__(self, *_exc) -> None:
        self.close()

    def close(self, park: bool | None = None) -> None:
        """Stops the probe link, leaving the firmware running; then, with `park` (default: park_on_close),
        parks the panel dark and stops the server."""
        if self._target is None:
            return
        import rig

        Path(self._out / "device.log").write_text("\n".join(self.log) + "\n")
        self._target.close()
        self._target = None
        if self.park_on_close if park is None else park:
            rig.park(self._host, rig.DEFAULT_PARK)
            rig.stop(self._host)

    def assert_no_aborts(self) -> None:
        """Raises AssertionError if the firmware logged a panic or a freeze, as delugemu's check does."""
        for line in self.log:
            if "panicked at" in line or "FREEZE" in line:
                raise AssertionError(f"rig harness: the firmware failed: {line}")


if __name__ == "__main__":
    print(__doc__, file=sys.stderr)
    sys.exit(os.EX_USAGE)
