#!/usr/bin/env bash
# pattern_load.sh — the pattern-load gate.
#
# Writes a pattern file from notes on the boot song's synth clip, clears the notes, and loads the file back through
# the real pattern browser, once per leg below, from a cleared clip each time.
#
# What the checks below cover, and why each is separate:
#   PT-SETUP     that the fixture wrote the notes it meant to and then cleared them. Without the clear, a load that
#                did nothing would find the notes already there.
#   PT-LISTED    that each leg's listing selected the fixture, so the leg below loads that file and no other.
#   PT-LOAD      that Enter put back exactly the notes that were written, landing after the browser closed.
#   PT-PREVIEW   that a PLAY preview did the same, from a cleared clip, with the browser still open.
#   PT-DISCARD   that a preview followed by BACK, pressed while the channel holds the preview's read, pastes nothing.
#   PT-SUPERSEDE that Enter pressed while the channel holds a preview's read lands Enter's load, not nothing.
#   PT-TIER      that no leg made a synchronous card call on the interaction tier after boot. Required 0:
#                the pattern's bytes must arrive through the channels. PT-TIER-AT names each entry point the meter
#                counted; dir_read and dir_close are listed with card=0 and not required, since they walk the
#                snapshot the directory channel's open already took.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
# Where the harness binary is: `dbt test` builds it once, before any gate runs, into CARGO_TARGET_DIR. Every
# scenario gate runs the same binary; GOLDEN_SCENARIO picks the scenario at run time.
TARGET_DIR="${TARGET_DIR:-${CARGO_TARGET_DIR:-$CRATE_DIR/target}}"
# An absolute CARGO_TARGET_DIR arrives ready to use; a relative one is relative to the repo root.
case "$TARGET_DIR" in /*) ;; *) TARGET_DIR="$REPO_ROOT/$TARGET_DIR" ;; esac

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' is not built -- run this gate through ./dbt test, which builds it first"
	exit 2
}

OUT="$(mktemp -d "${TMPDIR:-/tmp}/pattern_load.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

echo "pattern load: running"
set +e
GOLDEN_SCENARIO=pattern_load "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^PT-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	sed -n '1,25p' "$OUT/run.err" >&2
	exit 1
fi

if ! grep -q '^PT-RESULT ' "$OUT/run.txt"; then
	echo "FAIL — no PT-RESULT line; the scenario did not run to completion"
	sed -n '1,60p' "$OUT/run.txt"
	exit 1
fi

for marker in PT-SETUP PT-LISTED PT-LOAD PT-PREVIEW PT-DISCARD PT-SUPERSEDE PT-TIER; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^PT-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^PT-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^PT-' "$OUT/run.txt"
	exit 1
fi

grep -E '^PT-' "$OUT/run.txt"
echo "PASS — Enter and a PLAY preview each loaded the pattern's notes, with no synchronous card call on the"
echo "       interaction tier"
