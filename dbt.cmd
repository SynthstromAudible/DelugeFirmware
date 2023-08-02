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

if [%DBT_NO_PYTHON_UPGRADE%] == [] (
    python -c "import wheel"
    if %ERRORLEVEL% NEQ 0 (
        set "PIP_WHEEL_PATH=%DBT_TOOLCHAIN_ROOT%\python\wheel"
        for /R %PIP_WHEEL_PATH% %%G in (
            certifi*.whl
        ) do (
            %PIP_CMD% install -q "%%G"
        )
    )
    %PIP_CMD% install -q --upgrade pip | find /V "already satisfied"
    %PIP_CMD% install -q -f "%PIP_WHEEL_PATH%" -r "%PIP_REQUIREMENTS_PATH%" | find /V "already satisfied"
)

%DBT_EP% %*
