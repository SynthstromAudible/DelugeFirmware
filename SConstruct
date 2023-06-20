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
print("For help use argument: --help\n")

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
    Return()

# Start up a basic environment
env = Environment(
    ENV=os.environ.copy(),
    tools=["gcc", "g++", "as", "ar", "link"],
    TEMPFILE=TempFileMunge,
    TEMPFILEARGESCFUNC=tempfile_arg_esc_func,
    ABSPATHGETTERFUNC=extract_abs_dir_path,
    SINGLEQUOTEFUNC=single_quote,
    MAXLINELENGTH=2048,
)

# Export a `compile_commands.json` for help
# Note: no idea how well this plays with multiple targets at once
env.Tool("compilation_db")
env.CompilationDatabase()

# Prepare for multiple target environments
multienv = {}

# Important parameter to avoid conflict with VariantDir when calling SConscripts
# from another directory
SConscriptChdir(False)

# Our default operating mode.
if GetOption("base_config") == "e2_xml":
    if GetOption("e2_target"):
        Default(GetOption("e2_target"))

    from dbt.project import E2Project

    # Crank up the e2studio XML parsing class
    cproject = E2Project(log, GetLaunchDir())

    # Brand and sort things appropriately so we follow the general scheme of,
    # but don't overlap with e2 build directories.
    e2_build_targets = list(cproject.get_build_targets())
    dbt_build_targets = [p.replace("e2-build-", "dbt-build-") for p in e2_build_targets]
    e2_build_targets.sort()
    dbt_build_targets.sort()

    if not BUILD_TARGETS:
        print(
            "Cannot build without a specified target. Try one or more of the following:"
        )
        print("    Direct Targets:")
        for d_t in dbt_build_targets:
            print("        {}".format(d_t))
        # print("    Special Targets:")
        # print("        all (builds all targets, sequentially)")
        print("\nTo clean a build, specify a target and use the flag '-c'.")
        Return()

    # Launch and configure a new environment for each target BUILD_TARGETS
    # contains plain commandline targets or the contents of -e2_target
    # ONLY IF there are no commandline targets.
    for target_raw in BUILD_TARGETS:
        # Convert entry to plain string so there are no differences
        target = str(target_raw)
        # between plain commandline targets and "option" targets
        multienv[target] = env.Clone(
            BUILD_LABEL=target, BUILD_DIR=os.path.abspath(target)
        )

        if not multienv[target].SConscript(
            os.path.join("site_scons", "e2_xml.scons"),
            exports={
                "env": multienv[target],
                "log_parent": log,
                "cproject": cproject,
                "e2_build_targets": e2_build_targets,
                "dbt_build_targets": dbt_build_targets,
            },
        ):
            success = False
            Return("success")

# GENERIC ENVIRONMENT STUFF AND ACTIONS
# In this area below the "if" block, we keep environment changes and actions that are
# going to need doing regardless of the particular "mode" (eg: e2_xml).

# Iterate again, this time over configured environments. This should work regardless of
# --base_config setting!
for target, t_env in multienv.items():
    # Helper functions borrowed from FBT for wrapping lengthy commandlines into temp files.
    # This is primarily useful in windows, but shouldn't hurt anything on other platforms.
    wrap_tempfile(t_env, "LINKCOM")
    wrap_tempfile(t_env, "ARCOM")

    # VariantDir does the magic to ensure output goes to the dbt- whatever
    # build directory. Careful: THIS CAN BE REALLY FINICKY if paths aren't set right
    VariantDir(
        os.path.join(t_env["BUILD_LABEL"], REL_SOURCE_DIR), "#src", duplicate=False
    )

    # Load generic stuff (Builders, etc.) into the environment.
    SConscript(os.path.join("site_scons", "generic_env.scons"), exports={"env": t_env})

    # Using the specified include dirs rather than walking every path in src was
    # preventing a successful .elf build so generically went with the latter approach.
    # This will serve us better later, buuuuut could also potentially break something.
    # ¯\_(ツ)_/¯
    sources = walk_all_sources(REL_SOURCE_DIR, t_env["BUILD_LABEL"])

    # Wrangle those sources into objects!
    objects = t_env.Object(sources)

    # Turn those objects into a magical elf! (and a bonus .map file)
    elf_file = t_env.Program(
        os.path.join(t_env["BUILD_DIR"], t_env["FIRMWARE_FILENAME"]), source=objects
    )
    # Touch it.
    t_env.MAPBuilder(
        os.path.join(t_env["BUILD_DIR"], t_env["FIRMWARE_FILENAME"]), source=elf_file
    )
    # Bin it.
    t_env.BINBuilder(
        os.path.join(t_env["BUILD_DIR"], t_env["FIRMWARE_FILENAME"]), source=elf_file
    )
    # Hex it.
    t_env.HEXBuilder(
        os.path.join(t_env["BUILD_DIR"], t_env["FIRMWARE_FILENAME"]), source=elf_file
    )
    # Size it.
    t_env.SIZBuilder(
        os.path.join(t_env["BUILD_DIR"], t_env["FIRMWARE_FILENAME"]), source=elf_file
    )
    # Technologic.
    # Technologic.
