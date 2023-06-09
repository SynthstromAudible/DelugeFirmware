"""This file was borrowed from:
https://github.com/flipperdevices/flipperzero-firmware/

Licensed under GNU GPL v3"""


def generate(env):
    py_name = "python3"
    if env["PLATFORM"] == "win32":
        # On Windows, Python 3 executable is usually just "python"
        py_name = "python"

    env.SetDefault(
        PYTHON3=py_name,
    )


def exists(env):
    return True
