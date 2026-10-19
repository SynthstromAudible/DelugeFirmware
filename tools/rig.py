#!/usr/bin/env python3
"""Look after the J-Trace rig: restart its remote server, park the panel dark, stop the server.

Usage:
    tools/rig.py restart [--host pi-top]
    tools/rig.py park    [--host pi-top] [--elf .superpowers/bench-park.elf]
    tools/rig.py stop    [--host pi-top]

The probe hangs off a Raspberry Pi (`--host`, reached over ssh) that runs JLinkRemoteServerCLExe;
tools/deluge_jlink.py and tools/stream_bench.py reach the probe through it (`--select IP=<host>`).

restart  stops any running server, connects once with JLinkExe (which recovers a probe a previous
         session left wedged), and starts a fresh server detached from the ssh session.
park     restarts, then boots the park image through tools/dsp_bench.py, which leaves the pads and
         display dark; it retries a dropped connection a few times.
stop     stops the server, so the probe is free for a local session and the rig idles.

Exit codes: 0 done; 1 the host could not be reached or the step failed after its retries.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
DEFAULT_HOST = "pi-top"
DEFAULT_PARK = REPO / ".superpowers" / "bench-park.elf"
SERVER = "JLinkRemoteServerCLExe"
DEVICE = "R7S721020"

# Run on the Pi. `pidof` matches the executable's name exactly, so this never matches the shell
# running it (a `pgrep -f` pattern does, and kills the ssh session).
STOP = f"for p in $(pidof {SERVER}); do kill $p; done; sleep 1"
CONNECT = (
    f"printf 'connect\\nexit\\n' | timeout 20 JLinkExe -device {DEVICE} -if SWD -speed 4000 "
    "-autoconnect 1 > /tmp/jlinkexe.log 2>&1"
)
START = f"setsid nohup {SERVER} -Port 19020 > /tmp/jlinkremote.log 2>&1 < /dev/null & sleep 2"


def ssh(host: str, script: str, timeout: float = 60) -> subprocess.CompletedProcess:
    """Run `script` on `host`; stdin is closed so a backgrounded server cannot hold the session."""
    return subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "ConnectTimeout=10", host, script],
        stdin=subprocess.DEVNULL,
        capture_output=True,
        text=True,
        timeout=timeout,
        check=False,
    )


def restart(host: str) -> bool:
    """Stops any server, recovers the probe with one JLinkExe connect, and starts a server."""
    try:
        return ssh(host, f"{STOP}; {CONNECT}; {START}").returncode == 0
    except subprocess.TimeoutExpired:
        return False


def stop(host: str) -> bool:
    """Stops the server; true once none is running."""
    try:
        done = ssh(host, f"{STOP}; pidof {SERVER} || echo stopped")
    except subprocess.TimeoutExpired:
        return False
    return "stopped" in done.stdout


def park(host: str, elf: Path, tries: int = 3) -> bool:
    """Boots the park image, which leaves the panel dark; retries a dropped connection."""
    for attempt in range(tries):
        if not restart(host):
            time.sleep(10)
            continue
        with tempfile.TemporaryDirectory() as out:
            proc = subprocess.run(
                [
                    sys.executable,
                    str(REPO / "tools" / "dsp_bench.py"),
                    "--elf",
                    str(elf),
                    "--select",
                    f"IP={host}",
                    "--timeout",
                    "100",
                    "--out",
                    out,
                ],
                capture_output=True,
                text=True,
                check=False,
            )
        if proc.returncode == 0:
            return True
        if attempt + 1 < tries:
            time.sleep(10)
    return False


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Look after the J-Trace rig.")
    ap.add_argument("action", choices=("restart", "park", "stop"))
    ap.add_argument("--host", default=DEFAULT_HOST)
    ap.add_argument("--elf", type=Path, default=DEFAULT_PARK, help="the park image")
    args = ap.parse_args(argv)
    if args.action == "restart":
        ok = restart(args.host)
    elif args.action == "park":
        ok = park(args.host, args.elf)
    else:
        ok = stop(args.host)
    print(f"rig: {args.action} {'done' if ok else 'FAILED'}", file=sys.stderr)
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
