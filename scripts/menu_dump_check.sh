#!/usr/bin/env bash
#
# The diff net for src/scenes/menu/: rebuild golden_vt_render, regenerate
# website/src/data/generated/menu-tree.json with its menu_dump scenario, and fail loudly if that
# changes anything git already has committed -- i.e. the declarative menu tree drifted from the
# JSON dump a future .state golden's `app16/<id>` entries are meant to resolve against.
#
# Usage: scripts/menu_dump_check.sh
#
# If you are writing something that CONSUMES the generated JSON (not just diffing it, as this
# script does): it is close to, but not the same shape as, the retired Python DocEmitter's schema
# (see menu_dump.cpp's file header for the full mapping) --
#   - every entry's "description" key is entirely absent, not present-but-empty; code written
#     against the old schema that does entry["description"] will fail on this dump's output.
#   - "select_chain" is a SECOND nesting key beside "children": the pages a select press on that
#     node opens (legacy's MenuItem::selectButtonPress). Eleven pages appear in the dump ONLY
#     there, so a walker that recurses on "children" alone silently misses them.
#   - "ref": true marks a chain target already open above it on the same path -- a cycle stub,
#     carrying the same "id" as the full entry but no "children" and no "select_chain". The
#     patch-cable pages loop on purpose (legacy does), so a recursive walker MUST stop at "ref"
#     and resolve the id against the entry it has already seen. No other entry has the key.
#   - a node with dynamic_children set (type "multicontext") is UNIMPLEMENTED: as of this writing
#     no node uses it, and if one ever does, the dump refuses to guess a JSON shape for it
#     -- it prints the offending node to stderr and exits non-zero instead. Redesign the dump
#     against that node's real case rather than assume any particular shape survives from before.
#     (E2 task 10 looked for that case and found none: see menu_dump.cpp's
#     fail_on_dynamic_children note for why the three candidates cannot be dynamic submenus, and
#     for the shape that would survive if one ever is.)
# See also website/src/data/generated/README.md, next to the generated JSON itself.
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

./dbt test -n ui-menu-dump
git checkout src/bsp/rust/Cargo.lock src/bsp/rust/harness/golden_vt_render/Cargo.lock \
	tests/component/differential/fs_differential/Cargo.lock 2>/dev/null || true

tool="build/cargo-harness/release/golden_vt_render"
if [ ! -x "${tool}" ]; then
	echo "menu_dump_check.sh: ${tool} was not produced by the build" >&2
	exit 1
fi

committed="website/src/data/generated/menu-tree.json"
if [ ! -f "${committed}" ]; then
	echo "menu_dump_check.sh: ${committed} is missing -- run 'GOLDEN_SCENARIO=menu_dump MENU_DUMP_OUT=${committed} ${tool}' and commit it" >&2
	exit 1
fi

GOLDEN_SCENARIO=menu_dump MENU_DUMP_OUT="${committed}" "${tool}" >/dev/null

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
