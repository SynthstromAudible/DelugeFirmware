import argparse
import shutil
import sys
from pathlib import Path

import util

TARGET_DEVICE = "R7S721020"
# The Deluge's OpenOCD configs (OpenOCD 0.12 or newer). The binary is whichever `openocd` is first
# on PATH: dbt appends the toolchain's, so a system-wide install takes precedence.
OPENOCD_SCRIPT_DIR = Path(__file__).resolve().parents[1] / "debug" / "openocd"
OPENOCD_DEVICE_CFG = {"swd": "deluge-swj-swd.cfg", "jtag": "deluge-swj-jtag.cfg"}
OPENOCD_INSTALL_HINT = (
    "openocd not found on PATH. Run this through ./dbt (its toolchain bundles one), install "
    "OpenOCD 0.12 or newer, or pass --command."
)


def find_cmd_with_fallback(cmd, fallback):
    which_result = shutil.which(cmd)
    if which_result:
        return which_result
    else:
        return fallback


def jlink_gdb(cmd, device: str, endian: str, protocol: str, gdb_port: int):
    if not cmd:
        cmd = find_cmd_with_fallback(
            "JLinkGDBServerCL", "C:\\Program Files\\SEGGER\\JLink\\JLinkGDBServerCL"
        )

    return [
        cmd,
        "-if",
        protocol,
        "-device",
        device,
        "-endian",
        endian,
        "-nogui",
        "-ir",
        "-port",
        str(gdb_port),
    ]


def openocd_gdb(cmd, interface: str, target: str, protocol: str, gdb_port: int):
    if not cmd:
        cmd = shutil.which("openocd")
        if not cmd:
            raise FileNotFoundError(OPENOCD_INSTALL_HINT)
        print(f"Using OpenOCD: {cmd}", flush=True)

    if target == TARGET_DEVICE:
        # The Deluge config selects the transport itself.
        target_args = ["-f", OPENOCD_DEVICE_CFG[protocol]]
    else:
        # Another chip: an OpenOCD target name (e.g. "stm32f4x") or a path to a .cfg.
        if "/" not in target and not target.endswith(".cfg"):
            target = f"target/{target}.cfg"
        target_args = ["-c", f"transport select {protocol}", "-f", target]

    # No shell runs this, so each -c is passed as one argv entry without quotes.
    # -s puts the repo's configs (and interface/delugeprobe.cfg) ahead of OpenOCD's own scripts.
    return [
        cmd,
        "-s",
        str(OPENOCD_SCRIPT_DIR),
        "-f",
        f"interface/{interface}.cfg",
        *target_args,
        "-c",
        f"gdb_port {gdb_port}",
    ]


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="debug",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
        description="Run a debug server (JLink or OpenOCD)",
    )
    parser.group = "Debugging"
    parser.add_argument(
        "-j", "--jlink", action="store_true", help="Use JLinkGDB instead of OpenOCD"
    )
    parser.add_argument(
        "-c",
        "--command",
        help="Explicitly define the Jlink or OpenOCD executable to call",
        type=str,
    )
    parser.add_argument(
        "-d",
        "--target-device",
        help="The target device",
        default=TARGET_DEVICE,
        type=str,
    )
    parser.add_argument(
        "-g", "--gdb-port", help="The port for gdb to listen on", default=3333, type=int
    )
    parser.add_argument(
        "-v", "--verbose", help="Print the called command", action="store_true"
    )
    parser.add_argument(
        "-p",
        "--protocol",
        choices=["swd", "jtag"],
        help="Select the communication protocol",
        default="swd",
    )
    parser.add_argument(
        "-i",
        "--interface",
        help="Select the debug interface (OpenOCD only)",
        default="cmsis-dap",
        type=str,
    )
    return parser


def main() -> int:
    args = argparser().parse_args()
    if args.jlink:
        cmd = jlink_gdb(
            args.command, args.target_device, "little", args.protocol, args.gdb_port
        )
    else:
        try:
            cmd = openocd_gdb(
                args.command,
                args.interface,
                args.target_device,
                args.protocol,
                args.gdb_port,
            )
        except FileNotFoundError as e:
            print(f"ERROR: {e}", file=sys.stderr)
            return 1

    if args.verbose:
        print(" ".join(cmd))
    return util.run(cmd, False)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
