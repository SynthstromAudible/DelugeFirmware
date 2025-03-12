@echo off
setlocal EnableDelayedExpansion
set "SCRIPT_PATH=%DBT_ROOT%"

if not ["%DBT_ROOT%"] == [""] (
    goto already_set
)

set "DBT_ROOT=%cd%"

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

set "DBT_NEEDS_INSTALL=0"
set "DBT_TOOLCHAIN_INSTALLED=0"

if not exist "%DBT_TOOLCHAIN_ROOT%" (
	echo %DBT_TOOLCHAIN_ROOT% missing, will install...
	set "DBT_NEEDS_INSTALL=1"
)

if exist "%DBT_TOOLCHAIN_VERSION_FILE%" (
	set /p REAL_TOOLCHAIN_VERSION=<"%DBT_TOOLCHAIN_VERSION_FILE%"
) else (
	echo %DBT_TOOLCHAIN_VERSION_FILE% missing, will install...
	set "REAL_TOOLCHAIN_VERSION=0"
)

if not "%REAL_TOOLCHAIN_VERSION%" == "%DBT_TOOLCHAIN_VERSION%" (
	echo DBT: starting toolchain upgrade process to %DBT_TOOLCHAIN_VERSION%
	set "DBT_NEEDS_INSTALL=1"
)

if "%DBT_NEEDS_INSTALL%" == "1" (
    powershell -ExecutionPolicy Bypass -File "%DBT_ROOT%\scripts\toolchain\windows-toolchain-download.ps1" %DBT_TOOLCHAIN_VERSION% "%DBT_TOOLCHAIN_ROOT%"
    set /p REAL_TOOLCHAIN_VERSION=<"%DBT_TOOLCHAIN_VERSION_FILE%"
    set "DBT_TOOLCHAIN_INSTALLED=1"
)

if defined DBT_VERBOSE (
    echo DBT: using toolchain version %REAL_TOOLCHAIN_VERSION%
)

REM Set up the environment
set "HOME=%USERPROFILE%"
set "SSL_CERT_FILE=%DBT_TOOLCHAIN_ROOT%/python/Lib/site-packages/certifi/cacert.pem"
set "PYTHONHOME=%DBT_TOOLCHAIN_ROOT%\python"
set "PYTHONPATH=%DBT_ROOT%\scripts;%PYTHONPATH%"
set "PYTHONNOUSERSITE=1"
set "PATH=%DBT_TOOLCHAIN_ROOT%\python;%DBT_TOOLCHAIN_ROOT%\python\Scripts;%DBT_TOOLCHAIN_ROOT%\arm-none-eabi-gcc\bin;%DBT_TOOLCHAIN_ROOT%\openocd\bin;%DBT_TOOLCHAIN_ROOT%\cmake\bin;%DBT_TOOLCHAIN_ROOT%\ninja-build\bin;%PATH%"
set "PROMPT=(dbt) %PROMPT%"

set "DBT_TOOLCHAIN_INSTALLED=1"
set "PIP_REQUIREMENTS_PATH=%DBT_ROOT%\scripts\toolchain\requirements.txt"

if "%DBT_NEEDS_INSTALL%" == "1" (
	python -m pip install -q --upgrade pip
	python -m pip install -q -r "%PIP_REQUIREMENTS_PATH%"
    pre-commit install --install-hooks
)

REM Check if toolchain/current exists and remove it
if exist %DBT_ROOT%\toolchain\current (
    REM Query the junction and extract the "Print Name" (the visible target)
    REM Capture the entire line that contains "Print Name"
    for /f "delims=" %%L in ('fsutil reparsepoint query %DBT_ROOT%\toolchain\current\ ^| findstr /i "Print Name"') do (
        set "line=%%L"
    )

    REM Remove the known prefix "Print Name: " (including the space)
    REM Adjust the number (here 12) if the prefix length differs.
    set "target=!line:~23!"

    REM Compare the target with the expected values
    if /I "!target!" == "%DBT_TOOLCHAIN_ROOT%" (
        REM The junction is already set to the correct target
    ) else (
        echo Updating current toolchain link
        rmdir %DBT_ROOT%\toolchain\current
        mklink /j %DBT_ROOT%\toolchain\current %DBT_TOOLCHAIN_ROOT%
    )
) else (
    echo Updating current toolchain link
    mklink /j %DBT_ROOT%\toolchain\current %DBT_TOOLCHAIN_ROOT%
)

:already_set

set DBT_EP=python dbt.py

if [%DBT_NO_SYNC%] == [] (
	if exist ".git" (
		git submodule update --init --depth 1 --jobs %NUMBER_OF_PROCESSORS%
	) else (
		echo Not in a git repo, please clone with "git clone"
		exit /b 1
	)
)

%DBT_EP% %*
