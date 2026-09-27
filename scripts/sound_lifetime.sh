#!/usr/bin/env bash
# sound_lifetime.sh — the sound-lifetime probe.
#
# Deletes a sounding synth and a sounding kit through each real deletion path, and reports how many Sounds each path
# destroyed while they still had live voices (Sound::soundsDestroyedWithLiveVoices()).
#
# ## Why this gate exists
#
# A Sound destroyed while it still has live voices can have one culled by the render mid-destruction, and the cull
# calls back into the Sound's pure virtuals. Reading the deletion paths guesses which of them can do that; this
# measures it. Every deletion path must end a Sound's voices before any part of it is destroyed.
#
# What the lines cover (src/deluge/harness/sound_lifetime_scenario.cpp writes them):
#   SL-PROBE     per leg: the render ran over the held note, and how many voices were live at the deletion.
#   SL-SYNTH     a synth in the song, deleted through Song::deleteOutputThatIsInMainList().
#   SL-UNEDITED  an un-edited synth in no list, deleted through Song::deleteOrAddToHibernationListOutput().
#   SL-KIT       a kit in the song with a sounding synth drum, deleted through deleteOutputThatIsInMainList().
#   SL-DRUMTAIL  a sounding drum removed from its kit and destroyed the way the five drum-delete sites do it.
#
# Report only: the counts are printed, not asserted. The run fails only when a leg's fixture could not be built or
# never sounded, a line is missing, or a deletion terminates the process -- a leg that does still prints its count
# first, marked terminated=1, and the run's SL-RESULT, before it aborts.
#
# A missing marker fails the run: a stage that never ran reports nothing, and the failure count cannot show that.
#
# Usage: scripts/sound_lifetime.sh
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/sound_lifetime.XXXXXX")"
IMG_GLOB="${TMPDIR:-/tmp}/deluge-streaming-scenario-*.img"
IMGS_BEFORE="$OUT/.imgs_before"
ls -1 $IMG_GLOB 2>/dev/null | sort >"$IMGS_BEFORE" || true
cleanup() {
	if [ -f "$IMGS_BEFORE" ]; then
		ls -1 $IMG_GLOB 2>/dev/null | sort | comm -13 "$IMGS_BEFORE" - | while read -r img; do
			rm -f "$img"
		done
	fi
	rm -rf "$OUT"
}
trap cleanup EXIT

echo "sound lifetime: running"
set +e
GOLDEN_SCENARIO=sound_lifetime "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^SL-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	tail -20 "$OUT/run.err"
	exit 1
fi

for marker in SL-SYNTH SL-UNEDITED SL-KIT SL-DRUMTAIL SL-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line: a stage never ran"
		grep -E '^SL-' "$OUT/run.txt"
		exit 1
	fi
done

grep -E '^SL-' "$OUT/run.txt"

if ! grep -q '^SL-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^SL-RESULT' "$OUT/run.txt")"
	exit 1
fi

grep -hE '^TIER-SYNC(-AT)? ' "$OUT/run.txt" || true
echo "PASS — every leg sounded and was deleted; the counts above are reported, not asserted"
