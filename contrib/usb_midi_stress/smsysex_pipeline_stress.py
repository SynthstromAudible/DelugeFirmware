#!/usr/bin/env python3
"""Pipelined smSysex read stress test against a connected Deluge, over rtmidi.

Needs python-rtmidi and a Deluge connected over USB.

Exercises the USB MIDI send (device->host) path under concurrent smSysex
requests. Writes a random test file to the card serially, then reads it back
with a sliding window of 1/2/3 requests in flight, byte-verifying every pass
and reporting throughput, retries, and corruption (missing/mismatched bytes,
short or malformed replies).

Background: with more than one request in flight, large read replies can
overflow the firmware's USB MIDI send buffering. Depending on the firmware,
overflow either fails safe (whole reply dropped -> client timeout -> retry)
or corrupts silently (events excised mid-message, reply still well-framed).
This script tells the two apart, and measures the throughput gain pipelining
gives when it works (roughly +45% at window 2 vs serial).

It also exercises the firmware's own logging on the same USB cable: it
attaches the sysex debug console, floods the device with Pong messages (each
one is a D_PRINTLN in a Debug build) so the send ring overflows while the
firmware is trying to log, and then repeats the window-2 reads with the
console attached and a Pong riding along with every request. A Release build
logs nothing, so the console phases report themselves as NOT EXERCISED - run
this against a Debug build (`./dbt build debug`) too before calling the send
path safe; the #4886 -> #4900 revert was a crash only a Debug build shows.

Usage:
    python3 smsysex_pipeline_stress.py [--size KB] [--reps-w2 N] [--reps-w3 N]
                                       [--read-timeout S] [--max-retries N]
                                       [--flood N] [--no-console]

The write phase uses 600-byte chunks to stay under the macOS CoreMIDI
~752-byte inbound sysex limit; reads use 1024-byte blocks (device->host is
unaffected). The test file (PIPESTRS.TMP on the card root) is deleted at
the end of a completed run.
"""

import argparse
import json
import os
import sys
import time

import rtmidi

HEADER = [0xF0, 0x00, 0x21, 0x7B, 0x01]
TESTFILE = "PIPESTRS.TMP"
READ_BLOCK = 1024  # device->host is not affected by the macOS 752-byte cliff
WRITE_BLOCK = 600  # host->device must stay under the macOS cliff
PING = HEADER + [0x00, 0x00, 0xF7]
PONG = HEADER + [0x7F, 0x00, 0xF7]  # a Debug build logs "Pong" for each of these
CONSOLE_ON = HEADER + [0x03, 0x00, 0x01, 0xF7]
CONSOLE_OFF = HEADER + [0x03, 0x00, 0x00, 0xF7]
CONSOLE_HDR = bytes(HEADER + [0x03, 0x40, 0x00])


def pack(data: bytes) -> bytes:
    out = bytearray()
    for i in range(0, len(data), 7):
        group = data[i : i + 7]
        out.append(sum(1 << j for j, b in enumerate(group) if b & 0x80))
        out += bytes(b & 0x7F for b in group)
    return bytes(out)


def unpack(data: bytes) -> bytes:
    out = bytearray()
    for i in range(0, len(data), 8):
        group = data[i : i + 8]
        high = group[0]
        for j, b in enumerate(group[1:]):
            out.append(b | (0x80 if high & (1 << j) else 0))
    return bytes(out)


