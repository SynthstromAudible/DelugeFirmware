#! /usr/bin/env python3
import argparse
from pathlib import Path
import subprocess
import shutil
import sys
import os

# Based on dbt_tools/openocd.py by litui
class ExitCodes(Enum):
    """Exit Code Enum

    Add to this list in scripts/dbt/exit_codes.py when you want
    to Exit() with a new errorlevel/exitcode"""

    SUCCESS = 0
    UNKNOWN_ERROR = 1
    INVALID_GIT_REPOSITORY = 2
    FAILED_TO_START_STANDALONE_MODE = 3
    COMMANDLINE_VALIDATION_FAILURE = 4
    OPENOCD_UNSUPPORTED_HARDWARE = 5
    OPENOCD_UNSUPPORTED_PROTOCOL = 6

__OPENOCD_BIN = shutil.which("openocd")

OPENOCD = __OPENOCD_BIN
OPENOCD_SCRIPT_DIR = os.path.join(os.environ["DBT_DEBUG_DIR"], "openocd")
OPENOCD_OPTS = []
OPENOCD_COMMAND = []
OPENOCDCOM = "${OPENOCD} ${OPENOCD_OPTS} ${OPENOCD_COMMAND}"
OPENOCDCOMSTR = ""


_oocd_action = [
    "${OPENOCD} ${OPENOCD_OPTS} ${OPENOCD_COMMAND}",
    "${OPENOCDCOMSTR}",
]

# hardware_type -> protocol = filenames
# Make sure dependencies are sourced within the file
_OPENOCD_CONFIG_MAP = {
    "delugeprobe": {
        "jtag": [os.path.join("interface", "delugeprobe.cfg"), "deluge-swj-jtag.cfg"],
        "swd": [os.path.join("interface", "delugeprobe.cfg"), "deluge-swj-swd.cfg"],
    },
    "esp-prog": {
        "jtag": [os.path.join("interface", "esp-prog.cfg"), "deluge-jtag.cfg"],
    },
}


def _get_acceptable_hardware(hardware):
    if hardware in _OPENOCD_CONFIG_MAP.keys():
        return hardware

    # Always default to delugeprobe if weird stuff
    # is entered
    return "delugeprobe"


def _get_acceptable_protocol(hardware, protocol):
    checked_hw = _get_acceptable_hardware(hardware)
    protocols = _OPENOCD_CONFIG_MAP[checked_hw]

    if protocol in protocols.keys():
        return protocol

    # If provided protocol is not in list
    for k, v in protocols.items():
        return k

    # If for whatever reason there are no listed protocols
    return None


def exists():
    if not OPENOCD:
        raise FileNotFoundError("Could not detect openocd")

    return OPENOCD


def argparser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="openocd",
        description="Run OpenOCD with default arguments (CMSIS-DAP/DelugeProbe).\nThese can be changed using the OPENOCD_OPTS variable.",
    )
    parser.group = "Debugging"
    parser.add_argument(
        "-v",
        "--verbose",
        help="print the command used to call openocd",
        action="store_true",
    )
    return parser


def main() -> int:
    args = argparser().parse_args()

    hardware = os.environ.get("DEBUG_HARDWARE") or "delugeprobe"
    protocol = os.environ.get("DEBUG_PROTOCOL") or "swd"

    if not hardware in _OPENOCD_CONFIG_MAP.keys():
        print(f"Debugging hardware [{hardware}] is not yet supported.")
        sys.exit(ExitCodes.OPENOCD_UNSUPPORTED_HARDWARE)

    if not protocol in _OPENOCD_CONFIG_MAP[hardware].keys():
        better_protocol = _get_acceptable_protocol(hardware, protocol)
        print(
            f"Debugging protocol [{protocol}] is not supported on hardware [{hardware}]."
        )
        print(f"Trying supported protocol [{better_protocol}] instead.")
        protocol = better_protocol

    cfg_files = _OPENOCD_CONFIG_MAP[hardware][protocol]

    ocd_cmd = [__OPENOCD_BIN]
    for cfg_part in cfg_files:
        ocd_cmd.append("-f")
        path = os.path.join(OPENOCD_SCRIPT_DIR, cfg_part)
        ocd_cmd.append(f"{path}")

    if args.verbose:
        print(" ".join(ocd_cmd))
    result = subprocess.run(ocd_cmd)
    return result.returncode


if __name__ == "__main__":
    main()
