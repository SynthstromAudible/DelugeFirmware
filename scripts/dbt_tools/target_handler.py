import os
from SCons.Action import Action
from SCons.Script import (
    ARGUMENTS,
    BUILD_TARGETS,
    GetOption,
    SetOption,
    SConscript,
    Exit,
)
from dbt.project import compose_valid_targets
from dbt.util import vcheck, vprint


def _target_all_handler(env, target):
    all_list = []
    env.Alias(target, env["VALID_BUILD_TARGETS"])
    for sub_target in env["VALID_BUILD_TARGETS"]:
        all_list.append(_target_normal_handler(env, sub_target))

    return all_list


def _target_clean_handler(env, target):
    clean_list = _target_all_handler(env, target)
    SetOption("clean", True)

    return clean_list


def _target_openocd_handler(env, target):
    util_env = env.Clone(ENV_LABEL=target, tools=["debug_opts", "openocd"])
    util_env.PhonyTarget(target, "${OPENOCDCOM}")

    return util_env


def _target_shell_handler(env, target):
    util_env = env.Clone(ENV_LABEL=target, tools=["shell"])
    util_env.PhonyTarget(
        target, Action(util_env.CallShell, "Running shell in DBT environment.")
    )

    return util_env


def _target_debug_handler(env, target):
    """Create an environment for debugging

    Target name is either "debug-oled" or "debug-7seg" which
    is combined with the dbt-prefix to make

    """
    util_env = env.Clone(ENV_LABEL=target, tools=["debug_opts"])
    legacy_prefix = "e2" if GetOption("e2_orig_prefix") else "dbt"
    elf_target = "{}-build-{}".format(legacy_prefix, target)

    # Use the standalone build mode to determine the firmware
    # filename so our logic remains consistent
    scrap_env = env.Clone()
    scrap_env.Tool(
        "mode_standalone",
        BUILD_LABEL=elf_target,
        BUILD_DIR=os.path.abspath(elf_target),
        SOURCE_DIR=os.path.relpath("src"),
    )

    elf_file = os.path.join(
        elf_target, "{}.elf".format(scrap_env.get("FIRMWARE_FILENAME"))
    )

    util_env.PhonyTarget(
        target,
        "${GDBCOM}",
        source=elf_file,
        GDBOPTS="${GDBOPTS_BASE}",
        GDBREMOTE=ARGUMENTS.get("GDB_REMOTE", "localhost:3333"),
    )

    return util_env


def _target_normal_handler(env, target):
    build_env = env.Clone(ENV_LABEL=target, BUILD_MODE="standalone")
    build_env.Tool(
        "mode_standalone",
        BUILD_LABEL=target,
        BUILD_DIR=os.path.abspath(target),
        SOURCE_DIR=os.path.relpath("src"),
    )

    SConscript(
        os.path.join("site_scons", "build_opts.scons"), exports={"env": build_env}
    )

    return build_env


SPECIAL_TARGETS = {
    "all": _target_all_handler,
    "build": _target_all_handler,
    "clean": _target_clean_handler,
    "openocd": _target_openocd_handler,
    "shell": _target_shell_handler,
    "debug-oled": _target_debug_handler,
    "debug-7seg": _target_debug_handler,
}


def PrepareTargetEnvs(env):
    """Creates a new SCons env for each target

    Returns a list of envs.
    """
    final_envs = []

    for target in env["FINAL_TARGETS"]:
        if target in SPECIAL_TARGETS.keys():
            result = SPECIAL_TARGETS[target](env, target)
            if isinstance(result, list):
                final_envs.extend(result)
            else:
                final_envs.append(result)
        else:
            final_envs.append(_target_normal_handler(env, target))

    return final_envs


def exists():
    """Stop if there are no targets"""
    if len(BUILD_TARGETS) > 0:
        return True
    else:
        return False


def generate(env, cmd_env):
    env.SetDefault(
        # Valid targets are only those left after applying filters!
        VALID_BUILD_TARGETS=compose_valid_targets(cmd_env),
        FINAL_TARGETS=[],
    )

    env.AddMethod(PrepareTargetEnvs)

    # iterate through BUILD_TARGETS and catalogue used targets
    found_special_targets = set()
    found_valid_build_targets = set()
    found_unknown_targets = []
    for target in BUILD_TARGETS:
        if target in env["VALID_BUILD_TARGETS"]:
            found_valid_build_targets.add(target)
        elif target in SPECIAL_TARGETS.keys():
            found_special_targets.add(target)
        else:
            found_unknown_targets.append(target)

    if found_unknown_targets:
        vprint(
            "Don't know what to do with unknown target(s): [{}]".format(
                ", ".join(found_unknown_targets)
            )
        )
        Exit(4)

    if len(found_special_targets) > 1:
        vprint(
            "Only one of the targets [{}], may be used at a time.".format(
                ", ".join(found_special_targets)
            )
        )
        Exit(4)

    # Don't cross the streams!
    if found_special_targets and found_valid_build_targets:
        spec_target = list(found_special_targets)[0]
        vprint(
            "Cannot use target [{}] and targets [{}] at the same time.".format(
                spec_target, ", ".join(found_valid_build_targets)
            )
        )
        Exit(4)

    if len(found_special_targets) == 1:
        spec_target = list(found_special_targets)[0]
        env["FINAL_TARGETS"].append(spec_target)

    for target in found_valid_build_targets:
        env["FINAL_TARGETS"].append(target)