class Deluge:
    def __init__(self, port="DELUGE"):
        self.out, self.inp = rtmidi.MidiOut(), rtmidi.MidiIn()
        outs = [
            i for i, p in enumerate(self.out.get_ports()) if port.upper() in p.upper()
        ]
        ins = [
            i for i, p in enumerate(self.inp.get_ports()) if port.upper() in p.upper()
        ]
        if not outs or not ins:
            sys.exit(f"no MIDI port matching {port!r}")
        print(f"using MIDI port {self.out.get_ports()[outs[-1]]!r}", flush=True)
        self.out.open_port(outs[-1])
        self.inp.open_port(ins[-1])
        self.inp.ignore_types(False, True, True)
        self.seq = 1

    def collect(self, seconds):
        """Drain incoming frames for `seconds`; returns them (console frames included)."""
        frames = []
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            r = self.inp.get_message()
            if r:
                frames.append(bytes(r[0]))
            else:
                time.sleep(0.002)
        return frames

    @staticmethod
    def console_lines(frames):
        return [
            f[len(CONSOLE_HDR) : -1].decode("ascii", "replace").rstrip("\n")
            for f in frames
            if f.startswith(CONSOLE_HDR)
        ]

    def ping(self, tries=10, wait=0.5):
        """True if the device still answers a sysex ping."""
        for _ in range(tries):
            self.out.send_message(PING)
            if any(f[:6] == bytes(PONG[:6]) for f in self.collect(wait)):
                return True
        return False

    def console(self, on):
        self.out.send_message(CONSOLE_ON if on else CONSOLE_OFF)
        self.collect(0.3)

    def request(self, obj, payload=b"", timeout=6.0, retries=2):
        """Serial request/reply; used for setup (open/write/close/delete)."""
        for attempt in range(retries + 1):
            seq = self.seq
            self.seq = (self.seq + 1) & 0x7F or 1
            message = HEADER + [0x04, seq]
            message += list(json.dumps(obj, separators=(",", ":")).encode())
            if payload:
                message += [0x00] + list(pack(payload))
            message.append(0xF7)
            self.out.send_message(message)
            deadline = time.monotonic() + timeout
            while time.monotonic() < deadline:
                received = self.inp.get_message()
                if not received:
                    time.sleep(0.002)
                    continue
                data = bytes(received[0])
                if (
                    data[:5] != bytes(HEADER)
                    or len(data) < 8
                    or data[5] != 0x05
                    or data[6] != seq
                ):
                    continue
                body = data[7:-1]
                spacer = body.find(b"\0")
                if spacer < 0:
                    return json.loads(body.decode("ascii", "replace")), b""
                return json.loads(body[:spacer].decode("ascii", "replace")), unpack(
                    body[spacer + 1 :]
                )
            if attempt < retries:
                print(f"  timeout on {next(iter(obj))}, retrying...", flush=True)
        raise TimeoutError(f"no reply to {obj}")


