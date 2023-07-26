import os

def generate(env):
    env.SetDefault(
        COMPILATIONDB_USE_ABSPATH=True,
        COMPILATIONDB_FILE_PATH=os.path.join(
            env["BUILD_DIR"],
            "compile_commands.json"
        )
    )

    env.Tool("compilation_db")
    env.CompilationDatabase(env["COMPILATIONDB_FILE_PATH"])



def exists():
    return True
