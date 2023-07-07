"""This file was borrowed from:
https://github.com/flipperdevices/flipperzero-firmware/

Licensed under GNU GPL v3"""

import subprocess

import gdb
import objdump
import objcopy
import strip
import size
from dbt.util import vcheck, vprint
from SCons.Action import _subproc
from SCons.Errors import StopError
from SCons.Tool import ar, asm, gcc, gnulink, gxx
from SCons.Script import GetOption


def prefix_commands(env, command_prefix, cmd_list):
    for command in cmd_list:
        if command in env:
            env[command] = command_prefix + env[command]


def _get_tool_version(env, tool):
    verstr = "version unknown"
    proc = _subproc(
        env,
        env.subst("${%s} --version" % tool),
        stdout=subprocess.PIPE,
        stderr="devnull",
        stdin="devnull",
        universal_newlines=True,
        error="raise",
        shell=True,
    )
    if proc:
        verstr = proc.stdout.readline()
        proc.communicate()
    return verstr


def generate(env, **kw):
    if vcheck() < 3:
        if GetOption("no_exec"):
            env["CCCOMSTR"] = "Simulating compile of static object $TARGET"
            env["CXXCOMSTR"] = "Simulating compile of static object $TARGET"
            env["ASCOMSTR"] = "Pretending to assemble $TARGET"
            env["ASPPCOMSTR"] = "Pretending to assemble $TARGET"
            env["LINKCOMSTR"] = "Make-believe linking $TARGET"
            env["BINCOMSTR"] = "LARPing a conversion of .elf to .bin."
            env["HEXCOMSTR"] = "Role playing a conversion of .elf to .hex."
            env["SIZECOMSTR"] = "Lying about saving .elf size information to .siz."
            env["TOUCHCOMSTR"] = "Encouraging $TARGET in theory"
            env["FAKEDIRCOMSTR"] = "Giving moral support to directories."
        else:
            env["CCCOMSTR"] = "Compiling static object $TARGET"
            env["CXXCOMSTR"] = "Compiling static object $TARGET"
            env["ASCOMSTR"] = "Assembling $TARGET"
            env["ASPPCOMSTR"] = "Assembling $TARGET"
            env["LINKCOMSTR"] = "Linking $TARGET"
            env["BINCOMSTR"] = "Converting .elf to .bin."
            env["HEXCOMSTR"] = "Converting .elf to .hex."
            env["SIZECOMSTR"] = "Saving .elf size information to .siz."
            env["TOUCHCOMSTR"] = "Encouraging $TARGET"
            env["FAKEDIRCOMSTR"] = "Giving moral support to directories."

    for orig_tool in (gcc, gxx, asm, ar, gnulink, strip, gdb, objdump, objcopy, size):
        orig_tool.generate(env)
    env.SetDefault(
        TOOLCHAIN_PREFIX=kw.get("toolchain_prefix"),
    )
    prefix_commands(
        env,
        env.subst("$TOOLCHAIN_PREFIX"),
        [
            "AR",
            "AS",
            "CC",
            "CXX",
            "OBJCOPY",
            "RANLIB",
            "STRIP",
            "GDB",
            "GDBPY",
            "OBJDUMP",
            "SIZE",
        ],
    )
    # Call CC to check version
    if whitelisted_versions := kw.get("versions", ()):
        cc_version = _get_tool_version(env, "CC")
        # print("CC version =", cc_version)
        # print(list(filter(lambda v: v in cc_version, whitelisted_versions)))
        if not any(filter(lambda v: v in cc_version, whitelisted_versions)):
            raise StopError(
                f"Toolchain version is not supported. Allowed: {whitelisted_versions}, toolchain: {cc_version} "
            )


def exists(env):
    return True