class Pipeliner:
    """Window-N sliding requests over one Deluge connection, matching replies by seq."""

    def __init__(self, d):
        self.d = d
        self.stats = {"timeouts": 0, "malformed": 0, "unmatched": 0}

    def _send(self, obj, busy=(), chatter=False):
        if chatter:  # one log line's worth of console traffic per request
            self.d.out.send_message(PONG)
        seq = self.d.seq
        self.d.seq = (self.d.seq + 1) & 0x7F or 1
        while seq in busy:  # never reuse a seq that is still in flight
            seq = self.d.seq
            self.d.seq = (self.d.seq + 1) & 0x7F or 1
        msg = HEADER + [0x04, seq]
        msg += list(json.dumps(obj, separators=(",", ":")).encode())
        msg.append(0xF7)
        self.d.out.send_message(msg)
        return seq

    def _poll(self):
        """Return (seq, json, payload) for the next reply frame, or None."""
        r = self.d.inp.get_message()
        if not r:
            return None
        data = bytes(r[0])
        if data[:5] != bytes(HEADER) or len(data) < 8 or data[5] != 0x05:
            return None
        seq = data[6]
        body = data[7:-1]
        spacer = body.find(b"\0")
        try:
            if spacer < 0:
                return seq, json.loads(body.decode("ascii", "replace")), b""
            return (
                seq,
                json.loads(body[:spacer].decode("ascii", "replace")),
                unpack(body[spacer + 1 :]),
            )
        except (ValueError, IndexError):
            self.stats["malformed"] += 1
            return None

    def read_file(
        self,
        fid,
        total,
        window,
        per_request_timeout=4.0,
        max_retries=200,
        chatter=False,
    ):
        """Pipelined read of `total` bytes; returns (bytes, elapsed, retries)."""
        needed = list(range(0, total, READ_BLOCK))
        chunks = {}  # addr -> bytes
        pending = {}  # seq -> (addr, deadline)
        to_send = list(needed)  # queue; retried addrs get re-appended
        retries = 0
        t0 = time.monotonic()
        last_beat = t0
        while len(chunks) < len(needed):
            while to_send and len(pending) < window:
                addr = to_send.pop(0)
                if addr in chunks:
                    continue  # a late reply already covered it
                seq = self._send(
                    {"read": {"fid": fid, "addr": addr, "size": READ_BLOCK}},
                    busy=pending,
                    chatter=chatter,
                )
                pending[seq] = (addr, time.monotonic() + per_request_timeout)
            got = self._poll()
            if got:
                seq, reply, payload = got
                if seq in pending:
                    addr, _ = pending.pop(seq)
                    rr = reply.get("^read", {})
                    size = rr.get("size", 0)
                    if rr.get("err", -1) != 0 or size <= 0:
                        retries += 1
                        to_send.append(addr)
                    else:
                        chunks[addr] = payload[:size]
                else:
                    self.stats["unmatched"] += 1
                continue
            now = time.monotonic()
            expired = [s for s, (_, dl) in pending.items() if dl < now]
            for s in expired:
                addr, _ = pending.pop(s)
                self.stats["timeouts"] += 1
                retries += 1
                to_send.append(addr)  # re-request with a fresh seq
            if retries > max_retries:
                raise RuntimeError(f"exceeded {max_retries} retries, aborting pass")
            if now - last_beat > 5.0:
                last_beat = now
                print(
                    f"    ...{len(chunks)}/{len(needed)} chunks, {retries} retries, "
                    f"{len(pending)} pending, {now - t0:.0f}s",
                    flush=True,
                )
            if not got and not expired:
                time.sleep(0.001)
            if not pending and not to_send and len(chunks) < len(needed):
                raise RuntimeError("stalled: nothing pending yet chunks missing")
        elapsed = time.monotonic() - t0
        out = bytearray()
        for addr in sorted(chunks):
            out += chunks[addr]
        return bytes(out[:total]), elapsed, retries


