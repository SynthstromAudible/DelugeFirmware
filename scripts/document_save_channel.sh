#!/usr/bin/env bash
# document_save_channel.sh — the song-load channel gate.
#
# Saves a preset through the REAL ENTER gesture and requires that the document was written whole,
# that the two builds wrote IDENTICAL bytes, and that the channel build stored it off the owner.
#
# ## Why this gate exists
#
# `write_file` began life with NO harness building it -- exactly where the whole-file READ was one
# rung earlier, and running THAT for the first time found two defects at once. A feature no gate
# builds is code that has never run. Building this one immediately found two more: the no-channel arm
# handed the write to a function that no longer wrote anything, and the counter this rung is measured
# by was never incremented.
#
# ## Why TWO legs
#
# The off leg proves the build-configuration fallback still loads a song with no channel at all; the
# on leg proves the channel arm runs and runs off the owner. One leg alone would leave either the
# fallback or the claim untested.
#
# What the checks cover:
#   DS-LOADED     the song came back off the card. A value stamped into it before saving and
#                 deliberately overwritten in memory afterwards. Required on BOTH legs.
#   DS-CHANNEL    the on leg's staging read was SERVED by the channel -- the async arm ran.
#   DS-GATED      the off leg served ZERO, so the feature gate actually gates.
#   DS-OFFOWNER   the on leg's staging ran OFF the storage owner. The rung's actual claim.
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/document-save-channel}"
CHAN_TARGET_DIR="${CHAN_TARGET_DIR:-$CRATE_DIR/target/document-save-channel-on}"

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
	# BOTH channel features on the on leg, and that is not belt-and-braces: the staging read sizes its
	# buffer with the byte channel's stat and fills it with the write channel's read, so a leg with one
	# and not the other tests a configuration the code now refuses to enter.
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/document_save_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

run_leg() {
	local name="$1" bin="$2"
	echo "document save channel: running $name leg"
	set +e
	GOLDEN_SCENARIO=document_save_channel "$bin" >"$OUT/$name.txt" 2>"$OUT/$name.err"
	local status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		echo "FAIL [$name] — the run exited $status"
		grep -E '^DS-' "$OUT/$name.txt" || sed -n '1,40p' "$OUT/$name.txt"
		sed -n '1,40p' "$OUT/$name.err" >&2
		fail=1
		return
	fi
	# An abort or a wedge exits before this line is printed, which is what turns a freeze into a gate
	# failure rather than a silently truncated log.
	for marker in DS-SAVED DS-OCCUPIED DS-SUMMARY DS-RESULT; do
		if ! grep -q "^$marker " "$OUT/$name.txt"; then
			echo "FAIL [$name] — no $marker line; that stage did not run"
			grep -E '^DS-' "$OUT/$name.txt"
			fail=1
			return
		fi
	done
	if ! grep -q '^DS-RESULT failures=0$' "$OUT/$name.txt"; then
		echo "FAIL [$name] — $(grep '^DS-RESULT' "$OUT/$name.txt")"
		grep -E '^DS-' "$OUT/$name.txt"
		fail=1
	fi
}

run_leg off "$BIN"
run_leg on "$CHAN_BIN"

served_of() { sed -n 's/^DS-SUMMARY served=\([0-9]*\) .*/\1/p' "$1"; }
offowner_of() { sed -n 's/^DS-SUMMARY .*offOwnerTicks=\([0-9]*\).*/\1/p' "$1"; }

off_served="$(served_of "$OUT/off.txt")"
on_served="$(served_of "$OUT/on.txt")"
on_offowner="$(offowner_of "$OUT/on.txt")"

# DS-CHANNEL. The async arm actually RAN. Before this gate nothing built the feature, so this code
# had never executed anywhere -- which is exactly how it came to contain two defects at once.
if [ -z "${on_served:-}" ] || [ "$on_served" -eq 0 ]; then
	echo "FAIL DS-CHANNEL — the channel-on leg served ${on_served:-no} staging read(s); the async arm never ran"
	fail=1
fi

# DS-GATED. The feature gate actually gates: with the channels off, the load must take the owner arm.
if [ -z "${off_served:-}" ] || [ "$off_served" -ne 0 ]; then
	echo "FAIL DS-GATED — the channel-OFF leg served ${off_served:-no} staging read(s); the feature gate is not gating"
	fail=1
fi

# DS-OFFOWNER. The rung's actual claim, and the first one on this ladder that can be asserted at all.
if [ -z "${on_offowner:-}" ] || [ "$on_offowner" -eq 0 ]; then
	echo "FAIL DS-OFFOWNER — the staging read never ran off the storage owner"
	fail=1
fi

# DS-IDENTICAL. The arms must write the same document; the checksum is what says so.
off_saved="$(grep '^DS-SAVED ' "$OUT/off.txt" || true)"
on_saved="$(grep '^DS-SAVED ' "$OUT/on.txt" || true)"
if [ -z "$off_saved" ] || [ "$off_saved" != "$on_saved" ]; then
	echo "FAIL DS-IDENTICAL — the two arms wrote different documents:"
	echo "  [off] $off_saved"
	echo "  [on]  $on_saved"
	fail=1
fi

grep -E '^DS-' "$OUT/off.txt" | sed 's/^/[off] /'
grep -E '^DS-' "$OUT/on.txt" | sed 's/^/[on]  /'

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "PASS — both arms wrote the same document, the channel arm stored it off the storage owner, and"
echo "       a taken name was refused rather than replaced"
