@echo off
call "%~dp0scripts\toolchain\dbtenv.cmd" env
set "SCRIPT_PATH=%DBT_ROOT%"

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
set "PIP_REQUIREMENTS_PATH=%SCRIPT_PATH%\scripts\toolchain\requirements.txt"
set "PIP_WHEEL_PATH=%DBT_TOOLCHAIN_ROOT%\python\wheel"
for /R %PIP_WHEEL_PATH% %%G in (
    certifi*.whl
) do (
    %PIP_CMD% install -q "%%G"
)
%PIP_CMD% install -q --upgrade pip | find /V "already satisfied"
%PIP_CMD% install -q -f "%PIP_WHEEL_PATH%" -r "%PIP_REQUIREMENTS_PATH%" | find /V "already satisfied"

%SCONS_EP% %SCONS_DEFAULT_FLAGS% %*
