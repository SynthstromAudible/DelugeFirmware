#!/usr/bin/env bash
#
# Task 10's CI diff net for src/scenes/menu/: rebuild deluge_menu_dump, regenerate
# website/src/data/generated/menu-hierarchies.json from its output, and fail loudly if that
# changes anything git already has committed -- i.e. the declarative menu tree drifted from the
# JSON dump a future .state golden's `app16/<id>` entries are meant to resolve against.
#
# Usage: scripts/menu_dump_check.sh
#
# Assumes build-sim already exists and is configured for X64:
#   cmake -B build-sim -S sim -G Ninja -DDELUGE_SIM_X64=ON
# The CI job's own earlier "UI app scripts (level 3)" step does this; run it first if working
# locally. This script does not configure build-sim itself, on purpose: it is meant to run right
# after that step, not stand alone, and a stale/missing build-sim should fail loudly here rather
# than silently reconfigure with whatever flags happen to be convenient in this one script.
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "${repo_root}"

if [ ! -d build-sim ]; then
	echo "menu_dump_check.sh: build-sim/ is missing -- configure it first:" >&2
	echo "  cmake -B build-sim -S sim -G Ninja -DDELUGE_SIM_X64=ON" >&2
	exit 1
fi

cmake --build build-sim --target deluge_menu_dump

tool="build-sim/deluge_menu_dump"
if [ ! -x "${tool}" ]; then
	echo "menu_dump_check.sh: ${tool} was not produced by the build" >&2
	exit 1
fi

committed="website/src/data/generated/menu-hierarchies.json"
if [ ! -f "${committed}" ]; then
	echo "menu_dump_check.sh: ${committed} is missing -- run '${tool} > ${committed}' and commit it" >&2
	exit 1
fi

"${tool}" > "${committed}"

if [ ! -s "${committed}" ]; then
	echo "menu_dump_check.sh: ${tool} produced no output" >&2
	exit 1
fi

if ! git diff --exit-code -- "${committed}"; then
	echo "menu_dump_check.sh: ${committed} no longer matches ${tool}'s output (diff above)." >&2
	echo "menu_dump_check.sh: the menu tree changed -- review the diff, then commit the regenerated ${committed}." >&2
	exit 1
fi

echo "menu_dump_check.sh: ${committed} matches ${tool}'s output."
