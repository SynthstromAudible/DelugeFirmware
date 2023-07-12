"""This file was borrowed from:
https://github.com/flipperdevices/flipperzero-firmware/

Licensed under GNU GPL v3"""

from SCons.Action import Action
from SCons.Builder import Builder


def generate(env):
    env.SetDefault(
        STRIP="strip",
        STRIPFLAGS=[],
        STRIPCOM="$STRIP $STRIPFLAGS $SOURCES -o $TARGET",
    )
    env.Append(
        BUILDERS={
            "ELFStripper": Builder(
                action=Action(
                    "${STRIPCOM}",
                    "${STRIPCOMSTR}",
                ),
                suffix=".elf",
                src_suffix=".elf",
            ),
        }
    )


def exists(env):
    return True
