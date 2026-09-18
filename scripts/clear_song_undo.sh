#!/usr/bin/env bash
# clear_song_undo.sh — the clear-song/undo gate.
#
# Drives Create New Song the way the context menu does — clearSong.acceptCurrentOption() — and requires the whole
# operation to run where the caller stands, reading no card of its own, with the new song's first preset arriving
# afterwards through InstrumentLoad on the interaction tier.
#
# ## Why this gate exists
#
# Clear-song dispatched its whole body onto the storage owner because two things inside it read the card. Phase 4b
# took both away: ensureAtLeastOneSessionClip() builds its clip on the built-in default synth and asks InstrumentLoad
# for a preset afterwards, and the loadAllSamples() that followed it can only ever load nothing. What is left is model
# and UI work that belongs on the interaction tier — and nothing measured that, until this gate.
#
# What the checks cover (src/deluge/harness/clear_song_undo_scenario.cpp writes the lines):
#   CS-PROBE   per stage: the fixture written, the boot song settled, and what the clear was seen to do.
#   CS-TIER    the clear ran to completion before acceptCurrentOption() returned — a dispatched body has not run
#              yet — and read no document and loaded no sample of its own, on either tier.
#   CS-SONG    the cleared song holds one clip whose preset came from SYNTHS through exactly one InstrumentLoad
#              acquisition, with its sample bound.
#   CS-GATE    the clear left no UI-mode gate closed behind it.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count cannot show that,
# because it counts only assertions that DID run.
#
# Usage: scripts/clear_song_undo.sh
#
# Environment:
#   BUILD_DIR   the build-embassy-hostapp C++ object-closure build dir (default: build-embassy-hostapp)
#   NO_BUILD=1  skip the ninja/cargo rebuild steps
#
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
	# Cargo does not track the C++ sources the harness links against, so force the object closure first.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT="$(mktemp -d "${TMPDIR:-/tmp}/clear_song_undo.XXXXXX")"
IMG_GLOB="${TMPDIR:-/tmp}/deluge-streaming-scenario-*.img"
IMGS_BEFORE="$OUT/.imgs_before"
ls -1 $IMG_GLOB 2>/dev/null | sort >"$IMGS_BEFORE" || true
cleanup() {
	if [ -f "$IMGS_BEFORE" ]; then
		ls -1 $IMG_GLOB 2>/dev/null | sort | comm -13 "$IMGS_BEFORE" - | while read -r img; do
			rm -f "$img"
		done
	fi
	rm -rf "$OUT" "${TMPDIR:-/tmp}/deluge-clear-song-undo-card"
}
trap cleanup EXIT

echo "clear song undo: running"
set +e
GOLDEN_SCENARIO=clear_song_undo "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^(CS|UN)-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	tail -20 "$OUT/run.err"
	exit 1
fi

for marker in CS-PROBE CS-TIER CS-SONG CS-GATE UN-PROBE UN-SAMPLE UN-REDO UN-PLAIN UN-REFUSE UN-MISSING CS-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line: a stage never ran"
		grep -E '^(CS|UN)-' "$OUT/run.txt"
		exit 1
	fi
done

grep -E '^(CS|UN)-' "$OUT/run.txt"

if ! grep -q '^CS-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^CS-RESULT' "$OUT/run.txt")"
	exit 1
fi

echo "PASS — the clear ran on the caller's tier, read no card, and its song landed on a preset InstrumentLoad acquired"
