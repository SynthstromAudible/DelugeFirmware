from SCons.Action import Action
from SCons.Builder import Builder
from SCons.Script import Touch


def exists():
    return True


def generate(env):
    """Force use of C++ instead of ld for linking.

    This adapts what was in place in e2studio.
    """
    env.Replace(
        LINKER_SCRIPT_PATH="linker_script_rz_a1l.ld",
        LINKCOM="$CXX $CCFLAGS -o $TARGET $SOURCES $LINKFLAGS",
    )

    env.Append(
        BUILDERS={
            # Fake-out map building target, so the map file gets cleaned up.
            "MapFile": Builder(
                action=Action(Touch("${TARGET}"), "${TOUCHCOMSTR}"),
                suffix=".map",
                src_suffix=".elf",
            ),
        }
    )
