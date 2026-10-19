#!/usr/bin/env python3
"""Build a device bench image, optionally against another deluge-sdk checkout, and put the tree back.

Usage:
    tools/bench_image.py --out .superpowers/stream-bench-x.elf [--sdk PATH] [--no-bench]
                         [--define NAME=VALUE ...]

Builds `./dbt build RelWithDebInfo` with `-DDELUGE_STREAM_BENCH=ON` (or OFF with --no-bench) and any
--define, then copies build/RelWithDebInfo/deluge.elf to --out. With --sdk, the build's SDK path
override (src/bsp/rust/local-sdk.toml, gitignored) is pointed at PATH for this build only, e.g. a
worktree of an SDK branch not yet pushed.

Whatever happens, afterwards: local-sdk.toml is restored byte for byte, src/bsp/rust/Cargo.lock is
checked out again (a build through the override rewrites its SDK entries), and the bench option is
configured back to what build/CMakeCache.txt had.

Exit codes: 0 built; 1 the build failed (its error lines are printed); 2 bad arguments.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CACHE = REPO / "build" / "CMakeCache.txt"
LOCAL_SDK = REPO / "src" / "bsp" / "rust" / "local-sdk.toml"
CARGO_LOCK = "src/bsp/rust/Cargo.lock"
ELF = REPO / "build" / "RelWithDebInfo" / "deluge.elf"
# The override's paths, relative to the directory cargo resolves them from (see local-sdk.toml).
SDK_PREFIX = "../../../deluge-sdk/"
ERROR_RE = re.compile(r"(^|\s)error(\[|:)|FAILED|undefined reference")


def cached_bench() -> str | None:
    """DELUGE_STREAM_BENCH as build/CMakeCache.txt has it, or None before a configure."""
    if not CACHE.exists():
        return None
    m = re.search(r"^DELUGE_STREAM_BENCH:BOOL=(\w+)$", CACHE.read_text(), re.MULTILINE)
    return m.group(1) if m else None


def point_override_at(text: str, sdk: Path) -> str:
    """`text` (local-sdk.toml) with every SDK path redirected under `sdk`."""
    if SDK_PREFIX not in text:
        raise ValueError(f"local-sdk.toml names no {SDK_PREFIX} path to redirect")
    return text.replace(SDK_PREFIX, f"{sdk.resolve()}/")


def dbt(*args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        ["./dbt", *args], cwd=REPO, capture_output=True, text=True, check=False
    )


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="Build a device bench image.")
    ap.add_argument("--out", required=True, type=Path, help="where the ELF is copied")
    ap.add_argument("--sdk", type=Path, help="a deluge-sdk checkout to build against")
    ap.add_argument("--no-bench", action="store_true", help="configure the bench off")
    ap.add_argument("--define", action="append", default=[], metavar="NAME=VALUE")
    args = ap.parse_args(argv)
    if args.sdk and not LOCAL_SDK.exists():
        print(
            f"bench_image: --sdk needs {LOCAL_SDK.relative_to(REPO)}", file=sys.stderr
        )
        return 2

    before = cached_bench()
    saved = LOCAL_SDK.read_bytes() if LOCAL_SDK.exists() else None
    defines = [f"-DDELUGE_STREAM_BENCH={'OFF' if args.no_bench else 'ON'}"]
    defines += [f"-D{d}" for d in args.define]
    try:
        if args.sdk:
            LOCAL_SDK.write_text(point_override_at(saved.decode(), args.sdk))
        configured = dbt("configure", *defines)
        if configured.returncode != 0:
            print(
                configured.stdout[-2000:] + configured.stderr[-2000:], file=sys.stderr
            )
            return 1
        built = dbt("build", "RelWithDebInfo")
        if built.returncode != 0:
            lines = (built.stdout + built.stderr).splitlines()
            errors = [l for l in lines if ERROR_RE.search(l)] or lines[-30:]
            print("\n".join(errors[:40]), file=sys.stderr)
            return 1
        args.out.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(ELF, args.out)
    finally:
        if saved is not None:
            LOCAL_SDK.write_bytes(saved)
        subprocess.run(["git", "checkout", "-q", CARGO_LOCK], cwd=REPO, check=False)
        if before is not None:
            dbt("configure", f"-DDELUGE_STREAM_BENCH={before}")
    print(f"bench_image: built {args.out}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
