import os
import re
import logging

# Set the absolute current working directory so we can pass it around later
REL_SOURCE_DIR = "src"
PROJECT_SOURCE_DIR = os.path.abspath(REL_SOURCE_DIR)

print("Deluge Build Tool (DBT)\n")

logging.basicConfig(level=logging.DEBUG)
log = logging.getLogger("DBT")
log.debug("Project directory: {}".format(GetLaunchDir()))

# Commandline arguments are defined in here
cmd_vars = SConscript("site_scons/commandline.scons", exports={"log_parent": log})
# cmd_env = Environment(
#     toolpath=["#/scripts/dbt_tools"],
#     tools=[
#         ("dbt_help", {"vars": cmd_vars}),
#     ],
#     variables=cmd_vars,
# )

env_parent = DefaultEnvironment()
env_parent["ENV"] = os.environ.copy()
env_parent.Append(
    BUILDERS={
        "HEXBuilder": Builder(
            action=Action(
                '${OBJCOPY} -O ihex "${SOURCE}" "${TARGET}"',
                "${HEXCOMSTR}",
            ),
            suffix=".hex",
            src_suffix=".elf",
        ),
        "BINBuilder": Builder(
            action=Action(
                '${OBJCOPY} -O binary -S "${SOURCE}" "${TARGET}"',
                "${BINCOMSTR}",
            ),
            suffix=".bin",
            src_suffix=".elf",
        ),
    }
)


def create_source_globs(base, globs=["*.cpp", "*.CPP", "*.c", "*.C"], env=env_parent):
    files = []
    for glob in globs:
        files.extend(env.Glob(os.path.join(base, glob), source=True, strings=True))

    dirs = [
        os.path.join(base, name)
        for name in os.listdir(base)
        if os.path.isdir(os.path.join(base, name)) and name[0] != "."
    ]
    dirs.sort()

    for dir in dirs:
        files.extend(create_source_globs(dir, globs, env))

    return files


if GetOption("base_config") not in ["e2_xml"]:
    print("Error: invalid base config mode.")

    Exit(2)


if GetOption("base_config") == "e2_xml":
    from dbt.project import E2Project

    log.info("Detected e2_xml mode.")

    cproject = E2Project(log, GetLaunchDir())

    e2_build_targets = list(cproject.get_build_targets())
    dbt_build_targets = [p.replace("e2-build-", "dbt-build-") for p in e2_build_targets]
    e2_build_targets.sort()
    dbt_build_targets.sort()

    if GetOption("e2_target") not in dbt_build_targets:
        print('Error: "{}" is not a valid target in the e2_xml config.\n'.format(tgt))
        print("       Valid targets include:")
        for bt in dbt_build_targets:
            print("           {}".format(bt))
        print("")
        Return()

    build_label = GetOption("e2_target")
    env = env_parent.Clone()

    # build_label = "{}build-{}-{}".format(
    #     GetOption("build_dir_prefix"),
    #     "debug" if GetOption("build_debug") else "release",
    #     GetOption("build_model"),
    # )

    dbt_target_index = dbt_build_targets.index(build_label)
    e2_target = e2_build_targets[dbt_target_index]
    build_dir = os.path.abspath(build_label)
    source_dir = "src"
    build_src_dir = os.path.join(build_dir, source_dir)
    build_segs = build_label.split("-")
    firmware_filename = cproject.get_target_filename(e2_target)

    # env.Replace(VARIANT_DIR=build_label, SOURCE_DIR=source_dir)
    Mkdir(build_label)
    Mkdir(os.path.relpath(os.path.join(build_dir, "src")))
    env.VariantDir(
        os.path.relpath(os.path.join(build_dir, "src")), "#src", duplicate=False
    )

    # Build construction environment for selected or default e2 target
    env.Replace(**cproject.get_toolchain_tools(e2_target))
    # log.debug([i for i, v in dict(env).items() if v])
    env["ASFLAGS"] = " -x assembler-with-cpp"
    env["ASMPATH"] = [
        Dir("{}".format(inc)) for inc in list(cproject.get_asm_includes(e2_target))
    ]
    env["ASCOM"] = "$CC $CCFLAGS $ASFLAGS $ASPATH -o $TARGET -c $SOURCE"
    env["ASPPCOMSTR"] = "Assembling $TARGET"
    env["CCCOMSTR"] = "Compiling static object $TARGET"
    env["CCFLAGS"] = cproject.get_c_flags(e2_target)
    env["CPPFLAGS"] = cproject.get_cpp_flags(e2_target)
    env["CPPFLAGS"].append("-fdiagnostics-parseable-fixits")
    env["CPPPATH"] = [
        Dir("{}".format(p))
        for p in list(
            cproject.get_asm_includes(e2_target)
            + cproject.get_c_includes(e2_target)
            + cproject.get_cpp_includes(e2_target)
        )
    ]
    # log.debug(env["CPPPATH"])
    env["ASPATH"] = " {}".format(
        " ".join(['-I"{}"'.format(inc) for inc in env["ASMPATH"]])
    )
    env["CCCOM"] = "$CC -o $TARGET -c $CFLAGS $CCFLAGS $_CCCOMCOM $SOURCES"
    env["CXXCOM"] = "$CXX -o $TARGET -c $CXXFLAGS $CCFLAGS $_CCCOMCOM $SOURCES"
    env["CXXCOMSTR"] = "Compiling static object $TARGET"
    env["CPPDEFINES"] = cproject.get_preprocessor_defs(e2_target)
    env["PROGSUFFIX"] = cproject.get_target_ext(e2_target)
    env["LINKCOMSTR"] = "Linking $TARGET"
    build_src_dir = os.path.join(build_dir, "src")
    env["MAPFILE"] = "{}".format(
        os.path.join(
            build_dir, "{}.map".format(cproject.get_target_filename(e2_target))
        )
    )
    env["LINKFLAGS"] = cproject.get_link_flags(e2_target)
    env["LINKCOM"] = "$CXX $CPPFLAGS -o $TARGET $SOURCES $LINKFLAGS"
    env["FIRMWARE_FILENAME"] = cproject.get_target_filename(e2_target)
    env["BINCOMSTR"] = "Converting .elf to .bin."
    # env["LIBPATH"] = [str(i) for i in cproject.get_link_libs_order(e2_target)]
    # log.debug(env["LIBPATH"])


# Collect sources recursively through globbing
asm_sources = create_source_globs("src", ["*.s", "*.S"], env=env)
c_sources = create_source_globs("src", env=env)
sources = [
    File(os.path.relpath(os.path.join(build_dir, os.path.relpath(src))))
    for src in asm_sources + c_sources
]

objects = env.Object(c_sources + asm_sources)

elf_file = env.Program(
    os.path.join(build_dir, env["FIRMWARE_FILENAME"]), source=objects
)

env.BINBuilder(env["FIRMWARE_FILENAME"], source=elf_file)
