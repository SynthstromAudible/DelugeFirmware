from SCons.Action import Action
from SCons.Builder import Builder

def generate(env):
    env.SetDefault(
        SIZE="size",
    )

    env.Append(
        BUILDERS={
            "SizeFile": Builder(
                action=Action(
                    '"${SIZE}" "${SOURCE}" > "${TARGET}"',
                    "${SIZECOMSTR}",
                ),
                suffix=".siz",
                src_suffix=".elf",
            ),
        }
    )


def exists(env):
    return True
