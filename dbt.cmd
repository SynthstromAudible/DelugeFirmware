@echo off
call "%~dp0scripts\toolchain\dbtenv.cmd" env

if /i [%1] == [/h] (
    goto :HELP
)
if /i [%1] == [/help] (
    goto :HELP
)
if /i [%1] == [--help] (
    goto :HELP
)
if /i [%1] == [-h] (
    goto :HELP
)
if /i [%1] == [help] (
    goto :HELP
)

set SCONS_EP=python -m SCons

if [%POETRY_ACTIVE%] == [] (
    if not exist "%PYTHONHOME%\Scripts\poetry.exe" (
        ECHO Installing poetry.
        python -m pip install -q --upgrade pip;
        python -m pip install -q poetry
    )
    if exist "%DBT_ROOT\poetry.lock" (
        python -m poetry lock
    )
    python -m poetry install
    set SCONS_RUNNER=python -m poetry run 
)

if [%DBT_NO_SYNC%] == [] (
    if exist ".git" (
        git submodule update --init --depth 1 --jobs %NUMBER_OF_PROCESSORS%
    ) else (
        echo Not in a git repo, please clone with "git clone"
        exit /b 1
    )
)

set "SCONS_DEFAULT_FLAGS=--warn=target-not-built"

if not defined DBT_VERBOSE (
    set "SCONS_DEFAULT_FLAGS=%SCONS_DEFAULT_FLAGS% -Q"
)

%SCONS_RUNNER%%SCONS_EP% %SCONS_DEFAULT_FLAGS% %*
exit

:HELP
echo Valid commands:
echo TBD