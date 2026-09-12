#!/usr/bin/env bash
# song_save_channel.sh — the staged song-save channel gate.
#
# Saves a song whose document is far too big to fit the staging buffer, and requires that it was
# built in memory, placed on the card, read back and verified, and that overwriting it left a song
# behind whichever way the save went.
#
# ## Why this gate exists
#
# The song save was the last document write still held open across the storage owner for its whole
# length. Converting it means building the document in memory first and then placing it -- and the
# channel that carries it is NOT the one the rest of this flow uses. The byte channel copies into a
# fixed staging buffer and refuses anything over 8 KiB; no song has ever fitted that, the smallest
# measured being 11.5 KB and the largest 1.10 MB. The write channel passes the pointer instead. A
# build that sent a song down the copying channel failed every time and simply produced no file.
#
# One leg: neither channel has a feature switch. The document write rides the write channel; the
# temp-name search, the read-back, the unlink and the rename ride the byte channel.
#
# What the checks cover:
#   SS-GREW       the song really did outgrow the staging buffer, at least twice. FIRST, because a
#                 fixture that fits the initial 32 KiB buffer exercises the growth path not at all,
#                 and every check about it would then be a check that cannot fail.
#   SS-SAVED      the document is on the card, with its length and checksum.
#   SS-VERIFIED   the save COMPLETED, which it can only do after its read-back check -- length, head
#                 and tail -- has passed. A save that fails that check abandons AFTER writing its
#                 file, so SS-SAVED alone cannot tell success from a rejected write left on the card.
#                 Truncating the document by one byte is what proved that distinction was needed.
#   SS-PARSES     it loads back as a SONG. Bytes of the right length are not the claim.
#   SS-INTACT     after saving over an existing song, a song is still there and still loads. The
#                 property that justifies writing to a temp name at all -- but note precisely WHERE
#                 it holds. Nothing destructive runs until the replacement is written AND verified,
#                 so a save that fails up to that point leaves the original untouched, and failing
#                 the read-back is what demonstrates it. It does NOT hold across the unlink-then-
#                 rename pair at the end: a rename that fails after the unlink succeeded leaves the
#                 song gone and the new content stranded at the temp name. That window is inherited
#                 from the streaming save, which made the same two calls in the same order, and this
#                 conversion preserves it rather than widening or closing it.
#   SS-CHANNEL    the steps were SERVED by the channels, and the document write by the WRITE channel.
#   SS-ACCOUNTED  the channels account for every card step, and nothing was sent to the owner. Neither
#                 SS-CHANNEL nor SS-SAVED can see WHICH arm carried which step.
#   SS-OFFOWNER   the steps ran OFF the storage owner. The rung's actual claim.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
#
# What this gate does NOT prove on its own: that the temp-file ordering is load-bearing. Only the
# neutering does that -- fail the read-back and the save must fail while SS-INTACT stays green,
# which is what says the original survives a failure. Failing the RENAME instead turns SS-INTACT
# red, and that is not a defect in this gate: it is the inherited unlink-then-rename window,
# reported rather than papered over.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/song-save-channel}"

if [ "${NO_BUILD:-0}" != "1" ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against, so force the object closure
	# first — same reason recorder_write.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$REPO_ROOT/$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT="$(mktemp -d "${TMPDIR:-/tmp}/song_save_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

echo "song save channel: running"
set +e
GOLDEN_SCENARIO=song_save_channel "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e
if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^SS-' "$OUT/run.txt" || sed -n '1,40p' "$OUT/run.txt"
	sed -n '1,40p' "$OUT/run.err" >&2
	exit 1
fi
# An abort or a wedge exits before these lines are printed, which is what turns a freeze into a
# gate failure rather than a silently truncated log.
for marker in SS-BACKMIDSAVE SS-EXITMIDSAVE SS-GREW SS-SAVED SS-VERIFIED SS-PARSES SS-INTACT SS-SUMMARY SS-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^SS-' "$OUT/run.txt"
		exit 1
	fi
done
if ! grep -q '^SS-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^SS-RESULT' "$OUT/run.txt")"
	fail=1
fi

served="$(sed -n 's/^SS-SUMMARY served=\([0-9]*\) .*/\1/p' "$OUT/run.txt")"
owner="$(sed -n 's/^SS-SUMMARY .*ownerServed=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
offowner="$(sed -n 's/^SS-SUMMARY .*offOwnerTicks=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
steps="$(sed -n 's/^SS-SUMMARY .*steps=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
write="$(sed -n 's/^SS-SUMMARY .*writeServed=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
doublings="$(sed -n 's/^SS-GREW doublings=\([0-9]*\).*/\1/p' "$OUT/run.txt")"

# SS-CHANNEL. The async arms actually RAN, and the document itself went through the WRITE channel.
if [ -z "${served:-}" ] || [ "$served" -eq 0 ]; then
	echo "FAIL SS-CHANNEL — the save served ${served:-no} step(s); the async arm never ran"
	fail=1
fi
if [ -z "${write:-}" ] || [ "$write" -eq 0 ]; then
	echo "FAIL SS-CHANNEL — the write channel served ${write:-no} document write(s)"
	fail=1
fi

# SS-ACCOUNTED. The channels must account for EVERY card step, and none may have gone to the owner.
if [ -z "${steps:-}" ] || [ "${steps:-0}" -eq 0 ]; then
	echo "FAIL SS-ACCOUNTED — the save reported no card steps at all"
	fail=1
fi
if [ "${owner:-1}" -ne 0 ]; then
	echo "FAIL SS-ACCOUNTED — the save sent ${owner} step(s) to the owner; they should all go to a channel"
	fail=1
fi
if [ "${served:-0}" -ne "${steps:-0}" ]; then
	echo "FAIL SS-ACCOUNTED — the save took ${steps:-no} card step(s) but the channels served ${served:-no}"
	fail=1
fi

# SS-OFFOWNER. The rung's actual claim. This counts ONLY ticks that took a channel arm while genuinely
# off the owner.
if [ -z "${offowner:-}" ] || [ "$offowner" -eq 0 ]; then
	echo "FAIL SS-OFFOWNER — no document step ever ran off the storage owner"
	fail=1
fi

# SS-GROWTH. The song must have exercised the buffer growth, or every growth check proved nothing.
if [ -z "${doublings:-}" ] || [ "$doublings" -lt 2 ]; then
	echo "FAIL SS-GROWTH — the song doubled the buffer ${doublings:-no} time(s); the growth path was not exercised"
	fail=1
fi

# SS-BACKMIDSAVE / SS-EXITMIDSAVE. A song save spans many ticks with the UI live. Leaving the save screen
# mid-save -- BACK, or the long-press exit -- used to close it, and the save's own close() then indexed off
# the end of the UI stack. Both must be refused while the save runs, and both must actually be tried while
# it IS running, or the check proves nothing.
grep -q '^SS-BACKMIDSAVE inFlight=1 back=1$' "$OUT/run.txt" || { echo "FAIL SS-BACKMIDSAVE — $(grep '^SS-BACKMIDSAVE ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^SS-EXITMIDSAVE exit=1$' "$OUT/run.txt" || { echo "FAIL SS-EXITMIDSAVE — $(grep '^SS-EXITMIDSAVE ' "$OUT/run.txt" || echo 'no line')"; fail=1; }

grep -E '^SS-' "$OUT/run.txt"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "PASS — the song document was built in memory, placed on the card through the write channel off"
echo "       the storage owner, read back, and loaded as a song"
