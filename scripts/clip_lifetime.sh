#!/usr/bin/env bash
# clip_lifetime.sh — the clip-lifetime probe (report-only).
#
# Deletes a playing clip through each real clip deletion path, a sounding drum through the two direct drum destroys, and
# the song through each way it is replaced, and reports how many Clips, Songs and Sounds each path destroyed while the
# render could still reach them.
#
# ## Why this gate exists
#
# The render holds clip pointers across blocks: the current song's currentClip and syncScalingClip, every clip's
# beingRecordedFromClip, view.activeModControllableModelStack's timeline counter, and each output's activeClip.
# Song::deleteClipObject() destroys a clip at once, on the interaction tier, and no deletion path clears all of those
# first -- ~Clip's own fallback clears only currentClip. This measures which paths leave one pointing at a freed clip.
# The counts are the evidence for the paths that must hand the clip to the render instead; until they do, no count
# fails the run.
#
# What the lines cover (src/deluge/harness/clip_lifetime_scenario.cpp writes them). Every leg plays its clip on the
# internal clock, holding a note on the clip's blank synth, and the scenario renders 8 blocks before the deletion and 8
# after it:
#   CL-PROBE        per leg, before the deletion: whether the clip is playing, how many ticks the render actioned,
#                   the synth's live voices, and which references point at the clip.
#   CL-DANGLING     per leg, just after the deletion: which references still hold the clip's address, and whether
#                   the clip is still in the song (compared by value; the clip may be freed).
#   CL-SESSIONDELETE  the current, selected session clip, deleted through the session view's delete gesture
#                   (SessionView::removeClip() -> Song::removeSessionClip()).
#   CL-ARRANGERDELETE an arrangement-only clip that is the current clip, deleted from the arranger with the arranger's
#                   delete gesture (ArrangerView::deleteClipInstance()'s steps: view to the song, a real
#                   CLIP_INSTANCE_EDIT action, the instance's existence change, Song::deletingClipInstanceForClip()'s
#                   logged branch), then destroyed by discarding the logs.
#   CL-SWAPTYPE     the current, selected clip changed to an audio clip (SessionView::replaceInstrumentClipWithAudioClip()
#                   -> Song::swapClips()).
#   CL-UNDODELETE   the same arranger delete, undone (restored=), redone, and then destroyed by discarding the logs
#                   (ConsequenceClipExistence::prepareForDestruction()). The only logged clip deletion the firmware
#                   records: the session view's delete discards the logs and records nothing.
#   CL-UNDO         UNDODELETE's undo: restored=1 when the clip is back in arrangementOnlyClips with an instance.
#                   Printed before the redo, so it survives a redo that ends the process.
#   CL-DISCARD      ARRANGERDELETE and UNDODELETE: held=1 when the undo log still held the delete at the discard.
#   CL-RECORDSOURCE the clip another clip records from (beingRecordedFromClip), deleted through the session view's
#                   delete gesture.
#   CL-SYNCSCALE    the sync-scaling clip, deleted through Song::removeSessionClip() (the session view refuses it).
#   CL-SAVEKIT      a playing kit clip's kit holds a second drum no note row has, triggered just before the song is
#                   saved (Song::writeToFile() into memory, as the song save serialises): Kit::writeDataToFile()
#                   destroys that drum. CL-SAVE says what the drum was doing at the save and how many bytes it wrote;
#                   CL-DANGLING (inKit=, renderingActive=) whether the kit still holds it afterwards.
#   CL-SONGCLEAR    stopped clock, a synth sounding: the context menu's Create New Song
#                   (ClearSongContextMenu::runAccept() -> deleteOldSongBeforeLoadingNew()). CL-CLEARED once it is over.
#   CL-SONGLOAD     stopped clock, a synth sounding: a song the scenario wrote through Song::writeToFile() is loaded
#                   through the load browser (LoadSongUI -> deleteOldSongBeforeLoadingNew()). CL-LOADED loaded=1 when
#                   the song came off the card.
#   CL-SONGSWAP     the same load with the clock running and a synth sounding: the load arms a swap
#                   (Session::armForSongSwap()), the render launches it (doSongSwap()), and
#                   LoadSongUI::runFinishSegment() destroys the old song.
#   CL-RECLAIM      after those legs and before WONTSOUND: AudioEngine::retiredPending() once the render has run and
#                   reclaimRetired() has been called. More than 0 is red.
#   CL-WONTSOUND    last, because it is expected to end the process: a playing kit clip whose drum is sounding with an
#                   empty note row, then AudioEngine::songSwapAboutToHappen() -- what a load or clear started during
#                   playback runs first -> Song::deleteSoundsWhichWontSound() -> InstrumentClip::
#                   deleteSoundsWhichWontSound(), which destroys the drum (or freezes with E176 on an alpha/beta build,
#                   which the host harness is).
#   CL-RESULT       fixtures that could not be built, or a clock that never ran.
#
# Each leg line is `CL-<LEG> clipsReferenced=<n> songsReachable=<n> soundsLive=<n>`: how far
# Clip::clipsDestroyedWhileReferenced(), Song::songsDestroyedWhileReachable() and Sound::soundsDestroyedWithLiveVoices()
# moved over the leg. A leg reporting more than 0 is a red leg.
#
# ## Aborts
#
# A leg whose deletion, or the render reading what it left dangling, ends the process -- a firmware freeze
# (FREEZE_WITH_ERROR aborts on the host BSP), a std::terminate or a SIGSEGV -- still prints its line, marked aborted=1
# with the step it died in, and a CL-RESULT. That is a red leg, not a failure of the run: the script records it and
# starts a new run at the next leg (CLIP_LIFETIME_FROM), so one crashing path does not hide the others.
#
# The run FAILS only when a leg line or the CL-RECLAIM line is missing, a fixture could not be built (CL-RESULT
# failures>0), or the process ended some other way: an exit with no aborted=1 line for the leg that was running, or a
# wedge.
#
# Usage: scripts/clip_lifetime.sh
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
# same crate with the same features -- the scenario is chosen at run time by GOLDEN_SCENARIO.
TARGET_DIR="${TARGET_DIR:-${CARGO_TARGET_DIR:-$CRATE_DIR/target}}"
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/clip_lifetime.XXXXXX")"
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

