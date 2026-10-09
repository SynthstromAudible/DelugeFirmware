#!/usr/bin/env python3
"""PTM instruction trace of one function on the Deluge, end to end.

    tools/deluge_trace/deluge_trace.py run --elf build/RelWithDebInfo/deluge.elf \\
        --function 'deluge::bench::MonoVecGainCase::block' [--host pi-top | --local]

  1. resolve each --function to its address range from the ELF's symbols (here, where the toolchain is);
  2. capture (capture.py) and decode (decode.py) where the J-Trace is plugged in: on --host over ssh,
     copying the tool, the JLinkScript and a debug-stripped copy of the ELF to a scratch directory
     there; or here with --local. The trace needs the probe's own USB, so a remote probe through
     JLinkRemoteServer cannot capture it;
  3. bring the results back to .superpowers/estate/trace/<date>-<sha>-<function>/ and write the report
     (report.py) there: report.md, report.json, plus meta.json, rtt.txt, trace.bin, decode.txt and
     jlink.log from the capture.

The image must be one that ends by itself: by default the run stops at a bench image's
`bench: parked` line, after which the device is dark (configure with -DDELUGE_DSP_BENCH=ON). The
image is loaded into RAM only; flash is never written. The trace pins stay selected (TRCMUX_SEL) until
the next reset.

    tools/deluge_trace/deluge_trace.py report <run directory> --elf <elf>

re-runs the report over a run's decode.

Exit codes are capture.py's (0 captured, 1 a --fail-on line matched, 2 timed out, 3 probe or target
error, 4 usage), or 5 when the decode failed.
"""

from __future__ import annotations

import argparse
import datetime
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
sys.path.insert(0, str(HERE))

import report
import symbols

PROBE_SIDE = ("capture.py", "decode.py", "jlink.py", "coresight.py", "elf.py")
DEFAULT_SCRIPT = Path.home() / "GitHub/deluge-sdk/rza1_debug.JLinkScript"
DEFAULT_OUT = REPO / ".superpowers/estate/trace"
OBJCOPY_CANDIDATES = (
    "llvm-objcopy",
    str(REPO / "toolchain/current/arm-none-eabi-gcc/bin/arm-none-eabi-objcopy"),
)
# What comes back from the probe's side: everything but the snapshot's copies of the image's code.
PULL_EXCLUDE = "mem_*.bin"
EXIT_DECODE = 5


def git_sha() -> str:
    try:
        return subprocess.run(
            ["git", "rev-parse", "--short", "HEAD"],
            cwd=REPO,
            capture_output=True,
            text=True,
            check=True,
        ).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return "unknown"


def slug(name: str) -> str:
    """A directory-safe tag for a function: its last two name components."""
    bare = symbols.Symbol(name, 0, 0).bare
    tail = "-".join(bare.split("::")[-2:])
    return re.sub(r"[^A-Za-z0-9_.-]+", "_", tail).strip("_") or "range"


def run_dir(root: Path, tag: str) -> Path:
    """A fresh <root>/<date>-<sha>-<tag>, numbered when that run already exists."""
    stem = f"{datetime.datetime.now(tz=datetime.UTC).astimezone().date().isoformat()}-{git_sha()}-{tag}"
    out = root / stem
    n = 2
    while out.exists():
        out = root / f"{stem}-{n}"
        n += 1
    out.mkdir(parents=True)
    return out


def strip_debug(elf: Path, dest: Path) -> None:
    """Copy the ELF without its debug sections (the symbol table stays): a 5 MB copy, not 45."""
    for tool in OBJCOPY_CANDIDATES:
        try:
            subprocess.run([tool, "--strip-debug", str(elf), str(dest)], check=True)
            return
        except (OSError, subprocess.CalledProcessError):
            continue
    shutil.copyfile(elf, dest)


def ranges_for(args: argparse.Namespace) -> list[tuple[str, int, int]]:
    out = []
    if args.function:
        syms = symbols.read_symbols(args.elf)
        for name in args.function:
            s = symbols.lookup(syms, name)
            out.append((s.name, s.start, s.end))
    for text in args.range:
        a, _, b = text.partition("-")
        out.append((text, int(a, 0), int(b, 0)))
    return out


def capture_args(
    args: argparse.Namespace,
    ranges: list[tuple[str, int, int]],
    elf: str,
    script: str,
    out: str,
) -> list[str]:
    cmd = [
        "--elf",
        elf,
        "--script",
        script,
        "--out",
        out,
        "--timeout",
        str(args.timeout),
    ]
    for _, lo, hi in ranges:
        cmd += ["--range", f"0x{lo:08X}-0x{hi:08X}"]
    for pattern in args.until:
        cmd += ["--until", pattern]
    for command in args.exec:
        cmd += ["--exec", command]
    if args.speed:
        cmd += ["--speed", str(args.speed)]
    return cmd


