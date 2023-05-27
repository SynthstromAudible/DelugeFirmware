import os
import sys
import traceback

import SCons.Warnings as Warnings
from ansi.color import fg
from SCons.Errors import UserError


def find_deepest_user_frame(tb):
    tb.reverse()

    # find the deepest traceback frame that is not part
    # of SCons:
    for frame in tb:
        filename = frame[0]
        if filename.find("dbt_tweaks") != -1:
            continue
        if filename.find(os.sep + "SCons" + os.sep) == -1:
            return frame
    return tb[0]


def dbt_warning(e):
    filename, lineno, routine, dummy = find_deepest_user_frame(
        traceback.extract_stack()
    )
    dbt_line = "\ndbt: warning: %s\n" % e.args[0]
    sys.stderr.write(fg.boldmagenta(dbt_line))
    dbt_line = (
        fg.yellow("%s, line %d, " % (routine, lineno)) + 'in file "%s"\n' % filename
    )
    sys.stderr.write(fg.yellow(dbt_line))


def generate(env):
    Warnings._warningOut = dbt_warning


def exists():
    return True