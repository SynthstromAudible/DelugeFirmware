@echo off
call "%~dp0scripts\toolchain\dbtenv.cmd" env

set SCONS_EP=python -m SCons

if [%POETRY_ACTIVE%] == [] (
    set SCONS_RUNNER=poetry run
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