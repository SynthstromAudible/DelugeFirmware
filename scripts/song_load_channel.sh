#!/usr/bin/env bash
# song_load_channel.sh — the song-load channel gate.
#
# Saves a song carrying a distinctive value, loads it back through the REAL LoadSongUI, and requires
# both that the value returns and that the read went through the storage channel, off the owner.
#
# ## Why this gate exists
#
# recorder_write.sh was the ONLY harness that built `async_file_write_active`. The whole-file staging
# read the song load depends on had therefore never executed under ANY gate -- it could have been
# broken in any way and every gate would still have been green, because every one of them ran the
# build where that code is compiled out. Building it here found two real defects on its first run: the
# arm entered on a build with only one of the two channels it needs, and deferred forever when it did.
#
# ## Why TWO legs
#
# The off leg proves the build-configuration fallback still loads a song with no channel at all; the
# on leg proves the channel arm runs and runs off the owner. One leg alone would leave either the
# fallback or the claim untested.
#
# What the checks cover:
#   SL-LOADED     the song came back off the card. A value stamped into it before saving and
#                 deliberately overwritten in memory afterwards. Required on BOTH legs.
#   SL-CHANNEL    the on leg's staging read was SERVED by the channel -- the async arm ran.
#   SL-GATED      the off leg served ZERO, so the feature gate actually gates.
#   SL-OFFOWNER   the on leg's staging ran OFF the storage owner. The rung's actual claim.
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/song-load-channel}"
CHAN_TARGET_DIR="${CHAN_TARGET_DIR:-$CRATE_DIR/target/song-load-channel-on}"

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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/song_load_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

run_leg() {
	local name="$1" bin="$2"
	echo "song load channel: running $name leg"
	set +e
	GOLDEN_SCENARIO=song_load_channel "$bin" >"$OUT/$name.txt" 2>"$OUT/$name.err"
	local status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		echo "FAIL [$name] — the run exited $status"
		grep -E '^SL-' "$OUT/$name.txt" || sed -n '1,40p' "$OUT/$name.txt"
		sed -n '1,40p' "$OUT/$name.err" >&2
		fail=1
		return
	fi
	# An abort or a wedge exits before this line is printed, which is what turns a freeze into a gate
	# failure rather than a silently truncated log.
	for marker in SL-SETUP SL-LOADED SL-SUMMARY SL-RESULT; do
		if ! grep -q "^$marker " "$OUT/$name.txt"; then
			echo "FAIL [$name] — no $marker line; that stage did not run"
			grep -E '^SL-' "$OUT/$name.txt"
			fail=1
			return
		fi
	done
	if ! grep -q '^SL-RESULT failures=0$' "$OUT/$name.txt"; then
		echo "FAIL [$name] — $(grep '^SL-RESULT' "$OUT/$name.txt")"
		grep -E '^SL-' "$OUT/$name.txt"
		fail=1
	fi
}

run_leg off "$BIN"
run_leg on "$CHAN_BIN"

served_of() { sed -n 's/^SL-SUMMARY served=\([0-9]*\) .*/\1/p' "$1"; }
offowner_of() { sed -n 's/^SL-SUMMARY .*offOwnerTicks=\([0-9]*\).*/\1/p' "$1"; }

off_served="$(served_of "$OUT/off.txt")"
on_served="$(served_of "$OUT/on.txt")"
on_offowner="$(offowner_of "$OUT/on.txt")"

# SL-CHANNEL. The async arm actually RAN. Before this gate nothing built the feature, so this code
# had never executed anywhere -- which is exactly how it came to contain two defects at once.
if [ -z "${on_served:-}" ] || [ "$on_served" -eq 0 ]; then
	echo "FAIL SL-CHANNEL — the channel-on leg served ${on_served:-no} staging read(s); the async arm never ran"
	fail=1
fi

# SL-GATED. The feature gate actually gates: with the channels off, the load must take the owner arm.
if [ -z "${off_served:-}" ] || [ "$off_served" -ne 0 ]; then
	echo "FAIL SL-GATED — the channel-OFF leg served ${off_served:-no} staging read(s); the feature gate is not gating"
	fail=1
fi

# SL-OFFOWNER. The rung's actual claim, and the first one on this ladder that can be asserted at all.
if [ -z "${on_offowner:-}" ] || [ "$on_offowner" -eq 0 ]; then
	echo "FAIL SL-OFFOWNER — the staging read never ran off the storage owner"
	fail=1
fi

grep -E '^SL-' "$OUT/off.txt" | sed 's/^/[off] /'
grep -E '^SL-' "$OUT/on.txt" | sed 's/^/[on]  /'

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "PASS — the song loaded back on both legs, and the channel leg staged it off the storage owner"
