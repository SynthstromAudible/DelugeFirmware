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

# Important parameter to avoid conflict with VariantDir when calling SConscripts
# from another directory
SConscriptChdir(False)

# Our default operating mode.
if GetOption("base_config") == "e2_xml":
    env.SConscript(
        os.path.join("site_scons", "e2_xml.scons"),
        exports={"env": env, "log_parent": log},
    )

# GENERIC ENVIRONMENT STUFF AND ACTIONS
# In this area below the "if" block, we keep environment changes and actions that are
# going to need doing regardless of the particular "mode" (eg: e2_xml).

# Helper functions borrowed from FBT for wrapping lengthy commandlines into temp files.
# This is primarily useful in windows, but shouldn't hurt anything on other platforms.
wrap_tempfile(env, "LINKCOM")
wrap_tempfile(env, "ARCOM")

# VariantDir does the magic to ensure output goes to the dbt- whatever
# build directory. Careful: THIS CAN BE REALLY FINICKY if paths aren't set right
VariantDir(os.path.join(env["BUILD_LABEL"], REL_SOURCE_DIR), "#src", duplicate=False)

# Load generic stuff (Builders, etc.) into the environment.
SConscript(os.path.join("site_scons", "generic_env.scons"), exports={"env": env})

# Using the specified include dirs rather than walking every path in src was
# preventing a successful .elf build so generically went with the latter approach.
# This will serve us better later, buuuuut could also potentially break something.
# ¯\_(ツ)_/¯
sources = walk_all_sources(REL_SOURCE_DIR, env["BUILD_LABEL"])

# Wrangle those sources into objects!
objects = env.Object(sources)

# Turn those objects into a magical elf! (and a bonus .map file)
elf_file = env.Program(
    os.path.join(env["BUILD_DIR"], env["FIRMWARE_FILENAME"]), source=objects
)
# Touch the bonus map file so when we tell dbt to clean (-c) it removes as well
env.MAPBuilder(
    os.path.join(env["BUILD_DIR"], env["FIRMWARE_FILENAME"]), source=elf_file
)

# Bin it.
env.BINBuilder(
    os.path.join(env["BUILD_DIR"], env["FIRMWARE_FILENAME"]), source=elf_file
)
# Hex it.
env.HEXBuilder(
    os.path.join(env["BUILD_DIR"], env["FIRMWARE_FILENAME"]), source=elf_file
)
# Size it.
env.SIZBuilder(
    os.path.join(env["BUILD_DIR"], env["FIRMWARE_FILENAME"]), source=elf_file
)
