#!/usr/bin/env bash
# song_preview_ab.sh — the file-backed-vs-prefix-channel song-preview A/B gate.
#
# Builds the Embassy host harness TWICE from one source — once with the Rust prefix channel compiled
# but its switch off (`async_file_prefix`), once with the switch on and the reader task spawned
# (`async_file_prefix_active`) — draws the same fixture card's song artwork through the REAL
# `LoadSongUI` in both, and requires their per-song output to be byte-identical.
#
# Why two builds rather than two passes in one process: `deluge_file_prefix_async_active()` is
# `cfg!(feature = "async_file_prefix_active")` on the Rust side, a COMPILE-TIME constant. There is no
# runtime switch to flip mid-run, so an in-process A/B is not merely inconvenient, it does not exist.
#
# What the diff covers (src/deluge/harness/song_preview_ab_scenario.cpp writes the lines):
#   SP-CASE   one per song: the decode's `SongPreviewOutcome`, the `Error` it put on screen, and a
#             CRC of the rendered pad image. Diffed. Any difference is the gate failing.
#
#             The OUTCOME is diffed, not just the image, and that is the point rather than a detail:
#             an unreadable song, a song with no artwork and a preview that falls past the read
#             window all draw an IDENTICAL blank screen. A gate comparing only pixels passed while
#             one backing showed the user an error popup and the other silently showed nothing —
#             which is the state the two backings were in when this gate was written.
#   SP-EJECT  the card-swap property: a prefix is read and left uncollected, the card is ejected, a
#             DIFFERENT song is put at the same path, and the preview is asked for again. Diffed like
#             SP-CASE — the file backing is the oracle, since it holds no card-derived state to go
#             stale. The scenario ALSO asserts the post-swap artwork absolutely, against the known
#             second image: an inequality check ("not the first image") passes when both reads fail.
#   SP-WINDOW the ONE case whose two legs must legitimately DIFFER: a song whose `preview` sits past
#             `kSongPreviewWindow`. The file backing walks the whole document and renders it; the
#             prefix backing runs out of bytes and reports Truncated, drawing blank WITHOUT claiming
#             the song has no artwork. Not diffed — each leg asserts its own expected outcome
#             absolutely, which is stronger here than a diff would be (a backing that reported
#             Truncated for everything, or Rendered for everything, fails).
#   SP-DEFER / SP-SUMMARY / SP-SWAP-SUMMARY
#             which backing ran and how many times each song's preview deferred. EXPECTED to differ,
#             so not diffed — but each leg asserts its own consistency in-process, PER CASE: every
#             song on the async leg must have deferred at least once, or its preview was answered
#             without the async path being reached and it is not under test in that leg at all. A
#             run-wide total would be satisfied by one deferral anywhere.
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

# Two target dirs, never one. The two legs differ only by a cargo feature, so sharing a target dir
# would make every run rebuild the whole crate to swap features back and forth — and would clobber
# whichever leg was built last, so a stale binary could silently be diffed against itself.
TARGET_BASE="${CARGO_TARGET_DIR:-$CRATE_DIR/target}"
SYNC_TARGET="$TARGET_BASE/song-preview-ab-file"
ASYNC_TARGET="$TARGET_BASE/song-preview-ab-prefix"

