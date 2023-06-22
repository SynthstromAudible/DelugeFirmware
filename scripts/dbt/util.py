import os
import re

import SCons
from SCons.Script.Main import GetOption
from SCons.Errors import StopError
from SCons.Subst import quote_spaces

WINPATHSEP_RE = re.compile(r"\\([^\"'\\]|$)")
SOURCEWALK_RE = re.compile(r"\.[cs][p]{0,2}$", re.IGNORECASE)

# Used by default when globbing for files with GlobRecursive
# Excludes all files ending with ~, usually created by editors as backup files
GLOB_FILE_EXCLUSION = ["*~"]


def tempfile_arg_esc_func(arg):
    arg = quote_spaces(arg)
    if SCons.Platform.platform_default() != "win32":
        return arg
    # GCC requires double Windows slashes, let's use UNIX separator
    return WINPATHSEP_RE.sub(r"/\1", arg)


def wrap_tempfile(env, command):
    env[command] = '${TEMPFILE("' + env[command] + '","$' + command + 'STR")}'


def link_dir(target_path, source_path, is_windows):
    # print(f"link_dir: {target_path} -> {source_path}")
    if os.path.lexists(target_path) or os.path.exists(target_path):
        os.unlink(target_path)
    if is_windows:
        # Crete junction
        import _winapi

        if not os.path.isdir(source_path):
            raise StopError(f"Source directory {source_path} is not a directory")

        if not os.path.exists(target_path):
            _winapi.CreateJunction(source_path, target_path)
    else:
        os.symlink(source_path, target_path)


def single_quote(arg_list):
    return " ".join(f"'{arg}'" if " " in arg else str(arg) for arg in arg_list)


def extract_abs_dir(node):
    if isinstance(node, SCons.Node.FS.EntryProxy):
        node = node.get()

    for repo_dir in node.get_all_rdirs():
        if os.path.exists(repo_dir.abspath):
            return repo_dir


def extract_abs_dir_path(node):
    abs_dir_node = extract_abs_dir(node)
    if abs_dir_node is None:
        raise StopError(f"Can't find absolute path for {node.name}")

    return abs_dir_node.abspath


def path_as_posix(path):
    if SCons.Platform.platform_default() == "win32":
        return path.replace(os.path.sep, os.path.altsep)
    return path


def walk_all_sources(base, prefix):
    walker = list(os.walk(base))
    sources = [
        os.path.join(prefix, os.path.relpath(os.path.join(i[0], l)))
        for i in walker
        for l in i[2]
        if SOURCEWALK_RE.match(os.path.splitext(l)[1])
    ]
    return sources


def vcheck():
    """Verbosity level-checker

    Used to determine current verbosity level from commandline arguments alone

    Verbosity = 0 (silent)
    Verbosity = 1 (verbose, -warnings)
    Verbosity = 2 (verbose, +warnings)
    Verbosity = 3 (verbose, all)
    """
    if not GetOption("silent"):
        if GetOption("spew"):
            return 3
        elif GetOption("verbosity"):
            return int(GetOption("verbosity"))
        return 1

    return 0


def vprint(*args, **kwargs):
    """Verbosity-aware print function

    Will suppress output based on verbosity/spew/silent flags.
    """
    if vcheck():
        print(*args, **kwargs)
