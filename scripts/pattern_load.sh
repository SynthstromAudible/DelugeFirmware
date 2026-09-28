#!/usr/bin/env bash
# pattern_load.sh — the pattern-load gate.
#
# Writes a pattern file from notes on the boot song's synth clip, clears the notes, and loads the file back through
# the real pattern browser: once with Enter, once previewed with PLAY.
#
# What the checks below cover, and why each is separate:
#   PT-SETUP     that the fixture wrote the notes it meant to and then cleared them. Without the clear, a load that
#                did nothing would find the notes already there.
#   PT-LISTED    that each leg's listing selected the fixture, so the leg below loads that file and no other.
#   PT-LOAD      that Enter put back exactly the notes that were written.
#   PT-PREVIEW   that a PLAY preview did the same, from a cleared clip.
#   PT-TIER      that neither leg made a synchronous card call on the interaction tier after boot. Required 0:
#                the pattern's bytes must arrive through the channels. PT-TIER-AT names each entry point the meter
#                counted; dir_read and dir_close are listed with card=0 and not required, since they walk the
#                snapshot the directory channel's open already took.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
# One build tree for the harness binary: `dbt harness run` sets CARGO_TARGET_DIR, and every gate builds the
# same crate with the same features -- the scenario is chosen at run time by GOLDEN_SCENARIO. A per-gate
# subdir here would relink an identical binary into a tree of its own.
TARGET_DIR="${TARGET_DIR:-${CARGO_TARGET_DIR:-$CRATE_DIR/target}}"
# An absolute CARGO_TARGET_DIR arrives ready to use; a relative fallback is relative to the repo
# root, and the cargo build below runs with the crate directory as its working directory.
case "$TARGET_DIR" in /*) ;; *) TARGET_DIR="$REPO_ROOT/$TARGET_DIR" ;; esac

if [ "${NO_BUILD:-0}" != "1" ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against (this scenario is one of them),
	# so force the object closure first — same reason save_roundtrip.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
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

for marker in PT-SETUP PT-LISTED PT-LOAD PT-PREVIEW PT-TIER; do
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
