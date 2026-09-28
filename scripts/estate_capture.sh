#!/usr/bin/env bash
# Run every registered gate of the harness estate serially and collect what they printed, for comparing two trees.
#
#   scripts/estate_capture.sh <label>        -> .superpowers/estate/<label>/
#
# Runs each harness whose status is "gate" and whose CI tier is not "never" through `./dbt harness run` (gates share
# build trees, so they never run in parallel). Writes:
#   <gate>.log    the gate's whole output
#   markers.txt   "<gate>: <line>" for every marker line (an upper-case tag followed by a space, '=', ':' or a
#                 further '-TAG'), plus each gate's exit status -- what two captures are diffed on
#   summary.txt   one "<gate> rc=<n>" line per gate and the pass count
# Compare two captures with: diff .superpowers/estate/<a>/markers.txt .superpowers/estate/<b>/markers.txt
# Wall-time markers (maskMicros=, letGoMicros=, ...) differ run to run; everything else should not.
#
# The output lives under .superpowers/ (git-ignored) so it survives reboots, unlike a /tmp scratch directory.
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

label="${1:?usage: scripts/estate_capture.sh <label>}"
OUT="$REPO_ROOT/.superpowers/estate/$label"
mkdir -p "$OUT"
: >"$OUT/markers.txt"
: >"$OUT/summary.txt"

mapfile -t gates < <(./dbt harness list 2>/dev/null | awk '$1 ~ /^[A-Z]+$/ && $3 == "gate" && $4 != "never" { print $2 }')
if [ "${#gates[@]}" -eq 0 ]; then
	echo "ERROR: no gates found in './dbt harness list'"
	exit 2
fi

passed=0
for gate in "${gates[@]}"; do
	echo "== $gate"
	./dbt harness run "$gate" >"$OUT/$gate.log" 2>&1
	rc=$?
	[ "$rc" -eq 0 ] && passed=$((passed + 1))
	echo "$gate rc=$rc" >>"$OUT/summary.txt"
	{
		grep -E '^[A-Z][A-Z0-9_]*(-[A-Z0-9_]+)*([ =:]|$)' "$OUT/$gate.log" | sed "s/^/$gate: /"
		echo "$gate: rc=$rc"
	} >>"$OUT/markers.txt"
done

echo "passed $passed/${#gates[@]}" | tee -a "$OUT/summary.txt"
[ "$passed" -eq "${#gates[@]}" ]
