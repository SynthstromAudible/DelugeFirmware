#!/usr/bin/env bash
# clip_lifetime.sh — the clip-lifetime gate.
#
# Deletes a playing clip through each real clip deletion path, a drum through the two former direct drum destroys
# (SAVEKIT naturally, WONTSOUND by forcing a state no natural note-on/off path reaches -- see its bullet below), and
# the song through each way it is replaced, and reports how many Clips, Songs and Sounds each path destroyed while the
# render could still reach them.
#
# ## Why this gate exists
#
# The render holds clip pointers across blocks: the current song's currentClip and syncScalingClip, every clip's
# beingRecordedFromClip, view.activeModControllableModelStack's timeline counter, and each output's activeClip.
# Song::deleteClipObject() destroys a clip at once, on the interaction tier, and no deletion path clears all of those
# first -- ~Clip's own fallback clears only currentClip. This measures which paths leave one pointing at a freed clip.
# The clip deletion paths now hand the clip to the render (AudioEngine::retire(Clip*)), which drops those references at
# its next block before the clip is freed; the clip legs are REQUIRED clean (see "Required legs" below). SAVEKIT and
# WONTSOUND now hand their drum to the render the same way (AudioEngine::retire(SoundDrum*)) and are REQUIRED clean
# too. The song legs are still report-only.
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
#   CL-ARRANGERNOUNDO the same arranger delete when the action logger has no action to give (no undo memory): the
#                   gesture passes a null action, so Song::deletingClipInstanceForClip() deletes the clip at once. The
#                   one path that leaves the song's currentClip on the deleted clip for the render to drop.
#   CL-RECORDSOURCE the clip another clip records from (beingRecordedFromClip), deleted through the session view's
#                   delete gesture.
#   CL-SYNCSCALE    the sync-scaling clip, deleted through Song::removeSessionClip() (the session view refuses it).
#   CL-SAVEKIT      a playing kit clip's kit holds a second drum no note row has, triggered just before the song is
#                   saved (Song::writeToFile() into memory, as the song save serialises): Kit::writeDataToFile()
#                   hands that drum to the render (AudioEngine::retire(SoundDrum*)) instead of destroying it on the
#                   spot. CL-SAVE says what the drum was doing at the save and how many bytes it wrote; CL-DANGLING
#                   (inKit=, renderingActive=) whether the kit still holds it afterwards.
#   CL-SONGCLEAR    stopped clock, a synth sounding: the context menu's Create New Song
#                   (ClearSongContextMenu::runAccept() -> deleteOldSongBeforeLoadingNew()). CL-CLEARED once it is over.
#   CL-SONGLOAD     stopped clock, a synth sounding: a song the scenario wrote through Song::writeToFile() is loaded
#                   through the load browser (LoadSongUI -> deleteOldSongBeforeLoadingNew()). CL-LOADED loaded=1 when
#                   the song came off the card.
#   CL-SONGSWAP     the same load with the clock running and a synth sounding: the load arms a swap
#                   (Session::armForSongSwap()), the render launches it (doSongSwap()), and
#                   LoadSongUI::runFinishSegment() destroys the old song.
#   CL-WINDOW       SESSIONDELETE and ARRANGERNOUNDO, just after the deletion and before any block: whether the song's
#                   currentClip or the view's active timeline counter still names the retired clip. Required 0 and 0.
#   CL-FREED        per clip leg, before its leg line: the scenario renders a few blocks and reclaims, so the retired
#                   clip is destroyed (and ~Clip()'s check runs) inside its own leg; pending= is what is still waiting.
#   CL-RECLAIM      printed twice: once after the other legs and before WONTSOUND, and once more after WONTSOUND has
#                   run, each time AudioEngine::retiredPending() once the render has run and reclaimRetired() has been
#                   called. The first is report-only; the final one (the last CL-RECLAIM line) is REQUIRED pending=0.
#   CL-WONTSOUND    last: a playing kit clip whose drum has an empty note row and is put back in the kit's
#                   drumsWithRenderingActive by hand -- skipping rendering, yet still in that set, a state no natural
#                   note-on/off path reaches (SoundDrum::setSkippingRendering() is the only writer of the flag, and it
#                   erases from the set in the same call that sets it) -- then AudioEngine::songSwapAboutToHappen() --
#                   what a load or clear started during playback runs first -> Song::deleteSoundsWhichWontSound() ->
#                   InstrumentClip::deleteSoundsWhichWontSound(), which hands the drum to the render
#                   (AudioEngine::retire(SoundDrum*)) instead of destroying it on the spot. This leg does not
#                   reproduce a real precondition; it proves only that the retire hand-off is wired correctly. Its
#                   real safety value is on release builds: the hasActiveVoices() check that would otherwise catch a
#                   drum with genuinely active voices (FREEZE_WITH_ERROR("E176")) is ALPHA_OR_BETA_VERSION-only, and
#                   retire()'s let-go now kills a drum's voices unconditionally before it is freed, whichever build
#                   this runs on.
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
# ## Required legs
#
# SESSIONDELETE, ARRANGERDELETE, SWAPTYPE, UNDODELETE, ARRANGERNOUNDO, RECORDSOURCE and SYNCSCALE must each read clipsReferenced=0,
# must not abort, and must show CL-FREED pending=0 (the clip really was freed inside the leg); UNDODELETE must also
# read restored=1. SAVEKIT and WONTSOUND must each read soundsLive=0 and must not abort. The final CL-RECLAIM line
# (after WONTSOUND) must read pending=0. Any of them failing that fails the run.
#
# The run also FAILS when a leg line or a CL-RECLAIM line is missing, a fixture could not be built (CL-RESULT
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

