#!/usr/bin/env bash
# audio_load_ab.sh — the sync-vs-async load A/B gate.
#
# Builds the Embassy host harness TWICE from one source — once with the Rust audio loader compiled
# but its switch off (`async_audio_load`), once with the switch on and the loader task spawned
# (`async_audio_load_active`) — runs the same fixture card through both, and requires their
# per-file output to be byte-identical.
#
# Why two builds rather than two passes in one process: `deluge_audio_load_async_active()` is
# `cfg!(feature = "async_audio_load_active")` on the Rust side, a COMPILE-TIME constant. There is no
# runtime switch to flip mid-run, so an in-process A/B is not merely inconvenient, it does not
# exist.
#
# What the diff covers (src/deluge/harness/audio_load_ab_scenario.cpp writes the lines):
#   AB-CASE   one per sample: the Error, the file actually chosen (`file`/`alt`/`asked`), the whole
#             parsed geometry, and a CRC of the bytes read back through the Sample's own stream.
#             Diffed. Any difference is the gate failing.
#   AB-PREVIEW
#             the converted CALLER, driven end to end: SampleBrowser is asked to preview one file
#             ONCE and then driven only by its per-tick surface (graphicsRoutine), with no further
#             input. It must eventually draw. EXPECTED to differ (the synchronous leg never defers,
#             so it re-drives nothing), so not diffed — but each leg asserts its own consistency,
#             and this is the only check on either side that fails if the caller stops re-asking
#             for a deferred load. Everything else here drives the loader through the HARNESS's own
#             retry loop, which is precisely the handler the shipped caller was missing.
#   AB-CANCEL the converse property, on the same caller: a preview is asked for and then ABANDONED
#             before the storage owner runs its load (what a cursor move onto a folder does — the
#             one move that dispatches no replacement, so nothing supersedes the load in flight).
#             Nothing may load, draw or stay pending. EXPECTED to agree between the legs but not
#             diffed; each leg asserts it in-process. Without it, a cancelled preview is re-driven
#             every frame and then rendered over whatever the user moved onto.
#   AB-SUMMARY / AB-BEGIN
#             which backing ran and how many loads were deferred. EXPECTED to differ, so not
#             diffed — but each leg asserts its own consistency in-process (an "async" run that
#             never deferred is a run that never reached the collector, and fails there).
#
# Usage: scripts/audio_load_ab.sh
#
# Environment:
#   BUILD_DIR   the build-embassy-hostapp C++ object-closure build dir
#               (default: <repo>/build-embassy-hostapp)
#   NO_BUILD=1  skip the ninja/cargo rebuild steps
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO/build-embassy-hostapp}"
CRATE_DIR="$REPO/src/bsp/rust/harness/golden_vt_render"

# Two target dirs, never one. The two legs differ only by a cargo feature, so sharing a target dir
# would make every run rebuild the whole crate to swap features back and forth — and would clobber
# whichever leg was built last, so a stale binary could silently be diffed against itself.
# Honours CARGO_TARGET_DIR the way golden_embassy_diff.sh does: `dbt harness run` points every rig
# at a shared target tree, and these two live under it rather than beside it.
TARGET_BASE="${CARGO_TARGET_DIR:-$CRATE_DIR/target}"
SYNC_TARGET="$TARGET_BASE/audio-load-ab-sync"
ASYNC_TARGET="$TARGET_BASE/audio-load-ab-async"