if [ "${NO_BUILD:-0}" != 1 ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against (the A/B scenario itself is one
	# of them), so force the object closure first — same reason audio_load_ab.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --features async_file_prefix --target-dir "$SYNC_TARGET")
	(cd "$CRATE_DIR" && cargo build --release --features async_file_prefix_active --target-dir "$ASYNC_TARGET")
fi

SYNC_BIN="$SYNC_TARGET/release/golden_vt_render"
ASYNC_BIN="$ASYNC_TARGET/release/golden_vt_render"
for b in "$SYNC_BIN" "$ASYNC_BIN"; do
	[ -x "$b" ] || {
		echo "ERROR: '$b' not built (see NO_BUILD)"
		exit 2
	}
done

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/song_preview_ab.XXXXXX")"
IMG_GLOB="${TMPDIR:-/tmp}/deluge-streaming-scenario-*.img"

# Every run packs TWO 2.5 GB sparse FAT images, one per leg, and nothing else removes them:
# `sd_image_path::fingerprint_dir` hashes path/length/mtime rather than content and the fixture is
# rewritten on each process start, so the legs never share a cached image. Left alone they accumulate
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
		grep -E '^(FAIL|SP-WINDOW|SP-SWAP-SUMMARY|SP-SUMMARY|SP-RESULT|SP-LISTING|SP-SLOT) ' "$OUT_ROOT/$tag.txt"
		exit 1
	fi
	# Each stage prints exactly one of these. Its absence means the run reached SP-RESULT without
	# ever running that stage — which the failure count cannot show, because a stage that never ran
	# asserts nothing.
	for marker in SP-WINDOW SP-SWAP-BEFORE SP-EJECT SP-SWAP-SUMMARY SP-LISTING-SUMMARY SP-SLOT SP-ACQUIRE-SUMMARY; do
		if ! grep -q "^$marker " "$OUT_ROOT/$tag.txt"; then
			echo "FAIL — the $tag leg produced no $marker line; that stage did not run"
			exit 1
		fi
	done
}

echo "song-preview A/B: running the file-backed leg (async_file_prefix, switch off)"
run_leg "$SYNC_BIN" file
echo "song-preview A/B: running the prefix-channel leg (async_file_prefix_active, reader task spawned)"
run_leg "$ASYNC_BIN" prefix

# The acquisition must reach the same instrument on every leg. Compared against the file leg,
# which never touches the channel, so a difference can only come from the conversion.
for tag in prefix; do
	ref=$(grep '^SP-ACQUIRE converted ' "$OUT_ROOT/file.txt" | head -1)
	got=$(grep '^SP-ACQUIRE converted ' "$OUT_ROOT/$tag.txt" | head -1)
	# Compare the name field only; the dispatch count legitimately differs between legs.
	if [ "${ref%% dispatches=*}" != "${got%% dispatches=*}" ]; then
		echo "FAIL — the $tag leg acquired a different instrument:"
		echo "  file: $ref"
		echo "  $tag: $got"
		exit 1
	fi
done

# Each leg must be the leg it claims to be. The file leg with asyncActive=1 (or the reverse) means
# the two binaries were built the same way and the comparison is a build against itself.
grep -q '^SP-SUMMARY asyncActive=0 ' "$OUT_ROOT/file.txt" || {
	echo "FAIL — the 'file-backed' leg reports the prefix channel ACTIVE; both legs were built the same way"
	grep '^SP-SUMMARY ' "$OUT_ROOT/file.txt"
	exit 1
}
grep -q '^SP-SUMMARY asyncActive=1 ' "$OUT_ROOT/prefix.txt" || {
	echo "FAIL — the 'prefix-channel' leg reports the channel INACTIVE; both legs were built the same way"
	grep '^SP-SUMMARY ' "$OUT_ROOT/prefix.txt"
	exit 1
}

grep '^SP-CASE ' "$OUT_ROOT/file.txt" >"$OUT_ROOT/file.cases"
grep '^SP-CASE ' "$OUT_ROOT/prefix.txt" >"$OUT_ROOT/prefix.cases"
if ! diff -u "$OUT_ROOT/file.cases" "$OUT_ROOT/prefix.cases"; then
	echo "FAIL — the two backings did not draw the same artwork, or did not report the same outcome, for the same song (diff above: -file +prefix)"
	exit 1
fi

# A floor on the case count, so a future edit that shrinks the fixture cannot keep printing PASS over
# a comparison that no longer covers the shapes this gate was built for. Counts the SYNTHETIC cases
# only: the corpus is the maintainer's and may legitimately be absent. Bump it deliberately when
# cases are added; never lower it to make a run go green.
MIN_CASES=6
CASES="$(wc -l <"$OUT_ROOT/file.cases")"
SYNTHETIC="$(grep -cE '^SP-CASE (render|json|noprev|missing|corrupt|empty) ' "$OUT_ROOT/file.cases")"
if [ "$SYNTHETIC" -lt "$MIN_CASES" ]; then
	echo "FAIL — only $SYNTHETIC synthetic case(s) ran, fewer than the $MIN_CASES this gate requires; kCases was shrunk"
	exit 1
fi

# The card-swap stage's diffed line, kept out of the case files above so MIN_CASES keeps counting
# only the case list.
grep '^SP-EJECT ' "$OUT_ROOT/file.txt" >"$OUT_ROOT/file.eject"
grep '^SP-EJECT ' "$OUT_ROOT/prefix.txt" >"$OUT_ROOT/prefix.eject"
if ! diff -u "$OUT_ROOT/file.eject" "$OUT_ROOT/prefix.eject"; then
	echo "FAIL — after a card swap the two backings did not draw the same artwork (diff above: -file +prefix)"
	exit 1
fi

# The read-window case is the one line that must NOT match, and its legs are asserted individually
# in-process. Requiring the difference here as well is what stops the fixture quietly losing the
# case: if both legs ever agree, the long-attribute song is no longer past the window and the
# Truncated arm is untested.
if diff -q <(grep '^SP-WINDOW ' "$OUT_ROOT/file.txt" | sed 's/asyncActive=[01] //') \
	<(grep '^SP-WINDOW ' "$OUT_ROOT/prefix.txt" | sed 's/asyncActive=[01] //') >/dev/null; then
	echo "FAIL — the read-window song produced the SAME result on both backings, so its preview no longer falls past the window and the Truncated outcome is not under test"
	grep -h '^SP-WINDOW ' "$OUT_ROOT/file.txt" "$OUT_ROOT/prefix.txt"
	exit 1
fi

echo "PASS — both backings drew all $CASES song(s) identically, agreed on every outcome, and agreed across a card swap"
grep -h '^SP-SUMMARY \|^SP-DEFER \|^SP-WINDOW \|^SP-SWAP-SUMMARY ' "$OUT_ROOT/file.txt" "$OUT_ROOT/prefix.txt"
# The listing lines from both legs. Both carry the directory channel, so neither is a synchronous control: each leg
# asserts its own listing results in-process, and these lines are here to be read.
grep -hE '^SP-LISTING|^SP-NEWNAME|^SP-PREDICT|^SP-SLOT|^SP-TIER' "$OUT_ROOT/file.txt" | sed 's/^/ file    /'
grep -hE '^SP-LISTING|^SP-NEWNAME|^SP-PREDICT|^SP-SLOT|^SP-TIER' "$OUT_ROOT/prefix.txt" | sed 's/^/ prefix  /'
