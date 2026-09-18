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
#   SL-NOSTREAM   that no streaming document reader has come back. A SOURCE check, and unusually so:
#                 a gate cannot observe a path that is not compiled, so noticing the deletion being
#                 undone is the only way to keep it deleted. A runtime path that can block the
#                 storage owner is a reason the owner can never be removed, however rarely it runs.
#   SL-EVICT      that the round trip still produces the SAME bytes when the song has to be staged
#                 into an SDRAM heap too small to hold it without evicting. Loading a song now gives
#                 up cached sample chunks it never used to; that was a decision, so it gets measured.
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
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
	# Cargo does not track the C++ sources the harness links against (this scenario is one of them),
	# so force the object closure first — same reason song_preview_ab.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET_DIR")
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

# An abort (an assertion firing) exits before this line is ever printed, which is what turns a
# freeze into a gate failure rather than a silently truncated log.
if ! grep -q '^SR-RESULT ' "$OUT/run.txt"; then
	echo "FAIL — no SR-RESULT line; the scenario did not run to completion"
	sed -n '1,60p' "$OUT/run.txt"
	exit 1
fi

for marker in SR-SEAM SR-ROUNDTRIP; do
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

# SL-NOSTREAM. The three StorageManager functions that opened a document by streaming it off the card
# while parsing it. Their deletion is what makes "nothing parses from the card" true, and nothing at
# runtime can notice them coming back, so it is checked here.
if grep -rnE '\bStorageManager::open(XML|Json)File\(|\bopenDelugeFile\(' \
	"$REPO_ROOT/src" --include='*.cpp' --include='*.h' --include='*.hpp' >"$OUT/nostream.txt"; then
	echo "FAIL SL-NOSTREAM — a streaming document reader is back:"
	cat "$OUT/nostream.txt"
	echo "  A path that can block the storage owner at runtime is a reason the owner can never be"
	echo "  removed. Parse from a staged buffer (StorageManager::openDocumentSynchronously) instead."
	exit 1
fi
echo "SL-NOSTREAM ok — no streaming document reader in src/"

# SL-EVICT. Re-run the whole round trip with the usable SDRAM clamped low enough that staging a song
# cannot happen without the resource manager evicting cached chunks first, and require the SAME
# result. alloc_sdram evicts rather than failing, so this exercises that path rather than an error
# path -- and if the eviction ever corrupted or truncated what was staged, the CRC comparison inside
# the run is what would notice.
echo "save round-trip: running under a clamped SDRAM heap (SL-EVICT)"
set +e
DELUGE_SIM_SDRAM_MB="${SL_EVICT_SDRAM_MB:-2}" GOLDEN_SCENARIO=save_roundtrip \
	"$BIN" >"$OUT/evict.txt" 2>"$OUT/evict.err"
evict_status=$?
set -e
if [ "$evict_status" -ne 0 ]; then
	echo "FAIL SL-EVICT — the clamped-heap run exited $evict_status"
	grep -E '^SR-' "$OUT/evict.txt" || sed -n '1,60p' "$OUT/evict.txt"
	sed -n '1,25p' "$OUT/evict.err" >&2
	exit 1
fi
if ! grep -q '^SR-RESULT failures=0$' "$OUT/evict.txt"; then
	echo "FAIL SL-EVICT — in-process failures under a clamped heap:"
	grep -E '^SR-' "$OUT/evict.txt"
	exit 1
fi
# The round-trip CRC itself must be identical to the unclamped run: eviction must change WHAT IS
# CACHED, never what is written.
if ! diff <(grep '^SR-ROUNDTRIP ' "$OUT/run.txt") <(grep '^SR-ROUNDTRIP ' "$OUT/evict.txt"); then
	echo "FAIL SL-EVICT — the clamped-heap run wrote different bytes than the unclamped one"
	exit 1
fi
grep -E '^SR-' "$OUT/evict.txt"
echo "SL-EVICT ok — same bytes with the song staged into a heap that had to evict first"

echo "PASS — the song round-tripped byte-identically, the overwrite seam behaved, no streaming"
echo "       document reader survives, and staging under eviction changed nothing"
