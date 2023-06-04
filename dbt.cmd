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

%SCONS_EP% %SCONS_DEFAULT_FLAGS% %*
exit

:HELP
echo Valid commands:
echo TBD