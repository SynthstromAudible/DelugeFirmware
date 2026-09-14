#!/usr/bin/env bash
# sample_load_tree.sh — the sample-load tree gate.
#
# Builds a song through the firmware (a synth with a sample source and a wavetable source, a kit with a sample drum,
# and an audio clip), saves it with collected media, and loads it back through the load screen. Then writes a synth
# preset and a kit preset through the firmware serialiser and loads each. Every fixture document is written by the
# firmware; none is hand-written.
#
# What the checks cover (src/deluge/harness/sample_load_tree_scenario.cpp writes the lines):
#   SLT-HOLDER  per holder: its resolved path, whether a file is bound, and where the file was loaded from.
#               - The song's samples resolve to their collected copies in the song's folder, because their
#                 originals were removed from the card before the load.
#               - The audio clip's sample stays resident and is claimed by the load's memory-only pass.
#               - Each preset's sample resolves to the preset's own folder, with its path rewritten there.
#   SLT-CLIP    the audio clip's name after the load is its sample's path.
#   SLT-KIT     an abort raised after the kit preset's first drum binds stops the load: the second drum stays empty.
#   SLT-SONG / SLT-SYNTH / SLT-KIT
#               how many times each load called routineWithClusterLoading, the cadence that keeps audio running.
#   LT          the load trace: one line per call into the traced leaf functions, while a load runs.
#   SLT-CRUCIAL a song loaded while playback runs: which holders its crucial-only pass bound (only what the playing
#               clips need first), that its armed swap happened, and every holder bound to its collected copy after.
#   LTC         the crucial load's trace, kept apart from LT so the LT lines stay comparable with earlier runs.
#   SLT-ASYNC   each load again through the async sample load, ticked from the interaction tier with every file it
#               binds dropped from memory first: SLTSONG2's crucial and full walks, the synth preset, and the kit
#               preset with its abort. Each must end as its blocking load did, with no sample read from the card on
#               the interaction tier.
#
# A missing marker fails the run: a stage that never ran asserts nothing.
#
# Usage: scripts/sample_load_tree.sh
#
# Environment:
#   BUILD_DIR               the build-embassy-hostapp C++ object-closure build dir (default: build-embassy-hostapp)
#   NO_BUILD=1              skip the ninja/cargo rebuild steps
#   SAMPLE_LOAD_TREE_TRACE  if set, a file to write the run's LT lines to
#   SAMPLE_LOAD_TREE_CRUCIAL_TRACE  if set, a file to write the run's LTC lines to
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_BASE="${CARGO_TARGET_DIR:-$REPO_ROOT/$CRATE_DIR/target}"
TARGET_DIR="$TARGET_BASE/sample-load-tree"

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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/sample_load_tree.XXXXXX")"
IMG_GLOB="${TMPDIR:-/tmp}/deluge-streaming-scenario-*.img"
# Only the image this run packs is removed; other rigs' cached images are left alone.
IMGS_BEFORE="$OUT/.imgs_before"
ls -1 $IMG_GLOB 2>/dev/null | sort >"$IMGS_BEFORE" || true
cleanup() {
	if [ -f "$IMGS_BEFORE" ]; then
		ls -1 $IMG_GLOB 2>/dev/null | sort | comm -13 "$IMGS_BEFORE" - | while read -r img; do
			rm -f "$img"
		done
	fi
	rm -rf "$OUT" "${TMPDIR:-/tmp}/deluge-sample-load-tree-card"
}
trap cleanup EXIT

echo "sample load tree: running"
set +e
GOLDEN_SCENARIO=sample_load_tree "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ -n "${SAMPLE_LOAD_TREE_TRACE:-}" ]; then
	grep '^LT ' "$OUT/run.txt" >"$SAMPLE_LOAD_TREE_TRACE" || true
fi
if [ -n "${SAMPLE_LOAD_TREE_CRUCIAL_TRACE:-}" ]; then
	grep '^LTC ' "$OUT/run.txt" >"$SAMPLE_LOAD_TREE_CRUCIAL_TRACE" || true
fi

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^SLT-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	sed -n '1,20p' "$OUT/run.err" >&2
	exit 1
fi
for marker in SLT-SETUP SLT-HOLDER SLT-CLIP SLT-SONG SLT-SYNTH SLT-KIT SLT-CRUCIAL SLT-CRUCIAL-SUMMARY SLT-ASYNC SLT-SUMMARY \
	SLT-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^SLT-' "$OUT/run.txt"
		exit 1
	fi
done
if ! grep -q '^SLT-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^SLT-' "$OUT/run.txt"
	exit 1
fi

grep -E '^SLT-' "$OUT/run.txt"
echo "PASS — every holder resolved where the fixture put its file, the abort stopped the kit load, and the"
echo "       audio clip kept its sample's name; a load while playing bound only what the playing clips need"
echo "       first, and the rest after its armed swap; and each load, again through the async sample load,"
echo "       ended the same way without reading a sample on the interaction tier"