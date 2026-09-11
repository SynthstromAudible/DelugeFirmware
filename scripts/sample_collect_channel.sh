#!/usr/bin/env bash
# sample_collect_channel.sh — the sample-collection channel gate.
#
# Saves a song with collect-media on and requires that every sample the song references arrived in
# the song's own folder byte-identical to its source, that the audio-file list survived the process
# intact, and that the channel build did the copying off the storage owner.
#
# ## Why this gate exists
#
# Collecting media is the one save path that walks a container the audio engine and the UI both
# read, mutating a key the container is SORTED BY as it goes. The old code got away with that
# because the whole walk was one uninterrupted call on the storage owner; the moment the copying
# leaves the owner, the walk can be observed half-done. The rung answers that by deciding every
# path first, transferring with nothing shared held, and applying the decisions in one owner pass --
# and SC-ORDERED is what says the answer works. It guards an invariant the old code was
# ACCIDENTALLY safe from, which is exactly the kind that breaks silently when the accident goes away.
#
# Running this gate for the first time found three defects, none of which any other gate could see:
# transfers had no owner arm at all, the collected path was built by appending to an empty string
# (which took embedded-fatfs down), and the check itself asked the wrong question twice over.
#
# ## Why TWO legs
#
# The legs differ only in the WRITE channel. The collection's transfers ride the byte channel, which has
# no feature switch, so BOTH legs must copy every file through it, off the owner. The save around the
# collection stages its document on the owner (off leg) or through the write channel (on leg), and
# SC-IDENTICAL requires the two to collect the same bytes either way.
#
# What the checks cover:
#   SC-COPIED     every referenced sample is AT its collected destination, byte-identical to the
#                 source. Required on BOTH legs.
#   SC-RENAMED    the fixture's recording really was renamed. Required, because a collect-media save
#                 rewrites filePath for recordings and NOTHING else -- so if the rename stops
#                 happening, nothing moves in the audio-file vector and SC-ORDERED becomes a check
#                 that cannot fail.
#   SC-ORDERED    the audio-file list is still sorted and every resident file still findable, once
#                 the save is over. Required on BOTH legs.
#   SC-ATOMIC     the same invariant, checked on EVERY tick while the collection is in flight. This
#                 is the one the design is actually for: the hazard is another tier bisecting the
#                 vector BETWEEN two transfers, and SC-ORDERED cannot see that window because the
#                 apply pass tidies it away before the end. A path mutated during the planning walk
#                 passes SC-ORDERED and fails this.
#   SC-CHANNEL    both legs' transfers were SERVED by the byte channel -- the async arm ran.
#   SC-ACCOUNTED  on both legs the channel served every collected file and the owner served none.
#                 Neither SC-CHANNEL nor SC-COPIED can see which arm carried which file.
#   SC-OFFOWNER   both legs' transfers ran OFF the storage owner.
#   SC-IDENTICAL  both legs collected the same bytes to the same places.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
#
# Two things keep the ordering checks from becoming checks that cannot fail. A collect-media save
# rewrites filePath for RECORDINGS and nothing else, so the fixture carries one -- SC-RENAMED is
# what proves it is still being renamed -- and a second file positioned so the rename must move the
# recording ACROSS it in the sort order. And the neutering: mutate filePath during the PLAN walk
# instead of the APPLY pass, and SC-ATOMIC must go red. It was written expecting SC-ORDERED to catch
# that, and SC-ORDERED did not; the end state is repaired by the apply pass, so only the in-flight
# check can see it.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/sample-collect-channel}"
CHAN_TARGET_DIR="${CHAN_TARGET_DIR:-$CRATE_DIR/target/sample-collect-channel-on}"

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
	# The on leg turns the WRITE channel on, which the save around the collection stages its document
	# through. The byte channel the collection copies through is on in every build.
	(cd "$CRATE_DIR" && cargo build --release \
		--features async_file_write_active \
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/sample_collect_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

run_leg() {
	local name="$1" bin="$2"
	echo "sample collect channel: running $name leg"
	set +e
	GOLDEN_SCENARIO=sample_collect "$bin" >"$OUT/$name.txt" 2>"$OUT/$name.err"
	local status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		echo "FAIL [$name] — the run exited $status"
		grep -E '^SC-' "$OUT/$name.txt" || sed -n '1,40p' "$OUT/$name.txt"
		sed -n '1,40p' "$OUT/$name.err" >&2
		fail=1
		return
	fi
	# An abort or a wedge exits before these lines are printed, which is what turns a freeze into a
	# gate failure rather than a silently truncated log.
	for marker in SC-SETUP SC-COPIED SC-RENAMED SC-ATOMIC SC-ORDERED SC-SUMMARY SC-RESULT; do
		if ! grep -q "^$marker " "$OUT/$name.txt"; then
			echo "FAIL [$name] — no $marker line; that stage did not run"
			grep -E '^SC-' "$OUT/$name.txt"
			fail=1
			return
		fi
	done
	if ! grep -q '^SC-RESULT failures=0$' "$OUT/$name.txt"; then
		echo "FAIL [$name] — $(grep '^SC-RESULT' "$OUT/$name.txt")"
		grep -E '^SC-' "$OUT/$name.txt"
		fail=1
	fi
}

run_leg off "$BIN"
run_leg on "$CHAN_BIN"

served_of() { sed -n 's/^SC-SUMMARY served=\([0-9]*\) .*/\1/p' "$1"; }
owner_of() { sed -n 's/^SC-SUMMARY .*ownerServed=\([0-9]*\).*/\1/p' "$1"; }
offowner_of() { sed -n 's/^SC-SUMMARY .*offOwnerTicks=\([0-9]*\).*/\1/p' "$1"; }
copies_of() { grep -c '^SC-COPIED to=' "$1" || true; }


# SC-CHANNEL, SC-ACCOUNTED and SC-OFFOWNER, held on BOTH legs: the transfers ride the byte channel,
# which has no feature switch, so neither leg may send a file to the owner, and each leg's channel must
# have served exactly the files that arrived.
for leg in off on; do
	f="$OUT/$leg.txt"
	served="$(served_of "$f")"
	owner="$(owner_of "$f")"
	offowner="$(offowner_of "$f")"
	copies="$(copies_of "$f")"
	if [ -z "${served:-}" ] || [ "$served" -eq 0 ]; then
		echo "FAIL SC-CHANNEL — the $leg leg served ${served:-no} transfer(s); the async arm never ran"
		fail=1
	fi
	if [ "${owner:-1}" -ne 0 ]; then
		echo "FAIL SC-ACCOUNTED — the $leg leg sent ${owner:-an unknown number of} transfer(s) to the owner"
		fail=1
	fi
	if [ "${served:-0}" -ne "$copies" ]; then
		echo "FAIL SC-ACCOUNTED — the $leg leg copied $copies file(s) but the channel served ${served:-no}"
		fail=1
	fi
	if [ -z "${offowner:-}" ] || [ "$offowner" -eq 0 ]; then
		echo "FAIL SC-OFFOWNER — no transfer on the $leg leg ever ran off the storage owner"
		fail=1
	fi
done

# SC-IDENTICAL. The two arms must collect the same bytes to the same destinations; the per-file
# checksum lines are what say so.
#
# The recording's `_XXXXX` suffix is RANDOM by design -- it is what stops a second collect-media save
# colliding with the copy the first one made -- so it is normalised away before the legs are
# compared. Everything else in the line, the destination and the length and the checksum, is
# compared as written.
normalise() { sed 's/_[0-9A-Z]\{5\}\.WAV/_XXXXX.WAV/g' "$1" | grep '^SC-COPIED ' | sort || true; }
off_copied="$(normalise "$OUT/off.txt")"
on_copied="$(normalise "$OUT/on.txt")"
if [ -z "$off_copied" ] || [ "$off_copied" != "$on_copied" ]; then
	echo "FAIL SC-IDENTICAL — the two arms collected different files:"
	echo "$off_copied" | sed 's/^/  [off] /'
	echo "$on_copied" | sed 's/^/  [on]  /'
	fail=1
fi

grep -E '^SC-' "$OUT/off.txt" | sed 's/^/[off] /'
grep -E '^SC-' "$OUT/on.txt" | sed 's/^/[on]  /'

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "PASS — both arms collected the same samples into the song's folder, the audio-file list came"
echo "       through ordered and intact, and the channel arm did the copying off the storage owner"
