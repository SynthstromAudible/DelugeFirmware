#!/usr/bin/env bash
# save_roundtrip.sh — the save round-trip gate.
#
# Saves a song through the REAL SaveSongUI ENTER gesture, loads it back, overwrites it, and requires
# the two written files to be byte-identical. Also asserts the overwrite seam: a free name must not
# prompt, an occupied one must, and confirming must close the prompt.
#
# Single-leg on purpose. The A/B scripts beside this one compare two BUILDS because their subject is
# a Rust backing selected by a compile-time feature; this gate's subject is a C++ structural change,
# so there is no second leg to compare against. The round trip is self-checking instead: nothing is
# baselined, so there is no golden anyone can quietly re-baseline into passing.
#
# What the checks below cover, and why each is separate:
#   SR-SEAM       the prompt decisions. A save that silently overwrote would still round-trip fine.
#   SR-ROUNDTRIP  the written bytes. A CRC, not a length -- a save that truncated or reordered
#                 would pass a length check.
#   SR-TIER       that performSave never ran on the interaction tier.
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/save-roundtrip}"

if [ "${NO_BUILD:-0}" != "1" ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against (this scenario is one of them),
	# so force the object closure first — same reason song_preview_ab.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$(cd "$REPO_ROOT" && pwd)/$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT="$(mktemp -d "${TMPDIR:-/tmp}/save_roundtrip.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

echo "save round-trip: running"
set +e
GOLDEN_SCENARIO=save_roundtrip "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^SR-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	sed -n '1,25p' "$OUT/run.err" >&2
	exit 1
fi

# An abort (a tier assertion firing) exits before this line is ever printed, which is what turns a
# freeze into a gate failure rather than a silently truncated log.
if ! grep -q '^SR-RESULT ' "$OUT/run.txt"; then
	echo "FAIL — no SR-RESULT line; the scenario did not run to completion"
	sed -n '1,60p' "$OUT/run.txt"
	exit 1
fi

for marker in SR-SEAM SR-ROUNDTRIP SR-TIER; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^SR-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^SR-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^SR-' "$OUT/run.txt"
	exit 1
fi

grep -E '^SR-' "$OUT/run.txt"
echo "PASS — the song round-tripped byte-identically and the overwrite seam behaved"
