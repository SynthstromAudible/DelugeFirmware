@echo off
call "%~dp0scripts\toolchain\dbtenv.cmd" env
set "SCRIPT_PATH=%DBT_ROOT%"

set DBT_EP=python dbt.py

if [%DBT_NO_SYNC%] == [] (
    if exist ".git" (
        git submodule update --init --depth 1 --jobs %NUMBER_OF_PROCESSORS%
    ) else (
        echo Not in a git repo, please clone with "git clone"
        exit /b 1
    )
)

set PIP_CMD=python -m pip
set "PIP_REQUIREMENTS_PATH=%SCRIPT_PATH%\scripts\toolchain\requirements.txt"

%DBT_EP% %*
