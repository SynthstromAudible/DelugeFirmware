#!/usr/bin/env bash
# container_guard.sh — the container-guard probe.
#
# With playback running, makes one structural edit to each kind of container the audio render walks, through the
# model's own entry points, and reports how many structural edits were made while the render could have preempted
# them (deluge::unguardedStructuralEditsTotal(), src/deluge/util/structural_edit_probe.h).
#
# ## Why this gate exists
#
# On the device the audio render preempts thread mode at any instruction. An insert, erase, reorder, relink or
# reallocation of a container the render walks -- a clip's note rows, a row's notes, an automation node list, the
# song's clip arrays, an output's clip instances, the song's output list, a kit's drum list -- can be interrupted
# halfway by a render that walks it. Every such edit must run inside an AudioExcludedSection (libdeluge/system.h). The
# containers report each structural edit to the probe, which counts those made outside a section.
#
# What the lines cover (src/deluge/harness/container_guard_scenario.cpp writes them), each `CG-<LEG> unguarded=<n>
# kinds=<kind=n,...>`:
#   CG-NOTEADD / CG-NOTEDEL  a note added / deleted through NoteRow::attemptNoteAdd() / deleteNoteByPos()
#   CG-ROWADD / CG-ROWDEL    a note row made (getOrCreateNoteRowForYNote()) / deleted (InstrumentClip::deleteNoteRow())
#   CG-CLIPNEW / CG-CLIPDEL  a session clip made through the new-clip gesture / deleted (Song::removeSessionClip())
#   CG-CLIPDUP               a session clip cloned (SessionView::cloneClip())
#   CG-ARRANGE               a clip instance inserted into and erased from an output's clipInstances
#   CG-AUTOMATE              an automation node inserted into and erased from a playing clip's volume AutoParam
#   CG-DRUM                  a drum added to a playing kit (Kit::addDrum()) and removed (Kit::removeDrum())
#   CG-REPLACE               a playing clip's synth replaced (Song::replaceInstrument())
#   CG-UNDO                  a logged note delete, undone
#   CG-ROWDRAG               the clip view's note-row drag (scrollVertical_dragSelectedNoteRow(): two rows swapped)
#   CG-CLIPMOVE              the session view's clip drag (ClipArray::swapClips())
#   CG-OUTPUTMOVE            the arranger's output drag (Song::moveOutput(): a three-pointer splice)
#   CG-QUANTIZE              a row quantized (NoteRow::quantize(), which rotates its notes in place)
#   CG-RENDERSET             a kit drum sounded from the interaction tier (an audition): its kit's rendering set gains it
#   CG-RENDERALLOC           a kit clip playing for 200 blocks; CG-RENDERALLOC renderAllocs= counts heap allocations the
#                            render made (host only) -- required 0
#   CG-VOICEPOLY             more audition note-ons than a synth's voice limit, from the interaction tier
#   CG-VOICEMONO             a MONO synth re-triggered while its voice plays
#   CG-VOICEKILL             the oscillator-type menu's killAllVoices on a sounding synth
#   CG-VOICEMULTIRANGE       a multi-range deleted from a sounding synth (Sound::deleteMultiRange)
#   CG-VOICEFAIL             a ring-mod note-on whose voice fails to start (a sample source with no sample): the
#                            voice goes back to the pool unpublished and the note-on still runs to its end
#                            (CG-VOICEFAIL poolFreeDelta=0 voicesDelta=0 lastNoteSet=1 required)
#   CG-CHOKE                 two choke-mode drums sounded in turn, each choking the other, over 400 blocks
#   CG-BIGROW                a note row inserted below 128 others, moving them all; CG-MASK reports the edit's host
#                            wall time (maskMicros=), an upper bound on the section it holds (a block is ~2900 us)
#
# REQUIRED_LEGS below lists the legs whose count must be 0; every other leg is reported only. A leg whose edit ends the
# process is reported with aborted=1 and fails the run. A missing leg line fails the run.
#
# Usage: scripts/container_guard.sh
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/container_guard.XXXXXX")"
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

echo "container guard: running"
set +e
GOLDEN_SCENARIO=container_guard "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

grep -E '^CG-' "$OUT/run.txt" || true
grep -E '^GUARD-UNGUARDED ' "$OUT/run.txt" || true

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	tail -20 "$OUT/run.err"
	exit 1
fi

LEGS="NOTEADD NOTEDEL ROWADD ROWDEL CLIPNEW CLIPDEL CLIPDUP ARRANGE AUTOMATE DRUM REPLACE UNDO BIGROW ROWDRAG CLIPMOVE OUTPUTMOVE QUANTIZE RENDERSET RENDERALLOC CHOKE VOICEPOLY VOICEMONO VOICEKILL VOICEMULTIRANGE VOICEFAIL"
# The legs whose edits must all be made while the render is excluded.
REQUIRED_LEGS="NOTEADD NOTEDEL ROWADD ROWDEL CLIPNEW CLIPDEL CLIPDUP ARRANGE AUTOMATE DRUM REPLACE UNDO BIGROW ROWDRAG CLIPMOVE OUTPUTMOVE QUANTIZE RENDERSET RENDERALLOC CHOKE VOICEPOLY VOICEMONO VOICEKILL VOICEMULTIRANGE VOICEFAIL"

for leg in $LEGS; do
	if ! grep -qE "^CG-$leg unguarded=[0-9]+ " "$OUT/run.txt"; then
		echo "FAIL — no CG-$leg line: a stage never ran"
		exit 1
	fi
done

if ! grep -q '^CG-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^CG-RESULT' "$OUT/run.txt" || echo 'no CG-RESULT line')"
	exit 1
fi

# The render must not allocate: a kit's drums start and stop rendering without touching the heap.
if ! grep -qE '^CG-RENDERALLOC renderAllocs=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep -E '^CG-RENDERALLOC renderAllocs=' "$OUT/run.txt" || echo 'no CG-RENDERALLOC renderAllocs= line'): the render allocated"
	exit 1
fi

# A note-on whose voice cannot start returns it to the pool; it never reaches the Sound's voices.
if ! grep -qE '^CG-VOICEFAIL poolFreeDelta=0 voicesDelta=0 lastNoteSet=1$' "$OUT/run.txt"; then
	echo "FAIL — $(grep -E '^CG-VOICEFAIL poolFreeDelta' "$OUT/run.txt" || echo 'no CG-VOICEFAIL line'): a failed note-on leaked or published its voice, or did not run to its end"
	exit 1
fi

failed=""
for leg in $REQUIRED_LEGS; do
	if ! grep -qE "^CG-$leg unguarded=0 " "$OUT/run.txt"; then
		failed="$failed $leg"
	fi
done
if [ -n "$failed" ]; then
	echo "FAIL — required legs made structural edits while the render could preempt them:$failed"
	exit 1
fi

red=""
for leg in $LEGS; do
	if ! grep -qE "^CG-$leg unguarded=0 " "$OUT/run.txt"; then
		red="$red $leg"
	fi
done
grep -hE '^TIER-SYNC(-AT)? ' "$OUT/run.txt" || true
echo "PASS — required legs clean; unguarded legs:${red:- none}"
