#!/usr/bin/env bash
# sample_claim.sh — the sample-claim gate.
#
# Claims files from the real sample browser into the boot song's synth, a kit drum and an audio clip, and slices a
# kit, through the gestures a user makes: the sound editor's file selector opens the browser, a by-path listing
# selects the file, and the Select encoder claims it.
#
# What the checks cover (src/deluge/harness/sample_claim_scenario.cpp writes the lines):
#   SC-SETUP   per leg: whether the sound editor could be set up and the sample browser opened on it.
#   SC-SELECT  per leg: the file the by-path listing selected.
#   SC-SYNTH   the synth claim: the file bound, the oscillator type, the repeat mode and the transpose.
#   SC-WAVETABLE
#              the synth context menu's WaveTable claim of a single-cycle file: bound, as a wavetable oscillator.
#   SC-FALLBACK
#              a stereo file claimed into a wavetable oscillator: bound, as a sample oscillator instead.
#   SC-GATE    the select encoder turned while an explicit claim's load is held: the selection must not move, and
#              the gate must be released once the claim ends.
#   SC-STALL   an explicit claim whose load is held past the stall bound: it must give up, leaving the holder
#              unbound, the browser open and the gate released, having given up once.
#   SC-SUPERSEDE
#              an auto-load claim the cursor leaves while its pitch detection is out on the storage owner, for a next
#              preview that lands at once (case=landed) and for one still loading (case=loading): it must be
#              cancelled once and drain before the next claim starts, and only the next file's claim may finish.
#   SC-TIER    per leg: no claim or slice began its load on the storage owner, its wavetable lookups and pitch
#              detections all ran there, and no sample was read from the card on the interaction tier.
#   SC-KIT     the kit drum claim: the file bound, the repeat mode, the drum's name and its side-chain send.
#   SC-AUDIOCLIP
#              the audio clip claim: the file bound, and the clip's loop length and attack.
#   SC-SLICE   the slicer's four-region slice: how many drums the kit ends with, how many are bound to the sliced
#              sample, and how many NoteRows have a drum.
#   RD-ONE     the Drum Randomizer on one selected drum: it must end on a file from the folder its sample sat in,
#              named after that file, with every other row untouched.
#   RD-ALL     the same gesture with affect-entire held: every unmuted row that has notes must end on a file from
#              that folder, and the muted and empty rows must be untouched.
#   RD-TIER    per randomizer leg: every directory scan ran on the storage owner, no load began there, and no sample
#              was read from the card on the interaction tier.
#   RD-GATE    a randomize whose load the loader holds: while the walk is out, no second walk may start and an edit
#              pad must be refused; once it ends, the gate released and the same pad taken.
#   RD-STALL   a randomize over three rows whose second load is held past the stall bound: the walk must end there,
#              with the first row kept on its file, the second left unbound, the third untouched, and the gate
#              released.
#   IM-KIT     the kit context menu's "Load All" over a folder of its own: one drum per file in filename order, each
#              named after its file without the folder's shared prefix, bound to it and with a NoteRow; the kit
#              edited and the browser closed.
#   IM-MULTI   the synth context menu's "Multisamples" over three sines an octave apart: one range per file in pitch
#              order, each bound to its file, and the browser closed.
#   IM-TIER    per folder import: the listing and every pitch detection ran on the storage owner, no file load began
#              there, and no sample was read from the card on the interaction tier.
#   IM-GATE    a kit import whose loads the loader holds: while it is out, BACK and a second Select on the menu must be
#              refused; once it ends, the gate released and the kit holding its drums.
#   IM-STALL   a multisample import whose second file's load is held past the stall bound: it must abort with the
#              synth's ranges and the loaded files' reasons as they were, just the menu closed, the browser open and
#              the gate released.
#   SC-SLICE-MANUAL
#              a second kit sliced in manual mode into three slices, the live slice points changed straight after
#              Select: every drum bound to the sliced sample, with a NoteRow, and painted from the points as they
#              were when Select was pressed.
#
# A missing marker fails the run: a stage that never ran asserts nothing.
#
# Usage: scripts/sample_claim.sh
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
TARGET_BASE="${CARGO_TARGET_DIR:-$REPO_ROOT/$CRATE_DIR/target}"
TARGET_DIR="$TARGET_BASE/sample-claim"

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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/sample_claim.XXXXXX")"
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
	rm -rf "$OUT" "${TMPDIR:-/tmp}/deluge-sample-claim-card"
}
trap cleanup EXIT

echo "sample claim: running"
set +e
GOLDEN_SCENARIO=sample_claim "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^(SC|RD|IM)-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	tail -20 "$OUT/run.err"
	exit 1
fi

for marker in SC-SETUP SC-SELECT SC-SYNTH SC-WAVETABLE SC-FALLBACK SC-GATE SC-STALL SC-SUPERSEDE SC-TIER SC-KIT SC-AUDIOCLIP SC-SLICE SC-SLICE-MANUAL RD-ONE RD-ALL RD-TIER RD-GATE RD-STALL IM-KIT IM-MULTI IM-TIER IM-GATE IM-STALL SC-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line: a stage never ran"
		grep -E '^(SC|RD|IM)-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^SC-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^(SC|RD|IM)-' "$OUT/run.txt"
	exit 1
fi

echo "PASS — every claim bound what a user's claim binds"
grep -E '^(SC|RD|IM)-' "$OUT/run.txt"