LEGS=(SESSIONDELETE ARRANGERDELETE SWAPTYPE UNDODELETE RECORDSOURCE SYNCSCALE SAVEKIT SONGCLEAR SONGLOAD SONGSWAP
	WONTSOUND)

# The leg after $1, or nothing after the last.
next_leg() {
	local i
	for i in "${!LEGS[@]}"; do
		if [ "${LEGS[$i]}" = "$1" ]; then
			echo "${LEGS[$((i + 1))]:-}"
			return
		fi
	done
}

echo "clip lifetime: running (report-only)"
: >"$OUT/all.txt"
from=""
run=0
while :; do
	run=$((run + 1))
	set +e
	CLIP_LIFETIME_FROM="$from" GOLDEN_SCENARIO=clip_lifetime "$BIN" >"$OUT/run$run.txt" 2>"$OUT/run$run.err"
	status=$?
	set -e
	grep -E '^CL-' "$OUT/run$run.txt" >>"$OUT/all.txt" || true

	if [ "$status" -eq 0 ]; then
		break
	fi
	# A leg that ended the process says so with aborted=1. Carry on at the next leg; anything else fails the run.
	aborted="$(grep -E '^CL-[A-Z]+ .* aborted=1 ' "$OUT/run$run.txt" | tail -1 | sed -E 's/^CL-([A-Z]+) .*/\1/' || true)"
	if [ -z "$aborted" ]; then
		echo "FAIL — the run exited $status with no leg reporting aborted=1"
		grep -E '^CL-' "$OUT/all.txt" || sed -n '1,60p' "$OUT/run$run.txt"
		tail -20 "$OUT/run$run.err"
		exit 1
	fi
	echo "clip lifetime: CL-$aborted ended the process (exit $status); recorded as a red leg"
	from="$(next_leg "$aborted")"
	[ -n "$from" ] || break
done

for leg in "${LEGS[@]}"; do
	if ! grep -qE "^CL-$leg (restored=[01] )?clipsReferenced=" "$OUT/all.txt"; then
		echo "FAIL — no CL-$leg line: a leg never ran"
		grep -E '^CL-' "$OUT/all.txt"
		exit 1
	fi
done

if ! grep -qE '^CL-RECLAIM pending=[0-9]+$' "$OUT/all.txt"; then
	echo "FAIL — no CL-RECLAIM line"
	grep -E '^CL-' "$OUT/all.txt"
	exit 1
fi

grep -E '^CL-' "$OUT/all.txt"

if grep -E '^CL-RESULT ' "$OUT/all.txt" | grep -qv '^CL-RESULT failures=0$'; then
	echo "FAIL — a fixture could not be built or its clock never ran: $(grep '^CL-RESULT' "$OUT/all.txt" | tr '\n' ' ')"
	exit 1
fi

red="$(grep -E '^CL-[A-Z]+ (restored=[01] )?clipsReferenced=' "$OUT/all.txt" |
	grep -E 'aborted=1|(clipsReferenced|songsReachable|soundsLive)=[1-9]' | sed -E 's/^CL-([A-Z]+) .*/\1/' | sort -u |
	tr '\n' ' ' || true)"
if grep -qE '^CL-RECLAIM pending=[1-9]' "$OUT/all.txt"; then
	red="${red}RECLAIM "
fi
echo "PASS — report-only: every leg ran; red legs: ${red:-none}"
