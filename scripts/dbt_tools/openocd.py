"""This file was borrowed from:
https://github.com/flipperdevices/flipperzero-firmware/

Licensed under GNU GPL v3"""

import os
import shutil
import SCons
from SCons.Action import Action
from SCons.Builder import Builder
from SCons.Defaults import Touch
from SCons.Script import ARGUMENTS, Exit
from dbt.util import vcheck, vprint
from dbt.exit_codes import ExitCodes

__OPENOCD_BIN = shutil.which("openocd")

_oocd_action = Action(
    "${OPENOCD} ${OPENOCD_OPTS} ${OPENOCD_COMMAND}",
    "${OPENOCDCOMSTR}",
)

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


def generate(env):
    env.SetDefault(
        OPENOCD=__OPENOCD_BIN,
        OPENOCD_SCRIPT_DIR=os.path.join(env["DBT_DEBUG_DIR"], "openocd"),
        OPENOCD_OPTS=[],
        OPENOCD_COMMAND=[],
        OPENOCDCOM="${OPENOCD} ${OPENOCD_OPTS} ${OPENOCD_COMMAND}",
        OPENOCDCOMSTR="",
    )

    hardware = env.get("DEBUG_HARDWARE", "delugeprobe")
    protocol = env.get(
        "DEBUG_PROTOCOL",
    )

    if not hardware in _OPENOCD_CONFIG_MAP.keys():
        vprint("Debugging hardware [{}] is not yet supported.".format(hardware))
        Exit(value=ExitCodes.OPENOCD_UNSUPPORTED_HARDWARE)

    if not protocol in _OPENOCD_CONFIG_MAP[hardware].keys():
        better_protocol = _get_acceptable_protocol(hardware, protocol)
        vprint(
            "Debugging protocol [{}] is not supported on hardware [{}].".format(
                protocol, hardware
            )
        )
        vprint("Trying supported protocol [{}] instead.".format(better_protocol))
        protocol = better_protocol

    cfg_files = _OPENOCD_CONFIG_MAP[hardware][protocol]

    ocd_cmd = []
    for cfg_part in cfg_files:
        ocd_cmd.append("-f")
        ocd_cmd.append('"{}"'.format(os.path.join(env["OPENOCD_SCRIPT_DIR"], cfg_part)))

    env["OPENOCD_COMMAND"] = ocd_cmd

    env.Append(
        BUILDERS={
            "OpenOCDFlash": Builder(
                action=[
                    _oocd_action,
                    Touch("${TARGET}"),
                ],
                suffix=".flash",
                src_suffix=".bin",
            ),
        }
    )


def exists(env):
    try:
        return env["OPENOCD"]
    except KeyError:
        pass

    if openocd := env.WhereIs(__OPENOCD_BIN):
        return openocd

    raise SCons.Errors.StopError("Could not detect openocd")
