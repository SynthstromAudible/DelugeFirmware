import os
import re
import logging
import SCons
import multiprocessing

from SCons.Platform import TempFileMunge
from SCons.Subst import quote_spaces

# Progress indicator. Right now there are too many warnings for it to look cool. :(
Progress(["OwO\r", "owo\r", "uwu\r", "owo\r"], interval=15)

# Set some important "globals" in case we need 'em later.
REL_SOURCE_DIR = "src"
PROJECT_SOURCE_DIR = os.path.abspath(REL_SOURCE_DIR)

print("\nDeluge Build Tool (DBT)\n")

# Not really sure how the logger will be used yet, but wanted it
# in here.
logging.basicConfig(level=logging.INFO)
log = logging.getLogger("DBT")
log.debug("Project directory: {}".format(GetLaunchDir()))

# Commandline arguments are defined in here
cmd_vars = SConscript("site_scons/commandline.scons", exports={"log_parent": log})

# Automagically set the number of jobs to the processor count.
# This probably won't please everyone, but I figure most folks
# favour a faster build.
SetOption("num_jobs", multiprocessing.cpu_count())

# Import some useful helper library stuff courtesy of the Flipper folks.
from dbt.util import (
    tempfile_arg_esc_func,
    single_quote,
    extract_abs_dir,
    extract_abs_dir_path,
    wrap_tempfile,
    path_as_posix,
    walk_all_sources,
)

if GetOption("base_config") not in ["e2_xml"]:
    print("Error: invalid base config mode.")
    Exit(1)

# Our default operating mode.
if GetOption("base_config") == "e2_xml":
    from dbt.project import E2Project

    # Beep boop, human lifeform detected.
    log.info("Detected e2_xml mode.")

    # Crank up the e2studio XML parsing class
    cproject = E2Project(log, GetLaunchDir())

    # Brand and sort things appropriately so we follow the general scheme of,
    # but don't overlap with e2 build directories.
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


    # And with all that out of the way, we start up our
    # main/construction environment and configure the particulars
    # for e2_xml targeting all within the "if" block.
    build_label = GetOption("e2_target")
    env = Environment(ENV=os.environ.copy(),
                    tools=["gcc", "g++", "as", "ar", "link"],
                    TEMPFILE=TempFileMunge,
                    TEMPFILEARGESCFUNC=tempfile_arg_esc_func,
                    ABSPATHGETTERFUNC=extract_abs_dir_path,
                    SINGLEQUOTEFUNC=single_quote,
                    MAXLINELENGTH=2048)

    # Some shorthand for all the labels and directories we're
    # going to need below.
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
    env["CCFLAGS"] = " ".join(cproject.get_c_flags(e2_target))
    env["CPPFLAGS"] = " ".join(cproject.get_cpp_flags(e2_target))

    # Don't really know where this flag came from in the config, but it's in
    # the e2studio makefiles.
    env["CPPFLAGS"] += " -fdiagnostics-parseable-fixits"
    env["ASMPATH"] = [str(p) for p in cproject.get_asm_includes(e2_target)]
    env["CCPATH"] = [str(p) for p in cproject.get_c_includes(e2_target)]
    env["CXXPATH"] = [str(p) for p in cproject.get_cpp_includes(e2_target)]
    env["CPPPATH"] = env["ASMPATH"] + env["CCPATH"] + env["CXXPATH"]

    # Defining the ...COMSTR variables suppresses seeing the build commands raw.
    # Just comment these out if you hate things being hidden from you.
    env["CCCOMSTR"] = "Compiling static object $TARGET"
    env["CXXCOMSTR"] = "Compiling static object $TARGET"
    env["ASPPCOMSTR"] = "Assembling $TARGET"
    env["LINKCOMSTR"] = "Linking $TARGET"
    env["BINCOMSTR"] = "Converting .elf to .bin."
    env["HEXCOMSTR"] = "Converting .elf to .hex."

    # The assembler commandline needed a little bit of fudging for this build.
    env["ASPPFLAGS"] += " -x assembler-with-cpp"
    env["ASPATH"] = " {}".format(
        " ".join(['-I"{}"'.format(inc) for inc in env["ASMPATH"]])
    )
    env[
        "ASPPCOM"
    ] = "$CC $ASPPFLAGS $CPPFLAGS $_CPPDEFFLAGS $_CPPINCFLAGS -c -o $TARGET $SOURCES"
    env["ASCOM"] = "$CC $CCFLAGS $ASFLAGS $ASPATH -o $TARGET -c $SOURCE"
    # env["CCCOM"] = "$CC -o $TARGET -c $CFLAGS $CCFLAGS $_CCCOMCOM $SOURCES"
    # env["CXXCOM"] = "$CXX -o $TARGET -c $CXXFLAGS $CCFLAGS $_CCCOMCOM $SOURCES"

    # Here be preprocessor defs. Doesn't yet include new/re defines from the commandline
    # but it'll be easy to add if we want, for testing.
    env["CPPDEFINES"] = cproject.get_preprocessor_defs(e2_target)

    # Literally just says ".elf"
    env["PROGSUFFIX"] = cproject.get_target_ext(e2_target)

    # "Deluge-<build_type>-<model>" for the current e2 target
    env["FIRMWARE_FILENAME"] = cproject.get_target_filename(e2_target)

    # Had to fudge the linker command a bit to get both the mapfile in there,
    # and g++ used to perform the linking step.
    env["MAPFILE"] = "{}".format(
        os.path.join(
            build_dir, "{}.map".format(cproject.get_target_filename(e2_target))
        )
    )
    env["LINKFLAGS"] = cproject.get_link_flags(e2_target)
    env["LINKCOM"] = "$CXX $CPPFLAGS -o $TARGET $SOURCES $LINKFLAGS"
    
    
    env["LIBPATH"] = [str(i) for i in cproject.get_link_libs_order(e2_target)]
    # log.debug(env["LIBPATH"])

# GENERIC ENVIRONMENT STUFF AND ACTIONS
# In this area below the "if" block, we keep environment changes and actions that are
# going to need doing regardless of the particular "mode" (eg: e2_xml).

# Helper functions borrowed from FBT for wrapping lengthy commandlines into temp files.
# This is primarily useful in windows, but shouldn't hurt anything on other platforms.
wrap_tempfile(env, "LINKCOM")
wrap_tempfile(env, "ARCOM")

# Happy little custom builders and their actions.
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

# Export a `compile_commands.json` for help
env.Tool('compilation_db')
env.CompilationDatabase()

# VariantDir does the magic to ensure output goes to the dbt- whatever
# build directory. Careful: THIS CAN BE REALLY FINICKY if paths aren't set right
VariantDir(os.path.join(build_label, source_dir), "#src", duplicate=False)

# Using the specified include dirs rather than walking every path in src was
# preventing a successful .elf build so generically went with the latter approach.
# This will serve us better later, buuuuut could also potentially break something.
# ¯\_(ツ)_/¯
sources = walk_all_sources(source_dir, build_label)

# Wrangle those sources into objects! 
objects = env.Object(sources)

# Turn those objects into a magical elf! (and a bonus .map file)
elf_file = env.Program(
    os.path.join(build_dir, env["FIRMWARE_FILENAME"]), source=objects
)

# Bin it.
env.BINBuilder(os.path.join(build_dir, env["FIRMWARE_FILENAME"]), source=elf_file)
# Hex it.
env.HEXBuilder(os.path.join(build_dir, env["FIRMWARE_FILENAME"]), source=elf_file)