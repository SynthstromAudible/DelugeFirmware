targets_help = """Configuration variables:
"""

tail_help = """

TASKS:
Building:
    build:
        Build firmware; create distribution package

Flashing & debugging:
    flash:
        Flash firmware to target using debug probe
    debug, debug_other, blackmagic: 
        Start GDB

Other:
    lint, lint_py:
        run linters
    format, format_py:
        run code formatters

How to open a shell with toolchain environment and other build tools:
    In your shell, type "source `./dbt -s env`". You can also use "." instead of "source".
"""


def generate(env, **kw):
    vars = kw["vars"]
    basic_help = vars.GenerateHelpText(env)
    env.Help(targets_help + basic_help + tail_help)


def exists(env):
    return True