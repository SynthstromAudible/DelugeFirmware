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

Usage:
    python3 smsysex_pipeline_stress.py [--size KB] [--reps-w2 N] [--reps-w3 N]
                                       [--read-timeout S] [--max-retries N]

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
    def __init__(self):
        self.out, self.inp = rtmidi.MidiOut(), rtmidi.MidiIn()
        outs = [i for i, p in enumerate(self.out.get_ports()) if "DELUGE" in p.upper()]
        ins = [i for i, p in enumerate(self.inp.get_ports()) if "DELUGE" in p.upper()]
        if not outs or not ins:
            sys.exit("no Deluge MIDI port")
        self.out.open_port(outs[-1])
        self.inp.open_port(ins[-1])
        self.inp.ignore_types(False, True, True)
        self.seq = 1

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

    def _send(self, obj, busy=()):
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

    def read_file(self, fid, total, window, per_request_timeout=4.0, max_retries=200):
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


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--size", type=int, default=256, help="test file size in KB")
    ap.add_argument("--reps-w2", type=int, default=10)
    ap.add_argument("--reps-w3", type=int, default=3)
    ap.add_argument("--read-timeout", type=float, default=4.0)
    ap.add_argument("--max-retries", type=int, default=200)
    args = ap.parse_args()

    data = os.urandom(args.size * 1024)
    d = Deluge()
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

    results = []
    plan = [(1, 2), (2, args.reps_w2), (3, args.reps_w3)]
    for window, reps in plan:
        for rep in range(reps):
            reply, _ = d.request({"open": {"path": TESTFILE, "write": 0}})
            if reply["^open"].get("err", -1) != 0 or reply["^open"].get("fid", 0) == 0:
                sys.exit(f"open for read failed (leftover fids? power-cycle): {reply}")
            fid = reply["^open"]["fid"]
            try:
                got, elapsed, retries = p.read_file(
                    fid, len(data), window, args.read_timeout, args.max_retries
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
                results.append((window, rep, ok, rate, retries, detail))
                print(
                    f"w={window} rep={rep}: {'OK ' if ok else 'CORRUPT'} "
                    f"{rate:6.1f} KB/s retries={retries} {detail}",
                    flush=True,
                )
            except RuntimeError as e:
                results.append((window, rep, False, 0.0, -1, str(e)))
                print(f"w={window} rep={rep}: ABORT {e}", flush=True)
            finally:
                try:
                    d.request({"close": {"fid": fid}})
                except TimeoutError:
                    print("  (close timed out)", flush=True)

    d.request({"delete": {"path": TESTFILE}})
    print(f"\nframe stats: {p.stats}")
    print("\nsummary:")
    for window in (1, 2, 3):
        rows = [r for r in results if r[0] == window]
        if not rows:
            continue
        okc = sum(1 for r in rows if r[2])
        rates = [r[3] for r in rows if r[2]]
        avg = sum(rates) / len(rates) if rates else 0.0
        retr = sum(max(r[4], 0) for r in rows)
        print(
            f"  w={window}: {okc}/{len(rows)} verified, avg {avg:.1f} KB/s, "
            f"total retries {retr}"
        )


if __name__ == "__main__":
    main()
