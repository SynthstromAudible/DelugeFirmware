import shutil
import subprocess
import platform
from SCons.Action import Action
from SCons.Builder import Builder
from dbt.util import vcheck, vprint

PLATFORM = platform.system().lower()


def CallShell(*args, **kwargs):
    env = kwargs.get("env", {})
    subprocess.call(env["SHELL_COMMAND"])


def generate(env):
    if PLATFORM == "windows":
        shell_command = shutil.which("cmd.exe")
    else:
        shell_command = shutil.which("bash")
        if not shell_command:
            shell_command = shutil.which("sh")

    env.Replace(SHELL_COMMAND=shell_command)
    env.AddMethod(CallShell)


def exists(env):
    return True
