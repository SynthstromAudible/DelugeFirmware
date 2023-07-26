import os
import multiprocessing
import sys
import pathlib

# Remove current directory from search list (dbt package and dbt.py name collision)
sys.path = [d for d in sys.path if str(pathlib.Path(d)) != os.getcwd()]

from SCons.Action import Action
from SCons.Platform import TempFileMunge
from SCons.Subst import quote_spaces

from dbt.util import vprint
from dbt.project import compose_valid_targets

DefaultEnvironment(tools=[])

# Set some important "globals" in case we need 'em later.
REL_SOURCE_DIR = "src"
PROJECT_SOURCE_DIR = os.path.abspath(REL_SOURCE_DIR)

# Commandline arguments are defined in here
cmd_vars = SConscript("site_scons/commandline.scons")
cmd_env = Environment(
    ENV_TYPE="command",
    toolpath=["#/scripts/dbt_tools"],
    tools=[
        ("dbt_help", {"vars": cmd_vars}),
    ],
    variables=cmd_vars,
)

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
    vcheck,
    vprint,
)

# Start up a basic environment
env = Environment(
    ENV=os.environ.copy(),
    variables=cmd_vars,
    ENV_TYPE="generic",
    toolpath=[os.path.join(GetLaunchDir(), "scripts", "dbt_tools")],
    tools=[
        (
            "crosscc",
            {
                "toolchain_prefix": "arm-none-eabi-",
                "versions": (
                    " 12.0",
                    " 12.1",
                    " 12.2",
                ),
            },
        ),
        "asm_quirks",
        "link_quirks",
        "scons_modular",
    ],
    TEMPFILE=TempFileMunge,
    TEMPFILEARGESCFUNC=tempfile_arg_esc_func,
    ABSPATHGETTERFUNC=extract_abs_dir_path,
    SINGLEQUOTEFUNC=single_quote,
    MAXLINELENGTH=2048,
    BUILD_MODE="",
    ROOT_DIR=GetLaunchDir(),
    DBT_SCRIPT_DIR=os.path.join(GetLaunchDir(), "scripts"),
    DBT_CONTRIB_DIR=os.path.join(GetLaunchDir(), "contrib"),
)

# COMMAND LINE PARSING

# Set verbosity options in priority order:
# spew -> silent -> verbosity
if vcheck() == 0:
    SetOption("silent", True)
elif vcheck() == 3:
    SetOption("silent", False)

# Progress indicator. Right now there are too many warnings for it to look cool. :(
if (not GetOption("no_progress") and not GetOption("spew")) and vcheck in range(1, 4):
    Progress(["OwO\r", "owo\r", "uwu\r", "owo\r"], interval=15)

vprint()
print("Deluge Build Tool (DBT) {}".format("[silent mode]" if not vcheck() else ""))
vprint()
vprint("For help use argument: --help\n")

# Save options if --save-default-options is specified
if GetOption("save_options"):
    cmd_vars.Save(GetOption("optionfile"), cmd_env)
    vprint("Saved options to {}".format(str(GetOption("optionfile"))))
    vprint()

# Important parameter to avoid conflict with VariantDir when calling SConscripts
# from another directory
SConscriptChdir(False)

# BUILD OPERATIONS

env.Tool("target_handler", cmd_env=cmd_env)
env_katamari = env.PrepareTargetEnvs()
env_standalone_builds = [b for b in env_katamari if b.get("BUILD_MODE") == "standalone"]

for build_env in env_standalone_builds:
    target = build_env["ENV_LABEL"]

    # Suppress warnings if silent
    if vcheck() < 2:
        build_env.Append(CCFLAGS=" -w")
        build_env.Append(LINKFLAGS=" -w")

    # Populate version info
    build_env.Tool("version")

    # Helper functions borrowed from FBT for wrapping lengthy commandlines into temp files.
    # This is primarily useful in windows, but shouldn't hurt anything on other platforms.
    wrap_tempfile(build_env, "LINKCOM")
    wrap_tempfile(build_env, "ARCOM")

    # VariantDir does the magic to ensure output goes to the dbt- whatever
    # build directory. Careful: THIS CAN BE REALLY FINICKY if paths aren't set right
    VariantDir(
        os.path.relpath(os.path.join(build_env["BUILD_DIR"], REL_SOURCE_DIR)),
        "#src",
        duplicate=False,
    )

    # If we're in "only prepare mode" don't queue up any of the actual compilation steps.
    # Just bail at this point. There's also no point putting compilation db creation
    # before this as it won't compile the DB without awareness of the build steps.
    # C'est la vie
    if GetOption("only_prepare"):
        Return()

    ### BINARY BUILDING CONTINUES AFTER THIS POINT

    # Compilation DB settings wrapper
    build_env.Tool("compdb")

    # Using the specified include dirs rather than walking every path in src was
    # preventing a successful .elf build so generically went with the latter approach.
    # This will serve us better later, buuuuut could also potentially break something.
    # ¯\_(ツ)_/¯
    sources = walk_all_sources(REL_SOURCE_DIR, build_env["BUILD_LABEL"])

    # Wrangle those sources into objects!
    objects = build_env.Object(sources)

    # Turn those objects into a magical elf! (and a bonus .map file)
    elf_file = build_env.Program(
        os.path.join(build_env["BUILD_DIR"], build_env["FIRMWARE_FILENAME"]),
        source=objects,
    )
    # Touch it.
    map_file = build_env.MapFile(
        os.path.join(build_env["BUILD_DIR"], build_env["FIRMWARE_FILENAME"]),
        source=elf_file,
    )
    # Bin it.
    bin_file = build_env.BinFile(
        os.path.join(build_env["BUILD_DIR"], build_env["FIRMWARE_FILENAME"]),
        source=elf_file,
    )
    # Hex it.
    hex_file = build_env.HexFile(
        os.path.join(build_env["BUILD_DIR"], build_env["FIRMWARE_FILENAME"]),
        source=elf_file,
    )
    # Size it.
    size_file = build_env.SizeFile(
        os.path.join(build_env["BUILD_DIR"], build_env["FIRMWARE_FILENAME"]),
        source=elf_file,
    )
    # Technologic.
    # Technologic.