LEGS=(SESSIONDELETE ARRANGERDELETE SWAPTYPE UNDODELETE ARRANGERNOUNDO RECORDSOURCE SYNCSCALE SAVEKIT SONGCLEAR SONGLOAD SONGSWAP
	WONTSOUND)
# Required: the clip legs. The rest stay report-only.
REQUIRED_LEGS=(SESSIONDELETE ARRANGERDELETE SWAPTYPE UNDODELETE ARRANGERNOUNDO RECORDSOURCE SYNCSCALE)
# Required: the two direct-drum-destroy legs. soundsLive=0 and no abort -- not clipsReferenced (they destroy a drum,
# not a clip) and not CL-FREED (that stage only runs for the clip legs above).
REQUIRED_SOUND_LEGS=(SAVEKIT WONTSOUND)

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

echo "clip lifetime: running"
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

# The clip legs whose deletion hands the clip to the render: each must free its clip with nothing the render reads
# still pointing at it, and without ending the process. UNDODELETE's undo must also bring the clip back.
required_fail=""
for leg in "${REQUIRED_LEGS[@]}"; do
	line="$(grep -E "^CL-$leg (restored=[01] )?clipsReferenced=" "$OUT/all.txt" | tail -1)"
	if ! grep -qE '(^| )clipsReferenced=0( |$)' <<<"$line" || grep -q 'aborted=1' <<<"$line"; then
		required_fail="$required_fail$leg "
	elif [ "$leg" = UNDODELETE ] && ! grep -q ' restored=1 ' <<<"$line"; then
		required_fail="$required_fail$leg "
	elif ! grep -qE "^CL-FREED leg=$leg pending=0$" "$OUT/all.txt"; then
		# The clip was never freed inside the leg, so ~Clip()'s check never ran on it: a 0 would prove nothing.
		required_fail="$required_fail$leg(not-freed) "
	fi
done
# Just after the deletion, before any block: the interaction tier must no longer see the retired clip as the current
# clip or the view's active one (AudioEngine::retire(Clip*) clears them itself).
for leg in SESSIONDELETE ARRANGERNOUNDO; do
	if ! grep -qE "^CL-WINDOW leg=$leg currentClip=0 view=0$" "$OUT/all.txt"; then
		required_fail="$required_fail$leg(window) "
	fi
done
# The two direct-drum-destroy sites: the drum must be handed to the render (soundsLive=0) rather than destroyed on the
# spot, and the leg must not end the process.
for leg in "${REQUIRED_SOUND_LEGS[@]}"; do
	line="$(grep -E "^CL-$leg clipsReferenced=" "$OUT/all.txt" | tail -1)"
	if [ -z "$line" ] || grep -q 'aborted=1' <<<"$line" || ! grep -qE '(^| )soundsLive=0( |$)' <<<"$line"; then
		required_fail="$required_fail$leg "
	fi
done
if [ -n "$required_fail" ]; then
	echo "FAIL — a required leg left a reference to a freed clip, a live-voiced Sound destroyed on the spot, aborted, or did not restore: $required_fail"
	exit 1
fi

# The final CL-RECLAIM (after CL-WONTSOUND) must read pending=0: everything the render let go of, including the two
# direct-drum-destroy legs' retired drums, must actually have been reclaimed by the end of the run.
final_reclaim="$(grep -E '^CL-RECLAIM pending=[0-9]+$' "$OUT/all.txt" | tail -1)"
if [ -z "$final_reclaim" ] || ! grep -qE ' pending=0$' <<<"$final_reclaim"; then
	echo "FAIL — the final CL-RECLAIM did not read pending=0: ${final_reclaim:-missing}"
	exit 1
fi

red="$(grep -E '^CL-[A-Z]+ (restored=[01] )?clipsReferenced=' "$OUT/all.txt" |
	grep -E 'aborted=1|(clipsReferenced|songsReachable|soundsLive)=[1-9]' | sed -E 's/^CL-([A-Z]+) .*/\1/' | sort -u |
	tr '\n' ' ' || true)"
if grep -qE '^CL-RECLAIM pending=[1-9]' "$OUT/all.txt"; then
	red="${red}RECLAIM "
fi
echo "PASS — required clip legs clean; report-only legs ran; red legs: ${red:-none}"
