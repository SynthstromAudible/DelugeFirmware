import os
import multiprocessing

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

# UTIL OPERATIONS

util_env = env.Clone(tools=["python3", "openocd", "shell", "debug_opts"])
if "shell" in BUILD_TARGETS:
    util_env.PhonyTarget(
        "shell", Action(util_env.CallShell, "Running shell in DBT environment.")
    )
    Return()

elif "openocd" in BUILD_TARGETS:
    # Just start OpenOCD
    util_env.PhonyTarget("openocd", "${OPENOCDCOM}")
    Return()

elif "debug-oled" in BUILD_TARGETS:
    cmd_env.Replace(DEBUG=1, OLED=1)
    elf_target = compose_valid_targets(cmd_env)[0]
    # Load mode standalone to get us the elf file name
    # some redundancy here, ah well
    util_env.Tool(
        "mode_standalone",
        BUILD_LABEL=elf_target,
        BUILD_DIR=os.path.abspath(elf_target),
        SOURCE_DIR=os.path.relpath(REL_SOURCE_DIR),
    )
    debug_out = util_env.PhonyTarget(
        "debug-oled",
        "${GDBCOM}",
        source=os.path.join(
            util_env.get("BUILD_DIR"),
            "{}.elf".format(util_env.get("FIRMWARE_FILENAME")),
        ),
        GDBOPTS="${GDBOPTS_BASE}",
        GDBREMOTE=cmd_env["GDB_REMOTE"],
    )
    Return()

elif "debug-7seg" in BUILD_TARGETS:
    cmd_env.Replace(DEBUG=1, OLED=0)
    elf_target = compose_valid_targets(cmd_env)[0]
    # Load mode standalone to get us the elf file name
    # some redundancy here, ah well
    util_env.Tool(
        "mode_standalone",
        BUILD_LABEL=elf_target,
        BUILD_DIR=os.path.abspath(elf_target),
        SOURCE_DIR=os.path.relpath(REL_SOURCE_DIR),
    )
    debug_out = util_env.PhonyTarget(
        "debug-7seg",
        "${GDBCOM}",
        source=os.path.join(
            util_env.get("BUILD_DIR"),
            "{}.elf".format(util_env.get("FIRMWARE_FILENAME")),
        ),
        GDBOPTS="${GDBOPTS_BASE}",
        GDBREMOTE=cmd_env["GDB_REMOTE"],
    )
    Return()
    # No Return as we want this to build
    # We will revisit after the build

# BUILD OPERATIONS

# Prepare for multiple target environments
build_multienv = {}

# Target List Building
# Here we parse out shorthand targets and leave only our directory targets

FOCUS_TARGETS = compose_valid_targets(cmd_env)

# If one of our utility targets is in the list, ignore
# normal targets or treat them specially.

if "openocd" in BUILD_TARGETS:
    FOCUS_TARGETS = []
if "shell" in BUILD_TARGETS:
    FOCUS_TARGETS = []
if "debug-oled" in BUILD_TARGETS:
    FOCUS_TARGETS = []
if "debug-7seg" in BUILD_TARGETS:
    FOCUS_TARGETS = []

special_target = None
for b_target in BUILD_TARGETS:
    if b_target in ["all", "build", "clean"]:
        special_target = b_target
        break

if special_target is not None:
    # Formally map these aliases to the appropriate targets
    if special_target == "all":
        env.Alias("all", FOCUS_TARGETS)
    if special_target == "build":
        env.Alias("build", FOCUS_TARGETS)
    if special_target == "clean":
        env.Alias("clean", FOCUS_TARGETS)
        SetOption("clean", True)

for target in FOCUS_TARGETS:
    build_env = env.Clone(BUILD_MODE="standalone")
    build_multienv[target] = build_env
    build_env.Tool(
        "mode_standalone",
        BUILD_LABEL=target,
        BUILD_DIR=os.path.abspath(target),
        SOURCE_DIR=os.path.relpath(REL_SOURCE_DIR),
    )

    if not build_env["BUILD_TARGET_IS_VALID"]:
        Return()

    SConscript(
        os.path.join("site_scons", "build_opts.scons"), exports={"env": build_env}
    )

# Iterate through multiple build environments
for target, build_env in build_multienv.items():
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
