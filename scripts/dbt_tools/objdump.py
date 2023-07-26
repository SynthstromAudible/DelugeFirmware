"""This file was borrowed from:
https://github.com/flipperdevices/flipperzero-firmware/

Licensed under GNU GPL v3"""

from SCons.Action import Action
from SCons.Builder import Builder


def generate(env):
    env.SetDefault(
        OBJDUMP="objdump",
        OBJDUMPFLAGS=[],
        OBJDUMPCOM="$OBJDUMP $OBJDUMPFLAGS -S $SOURCES > $TARGET",
    )
    env.Append(
        BUILDERS={
            "ObjDump": Builder(
                action=Action(
                    "${OBJDUMPCOM}",
                    "${OBJDUMPCOMSTR}",
                ),
                suffix=".lst",
                src_suffix=".elf",
            ),
        }
    )


def exists(env):
    return True
