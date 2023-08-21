ifeq ($(OS),Windows_NT)
		export DBT := cmd.exe /c dbt.cmd
else
		export DBT := ./dbt
endif

all: debug release

debug:
	$(DBT) build debug

release:
	$(DBT) build release

clean:
	$(DBT) clean
