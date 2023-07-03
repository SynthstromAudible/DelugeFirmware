targets_help = """
Configuration variables:
"""

tail_help = """
Build Targets:
    all, build:
        Shortcut to build all targets

        The build variable DEBUG can be used to filter
        debug builds (1) or release builds (0) only.

        The build variable OLED can be used to filter
        OLED builds (1) or 7-segment builds (0) only.

        These variables can be used together, eg:
            ./dbt all DEBUG=1 OLED=1

    clean:
        Shortcut for "./dbt all -c" to unconfuse makefile
        enthusiasts.

    dbt-build-release-oled:
        Release, OLED build
    dbt-build-release-7seg:
        Release, 7-segment display build
    dbt-build-debug-oled:
        Debug, OLED build
    dbt-build-debug-7seg
        Release, 7-segment display build

Debugging Targets (Debug is currently iffy):
    openocd:
        Run OpenOCD with default arguments (CMSIS-DAP/DelugeProbe).
        These can be changed using the OPENOCD_OPTS variable.
    debug-oled, debug-7seg: 
        Assuming OpenOCD is already running, run GDB and target
        either the OLED or 7-segment display build on the remote
        specified by the GDB_REMOTE variable.

    Debugging is currently broken using OpenOCD. See DelugeFirmware
    issue #86 for more.

Other Targets:
    shell:
        run a bash, sh, or CMD prompt within the DBT environment.
    format:
        run clang-format to advise on c/c++ file formatting
"""


def generate(env, **kwargs):
    vars = kwargs["vars"]

    basic_help = vars.GenerateHelpText(env)
    env.Help(targets_help + basic_help + tail_help, append=True)


def exists():
    return True
