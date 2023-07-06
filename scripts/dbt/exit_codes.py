from enum import Enum


class ExitCodes(Enum):
    """Exit Code Enum

    Add to this list in scripts/dbt/exit_codes.py when you want
    to Exit() with a new errorlevel/exitcode"""

    SUCCESS = 0
    UNKNOWN_ERROR = 1
    INVALID_GIT_REPOSITORY = 2
    FAILED_TO_START_STANDALONE_MODE = 3
    COMMANDLINE_VALIDATION_FAILURE = 4
    OPENOCD_UNSUPPORTED_HARDWARE = 5
    OPENOCD_UNSUPPORTED_PROTOCOL = 6
