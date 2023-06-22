def exists():
    return True


def generate(env):
    env.Replace(
        ASPPFLAGS="-x assembler-with-cpp",
        ASCOM="$CC $CCFLAGS $ASFLAGS $ASPPFLAGS $ASPATH -o $TARGET -c $SOURCE",
    )
