#!/usr/bin/env bash
# song_preview_ab.sh — the song-preview gate: the prefix channel against the file read.
#
# Builds the Embassy host harness once, draws the fixture card's song artwork through the REAL
# `LoadSongUI` over the prefix channel, and holds every song to the same file decoded the synchronous
# way on the storage owner, in the same process. The channel has no switch, so there is no second
# build to compare against: the reference decode is what that build used to provide.
#
# What the run checks (src/deluge/harness/song_preview_ab_scenario.cpp writes the lines):
#   SP-CASE   one per song: the decode's `SongPreviewOutcome`, the `Error` it put on screen, and a
#             CRC of the rendered pad image. Held to SP-REFERENCE and to its absolute expectation.
#
#             The OUTCOME is compared, not just the image, and that is the point rather than a detail:
#             an unreadable song, a song with no artwork and a preview that falls past the read
#             window all draw an IDENTICAL blank screen. A gate comparing only pixels passed while
#             one backing showed the user an error popup and the other silently showed nothing —
#             which is the state the two backings were in when this gate was written.
#   SP-EJECT  the card-swap property: a prefix is read and left uncollected, the card is ejected, a
#             DIFFERENT song is put at the same path, and the preview is asked for again. Held to a
#             reference decode of the file now at that path, which holds no card-derived state to go
#             stale. The scenario ALSO asserts the post-swap artwork absolutely, against the known
#             second image: an inequality check ("not the first image") passes when both reads fail.
#   SP-WINDOW the ONE song the browser must NOT draw as the file read does: its `preview` sits past
#             `kSongPreviewWindow`. The reference walks the whole document and renders it; the prefix
#             channel runs out of bytes and reports Truncated, drawing blank WITHOUT claiming the song has
#             no artwork. Both halves are asserted absolutely.
#   SP-REFERENCE
#             one per song, beside its SP-CASE: the same file decoded the synchronous way on the storage owner.
#             The run requires the browser's outcome, error and image CRC to equal it; for the read-window
#             song the reference must render the late artwork instead. Not diffed.
#   SP-DEFER / SP-SUMMARY / SP-SWAP-SUMMARY
#             how many times each song's preview deferred, and the channel's state. Read, not compared,
#             except that SP-SUMMARY must report asyncActive=1.
#   SP-LISTING / SP-PREDICT / SP-NEWNAME / SP-SLOT / SP-BYPATH / SP-TIER
#             the browser's listings, typing prediction, brand-new slot name, one-sided slot read and by-path
#             opens, each driven through the directory channel as steps of the scenario's tick machine and
#             checked in-process against what its fixture implies, with exactly one channel request per open (a
#             listing that restarted would ask again). SP-TIER
#             requires no synchronous browser directory operation off the storage owner.
#   SP-LISTING-STALL
#             a listing whose directory open is held far past the browser's 600-tick bound gives up with the
#             card error: begun inside the open browser, it leaves the browser open on what it had; opening
#             the browser, it closes it.
#   SP-PREVIEW-STALL
#             a song preview whose prefix read is held far past the preview's 600-tick bound gives up with
#             the card error, draws blank, and releases the scroll-in it owed.
#   SP-DX     one DX7 bank picked in the DX browser, read through the prefix channel and parsed. The run
#             requires the fixture's file, 32 patches and first patch name. SP-DX-WAIT prints the ticks it took.
#   SP-DX-STALL
#             the same bank with its read held far past the load's 600-tick bound: Enter gives up with the
#             read-error popup, clears the load's flag and leaves the browser open.
#
# Usage: scripts/song_preview_ab.sh
#
# Environment:
#   BUILD_DIR           the build-embassy-hostapp C++ object-closure build dir
#                       (default: <repo>/build-embassy-hostapp)
#   DELUGE_SONG_CORPUS  a directory of real .XML songs. Optional: with it set, the fixture also
#                       stages up to 16 of them as extra cases, which is the only part of this gate
#                       that sees documents nobody here wrote. Without it the synthetic cases still
#                       run and the gate still gates.
#   NO_BUILD=1          skip the ninja/cargo rebuild steps
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$REPO/build-embassy-hostapp}"
CRATE_DIR="$REPO/src/bsp/rust/harness/golden_vt_render"

# Its own target dir, so this gate's build never replaces another gate's binary.
TARGET_BASE="${CARGO_TARGET_DIR:-$CRATE_DIR/target}"
TARGET="$TARGET_BASE/song-preview-ab"

