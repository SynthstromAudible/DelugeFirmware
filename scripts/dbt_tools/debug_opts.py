import os
import shutil
from SCons.Errors import UserError


def generate(env, **kw):
    # env.AddMethod(GetDevices)
    env.SetDefault(DBT_DEBUG_DIR=os.path.join(env["DBT_SCRIPT_DIR"], "debug"))

    env.SetDefault(
        # OPENOCD_GDB_PIPE=[
        #     "|openocd -c 'gdb_port pipe; log_output ${DBT_DEBUG_DIR}/openocd.log' ${[SINGLEQUOTEFUNC(OPENOCD_OPTS)]}"
        # ],
        GDBOPTS_BASE=[
            "-ex",
            "source ${DBT_DEBUG_DIR}/gdbinit",
            "-ex",
            "target extended-remote ${GDBREMOTE}",
        ],
        # GDBOPTS_BLACKMAGIC=[
        #     "-q",
        #     "-ex",
        #     "monitor swdp_scan",
        #     "-ex",
        #     "monitor debug_bmp enable",
        #     "-ex",
        #     "attach 1",
        #     "-ex",
        #     "set mem inaccessible-by-default off",
        # ],
        GDBPYOPTS=[
            "-ex",
            "source ${DBT_DEBUG_DIR}/PyCortexMDebug/scripts/gdb.py",
            "-ex",
            "svd_load ${SVD_FILE}",
            "-ex",
            "compare-sections",
            "-ex",
            "fw-version",
        ],
    )


def exists(env):
    return True
