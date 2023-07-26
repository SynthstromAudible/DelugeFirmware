import os
from SCons.Script import GetOption
from dbt.util import vcheck, vprint


def compose_valid_targets(cmd_env=None):
    """Target name composition"""

    if GetOption("e2_orig_prefix"):
        build_prefix_choice = ["e2"]
    else:
        build_prefix_choice = ["dbt"]

    if cmd_env:
        # When the variable is None/undefined select both
        build_type_choice = []
        if cmd_env.get("DEBUG", None) is None:
            build_type_choice.append("debug")
            build_type_choice.append("release")
        elif int(cmd_env.get("DEBUG", "1")) == 1:
            build_type_choice.append("debug")
        else:
            build_type_choice.append("release")

        # When the variable is None/undefined select both
        build_hardware_choice = []
        if cmd_env.get("OLED", None) is None:
            build_hardware_choice.append("7seg")
            build_hardware_choice.append("oled")
        elif int(cmd_env.get("OLED", "1")):
            build_hardware_choice.append("oled")
        else:
            build_hardware_choice.append("7seg")

    else:
        build_type_choice = ["debug", "release"]
        build_hardware_choice = ["7seg", "oled"]

    valid_targets = []

    for bp in build_prefix_choice:
        for bt in build_type_choice:
            for bh in build_hardware_choice:
                val_targ = "{}-build-{}-{}".format(bp, bt, bh)
                valid_targets.append(val_targ)

    return valid_targets
