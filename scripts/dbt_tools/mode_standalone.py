import sys
import os
from git import Repo, InvalidGitRepositoryError
from SCons.Script import GetLaunchDir, GetOption, Exit
from SCons.Action import Action
from dbt.exit_codes import ExitCodes
from dbt.util import vcheck, vprint

BUILD_HARDWARE_CHOICE = ["7seg", "oled"]
BUILD_TYPE_CHOICE = ["debug", "release"]
BUILD_PREFIX_CHOICE = ["dbt"]

if GetOption("e2_orig_prefix"):
    BUILD_PREFIX_CHOICE.append("e2")

EXIT_VALUE = False


def _compose_valid_targets(env):
    """Target name composition"""
    valid_targets = []

    for bp in BUILD_PREFIX_CHOICE:
        for bt in BUILD_TYPE_CHOICE:
            for bh in BUILD_HARDWARE_CHOICE:
                val_targ = "{}-build-{}-{}".format(bp, bt, bh)
                valid_targets.append(val_targ)

    return valid_targets


def _validate_build_target(env, valid_targets):
    if env["BUILD_LABEL"] in valid_targets:
        return True
    return False


def exists():
    if GetOption("base_config") == "standalone":
        return True

    return False


def generate(env, **kwargs):
    env.Replace(**kwargs)

    valid_targets = _compose_valid_targets(env)
    env["BUILD_TARGET_IS_VALID"] = False
    if _validate_build_target(env, valid_targets):
        vprint("Found valid standalone target: {}".format(env["BUILD_LABEL"]))
        env["BUILD_TARGET_IS_VALID"] = True
    else:
        vprint(
            'Error: "{}" is not a valid standalone target.\n'.format(env["BUILD_LABEL"])
        )
        vprint("       Valid targets include:")
        for bt in valid_targets:
            vprint("           {}".format(bt))
        vprint("")

        return

    # Add useful shorthands we may need later

    target_parts = env["BUILD_LABEL"].split("-")
    env["BUILD_PREFIX"] = target_parts[0]
    env["BUILD_TYPE"] = target_parts[2]
    env["BUILD_HARDWARE"] = target_parts[3]

    # Build particulars

    env["FIRMWARE_FILENAME"] = "Deluge-{}-{}".format(
        env["BUILD_TYPE"], env["BUILD_HARDWARE"]
    )
    env["MAPFILE"] = "{}".format(
        os.path.join(env["BUILD_DIR"], "{}.map".format(env["FIRMWARE_FILENAME"]))
    )
    env["PROGSUFFIX"] = ".elf"
