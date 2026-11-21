#!/usr/bin/env python3
"""openocd_rtt.py -- read SEGGER RTT from the Deluge through a running OpenOCD.

OpenOCD only implements RTT for Cortex-M (and HLA/ARC) targets, not the Deluge's Cortex-A9. This does the
RTT protocol on the host instead, over OpenOCD's Tcl RPC port, reading through the AHB-AP target that
scripts/debug/openocd/rza1l.cfg defines. It never halts the core, so it runs alongside a GDB session
(`./dbt debug` + `./dbt gdb`).

The control block address comes from the ELF's _SEGGER_RTT (the firmware keeps it in the uncached
0x60000000 SRAM mirror, so a running core's writes are visible over the AHB-AP), or from --address.

    tools/openocd_rtt.py build/RelWithDebInfo/deluge.elf
    tools/openocd_rtt.py --address 0x602ef000 --channel 0
"""

from __future__ import annotations

import argparse
import shutil
import socket
import subprocess
import sys
import time

TERMINATOR = b"\x1a"
RTT_ID = b"SEGGER RTT"
CB_HEADER = 24  # acID[16], MaxNumUpBuffers, MaxNumDownBuffers
BUF_DESC = 24  # sName, pBuffer, SizeOfBuffer, WrOff, RdOff, Flags


class OpenOcd:
    def __init__(self, host: str, port: int, mem_target: str):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.mem = mem_target
        self.buf = b""

    def cmd(self, command: str) -> str:
        self.sock.sendall(command.encode() + TERMINATOR)
        while TERMINATOR not in self.buf:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("OpenOCD closed the connection")
            self.buf += chunk
        reply, _, self.buf = self.buf.partition(TERMINATOR)
        return reply.decode(errors="replace")

    def read32(self, addr: int, count: int = 1) -> list[int]:
        reply = self.cmd(f"{self.mem} read_memory {addr:#x} 32 {count}")
        try:
            return [int(v, 0) for v in reply.split()]
        except ValueError:
            raise RuntimeError(f"read_memory {addr:#x}: {reply.strip()}") from None

    def read8(self, addr: int, count: int) -> bytes:
        reply = self.cmd(f"{self.mem} read_memory {addr:#x} 8 {count}")
        try:
            return bytes(int(v, 0) for v in reply.split())
        except ValueError:
            raise RuntimeError(f"read_memory {addr:#x}: {reply.strip()}") from None

    def write32(self, addr: int, value: int) -> None:
        reply = self.cmd(f"{self.mem} write_memory {addr:#x} 32 {{{value:#x}}}")
        if reply.strip():
            raise RuntimeError(f"write_memory {addr:#x}: {reply.strip()}")


def rtt_address_from_elf(elf: str) -> int | None:
    nm = shutil.which("arm-none-eabi-nm")
    if not nm:
        return None
    out = subprocess.run([nm, elf], capture_output=True, text=True, check=False).stdout
    for line in out.splitlines():
        parts = line.split()
        if len(parts) == 3 and parts[2] == "_SEGGER_RTT":
            return int(parts[0], 16)
    return None


def main() -> int:
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("elf", nargs="?", help="firmware ELF, to find _SEGGER_RTT")
    ap.add_argument(
        "--address",
        type=lambda s: int(s, 0),
        help="control block address (overrides the ELF)",
    )
    ap.add_argument(
        "--channel", type=int, default=0, help="up-channel to read (default 0)"
    )
    ap.add_argument("--host", default="localhost")
    ap.add_argument(
        "--port", type=int, default=6666, help="OpenOCD Tcl RPC port (default 6666)"
    )
    ap.add_argument(
        "--target",
        default="rza1l.ahb",
        help="OpenOCD memory target (default rza1l.ahb)",
    )
    ap.add_argument(
        "--interval", type=float, default=0.01, help="poll interval in seconds"
    )
    ap.add_argument(
        "--timeout",
        type=float,
        default=0,
        help="stop after this many seconds (0 = never)",
    )
    args = ap.parse_args()

    addr = args.address
    if addr is None and args.elf:
        addr = rtt_address_from_elf(args.elf)
    if addr is None:
        ap.error(
            "no control block address: pass the ELF (with arm-none-eabi-nm on PATH) or --address"
        )

    try:
        ocd = OpenOcd(args.host, args.port, args.target)
    except OSError as e:
        print(
            f"openocd_rtt: cannot reach OpenOCD at {args.host}:{args.port}: {e}",
            file=sys.stderr,
        )
        return 3

    deadline = time.monotonic() + args.timeout if args.timeout else None
    desc = None
    out = sys.stdout.buffer
    try:
        while deadline is None or time.monotonic() < deadline:
            if desc is None:
                # The firmware initialises the block at start-up; wait for its ID.
                if ocd.read8(addr, len(RTT_ID)) != RTT_ID:
                    time.sleep(0.1)
                    continue
                n_up, _n_down = ocd.read32(addr + 16, 2)
                if args.channel >= n_up:
                    print(f"openocd_rtt: only {n_up} up-channels", file=sys.stderr)
                    return 4
                desc = addr + CB_HEADER + args.channel * BUF_DESC
                _name, buf, size, _wr, _rd, _flags = ocd.read32(desc, 6)
                print(
                    f"openocd_rtt: control block at {addr:#x}, channel {args.channel}: "
                    f"{size} bytes at {buf:#x}",
                    file=sys.stderr,
                )

            wr, rd = ocd.read32(desc + 12, 2)
            if wr == rd:
                time.sleep(args.interval)
                continue
            if wr > rd:
                data = ocd.read8(buf + rd, wr - rd)
            else:  # wrapped
                data = ocd.read8(buf + rd, size - rd) + ocd.read8(buf, wr)
            ocd.write32(desc + 16, wr)
            out.write(data)
            out.flush()
    except KeyboardInterrupt:
        pass
    except (ConnectionError, RuntimeError) as e:
        print(f"openocd_rtt: {e}", file=sys.stderr)
        return 3
    return 0


if __name__ == "__main__":
    sys.exit(main())
