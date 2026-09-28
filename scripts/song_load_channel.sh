#!/usr/bin/env bash
# song_load_channel.sh — the song-load channel gate.
#
# Saves a song carrying a distinctive value, loads it back through the REAL LoadSongUI, and requires
# both that the value returns and that the read went through the storage channel.
#
# ## Why this gate exists
#
# A load that brings back the right song says nothing about how its bytes arrived. This gate pins the
# whole-file staging read the song load depends on: it must be served by the channels, after boot, and
# a gate that only checks what came back would stay green with that read broken in any way that still
# produced the song.
#
# One leg: neither the byte channel nor the write channel has a feature switch, so there is no build
# without them to compare against.
#
# What the checks cover:
#   SL-LOADED     the song came back off the card. A value stamped into it before saving and
#                 deliberately overwritten in memory afterwards.
#   SL-CHANNEL    the staging read was SERVED by the channel -- the async arm ran.
#   SL-TICKED   the staging ran through the channels after boot. The gate's actual claim.
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
	# Cargo does not track the C++ sources the harness links against, so force the object closure
	# first — same reason recorder_write.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT="$(mktemp -d "${TMPDIR:-/tmp}/song_load_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

echo "song load channel: running"
set +e
GOLDEN_SCENARIO=song_load_channel "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e
if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^SL-' "$OUT/run.txt" || sed -n '1,40p' "$OUT/run.txt"
	sed -n '1,40p' "$OUT/run.err" >&2
	exit 1
fi
# An abort or a wedge exits before these lines are printed, which is what turns a freeze into a gate
# failure rather than a silently truncated log.
for marker in SL-SETUP SL-LOADED SL-ORDER SL-SUMMARY SL-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^SL-' "$OUT/run.txt"
		exit 1
	fi
done
if ! grep -q '^SL-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^SL-RESULT' "$OUT/run.txt")"
	fail=1
fi

served="$(sed -n 's/^SL-SUMMARY served=\([0-9]*\) .*/\1/p' "$OUT/run.txt")"
channel_ticks="$(sed -n 's/^SL-SUMMARY .*channelTicks=\([0-9]*\).*/\1/p' "$OUT/run.txt")"

# SL-CHANNEL. The async arm actually RAN.
if [ -z "${served:-}" ] || [ "$served" -eq 0 ]; then
	echo "FAIL SL-CHANNEL — the load served ${served:-no} staging read(s); the async arm never ran"
	fail=1
fi

# SL-TICKED. The gate's actual claim: `channelTicks` counts staging attempts after boot, through the channels.
if [ -z "${channel_ticks:-}" ] || [ "$channel_ticks" -eq 0 ]; then
	echo "FAIL SL-TICKED — the staging read never ran through the channel across ticks"
	fail=1
fi

grep -E '^SL-' "$OUT/run.txt"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
grep -hE '^TIER-SYNC(-AT)? ' "$OUT/run.txt" || true
echo "PASS — the song loaded back, and the channel staged it across ticks"
