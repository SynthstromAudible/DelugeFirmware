#!/usr/bin/env bash
# sound_lifetime.sh — the sound-lifetime probe.
#
# Deletes a sounding synth and a sounding kit through each real deletion path, and requires that no path destroys a
# Sound while it still has live voices (Sound::soundsDestroyedWithLiveVoices()), and that everything the deletions
# retired is freed once the render has run past it.
#
# ## Why this gate exists
#
# A Sound destroyed while it still has live voices crashes itself, with no render involved: after ~Sound's body, its
# own voices_ member destructor recycles each voice, and ~Voice -> Voice::setAsUnassigned -> Sound::voiceUnassigned ->
# reassessRenderSkippingStatus calls getArpSettings(), pure virtual in Sound by then. Reading the deletion paths
# guesses which of them can do that; this measures it. Every deletion path must end a Sound's voices before any part
# of it is destroyed.
#
# What the lines cover (src/deluge/harness/sound_lifetime_scenario.cpp writes them):
#   SL-PROBE     per leg: how many voices were live at the deletion, whether the output is in the song (the render
#                reaches only those voices), and how far the render's clock moved with the note held.
#   SL-SYNTH     a synth in the song, deleted through Song::deleteOutputThatIsInMainList().
#   SL-UNEDITED  an un-edited synth in no list, deleted through Song::deleteOrAddToHibernationListOutput().
#   SL-KIT       a kit in the song with a sounding synth drum, deleted through deleteOutputThatIsInMainList().
#   SL-DELETEDRUM a sounding drum deleted with InstrumentClipView::deleteDrum()'s steps: the note row's note stopped
#                and the drum taken out of drumsWithRenderingActive, Kit::removeDrum(), the note row's reassignment,
#                then AudioEngine::retire(). The only drum-delete site that can reach a sounding drum: the
#                preset-replace site kills the voices first, and the folder-import, slicer and instrument-load sites
#                only drop drums that never sounded.
#   SL-OPPORTUNISTIC  in one step: retire a blank drum, render 3 blocks, retire another. The second retire must free
#                the first on its own (pending=1): no reclaim-task tick runs inside a step.
#   SL-BURST     in one step: retire 80 blank drums (more than the retire ring's 64 slots), then render and reclaim
#                block by block. Required: violations=0 (no drum destroyed while still in the Sound registry, i.e.
#                before the render let go of it) and pending=0.
#   SL-RECLAIM   after the last leg and a few more blocks: how many retired objects still wait to be destroyed.
#
# Every deletion now retires (AudioEngine::retire): the render ends the voices and deregisters the Sounds at the top
# of its next block, and the memory is freed by the first reclaim after that once two blocks have begun. So a leg's
# count is read after its settle blocks, and a leg whose objects are still waiting at that point fails. Required:
# every leg destroyedWithLiveVoices=0, SL-OPPORTUNISTIC pending=1, SL-BURST violations=0 pending=0 and SL-RECLAIM
# pending=0. The run also fails when a leg's fixture could not be built or never sounded, a line is
# missing, or a deletion ends the process: a firmware freeze (E321 from Kit::removeDrum on a drum still in
# drumsWithRenderingActive; FREEZE_WITH_ERROR aborts on the host BSP) or a std::terminate, during the deletion or the
# deferred destruction in the settle. A leg that does still prints its count first, marked aborted=1 with the step it
# died in, and the run's SL-RESULT.
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
# One build tree for the harness binary: `dbt test` sets CARGO_TARGET_DIR, and every gate builds the
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
		{ ls -1 $IMG_GLOB 2>/dev/null || true; } | sort | comm -13 "$IMGS_BEFORE" - | while read -r img; do
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

for marker in SL-SYNTH SL-UNEDITED SL-KIT SL-DELETEDRUM SL-OPPORTUNISTIC SL-BURST SL-RECLAIM SL-RESULT; do
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

# Every deletion path must end a Sound's voices before any part of it is destroyed.
for leg in SL-SYNTH SL-UNEDITED SL-KIT SL-DELETEDRUM; do
	if ! grep -q "^$leg destroyedWithLiveVoices=0\$" "$OUT/run.txt"; then
		echo "FAIL — $(grep "^$leg destroyedWithLiveVoices=" "$OUT/run.txt"): a Sound was destroyed with live voices"
		exit 1
	fi
done

# A retire frees what the render has already let go of and run two blocks past, with no reclaim-task tick needed.
if ! grep -q '^SL-OPPORTUNISTIC pending=1$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^SL-OPPORTUNISTIC' "$OUT/run.txt"): a retire did not free an older, let-go object"
	exit 1
fi

# A burst past the ring's capacity: nothing is destroyed before the render lets go of it, and everything is freed.
if ! grep -qE '^SL-BURST .* violations=0 pending=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^SL-BURST' "$OUT/run.txt"): a retired object was destroyed before the render let go of it, or never freed"
	exit 1
fi

# Everything retired has been freed once the render has run two blocks past it.
if ! grep -q '^SL-RECLAIM pending=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^SL-RECLAIM' "$OUT/run.txt"): a retired object was never freed"
	exit 1
fi

grep -hE '^TIER-SYNC(-AT)? ' "$OUT/run.txt" || true
echo "PASS — every leg sounded and was deleted with no live voices, and everything retired was freed"
