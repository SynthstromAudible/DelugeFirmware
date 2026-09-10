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
# ## Why TWO legs
#
# The off leg proves a build with no channels still saves through the owner; the on leg proves the
# channel arm runs and runs off the owner. One leg alone leaves either the fallback or the claim
# untested.
#
# What the checks cover:
#   SS-GREW       the song really did outgrow the staging buffer, at least twice. FIRST, because a
#                 fixture that fits the initial 32 KiB buffer exercises the growth path not at all,
#                 and every check about it would then be a check that cannot fail.
#   SS-SAVED      the document is on the card, with its length and checksum. Both legs.
#   SS-VERIFIED   the save COMPLETED, which it can only do after its read-back check -- length, head
#                 and tail -- has passed. A save that fails that check abandons AFTER writing its
#                 file, so SS-SAVED alone cannot tell success from a rejected write left on the card.
#                 Truncating the document by one byte is what proved that distinction was needed.
#   SS-PARSES     it loads back as a SONG. Bytes of the right length are not the claim, and a
#                 document broken identically on both legs would pass every comparison between them.
#   SS-INTACT     after saving over an existing song, a song is still there and still loads. The
#                 property that justifies writing to a temp name and renaming.
#   SS-IDENTICAL  both legs wrote the same document.
#   SS-CHANNEL    the on leg's steps were SERVED by a channel -- the async arm ran.
#   SS-GATED      the off leg served ZERO, so the feature gate actually gates.
#   SS-ACCOUNTED  the two arms account for every card step between them, and only ONE ran per leg.
#                 Neither SS-CHANNEL nor SS-SAVED can see WHICH arm carried which step, so a leg
#                 where half of them quietly took the other arm passes both.
#   SS-OFFOWNER   the on leg's steps ran OFF the storage owner. The rung's actual claim.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
#
# What this gate does NOT prove on its own: that the temp-file ordering is load-bearing. Only the
# neutering does that -- fail the rename and SS-INTACT must stay green while the save fails; unlink
# before verifying and it must go red.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/song-save-channel}"
CHAN_TARGET_DIR="${CHAN_TARGET_DIR:-$CRATE_DIR/target/song-save-channel-on}"

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
	# BOTH channels on the on leg, and that is not belt-and-braces: the document write asks the WRITE
	# channel while the temp-name search, the read-back, the unlink and the rename ask the BYTE
	# channel, so a leg with one and not the other tests a configuration the save never takes.
	(cd "$CRATE_DIR" && cargo build --release \
		--features async_file_write_active,async_file_bytes_active \
		--target-dir "$REPO_ROOT/$CHAN_TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
CHAN_BIN="$CHAN_TARGET_DIR/release/golden_vt_render"
for b in "$BIN" "$CHAN_BIN"; do
	[ -x "$b" ] || {
		echo "ERROR: '$b' not built (see NO_BUILD)"
		exit 2
	}
done

OUT="$(mktemp -d "${TMPDIR:-/tmp}/song_save_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

run_leg() {
	local name="$1" bin="$2"
	echo "song save channel: running $name leg"
	set +e
	GOLDEN_SCENARIO=song_save_channel "$bin" >"$OUT/$name.txt" 2>"$OUT/$name.err"
	local status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		echo "FAIL [$name] — the run exited $status"
		grep -E '^SS-' "$OUT/$name.txt" || sed -n '1,40p' "$OUT/$name.txt"
		sed -n '1,40p' "$OUT/$name.err" >&2
		fail=1
		return
	fi
	# An abort or a wedge exits before these lines are printed, which is what turns a freeze into a
	# gate failure rather than a silently truncated log.
	for marker in SS-GREW SS-SAVED SS-VERIFIED SS-PARSES SS-INTACT SS-SUMMARY SS-RESULT; do
		if ! grep -q "^$marker " "$OUT/$name.txt"; then
			echo "FAIL [$name] — no $marker line; that stage did not run"
			grep -E '^SS-' "$OUT/$name.txt"
			fail=1
			return
		fi
	done
	if ! grep -q '^SS-RESULT failures=0$' "$OUT/$name.txt"; then
		echo "FAIL [$name] — $(grep '^SS-RESULT' "$OUT/$name.txt")"
		grep -E '^SS-' "$OUT/$name.txt"
		fail=1
	fi
}

run_leg off "$BIN"
run_leg on "$CHAN_BIN"

served_of() { sed -n 's/^SS-SUMMARY served=\([0-9]*\) .*/\1/p' "$1"; }
owner_of() { sed -n 's/^SS-SUMMARY .*ownerServed=\([0-9]*\).*/\1/p' "$1"; }
offowner_of() { sed -n 's/^SS-SUMMARY .*offOwnerTicks=\([0-9]*\).*/\1/p' "$1"; }
steps_of() { sed -n 's/^SS-SUMMARY .*steps=\([0-9]*\).*/\1/p' "$1"; }
doublings_of() { sed -n 's/^SS-GREW doublings=\([0-9]*\).*/\1/p' "$1"; }

off_served="$(served_of "$OUT/off.txt")"
on_served="$(served_of "$OUT/on.txt")"
off_owner="$(owner_of "$OUT/off.txt")"
on_owner="$(owner_of "$OUT/on.txt")"
on_offowner="$(offowner_of "$OUT/on.txt")"
off_steps="$(steps_of "$OUT/off.txt")"
on_steps="$(steps_of "$OUT/on.txt")"

# SS-CHANNEL. The async arm actually RAN. Nothing else in the tree drives a whole-song document
# write through a channel, so without this leg that code has never executed anywhere.
if [ -z "${on_served:-}" ] || [ "$on_served" -eq 0 ]; then
	echo "FAIL SS-CHANNEL — the channel-on leg served ${on_served:-no} step(s); the async arm never ran"
	fail=1
fi

# SS-GATED. The feature gate actually gates: with the channels off, every step must take the owner.
if [ -z "${off_served:-}" ] || [ "$off_served" -ne 0 ]; then
	echo "FAIL SS-GATED — the channel-OFF leg served ${off_served:-no} step(s); the feature gate is not gating"
	fail=1
fi

# SS-ACCOUNTED. The two arms must account for EVERY card step between them, and on each leg only one
# of them may have run. Without this a leg where half the steps quietly took the other arm reads as a
# pass: SS-CHANNEL only asks whether a channel served ANY, and SS-SAVED only asks whether the file
# arrived -- neither can see WHICH arm carried each step.
if [ -z "${off_steps:-}" ] || [ "${off_steps:-0}" -eq 0 ]; then
	echo "FAIL SS-ACCOUNTED — the channel-OFF leg reported no card steps at all"
	fail=1
elif [ "${off_owner:-0}" -ne "${off_steps}" ]; then
	echo "FAIL SS-ACCOUNTED — the OFF leg took ${off_steps} card step(s) but the owner served ${off_owner:-no}"
	fail=1
fi
if [ "${on_owner:-1}" -ne 0 ]; then
	echo "FAIL SS-ACCOUNTED — the ON leg sent ${on_owner} step(s) to the owner; they should all go to a channel"
	fail=1
fi
if [ -z "${on_steps:-}" ] || [ "${on_served:-0}" -ne "${on_steps:-0}" ]; then
	echo "FAIL SS-ACCOUNTED — the ON leg took ${on_steps:-no} card step(s) but the channels served ${on_served:-no}"
	fail=1
fi

# SS-OFFOWNER. The rung's actual claim. This counts ONLY ticks that took a channel arm while
# genuinely off the owner -- work done on the owner is not counted here, or the number would be
# saying "off the owner" about work that ran on it.
if [ -z "${on_offowner:-}" ] || [ "$on_offowner" -eq 0 ]; then
	echo "FAIL SS-OFFOWNER — no document step ever ran off the storage owner"
	fail=1
fi

# SS-GROWTH. Both legs must have exercised the buffer growth, or every growth check proved nothing.
for leg in off on; do
	d="$(doublings_of "$OUT/$leg.txt")"
	if [ -z "${d:-}" ] || [ "$d" -lt 2 ]; then
		echo "FAIL SS-GROWTH — the $leg leg's song doubled the buffer ${d:-no} time(s); the growth path was not exercised"
		fail=1
	fi
done

# SS-IDENTICAL. Both arms must write the same document. The temp name embeds a counter rather than a
# random suffix, so these lines compare as written.
off_saved="$(grep '^SS-SAVED ' "$OUT/off.txt" || true)"
on_saved="$(grep '^SS-SAVED ' "$OUT/on.txt" || true)"
if [ -z "$off_saved" ] || [ "$off_saved" != "$on_saved" ]; then
	echo "FAIL SS-IDENTICAL — the two arms wrote different documents:"
	echo "  [off] $off_saved"
	echo "  [on]  $on_saved"
	fail=1
fi

grep -E '^SS-' "$OUT/off.txt" | sed 's/^/[off] /'
grep -E '^SS-' "$OUT/on.txt" | sed 's/^/[on]  /'

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "PASS — both arms built the same song document in memory and placed it on the card, it read"
echo "       back and loaded as a song, and the channel arm did the work off the storage owner"
