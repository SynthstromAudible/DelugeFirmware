import os
import re
import logging

Progress(["OwO\r", "owo\r", "uwu\r", "owo\r"], interval=15)

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


def walk_all_sources(base, prefix):
    walker = list(os.walk(base))
    sources = [
        os.path.join(prefix, os.path.relpath(os.path.join(i[0], l)))
        for i in walker
        for l in i[2]
        if re.match(r"\.[cs][p]{0,2}", os.path.splitext(l)[1], re.IGNORECASE)
    ]
    return sources


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
        print(
            'Error: "{}" is not a valid target in the e2_xml config.\n'.format(
                GetOption("e2_target")
            )
        )
        print("       Valid targets include:")
        for bt in dbt_build_targets:
            print("           {}".format(bt))
        print("")
        Return()

    build_label = GetOption("e2_target")
    env = Environment(ENV=os.environ.copy())

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

    # Build construction environment for selected or default e2 target
    env.Replace(**cproject.get_toolchain_tools(e2_target))
    # log.debug([i for i, v in dict(env).items() if v])
    env["ASPPFLAGS"] += " -x assembler-with-cpp"
    env["CCFLAGS"] = " ".join(cproject.get_c_flags(e2_target))
    env["CPPFLAGS"] = " ".join(cproject.get_cpp_flags(e2_target))
    env["CPPFLAGS"] += " -fdiagnostics-parseable-fixits"
    env["ASMPATH"] = [str(p) for p in cproject.get_asm_includes(e2_target)]
    env["CCPATH"] = [str(p) for p in cproject.get_c_includes(e2_target)]
    env["CXXPATH"] = [str(p) for p in cproject.get_cpp_includes(e2_target)]
    env["CPPPATH"] = env["ASMPATH"] + env["CCPATH"] + env["CXXPATH"]
    # log.debug(env["CPPPATH"])

    env["CCCOMSTR"] = "Compiling static object $TARGET"
    env["CXXCOMSTR"] = "Compiling static object $TARGET"
    env["ASPPCOMSTR"] = "Assembling $TARGET"
    env["LINKCOMSTR"] = "Linking $TARGET"

    env["ASPATH"] = " {}".format(
        " ".join(['-I"{}"'.format(inc) for inc in env["ASMPATH"]])
    )
    env[
        "ASPPCOM"
    ] = "$CC $ASPPFLAGS $CPPFLAGS $_CPPDEFFLAGS $_CPPINCFLAGS -c -o $TARGET $SOURCES"
    env["ASCOM"] = "$CC $CCFLAGS $ASFLAGS $ASPATH -o $TARGET -c $SOURCE"
    # env["CCCOM"] = "$CC -o $TARGET -c $CFLAGS $CCFLAGS $_CCCOMCOM $SOURCES"
    # env["CXXCOM"] = "$CXX -o $TARGET -c $CXXFLAGS $CCFLAGS $_CCCOMCOM $SOURCES"

    env["CPPDEFINES"] = cproject.get_preprocessor_defs(e2_target)
    env["PROGSUFFIX"] = cproject.get_target_ext(e2_target)
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
    env["LIBPATH"] = [str(i) for i in cproject.get_link_libs_order(e2_target)]
    # log.debug(env["LIBPATH"])


env.Append(
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

# VariantDir does the magic to ensure output goes to the dbt- whatever
# build directory. Careful: this is really finicky if paths aren't set right
VariantDir(os.path.join(build_label, source_dir), "#src", duplicate=False)

# Using the specified include dirs rather than walking every path in src was
# preventing a successful .elf build so generically went with the latter approach.
sources = walk_all_sources(source_dir, build_label)

objects = env.Object(sources)

elf_file = env.Program(
    os.path.join(build_dir, env["FIRMWARE_FILENAME"]), source=objects
)

env.BINBuilder(os.path.join(build_dir, env["FIRMWARE_FILENAME"]), source=elf_file)
