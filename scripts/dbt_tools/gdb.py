"""This file was borrowed from:
https://github.com/flipperdevices/flipperzero-firmware/

Licensed under GNU GPL v3"""
import os
import shutil


def generate(env):
    env.SetDefault(
        GDB="gdb",
        GDBPY="gdb-py3",
        GDBCOM="$GDB $GDBOPTS $SOURCES",  # no $TARGET
        GDBPYCOM="$GDBPY $GDBOPTS $GDBPYOPTS $SOURCES",  # no $TARGET
    )


def exists(env):
    return True
