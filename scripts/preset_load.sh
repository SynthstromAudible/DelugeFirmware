#!/usr/bin/env bash
# preset_load.sh — the preset-load gate.
#
# Loads synth and kit presets through the real preset browser, as a user does: the clip view's LOAD gesture opens the
# browser, a by-path listing selects the preset, and Select loads it into the clip, a kit row, or -- from the long-press
# menu -- a clone.
#
# What the checks cover (src/deluge/harness/preset_load_scenario.cpp writes the lines):
#   PL-SETUP   per leg: whether the preset browser opened on the leg's clip, and how many outputs the song had.
#   PL-SELECT  per leg: the preset the by-path listing selected.
#   PL-SYNTH   a synth preset loaded into the boot song's synth clip: the loaded synth, named after the preset, its
#              sample bound, and the browser closed.
#   PL-KIT     a kit preset loaded into a kit clip: three drums, each named and bound to its sample, with NoteRows.
#   PL-ROW     a synth preset loaded into a kit row: the row's drum named after the preset, its sample bound.
#   PL-CLONE   the long-press menu's clone of a synth preset: a new synth under a variation of the preset's name, its
#              sample bound, edited, and the browser closed.
#   PL-TIER    per load: documents read and samples loaded, none of it begun on the storage owner, and no sample read
#              from the card on the interaction tier.
#   PL-RACE    a clone made straight after the selection, without waiting for its scroll-load: the clone survives.
#   PL-SUPERSEDE
#              the loader holding every sample, a scroll onto a heavy kit and on to a light one: the heavy kit never
#              swapped in, nothing of it left in the song, the light kit loaded.
#   PL-GATE    a commit whose sample the loader holds: an output-type button and an encoder turn refused while it is
#              out; once it ends, the gate released, the preset loaded and the browser closed.
#   PL-STALL   a commit whose sample load is held past the stall bound: the synth swapped in with that holder empty,
#              the gate released and the browser closed.
#
# The acquisition legs pick presets without the browser, by the gestures a user makes:
#   AQ-NEW     an empty session pad pressed makes a synth track on the first unused synth preset, its sample bound; then
#              shift+SYNTH gives that clip a new blank synth under an unused slot name.
#   AQ-CHANGE  the clip view's KIT button on that empty clip lands on the first unused kit preset, its drum bound.
#   AQ-NAVIGATE
#              the select encoder turned from the clip view, a held grid pad and an auditioned arranger row: each turn
#              lands on the next unused kit preset, its drum bound.
#   AQ-SUPERSEDE
#              the loader holding every sample load, a turn onto a heavy kit and a second once its sample load has begun:
#              the light kit loaded, the heavy kit never swapped in, nothing of it left, and a supersession counted.
#   AQ-REFUSE  three new-track presses in one tick (synth, kit, synth): the in-flight and the queued one kept, the third
#              refused.
#   AQ-CLONE   a new-track press and a grid clone into an empty column in one tick: both tracks made.
#   AQ-STALL   a turn whose sample load is held past the stall bound: the synth delivered with that holder empty and one
#              give-up counted.
#   AQ-TIER    per acquisition leg: documents read and samples loaded, none of it begun on the storage owner, and no
#              sample read from the card on the interaction tier.
#
# The revert legs back out of the browser, which puts the clip's original preset back:
#   RV-CARD    an original that left memory: the browser closed on the exit tick, the gate closed while the revert
#              loaded, the original back with its sample bound, and the read the machine's.
#   RV-MEMORY  an edited original, which hibernates: put back with no document read at all.
#   RV-STALL   an original whose sample load is held past the stall bound: back with that holder empty, the gate
#              released.
#   MD-BROWSER a MIDI device definition written through the firmware and loaded through its own browser: the file's CC
#              label on the instrument, the browser closed, and the document staged once by InstrumentLoad off the
#              storage owner.
#
# The blank-song legs cover the song a Deluge starts with, and the one Create New Song makes:
#   BS-BOOT    the boot song, before any preset is written: its clip on the default synth, having asked for a preset
#              exactly once and found none.
#   BS-CLEAR   clear-song with presets on the card: the new song's clip on a SYNTHS preset, its sample bound, the
#              document staged by InstrumentLoad off the storage owner and nothing read synchronously.
#   BS-EDITED  clear-song with the preset's sample held and the default synth edited while the request is out: the
#              preset not swapped in, and nothing of it left in the song.
#
# A missing marker fails the run: a stage that never ran asserts nothing.
#
# Usage: scripts/preset_load.sh
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
TARGET_DIR="${CARGO_TARGET_DIR:-$REPO_ROOT/$CRATE_DIR/target}"

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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/preset_load.XXXXXX")"
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
	rm -rf "$OUT" "${TMPDIR:-/tmp}/deluge-preset-load-card"
}
trap cleanup EXIT

echo "preset load: running"
set +e
# Three legs hold the loader for 30 virtual seconds each (PL-STALL, AQ-STALL, RV-STALL), which is more than the
# runner's default 60s virtual-time budget covers.
GOLDEN_SCENARIO=preset_load GOLDEN_VIRTUAL_BUDGET_MS=180000 "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^(PL|PW|AQ|RV|MD|BS)-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	tail -20 "$OUT/run.err"
	exit 1
fi

for marker in PL-SETUP PL-SELECT PL-SYNTH PL-KIT PL-ROW PL-CLONE PL-TIER PL-RACE PL-SUPERSEDE PL-GATE PL-STALL \
	AQ-NEW AQ-CHANGE AQ-NAVIGATE AQ-SUPERSEDE AQ-REFUSE AQ-CLONE AQ-STALL AQ-TIER \
	RV-CARD RV-MEMORY RV-STALL MD-BROWSER BS-BOOT BS-CLEAR BS-EDITED \
	PL-WALK-FOUND PL-WALK-DEPTH PL-WALK-PAGED PL-WALK-EXHAUST PL-WALK-TIER \
	PW-WALK-FOUND PW-WALK-DEPTH PW-WALK-PAGED PW-WALK-EXHAUST PW-WALK-TIER PL-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line: a stage never ran"
		grep -E '^(PL|PW|AQ|RV|MD|BS)-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^PL-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^(PL|PW|AQ|RV|MD|BS)-' "$OUT/run.txt"
	exit 1
fi

echo "PASS — every preset load bound what a user's load binds"
grep -E '^(PL|PW|AQ|RV|MD|BS)-' "$OUT/run.txt"
