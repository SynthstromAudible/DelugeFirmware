@echo off

set "SCRIPT_PATH=%DBT_ROOT%"
call "%~dp0scripts\toolchain\dbtenv.cmd" env

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