"""This file was borrowed from:
https://github.com/flipperdevices/flipperzero-firmware/

Licensed under GNU GPL v3"""

import os
import shutil
import SCons
from SCons.Action import Action
from SCons.Builder import Builder
from SCons.Defaults import Touch

__OPENOCD_BIN = shutil.which("openocd")

_oocd_action = Action(
    "${OPENOCD} ${OPENOCD_OPTS} ${OPENOCD_COMMAND}",
    "${OPENOCDCOMSTR}",
)


def generate(env):
    env.SetDefault(
        OPENOCD=__OPENOCD_BIN,
        OPENOCD_OPTS=[
            "-f",
            '"{}"'.format(os.path.relpath(os.path.join("interface", "cmsis-dap.cfg"))),
            "-f",
            '"{}"'.format(
                os.path.relpath(os.path.join("contrib", "renesas-rz-a1lu.cfg"))
            ),
        ],
        OPENOCD_COMMAND=[],
        OPENOCDCOM="${OPENOCD} ${OPENOCD_OPTS} ${OPENOCD_COMMAND}",
        OPENOCDCOMSTR="",
    )

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
