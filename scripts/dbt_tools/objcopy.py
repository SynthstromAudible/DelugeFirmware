from SCons.Action import Action
from SCons.Builder import Builder

def generate(env):
    env.SetDefault(
        OBJCOPY="objcopy",
    )

    env.Append(
        BUILDERS={
            "HexFile": Builder(
                action=Action(
                    '${OBJCOPY} -O ihex "${SOURCE}" "${TARGET}"',
                    "${HEXCOMSTR}",
                ),
                suffix=".hex",
                src_suffix=".elf",
            ),
            "BinFile": Builder(
                action=Action(
                    '${OBJCOPY} -O binary -S "${SOURCE}" "${TARGET}"',
                    "${BINCOMSTR}",
                ),
                suffix=".bin",
                src_suffix=".elf",
            ),
        }
    )

def exists(env):
    return True
