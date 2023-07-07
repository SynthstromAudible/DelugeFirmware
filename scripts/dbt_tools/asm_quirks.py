def exists():
    return True


def generate(env):
    env.Replace(
        ASPPFLAGS="-x assembler-with-cpp",
        ASPPCOM="$CC $CCFLAGS $ASPATH $ASPPFLAGS $ASFLAGS -o $TARGET -c $SOURCE",
        ASCOM="$CC $CCFLAGS $ASPATH $ASPPFLAGS $ASFLAGS -o $TARGET -c $SOURCE",
    )