if [ "${NO_BUILD:-0}" != 1 ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against (the A/B scenario itself is one
	# of them), so force the object closure first — same reason golden_embassy_diff.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --features async_audio_load --target-dir "$SYNC_TARGET")
	(cd "$CRATE_DIR" && cargo build --release --features async_audio_load_active --target-dir "$ASYNC_TARGET")
fi

SYNC_BIN="$SYNC_TARGET/release/golden_vt_render"
ASYNC_BIN="$ASYNC_TARGET/release/golden_vt_render"
for b in "$SYNC_BIN" "$ASYNC_BIN"; do
	[ -x "$b" ] || {
		echo "ERROR: '$b' not built (see NO_BUILD)"
		exit 2
	}
done

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/audio_load_ab.XXXXXX")"
IMG_GLOB="${TMPDIR:-/tmp}/deluge-streaming-scenario-*.img"

# Every run of this gate packs TWO 2.5 GB sparse FAT images, one per leg, and nothing else removes
# them: `sd_image_path::fingerprint_dir` hashes path/length/mtime rather than content, and the
# fixture is rewritten on each process start, so the legs never share a cached image and never reuse
# a previous run's. Left alone they accumulate under exactly the prefix this project's golden
# invocations all begin by clearing — and a full /tmp is a known way to make an unrelated stem
# export hang forever, presenting as a firmware wedge rather than a disk problem.
#
# Only images that appeared DURING this run are removed. A blanket `rm` of the glob would delete
# other rigs' cached corpus images (packing one of those is minutes of work), so the set is
# snapshotted first and differenced at exit.
IMGS_BEFORE="$OUT_ROOT/.imgs_before"
ls -1 $IMG_GLOB 2>/dev/null | sort >"$IMGS_BEFORE" || true

cleanup() {
	if [ -f "$IMGS_BEFORE" ]; then
		ls -1 $IMG_GLOB 2>/dev/null | sort | comm -13 "$IMGS_BEFORE" - | while read -r img; do
			rm -f "$img"
		done
	fi
	rm -rf "$OUT_ROOT" "${TMPDIR:-/tmp}/deluge-audio-load-ab-card"
}
trap cleanup EXIT

# Run one leg. $1 = binary, $2 = tag. Echoes nothing; writes $OUT_ROOT/$2.txt.
run_leg() {
	local bin="$1" tag="$2"
	set +e
	GOLDEN_SCENARIO=audio_load_ab "$bin" >"$OUT_ROOT/$tag.txt" 2>"$OUT_ROOT/$tag.err"
	local status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		echo "FAIL — the $tag leg exited $status"
		sed -n '1,60p' "$OUT_ROOT/$tag.txt"
		sed -n '1,20p' "$OUT_ROOT/$tag.err" >&2
		exit 1
	fi
	# An empty body means the scenario never ran (a wedge that still exited 0, a scenario name that
	# did not match). Diffing two empty files would "pass" and prove nothing — the exact failure
	# mode this whole gate exists to avoid.
	if ! grep -q '^AB-RESULT ' "$OUT_ROOT/$tag.txt"; then
		echo "FAIL — the $tag leg produced no AB-RESULT line; it did not run the scenario to completion"
		sed -n '1,60p' "$OUT_ROOT/$tag.txt"
		exit 1
	fi
	if ! grep -q '^AB-RESULT failures=0$' "$OUT_ROOT/$tag.txt"; then
		echo "FAIL — the $tag leg reported in-process failures:"
		grep -E '^(FAIL|AB-PREVIEW|AB-CANCEL|AB-SUMMARY|AB-RESULT) ' "$OUT_ROOT/$tag.txt"
		exit 1
	fi
	# The preview stage prints exactly one of these, and prints it before the summary. Its absence
	# means the run reached AB-RESULT without ever driving the caller — which the failure count
	# above cannot show, because a stage that never ran asserts nothing.
	if ! grep -q '^AB-PREVIEW ' "$OUT_ROOT/$tag.txt"; then
		echo "FAIL — the $tag leg produced no AB-PREVIEW line; the preview re-drive stage did not run"
		exit 1
	fi
	# Same reasoning for the cancellation stage: it asserts that NOTHING happened, so a stage that
	# never ran satisfies every one of its checks and contributes nothing to the failure count.
	if ! grep -q '^AB-CANCEL ' "$OUT_ROOT/$tag.txt"; then
		echo "FAIL — the $tag leg produced no AB-CANCEL line; the preview cancellation stage did not run"
		exit 1
	fi
}

echo "audio-load A/B: running the synchronous leg (async_audio_load, switch off)"
run_leg "$SYNC_BIN" sync
echo "audio-load A/B: running the async leg (async_audio_load_active, Rust loader task spawned)"
run_leg "$ASYNC_BIN" async

# Each leg must be the leg it claims to be. The sync leg with asyncActive=1 (or the reverse) means
# the two binaries were built the same way and the comparison is a build against itself.
grep -q '^AB-SUMMARY asyncActive=0 ' "$OUT_ROOT/sync.txt" || {
	echo "FAIL — the 'synchronous' leg reports the async backing ACTIVE; both legs were built the same way"
	grep '^AB-SUMMARY ' "$OUT_ROOT/sync.txt"
	exit 1
}
grep -q '^AB-SUMMARY asyncActive=1 ' "$OUT_ROOT/async.txt" || {
	echo "FAIL — the 'async' leg reports the async backing INACTIVE; both legs were built the same way"
	grep '^AB-SUMMARY ' "$OUT_ROOT/async.txt"
	exit 1
}

grep '^AB-CASE ' "$OUT_ROOT/sync.txt" >"$OUT_ROOT/sync.cases"
grep '^AB-CASE ' "$OUT_ROOT/async.txt" >"$OUT_ROOT/async.cases"
if ! diff -u "$OUT_ROOT/sync.cases" "$OUT_ROOT/async.cases"; then
	echo "FAIL — the two backings did not load the same files the same way (diff above: -sync +async)"
	exit 1
fi

# A floor on the case count, so a future edit that shrinks `kCases` cannot keep printing PASS over a
# comparison that no longer covers the properties this gate was built for. Bump it deliberately when
# cases are added; never lower it to make a run go green.
MIN_CASES=10
CASES="$(wc -l <"$OUT_ROOT/sync.cases")"
if [ "$CASES" -lt "$MIN_CASES" ]; then
	echo "FAIL — only $CASES case(s) ran, fewer than the $MIN_CASES this gate requires; kCases was shrunk"
	exit 1
fi

echo "PASS — both backings loaded all $CASES sample(s) identically"
grep -h '^AB-SUMMARY \|^AB-DEFER \|^AB-PREVIEW \|^AB-CANCEL ' "$OUT_ROOT/sync.txt" "$OUT_ROOT/async.txt"
