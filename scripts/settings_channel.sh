#!/usr/bin/env bash
# settings_channel.sh — the settings-channel gate.
#
# Writes a settings file through the REAL consumer entry point, reads it back, and requires the value
# to survive -- while counting every settings operation that began on the interaction tier.
#
# What the checks below cover, and why each is separate:
#   SC-ROUNDTRIP  the settings survive. This passes BEFORE the conversion too, and that is the point:
#                 it is the safety half, and it is what catches a conversion that moved the I/O and
#                 broke the settings on the way.
#   SC-TIER       that no settings operation ran on the interaction tier. This is the EVIDENCE half.
#                 It is deliberately non-zero before the conversion, so the gate FAILS then; the
#                 transition to zero is the only thing that proves anything moved.
#   SC-BURST      a burst of three writes, the first already in flight, lands the last value in exactly two card
#                 writes on the channel leg: the waiting write is replaced, the in-flight one never is.
#   SC-ORDER      a read queued behind a write to the same file sees that write.
#   SC-FALLBACK   with the channel in use, a busy channel queues -- nothing reaches the storage owner.
#
# Rung 6 is why both halves exist. Its round trip passed identically before and after, so the tier
# count going 3 -> 0 was the entire proof; a gate with only a round trip would have reported success
# for a conversion that never happened.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/settings-channel}"
# The channel-on leg's own target dir. Separate because the two legs differ by a cargo FEATURE, and
# sharing a target dir would make each run rebuild the other's artefacts every time.
CHAN_TARGET_DIR="${CHAN_TARGET_DIR:-$CRATE_DIR/target/settings-channel-on}"

if [ "${NO_BUILD:-0}" != "1" ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against (this scenario is one of them),
	# so force the object closure first — same reason save_roundtrip.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$(cd "$REPO_ROOT" && pwd)/$TARGET_DIR")
	(cd "$CRATE_DIR" && cargo build --release --features async_file_bytes_active \
		--target-dir "$(cd "$REPO_ROOT" && pwd)/$CHAN_TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
CHAN_BIN="$CHAN_TARGET_DIR/release/golden_vt_render"
for b in "$BIN" "$CHAN_BIN"; do
	[ -x "$b" ] || {
		echo "ERROR: '$b' not built (see NO_BUILD)"
		exit 2
	}
done

OUT="$(mktemp -d "${TMPDIR:-/tmp}/settings_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

echo "settings channel: running"
set +e
GOLDEN_SCENARIO=settings_channel "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^SC-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	sed -n '1,25p' "$OUT/run.err" >&2
	exit 1
fi

# An abort (a tier assertion firing) exits before this line is ever printed, which is what turns a
# freeze into a gate failure rather than a silently truncated log.
if ! grep -q '^SC-RESULT ' "$OUT/run.txt"; then
	echo "FAIL — no SC-RESULT line; the scenario did not run to completion"
	sed -n '1,60p' "$OUT/run.txt"
	exit 1
fi

for marker in SC-ROUNDTRIP SC-BURST SC-ORDER SC-FALLBACK SC-TIER; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^SC-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^SC-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^SC-' "$OUT/run.txt"
	exit 1
fi

grep -E '^SC-' "$OUT/run.txt"

# --- the channel-on leg ------------------------------------------------------
#
# The leg above proves the tier fix, which the owner-dispatch arm delivers on its own. It says
# NOTHING about the channel: with the byte channel compiled out, every consumer falls back to that
# arm, and the fallback is correct and silent. A channel that never worked would leave the leg above
# green and every settings file intact.
#
# So this leg builds the same scenario against a binary with `async_file_bytes_active` and requires
# SC-CHANNEL to be non-zero. That is the check that the async arm is EXECUTED, not merely compiled --
# rung 3's livelock survived review because nothing anywhere ran the channel, and the first
# channel-on run of this gate reported `served=0` because the module had not been added to the
# harness's own module list. Both legs must also agree on the round-tripped value: a channel that
# runs but corrupts is no better than one that never runs.
echo "settings channel: running the channel-on leg (async_file_bytes_active, byte worker spawned)"
set +e
GOLDEN_SCENARIO=settings_channel "$CHAN_BIN" >"$OUT/chan.txt" 2>"$OUT/chan.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the channel-on leg exited $status"
	grep -E '^SC-' "$OUT/chan.txt" || sed -n '1,60p' "$OUT/chan.txt"
	sed -n '1,25p' "$OUT/chan.err" >&2
	exit 1
fi

if ! grep -q '^SC-RESULT failures=0$' "$OUT/chan.txt"; then
	echo "FAIL — in-process failures on the channel-on leg:"
	grep -E '^SC-' "$OUT/chan.txt"
	exit 1
fi

for marker in SC-ROUNDTRIP SC-BURST SC-ORDER SC-FALLBACK SC-TIER; do
	if ! grep -q "^$marker " "$OUT/chan.txt"; then
		echo "FAIL — no $marker line on the channel-on leg; that stage did not run"
		grep -E '^SC-' "$OUT/chan.txt"
		exit 1
	fi
done

served="$(sed -n 's/^SC-CHANNEL served=\([0-9]*\)$/\1/p' "$OUT/chan.txt")"
if [ -z "$served" ]; then
	echo "FAIL — no SC-CHANNEL line on the channel-on leg"
	grep -E '^SC-' "$OUT/chan.txt"
	exit 1
fi
if [ "$served" -eq 0 ]; then
	echo "FAIL — SC-CHANNEL served=0: the byte channel served nothing, so every settings operation"
	echo "       fell back to the owner arm and this leg tested the same code as the one above."
	grep -E '^SC-' "$OUT/chan.txt"
	exit 1
fi

if ! diff <(grep -E '^SC-ROUNDTRIP ' "$OUT/run.txt") <(grep -E '^SC-ROUNDTRIP ' "$OUT/chan.txt") >/dev/null; then
	echo "FAIL — the two legs disagree on what round-tripped:"
	diff <(grep -E '^SC-ROUNDTRIP ' "$OUT/run.txt") <(grep -E '^SC-ROUNDTRIP ' "$OUT/chan.txt") || true
	exit 1
fi

grep -E '^SC-' "$OUT/chan.txt"
echo "PASS — the settings round-tripped identically on both legs, the byte channel served $served"
echo "       operation(s), and no settings I/O ran on the interaction tier"
