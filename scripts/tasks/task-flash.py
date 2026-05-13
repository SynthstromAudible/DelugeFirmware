#! /usr/bin/env python3
"""Flash firmware over SWD using OpenOCD with the Deluge SPIBSC driver and ``smart_program``.

Resolves the OpenOCD binary from (in order): ``$OPENOCD`` env var, the DBT toolchain at
``toolchain/v<N>/<sys>-<arch>/openocd/bin/openocd``, or system ``$PATH``. The toolchain
binary needs to be the one built by ``scripts/debug/openocd/spibsc/build-openocd.sh`` —
the stock xPack openocd that DBT downloads does NOT have the ``renesas_spibsc`` flash
driver. After running that script once, the patched binary replaces the xPack one in
place and every subsequent ``make flash`` uses it.

Inner command run by OpenOCD::

    renesas_spibsc smart_program <deluge.bin> <offset>; reset run; exit

``smart_program`` stages the image in SDRAM, CRC-sweeps the destination flash range,
and reprograms only the 4 KiB sub-sectors that differ — typically ~10x faster than
upstream ``program`` for incremental builds.

By default runs a firmware build first, then flashes ``build/<Config>/deluge.bin`` at
flash offset ``0x080000`` (512 KiB — application region after the bootloader).

Environment overrides:

- ``OPENOCD``: explicit path to an openocd binary (must have the renesas_spibsc driver).
- ``SMART_PROGRAM_OFFSET``: hex or decimal flash offset (default ``0x080000``).
"""

from __future__ import annotations

import argparse
import importlib
import os
import shlex
import sys
from pathlib import Path

import platform
import shutil

import util

_DEFAULT_INTERFACE = Path("scripts/debug/openocd/interface/delugeprobe.cfg")
_DEFAULT_TARGET = Path("scripts/debug/openocd/deluge-swd.cfg")
_DEFAULT_FLASH_OFFSET = 0x080000

_BUILD_CONFIGS = {
    "release": "Release",
    "debug": "Debug",
    "relwithdebinfo": "RelWithDebInfo",
}


def _bin_for_cmake_config(cmake_config: str) -> Path:
    return Path("build") / cmake_config / "deluge.bin"


def _normalize_build_arg(config: str | None) -> str:
    if not config:
        return "Release"
    low = config.lower()
    if low in _BUILD_CONFIGS:
        return _BUILD_CONFIGS[low]
    return config


def _tcl_quote_path(path: Path) -> str:
    s = path.resolve().as_posix()
    return "{" + s + "}"


def _flash_offset() -> int:
    raw = os.environ.get("SMART_PROGRAM_OFFSET", "").strip()
    if not raw:
        return _DEFAULT_FLASH_OFFSET
    if raw.lower().startswith("0x"):
        return int(raw, 16)
    return int(raw, 0)


def _dbt_toolchain_openocd() -> Path | None:
    """Find the openocd installed in the DBT toolchain (xPack location, post-replaced by us)."""
    root = util.get_git_root()
    ver_file = root / "toolchain" / "REQUIRED_VERSION"
    if not ver_file.is_file():
        return None
    version = ver_file.read_text().strip()
    sys_type = platform.system().lower()
    arch = platform.machine().lower()
    if arch == "aarch64":
        arch = "arm64"
    candidate = (
        root
        / "toolchain"
        / f"v{version}"
        / f"{sys_type}-{arch}"
        / "openocd"
        / "bin"
        / "openocd"
    )
    return candidate if candidate.is_file() and os.access(candidate, os.X_OK) else None


def _openocd_bin() -> str:
    env = (os.environ.get("OPENOCD") or "").strip()
    if env:
        return env
    found = _dbt_toolchain_openocd()
    if found is not None:
        return str(found.resolve())
    path_lookup = shutil.which("openocd")
    if path_lookup:
        return path_lookup
    print(
        "No OpenOCD binary found.\n"
        "Build the patched openocd once:\n"
        "  ./scripts/debug/openocd/spibsc/build-openocd.sh\n"
        "(clones upstream openocd, applies the Deluge SPIBSC driver patch, installs over\n"
        "the DBT toolchain's xPack openocd). After that, every `make flash` uses it.",
        file=sys.stderr,
    )
    sys.exit(1)


def _openocd_smart_flash_argv(
    bin_path: Path, *, fallback_slow_path: bool = False
) -> list[str]:
    root = util.get_git_root()
    ocd = _openocd_bin()

    extra_script = (os.environ.get("OPENOCD_SCRIPT_PATH") or "").strip()

    ocd_cmd: list[str] = [ocd]
    if extra_script:
        ocd_cmd += ["-s", str(Path(extra_script).expanduser().resolve())]

    iface = Path(os.environ.get("OPENOCD_INTERFACE", _DEFAULT_INTERFACE))
    target = Path(os.environ.get("OPENOCD_TARGET", _DEFAULT_TARGET))
    ocd_cmd += [
        "-f",
        str((root / iface).resolve()),
        "-f",
        str((root / target).resolve()),
    ]

    off = _flash_offset()
    bin_arg = _tcl_quote_path(root / bin_path)
    if fallback_slow_path:
        # The slow-path `program` command uses the driver's .write hook (no in-RAM handler needed).
        # Address is XIP-window base + flash offset. Slower but works even when the handler upload
        # has failed (e.g. after a fault that left RAM at 0x20200000 in an unhelpful state).
        # `reset halt` here is harmless — `program` doesn't need the SPIBSC handler in RAM.
        inner = f"reset halt; program {bin_arg} 0x{0x18000000 + off:x} reset exit"
    else:
        # IMPORTANT: do NOT prepend `reset halt` here. The flash-bank declaration in deluge-swd.cfg
        # triggers an auto-probe at `init` time which uploads the SPIBSC mailbox handler to RAM at
        # 0x20200100. A subsequent `reset halt` resets the CPU, wiping that handler — and then
        # smart_program fails with "handler not running". The cfg's own `init; reset; halt` already
        # leaves the CPU halted with the handler resident, which is what smart_program needs.
        inner = f"renesas_spibsc smart_program {bin_arg} 0x{off:x}; reset run; exit"
    ocd_cmd += ["-c", inner]
    return ocd_cmd


