ifeq ($(OS),Windows_NT)
		export DBT := cmd.exe /c dbt.cmd
else
		export DBT := ./dbt
endif

# Build configuration for `make flash` (release | debug | relwithdebinfo | CMake name).
CONFIG ?= release

all: debug release

debug:
	$(DBT) build debug

release:
	$(DBT) build release

clean:
	$(DBT) clean

.PHONY: flash
flash:
	$(DBT) flash $(CONFIG)
