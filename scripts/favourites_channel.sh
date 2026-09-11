#!/usr/bin/env bash
# favourites_channel.sh — the favourites-channel gate.
#
# Drives the favourites bank through its real entry points on two builds and requires that no bank is
# ever written with less than it held, while counting every favourites card operation that began on the
# interaction tier.
#
#   FV-SEED       precondition: bank 2 was seeded, or the stale check proves nothing
#   FV-ABSENT     opening a bank with no file leaves no file behind
#   FV-REFUSE     an edit made while its bank is loading changes nothing and writes nothing
#   FV-ROUNDTRIP  a favourite survives leaving its bank and coming back; the file's CRC must match ACROSS legs
#   FV-ORDER      a save made just before switching away and back lands before the bank is read again
#   FV-STALE      a bank's answer that arrives after another bank was selected is never applied
#   FV-TIER       no favourites card I/O began on the interaction tier
#   FV-CHANNEL    the channel-on leg's bank operations went through the channel; the off leg's did not
#   FV-ACCOUNTED  every issued operation ended on exactly one arm
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count cannot
# show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/favourites-channel}"
CHAN_TARGET_DIR="${CHAN_TARGET_DIR:-$CRATE_DIR/target/favourites-channel-on}"

if [ "${NO_BUILD:-0}" != "1" ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against, so force the object closure first.
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/favourites_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

fail=0
run_leg() { # $1 = leg name, $2 = binary
	local name="$1" bin="$2" status
	echo "favourites channel: running the $name leg"
	set +e
	GOLDEN_SCENARIO=favourites_channel "$bin" >"$OUT/$name.txt" 2>"$OUT/$name.err"
	status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		echo "FAIL [$name] — the run exited $status"
		grep -E '^FV-' "$OUT/$name.txt" || sed -n '1,60p' "$OUT/$name.txt"
		sed -n '1,25p' "$OUT/$name.err" >&2
		fail=1
		return
	fi
	for marker in FV-SEED FV-ABSENT FV-REFUSE FV-ROUNDTRIP FV-ORDER FV-STALE FV-TIER FV-CHANNEL FV-ACCOUNTED FV-RESULT; do
		if ! grep -q "^$marker " "$OUT/$name.txt"; then
			echo "FAIL [$name] — no $marker line; that stage did not run"
			grep -E '^FV-' "$OUT/$name.txt"
			fail=1
			return
		fi
	done
	if ! grep -q '^FV-RESULT failures=0$' "$OUT/$name.txt"; then
		echo "FAIL [$name] — in-process failures:"
		grep -E '^FV-' "$OUT/$name.txt"
		fail=1
	fi
}

run_leg off "$BIN"
run_leg on "$CHAN_BIN"

# FV-CHANNEL. With the byte channel compiled out every operation takes the owner arm, which is correct and
# silent -- so a channel that never worked would leave the off leg green. The on leg must have carried the
# bank's operations through the channel, and the off leg must have carried none.
if [ -f "$OUT/off.txt" ] && ! grep -q '^FV-CHANNEL served=0 favourites=0$' "$OUT/off.txt"; then
	echo "FAIL FV-CHANNEL — the channel-off leg served operations: $(grep '^FV-CHANNEL ' "$OUT/off.txt" || echo 'no line')"
	fail=1
fi
if [ -f "$OUT/on.txt" ]; then
	on_line="$(grep '^FV-CHANNEL ' "$OUT/on.txt" || true)"
	on_fav="$(sed -n 's/^FV-CHANNEL served=[0-9]* favourites=\([0-9]*\)$/\1/p' "$OUT/on.txt")"
	if [ -z "$on_fav" ] || [ "$on_fav" -eq 0 ]; then
		echo "FAIL FV-CHANNEL — the channel-on leg carried no favourites operation through the channel: ${on_line:-no line}"
		fail=1
	fi
fi

# FV-ROUNDTRIP across legs: the same bank must be written byte-identically whichever arm carried it.
if [ -f "$OUT/off.txt" ] && [ -f "$OUT/on.txt" ]; then
	if ! diff <(grep -E '^FV-ROUNDTRIP ' "$OUT/off.txt") <(grep -E '^FV-ROUNDTRIP ' "$OUT/on.txt") >/dev/null; then
		echo "FAIL FV-ROUNDTRIP — the two legs wrote different banks:"
		diff <(grep -E '^FV-ROUNDTRIP ' "$OUT/off.txt") <(grep -E '^FV-ROUNDTRIP ' "$OUT/on.txt") || true
		fail=1
	fi
fi

# `|| true`: under pipefail a leg that printed no FV- line (an abort) would otherwise end the script here,
# before the other leg's lines -- the ones that say what DID happen -- are ever shown.
[ -f "$OUT/off.txt" ] && grep -E '^FV-' "$OUT/off.txt" | sed 's/^/[off] /' || true
[ -f "$OUT/on.txt" ] && grep -E '^FV-' "$OUT/on.txt" | sed 's/^/[on]  /' || true

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "PASS — the favourites bank round-tripped identically on both legs, never applied a stale bank,"
echo "       never wrote a bank it had not loaded, and did no card I/O on the interaction tier"
