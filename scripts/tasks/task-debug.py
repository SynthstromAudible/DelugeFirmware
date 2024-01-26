import argparse
from pathlib import Path
import shutil
import util

TARGET_DEVICE = "R7S721020"
OPENOCD_PREFIX = "toolchain/win32-x64/openocd/openocd/scripts/"
OPENOCD_TARGET_DEVICE_SWD = "scripts/debug/openocd/deluge-swj-swd.cfg"
OPENOCD_TARGET_DEVICE_JTAG = "scripts/debug/openocd/deluge-swj-jtag.cfg"


def find_cmd_with_fallback(cmd, fallback):
    which_result = shutil.which(cmd)
    if which_result:
        return which_result
    else:
        return fallback


def absolute_path_str(path: str):
    return str(Path(path).absolute())


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
        cmd = find_cmd_with_fallback("openocd", "openocd")

    # if we haven't gotten an interface with a path already, qualify it
    if len(target.split("/")) == 1:
        target = f"target/{target}.cfg"

    return [
        cmd,
        "-f",
        absolute_path_str(OPENOCD_PREFIX + f"interface/{interface}.cfg"),
        "-f",
        absolute_path_str(target),
        "-c",
        f'"transport select {protocol}"',
        "-c",
        f'"gdb_port {gdb_port}"',
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


def get_openocd_target(target: str, protocol: str) -> str:
    if target == TARGET_DEVICE:
        if protocol == "jtag":
            target = OPENOCD_TARGET_DEVICE_JTAG
        else:
            target = OPENOCD_TARGET_DEVICE_SWD
            if protocol != "swd":
                print("Warning! Interface unsupported, falling back to SWD")
    else:
        target = target

    return target


def main() -> int:
    args = argparser().parse_args()
    if args.jlink:
        cmd = jlink_gdb(
            args.command, args.target_device, "little", args.protocol, args.gdb_port
        )
    else:
        target = get_openocd_target(args.target_device, args.protocol)
        cmd = openocd_gdb(
            args.command, args.interface, target, args.protocol, args.gdb_port
        )

    if args.verbose:
        print(" ".join(cmd))
    return util.run(cmd, False)


if __name__ == "__main__":
    try:
        exit(main())
    except KeyboardInterrupt:
        exit(130)