def run_remote(
    args: argparse.Namespace, ranges: list[tuple[str, int, int]], out: Path
) -> tuple[int, int]:
    """Capture and decode on args.host; return the two exit codes."""
    host = args.host if "@" in args.host else f"pi@{args.host}"
    remote = f"/tmp/deluge_trace/{out.name}"

    def ssh(command: str, **kw) -> subprocess.CompletedProcess:
        return subprocess.run(["ssh", host, command], check=False, **kw)

    with tempfile.TemporaryDirectory() as tmp:
        stripped = Path(tmp) / "deluge.elf"
        strip_debug(Path(args.elf), stripped)
        if ssh(f"mkdir -p {shlex.quote(remote)}/tool").returncode:
            print(f"deluge_trace: cannot reach {host}", file=sys.stderr)
            return 3, 3
        files = [str(HERE / f) for f in PROBE_SIDE] + [str(args.script)]
        subprocess.run(["scp", "-q", *files, f"{host}:{remote}/tool/"], check=True)
        subprocess.run(
            ["scp", "-q", str(stripped), f"{host}:{remote}/deluge.elf"], check=True
        )
    cap = capture_args(
        args, ranges, "deluge.elf", "tool/" + Path(args.script).name, "out"
    )
    code = ssh(
        f"cd {shlex.quote(remote)} && python3 tool/capture.py {shlex.join(cap)}"
    ).returncode
    dcode = EXIT_DECODE
    if code in (0, 1, 2):
        dcode = ssh(
            f"cd {shlex.quote(remote)} && python3 tool/decode.py out --elf deluge.elf"
        ).returncode
    pull = subprocess.run(
        [
            "ssh",
            host,
            f"tar -C {shlex.quote(remote)}/out --exclude='{PULL_EXCLUDE}' -cf - .",
        ],
        capture_output=True,
        check=False,
    )
    if pull.returncode == 0:
        subprocess.run(
            ["tar", "-C", str(out), "-xf", "-"], input=pull.stdout, check=True
        )
    if not args.keep_remote:
        ssh(f"rm -rf {shlex.quote(remote)}")
    return code, dcode


def run_local(
    args: argparse.Namespace, ranges: list[tuple[str, int, int]], out: Path
) -> tuple[int, int]:
    cap = capture_args(args, ranges, str(args.elf), str(args.script), str(out))
    code = subprocess.run(
        [sys.executable, str(HERE / "capture.py"), *cap], check=False
    ).returncode
    dcode = EXIT_DECODE
    if code in (0, 1, 2):
        dcode = subprocess.run(
            [sys.executable, str(HERE / "decode.py"), str(out), "--elf", str(args.elf)],
            check=False,
        ).returncode
    return code, dcode


def cmd_run(args: argparse.Namespace) -> int:
    if not Path(args.elf).exists():
        print(f"deluge_trace: no such ELF {args.elf}", file=sys.stderr)
        return 4
    if not Path(args.script).exists():
        print(f"deluge_trace: no JLinkScript at {args.script}", file=sys.stderr)
        return 4
    try:
        ranges = ranges_for(args)
    except (LookupError, ValueError, RuntimeError) as e:
        print(f"deluge_trace: {e}", file=sys.stderr)
        return 4
    if not 1 <= len(ranges) <= 4:
        print("deluge_trace: give one to four --function or --range", file=sys.stderr)
        return 4
    out = run_dir(Path(args.out), slug(ranges[0][0]))
    for name, lo, hi in ranges:
        print(f"deluge_trace: {name}: 0x{lo:08X}-0x{hi:08X}", file=sys.stderr)
    code, dcode = (run_local if args.local else run_remote)(args, ranges, out)
    print(f"deluge_trace: results in {out}", file=sys.stderr)
    if code not in (0, 1, 2):
        return code
    if dcode != 0 or not (out / "decode.txt").exists():
        print(
            "deluge_trace: the decode failed; the raw capture is kept", file=sys.stderr
        )
        return EXIT_DECODE
    report.main([str(out), "--elf", str(args.elf)])
    return code


def cmd_report(args: argparse.Namespace) -> int:
    return report.main([args.run, "--elf", args.elf])


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(
        description="PTM instruction trace of a function on the Deluge."
    )
    sub = ap.add_subparsers(dest="command", required=True)
    run = sub.add_parser("run", help="capture, decode and report")
    run.add_argument("--elf", required=True, help="the RAM-linked image (unstripped)")
    run.add_argument(
        "--function",
        action="append",
        default=[],
        metavar="NAME",
        help="a function to trace, demangled, with or without its parameters (repeatable, up to 4)",
    )
    run.add_argument(
        "--range",
        action="append",
        default=[],
        metavar="START-END",
        help="a raw range [START, END)",
    )
    where = run.add_mutually_exclusive_group()
    where.add_argument(
        "--host", default="pi-top", help="ssh host with the J-Trace (default pi-top)"
    )
    where.add_argument(
        "--local", action="store_true", help="the J-Trace is on this machine"
    )
    run.add_argument(
        "--timeout", type=float, default=120.0, help="seconds to let the image run"
    )
    run.add_argument(
        "--until",
        action="append",
        default=[],
        metavar="RE",
        help="RTT line that ends the run",
    )
    run.add_argument(
        "--exec",
        action="append",
        default=[],
        metavar="CMD",
        help="a J-Link command string",
    )
    run.add_argument("--speed", type=int, help="SWD clock, kHz")
    run.add_argument(
        "--script", default=str(DEFAULT_SCRIPT), help="rza1_debug.JLinkScript"
    )
    run.add_argument("--out", default=str(DEFAULT_OUT), help="where run directories go")
    run.add_argument(
        "--keep-remote",
        action="store_true",
        help="leave the scratch directory on the host",
    )
    run.set_defaults(func=cmd_run)
    rep = sub.add_parser("report", help="re-run the report over a run directory")
    rep.add_argument("run")
    rep.add_argument("--elf", required=True)
    rep.set_defaults(func=cmd_report)
    args = ap.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