if [ "${NO_BUILD:-0}" != 1 ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against (the scenario itself is one of
	# them), so force the object closure first — same reason audio_load_ab.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET")
fi

BIN="$TARGET/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/song_preview_ab.XXXXXX")"
IMG_GLOB="${TMPDIR:-/tmp}/deluge-streaming-scenario-*.img"

# Every run packs a 2.5 GB sparse FAT image, and nothing else removes it:
# `sd_image_path::fingerprint_dir` hashes path/length/mtime rather than content and the fixture is
# rewritten on each process start, so no run reuses a cached image. Left alone they accumulate
# under exactly the prefix this project's golden invocations all begin by clearing — and a full /tmp
# is a known way to make an unrelated stem export hang forever.
#
# Only images that appeared DURING this run are removed: a blanket `rm` of the glob would delete
# other rigs' cached corpus images, which are minutes of work to repack.
IMGS_BEFORE="$OUT_ROOT/.imgs_before"
ls -1 $IMG_GLOB 2>/dev/null | sort >"$IMGS_BEFORE" || true

cleanup() {
	if [ -f "$IMGS_BEFORE" ]; then
		ls -1 $IMG_GLOB 2>/dev/null | sort | comm -13 "$IMGS_BEFORE" - | while read -r img; do
			rm -f "$img"
		done
	fi
	rm -rf "$OUT_ROOT" "${TMPDIR:-/tmp}/deluge-song-preview-ab-card"
}
trap cleanup EXIT

# Run one leg. $1 = binary, $2 = tag. Echoes nothing; writes $OUT_ROOT/$2.txt.
run_leg() {
	local bin="$1" tag="$2"
	set +e
	GOLDEN_SCENARIO=song_preview_ab "$bin" >"$OUT_ROOT/$tag.txt" 2>"$OUT_ROOT/$tag.err"
	local status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		echo "FAIL — the $tag leg exited $status"
		sed -n '1,60p' "$OUT_ROOT/$tag.txt"
		sed -n '1,20p' "$OUT_ROOT/$tag.err" >&2
		exit 1
	fi
	# An empty body means the scenario never ran (a wedge that still exited 0, a scenario name that
	# did not match). Diffing two empty files would "pass" and prove nothing.
	if ! grep -q '^SP-RESULT ' "$OUT_ROOT/$tag.txt"; then
		echo "FAIL — the $tag leg produced no SP-RESULT line; it did not run the scenario to completion"
		sed -n '1,60p' "$OUT_ROOT/$tag.txt"
		exit 1
	fi
	if ! grep -q '^SP-RESULT failures=0$' "$OUT_ROOT/$tag.txt"; then
		echo "FAIL — the $tag leg reported in-process failures:"
		grep -E '^(FAIL|SP-WINDOW|SP-SWAP-SUMMARY|SP-SUMMARY|SP-RESULT|SP-LISTING|SP-PREDICT|SP-NEWNAME|SP-SLOT|SP-BYPATH|SP-LISTING-STALL|SP-PREVIEW-STALL|SP-DX|SP-DX-STALL|SP-REFERENCE|SP-TIER) ' \
			"$OUT_ROOT/$tag.txt"
		exit 1
	fi
	# Each stage prints exactly one of these. Its absence means the run reached SP-RESULT without
	# ever running that stage — which the failure count cannot show, because a stage that never ran
	# asserts nothing.
	for marker in SP-WINDOW SP-SWAP-BEFORE SP-EJECT SP-SWAP-SUMMARY SP-LISTING-SUMMARY SP-PREDICT-SUMMARY SP-NEWNAME \
		SP-SLOT SP-ACQUIRE-SUMMARY SP-BYPATH SP-LISTING-STALL SP-PREVIEW-STALL SP-DX SP-DX-STALL SP-REFERENCE SP-TIER; do
		if ! grep -q "^$marker " "$OUT_ROOT/$tag.txt"; then
			echo "FAIL — the $tag leg produced no $marker line; that stage did not run"
			exit 1
		fi
	done
}

echo "song-preview: running the harness (the prefix channel, held to an in-process file read)"
run_leg "$BIN" prefix

# The run must have gone through the channel. A binary without it would take the inline file read for
# every preview, which matches its own reference trivially.
grep -q '^SP-SUMMARY asyncActive=1 ' "$OUT_ROOT/prefix.txt" || {
	echo "FAIL — the run reports the prefix channel INACTIVE, so no preview went through it"
	grep '^SP-SUMMARY ' "$OUT_ROOT/prefix.txt"
	exit 1
}

grep '^SP-CASE ' "$OUT_ROOT/prefix.txt" >"$OUT_ROOT/prefix.cases"

# A floor on the case count, so a future edit that shrinks the fixture cannot keep printing PASS over
# a comparison that no longer covers the shapes this gate was built for. Counts the SYNTHETIC cases
# only: the corpus is the maintainer's and may legitimately be absent. Bump it deliberately when
# cases are added; never lower it to make a run go green.
MIN_CASES=6
CASES="$(wc -l <"$OUT_ROOT/prefix.cases")"
SYNTHETIC="$(grep -cE '^SP-CASE (render|json|noprev|missing|corrupt|empty) ' "$OUT_ROOT/prefix.cases")"
if [ "$SYNTHETIC" -lt "$MIN_CASES" ]; then
	echo "FAIL — only $SYNTHETIC synthetic case(s) ran, fewer than the $MIN_CASES this gate requires; kCases was shrunk"
	exit 1
fi

# Every song drawn has its reference beside it: each SP-CASE, the read-window song and the swapped song.
REFERENCES="$(grep -c '^SP-REFERENCE ' "$OUT_ROOT/prefix.txt")"
if [ "$REFERENCES" -ne $((CASES + 2)) ]; then
	echo "FAIL — $REFERENCES reference decode(s) for $CASES case(s), the read-window song and the swap; wanted $((CASES + 2))"
	exit 1
fi

echo "PASS — the prefix channel drew all $CASES song(s) as the file read does, agreed on every outcome, held across a card swap, and parsed the DX7 bank"
grep -h '^SP-SUMMARY \|^SP-DEFER \|^SP-WINDOW \|^SP-SWAP-SUMMARY \|^SP-PREVIEW-STALL ' "$OUT_ROOT/prefix.txt"
grep -hE '^SP-LISTING|^SP-NEWNAME|^SP-PREDICT|^SP-SLOT|^SP-BYPATH|^SP-DX|^SP-TIER' "$OUT_ROOT/prefix.txt" | sed 's/^/ run     /'
