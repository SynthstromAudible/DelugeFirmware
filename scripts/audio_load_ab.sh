#!/usr/bin/env bash
# audio_load_ab.sh — the audio-load A/B gate.
#
# Builds the Embassy host harness once and runs the fixture card through it. The Rust audio loader is
# always on, and the scenario holds it to an in-process reference: every sample is loaded first
# through the synchronous C++ card path (`mayLoadAsync=false`) and then through the loader, and the
# two must agree on every field.
#
# What the checks cover (src/deluge/harness/audio_load_ab_scenario.cpp writes the lines):
#   AB-CASE   one per sample: the Error, the file actually chosen (`file`/`alt`/`asked`), the whole
#             parsed geometry, and a CRC of the bytes read back through the Sample's own stream. The
#             scenario compares it with the synchronous reference in process, and prints AB-REF with a
#             FAIL on any difference.
#   AB-EJECT  the card-swap property: a header is parsed from one card and left uncollected, the card
#             is ejected, a DIFFERENT file is put at the same path, and the load is asked for again. It
#             must match a synchronous load of the file now at that path. A stale collect shows up here
#             as the wrong geometry against the same file.
#   AB-EJECT-SUMMARY
#             that stage's deferral counts. A post-swap collect that deferred ZERO times is itself the
#             bug (a flushed path reports UNKNOWN and is asked for again; a stale one is READY on the
#             first ask).
#   AB-PREVIEW
#             the converted CALLER, driven end to end: SampleBrowser previews one file ONCE and is then
#             driven only by its per-tick surface. It must defer at least once and eventually draw.
#   AB-CANCEL a preview asked for and then abandoned before its load runs: nothing may load, draw or
#             stay pending.
#   AB-PREVIEW-STALL
#             a preview whose load is held far past the browser's 600-attempt bound: it must give up
#             once, with nothing pending and nothing drawn, and stay given up once the hold is lifted.
#   AB-SUMMARY / AB-DEFER
#             that the loader was active and that every case deferred at least once, so no case was
#             quietly answered on the synchronous path.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count cannot
# show that, because it counts only assertions that DID run.
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

# Honours CARGO_TARGET_DIR the way golden_embassy_diff.sh does: `dbt harness run` points every rig at a
# shared target tree, and this one lives under it rather than beside it.
TARGET_BASE="${CARGO_TARGET_DIR:-$CRATE_DIR/target}"
TARGET="$TARGET_BASE/audio-load-ab"

if [ "${NO_BUILD:-0}" != 1 ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against (the A/B scenario itself is one
	# of them), so force the object closure first — same reason golden_embassy_diff.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET")
fi

BIN="$TARGET/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/audio_load_ab.XXXXXX")"
IMG_GLOB="${TMPDIR:-/tmp}/deluge-streaming-scenario-*.img"

# A run of this gate packs a 2.5 GB sparse FAT image, and nothing else removes it:
# `sd_image_path::fingerprint_dir` hashes path/length/mtime rather than content, and the fixture is
# rewritten on each process start, so it never reuses a previous run's image. Left alone they
# accumulate under exactly the prefix this project's golden invocations all begin by clearing -- and
# a full /tmp is a known way to make an unrelated stem export hang forever.
#
# Only images that appeared DURING this run are removed. A blanket `rm` of the glob would delete other
# rigs' cached corpus images, so the set is snapshotted first and differenced at exit.
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

echo "audio-load A/B: running (every case against its in-process synchronous reference)"
set +e
GOLDEN_SCENARIO=audio_load_ab "$BIN" >"$OUT_ROOT/run.txt" 2>"$OUT_ROOT/run.err"
status=$?
set -e
if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^(FAIL|AB-)' "$OUT_ROOT/run.txt" || sed -n '1,60p' "$OUT_ROOT/run.txt"
	sed -n '1,20p' "$OUT_ROOT/run.err" >&2
	exit 1
fi

# An empty body means the scenario never ran (a wedge that still exited 0, a scenario name that did not
# match), and every check below would pass over nothing.
if ! grep -q '^AB-RESULT ' "$OUT_ROOT/run.txt"; then
	echo "FAIL — no AB-RESULT line; the scenario did not run to completion"
	sed -n '1,60p' "$OUT_ROOT/run.txt"
	exit 1
fi
if ! grep -q '^AB-RESULT failures=0$' "$OUT_ROOT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^(FAIL|AB-REF|AB-PREVIEW|AB-CANCEL|AB-PREVIEW-STALL|AB-EJECT-SUMMARY|AB-SUMMARY|AB-RESULT) ' "$OUT_ROOT/run.txt"
	exit 1
fi

for marker in AB-PREVIEW AB-CANCEL AB-PREVIEW-STALL AB-EJECT AB-EJECT-SUMMARY AB-DEFER AB-SUMMARY; do
	if ! grep -q "^$marker " "$OUT_ROOT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^AB-' "$OUT_ROOT/run.txt"
		exit 1
	fi
done

# The loader must be the one that ran. `asyncActive=0` means this binary resolved the weak C++ fallback,
# and every load was answered by the very path it is compared against.
grep -q '^AB-SUMMARY asyncActive=1 ' "$OUT_ROOT/run.txt" || {
	echo "FAIL — the async loader was not active; every load took the synchronous path it is compared with"
	grep '^AB-SUMMARY ' "$OUT_ROOT/run.txt"
	exit 1
}

# A floor on the case count, so a future edit that shrinks `kCases` cannot keep printing PASS over a
# comparison that no longer covers the properties this gate was built for. Bump it deliberately when
# cases are added; never lower it to make a run go green.
MIN_CASES=10
CASES="$(grep -c '^AB-CASE ' "$OUT_ROOT/run.txt")"
if [ "$CASES" -lt "$MIN_CASES" ]; then
	echo "FAIL — only $CASES case(s) ran, fewer than the $MIN_CASES this gate requires; kCases was shrunk"
	exit 1
fi

echo "PASS — the async loader loaded all $CASES sample(s) as the synchronous path does, and agreed with it across a"
echo "       card swap, and a preview held past its bound gave up"
grep -h '^AB-SUMMARY \|^AB-DEFER \|^AB-PREVIEW \|^AB-CANCEL \|^AB-PREVIEW-STALL \|^AB-EJECT-SUMMARY ' "$OUT_ROOT/run.txt"