def log_flood_phase(d, count):
    """Attach the debug console and make the firmware log faster than USB drains.

    Returns True if the console is live and the device survived, False if the
    device stopped answering, None if the build does not log (Release).
    """
    d.console(True)
    lines = []
    for _ in range(3):  # a few probes: any console frame at all proves the build logs
        d.out.send_message(PONG)
        lines += d.console_lines(d.collect(0.7))
        if lines:
            break
    if not lines:
        print(
            "debug console: silent (Release build) - log-path phases NOT EXERCISED; "
            "rerun against a Debug build",
            flush=True,
        )
        d.console(False)
        return None
    print(f"debug console: live ({lines[-1]!r})", flush=True)
    print(f"  flooding {count} Pongs (one log line each)...", flush=True)
    t0 = time.monotonic()
    for _ in range(count):
        d.out.send_message(PONG)
    sent = time.monotonic() - t0
    delivered = len(d.console_lines(d.collect(3.0)))
    alive = d.ping()
    print(
        f"  sent in {sent:.2f}s, {delivered}/{count} log lines delivered, "
        f"device {'ALIVE' if alive else 'DEAD'}",
        flush=True,
    )
    return alive


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=256, help="test file size in KB")
    ap.add_argument("--reps-w2", type=int, default=10)
    ap.add_argument("--reps-w3", type=int, default=3)
    ap.add_argument("--read-timeout", type=float, default=4.0)
    ap.add_argument("--max-retries", type=int, default=200)
    ap.add_argument(
        "--flood", type=int, default=3000, help="Pongs in the log-flood phase"
    )
    ap.add_argument(
        "--no-console", action="store_true", help="skip the debug-console phases"
    )
    ap.add_argument(
        "--port", default="DELUGE", help="MIDI port name substring (last match wins)"
    )
    args = ap.parse_args()

    data = os.urandom(args.size * 1024)
    d = Deluge(args.port)
    p = Pipeliner(d)

    print(f"writing {len(data)} bytes to {TESTFILE} (serial)...", flush=True)
    reply, _ = d.request({"open": {"path": TESTFILE, "write": 1}})
    if reply["^open"].get("err", -1) != 0 or reply["^open"].get("fid", 0) == 0:
        sys.exit(f"open for write failed: {reply}")
    fid = reply["^open"]["fid"]
    addr = 0
    t0 = time.monotonic()
    while addr < len(data):
        chunk = data[addr : addr + WRITE_BLOCK]
        r, _ = d.request(
            {"write": {"fid": fid, "addr": addr, "size": len(chunk)}}, chunk
        )
        if r["^write"]["err"] != 0 or r["^write"]["size"] != len(chunk):
            sys.exit(f"write failed at {addr}: {r}")
        addr += len(chunk)
    d.request({"close": {"fid": fid}})
    print(
        f"  wrote at {len(data) / (time.monotonic() - t0) / 1024:.1f} KB/s", flush=True
    )

    console_live = None
    if not args.no_console:
        console_live = log_flood_phase(d, args.flood)
        if console_live is False:
            sys.exit(
                "FAIL: device stopped answering after the log flood "
                "(power-cycle it). A drop path that logs re-enters the send path."
            )

    results = []
    plan = [(1, 2, False), (2, args.reps_w2, False), (3, args.reps_w3, False)]
    if console_live:
        # Same reads with the console attached and a log line per request, so
        # firmware logging competes with replies for the send ring.
        plan.append((2, args.reps_w2, True))
    for window, reps, chatter in plan:
        label = f"w={window}{'+log' if chatter else ''}"
        for rep in range(reps):
            reply, _ = d.request({"open": {"path": TESTFILE, "write": 0}})
            if reply["^open"].get("err", -1) != 0 or reply["^open"].get("fid", 0) == 0:
                sys.exit(f"open for read failed (leftover fids? power-cycle): {reply}")
            fid = reply["^open"]["fid"]
            try:
                got, elapsed, retries = p.read_file(
                    fid, len(data), window, args.read_timeout, args.max_retries, chatter
                )
                ok = got == data
                if not ok:
                    diffs = [
                        i for i in range(min(len(got), len(data))) if got[i] != data[i]
                    ]
                    detail = (
                        f"len {len(got)} vs {len(data)}, "
                        f"first-diff {diffs[0] if diffs else 'none'}, ndiff {len(diffs)}"
                    )
                else:
                    detail = ""
                rate = len(data) / elapsed / 1024
                results.append((label, rep, ok, rate, retries, detail))
                print(
                    f"{label} rep={rep}: {'OK ' if ok else 'CORRUPT'} "
                    f"{rate:6.1f} KB/s retries={retries} {detail}",
                    flush=True,
                )
            except RuntimeError as e:
                results.append((label, rep, False, 0.0, -1, str(e)))
                print(f"{label} rep={rep}: ABORT {e}", flush=True)
            finally:
                try:
                    d.request({"close": {"fid": fid}})
                except TimeoutError:
                    print("  (close timed out)", flush=True)

    if console_live:
        d.console(False)
        alive = d.ping()
        print(f"\ndevice after console phases: {'ALIVE' if alive else 'DEAD'}")
    d.request({"delete": {"path": TESTFILE}})
    print(f"\nframe stats: {p.stats}")
    print("\nsummary:")
    if console_live is None and not args.no_console:
        print("  debug-console phases: NOT EXERCISED (Release build)")
    for label in dict.fromkeys(r[0] for r in results):
        rows = [r for r in results if r[0] == label]
        okc = sum(1 for r in rows if r[2])
        rates = [r[3] for r in rows if r[2]]
        avg = sum(rates) / len(rates) if rates else 0.0
        retr = sum(max(r[4], 0) for r in rows)
        print(
            f"  {label}: {okc}/{len(rows)} verified, avg {avg:.1f} KB/s, "
            f"total retries {retr}"
        )
    if any(not r[2] for r in results):
        sys.exit(1)


if __name__ == "__main__":
    main()