# Error patterns that indicate a transient debug-state problem (CPU stuck in fault handler, stale DP
# session from a previous openocd that didn't shut down cleanly, etc). For these we retry the whole
# openocd invocation — each run starts with `reset halt` which kicks the CPU out of most stuck states.
_TRANSIENT_PATTERNS = (
    "cannot read IDR",
    "handler upload failed",
    "smart_program needs the in-RAM handler",
    "DSCR_DTR_RX_FULL",
    "target not halted",
    "auto_probe failed",
    "no flash responded",
    "Instruction fault registers",
    # ARM data/prefetch aborts encountered during probe — usually the SPIBSC controller is in a
    # half-initialized state from an aborted previous run; a fresh `reset halt` clears it.
    "Data fault registers",
)


def _run_openocd_with_recovery(bin_path: Path, *, verbose: bool) -> int:
    """Run the smart_program flash, retrying on transient debug-state errors.

    The first failure is the most common case (CPU was running real firmware that clobbered the
    handler load area at 0x20200100, or the previous openocd session left the DP in a half-attached
    state). A second attempt with a fresh `reset halt` almost always works.

    If smart_program still fails after retries, fall back to plain `program` — it goes through the
    driver's `.write` hook, doesn't need the in-RAM handler, and is reliable even when the chip is
    in a degraded state. Slower (~30s vs ~10s for a clean smart_program) but better than failing.
    """
    import subprocess
    import time

    def run_with_pattern_capture(argv: list[str]) -> tuple[int, str]:
        """Run openocd while streaming output live; also accumulate stderr for pattern matching."""
        proc = subprocess.Popen(
            argv,
            stdin=subprocess.DEVNULL,
            stdout=None,  # inherits — user sees live progress
            stderr=subprocess.PIPE,
            text=True,
            bufsize=1,
        )
        seen = []
        # OpenOCD emits its log to stderr. Tee each line through to the user's stderr while
        # accumulating for the retry-pattern scan.
        assert proc.stderr is not None
        for line in proc.stderr:
            sys.stderr.write(line)
            sys.stderr.flush()
            seen.append(line)
        rc = proc.wait()
        return rc, "".join(seen)

    for attempt in range(3):
        argv = _openocd_smart_flash_argv(bin_path)
        if verbose:
            print(
                f"[flash attempt {attempt + 1}/3] "
                + " ".join(shlex.quote(a) for a in argv)
            )
        rc, log = run_with_pattern_capture(argv)
        if rc == 0 and "smart_program done" in log:
            return 0
        if not any(p in log for p in _TRANSIENT_PATTERNS):
            return rc if rc != 0 else 1
        print(
            f"flash: transient debug-state error on attempt {attempt + 1}; retrying",
            file=sys.stderr,
        )
        time.sleep(1.0)

    print(
        "flash: smart_program failed 3× — falling back to slow-path program",
        file=sys.stderr,
    )
    argv = _openocd_smart_flash_argv(bin_path, fallback_slow_path=True)
    if verbose:
        print(" ".join(shlex.quote(a) for a in argv))
    rc, _ = run_with_pattern_capture(argv)
    if rc != 0:
        print(
            "flash: slow-path program also failed. Power-cycle the Deluge (USB unplug/replug) and "
            "rerun. If the device is bootloader-only (e.g. after a deep brick), a manual `program "
            "deluge.bin 0x18000000` with sector 0 protection disabled may be needed.",
            file=sys.stderr,
        )
    return rc


def argparser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="flash",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        description="Build (unless disabled) and flash deluge.bin via vendor OpenOCD "
        "(renesas_spibsc smart_program).",
    )
    p.group = "Building"
    p.add_argument(
        "config",
        nargs="?",
        default="release",
        help="Build configuration: release, debug, relwithdebinfo, or a CMake config name",
    )
    p.add_argument(
        "--no-build",
        action="store_true",
        help="Do not run a build; only flash an existing deluge.bin",
    )
    p.add_argument(
        "-v",
        "--verbose",
        action="store_true",
        help="Print the OpenOCD command line",
    )
    return p


def main(argv: list[str] | None = None) -> int:
    args = argparser().parse_args(argv)
    os.chdir(util.get_git_root())

    cmake_cfg = _normalize_build_arg(args.config)
    bin_path = _bin_for_cmake_config(cmake_cfg)

    if not args.no_build:
        build_mod = importlib.import_module("task-build")
        key = args.config or "release"
        if key.lower() in _BUILD_CONFIGS:
            build_arg = key.lower()
        elif key in _BUILD_CONFIGS.values():
            build_arg = next(k for k, v in _BUILD_CONFIGS.items() if v == key)
        else:
            build_arg = key
        ret = build_mod.main([build_arg])
        if ret != 0:
            return ret

    if not bin_path.is_file():
        print(f"Firmware image not found: {bin_path}", file=sys.stderr)
        print("Run a build first or drop --no-build.", file=sys.stderr)
        return 1

    return _run_openocd_with_recovery(bin_path, verbose=args.verbose)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
