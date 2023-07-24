ifeq ($(OS),Windows_NT)
		export DBT := cmd.exe /c dbt.cmd
else
		export DBT := ./dbt
endif

all: 7seg-debug oled-debug 7seg-release oled-release

release: 7seg-release oled-release

debug: 7seg-debug oled-debug

7seg: 7seg-debug 7seg-release

oled: oled-debug oled-release

7seg-debug:
	$(DBT) build 7seg debug

oled-debug:
	$(DBT) build oled debug

7seg-release:
	$(DBT) build 7seg release

oled-release:
	$(DBT) build oled release

clean:
	$(DBT) clean
