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

set PIP_CMD=python -m pip
set "PIP_WHEEL_PATH=%DBT_TOOLCHAIN_ROOT%\python\wheel"
del 
for /R %PIP_WHEEL_PATH% %%G in (
    certifi*.whl,
    pyserial*.whl,
    ansi*.whl
) do (
    %PIP_CMD% install -q "%PIP_WHEEL_PATH%\%%G"
)
%PIP_CMD% install -q --upgrade pip
@REM Whoops, forgot to bundle scons in the toolchain
%PIP_CMD% show SCons
if [%ERRORLEVEL%] == [1]
    %PIP_CMD% install -q SCons==4.5.2
fi

@REM %SCONS_EP% %SCONS_DEFAULT_FLAGS% %*
echo Beyond downloading and updating the toolchains, dbt is currently not fully functional.
exit

:HELP
echo Valid commands:
echo TBD