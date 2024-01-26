@echo off

if not ["%DBT_ROOT%"] == [""] (
    goto already_set
)

set "DBT_ROOT=%~dp0\..\..\"
pushd "%DBT_ROOT%"
set "DBT_ROOT=%cd%"
popd

if not ["%DBT_NOENV%"] == [""] (
    exit /b 0
)

if ["%DBT_TOOLCHAIN_PATH%"] == [""] (
    set "DBT_TOOLCHAIN_PATH=%DBT_ROOT%"
)

set "DBT_TOOLCHAIN_REQUIRED_VERSION_FILE=%DBT_TOOLCHAIN_PATH%\toolchain\REQUIRED_VERSION"
set /p DBT_TOOLCHAIN_VERSION=<"%DBT_TOOLCHAIN_REQUIRED_VERSION_FILE%"

set "DBT_TOOLCHAIN_ROOT=%DBT_TOOLCHAIN_PATH%\toolchain\v%DBT_TOOLCHAIN_VERSION%\win32-x86_64"

set "DBT_TOOLCHAIN_VERSION_FILE=%DBT_TOOLCHAIN_ROOT%\VERSION"

set "DBT_TOOLCHAIN_INSTALLED=0"

if not exist "%DBT_TOOLCHAIN_ROOT%" (
    powershell -ExecutionPolicy Bypass -File "%DBT_ROOT%\scripts\toolchain\windows-toolchain-download.ps1" %DBT_TOOLCHAIN_VERSION% "%DBT_TOOLCHAIN_ROOT%"
    set "DBT_TOOLCHAIN_INSTALLED=1"
)

if not exist "%DBT_TOOLCHAIN_VERSION_FILE%" (
    powershell -ExecutionPolicy Bypass -File "%DBT_ROOT%\scripts\toolchain\windows-toolchain-download.ps1" %DBT_TOOLCHAIN_VERSION% "%DBT_TOOLCHAIN_ROOT%"
    set "DBT_TOOLCHAIN_INSTALLED=1"
)

set /p REAL_TOOLCHAIN_VERSION=<"%DBT_TOOLCHAIN_VERSION_FILE%"
if not "%REAL_TOOLCHAIN_VERSION%" == "%DBT_TOOLCHAIN_VERSION%" (
    echo DBT: starting toolchain upgrade process..
    powershell -ExecutionPolicy Bypass -File "%DBT_ROOT%\scripts\toolchain\windows-toolchain-download.ps1" %DBT_TOOLCHAIN_VERSION% "%DBT_TOOLCHAIN_ROOT%"
    set /p REAL_TOOLCHAIN_VERSION=<"%DBT_TOOLCHAIN_VERSION_FILE%"
    set "DBT_TOOLCHAIN_INSTALLED=1"
)

if defined DBT_VERBOSE (
    echo DBT: using toolchain version %REAL_TOOLCHAIN_VERSION%
)

set "HOME=%USERPROFILE%"
set "SSL_CERT_FILE=%DBT_TOOLCHAIN_ROOT%/python/Lib/site-packages/certifi/cacert.pem"
set "PYTHONHOME=%DBT_TOOLCHAIN_ROOT%\python"
set "PYTHONPATH=%DBT_ROOT%\scripts;%PYTHONPATH%"
set "PYTHONNOUSERSITE=1"
set "PATH=%DBT_TOOLCHAIN_ROOT%\python;%DBT_TOOLCHAIN_ROOT%\python\Scripts;%DBT_TOOLCHAIN_ROOT%\arm-none-eabi-gcc\bin;%DBT_TOOLCHAIN_ROOT%\openocd\bin;%DBT_TOOLCHAIN_ROOT%\cmake\bin;%DBT_TOOLCHAIN_ROOT%\ninja-build\bin;%PATH%"
set "PROMPT=(dbt) %PROMPT%"

if "%DBT_TOOLCHAIN_INSTALLED%" == "1" (
    if [%DBT_NO_PYTHON_UPGRADE%] == [] (
        echo DBT: Toolchain freshly installed, running python setup
        set "PIP_CMD=python -m pip"
        set "PIP_REQUIREMENTS_PATH=%SCRIPT_PATH%\scripts\toolchain\requirements.txt"
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
)

:already_set

if not "%1" == "env" (
    echo *********************************
    echo *     dbt build environment     *
    echo *********************************
    cd "%DBT_ROOT%"
    cmd /k
)
