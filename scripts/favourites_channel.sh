#!/usr/bin/env bash
# favourites_channel.sh — the favourites-channel gate.
#
# Drives the favourites bank through its real entry points and requires that no bank is ever written
# with less than it held, while counting every favourites card operation that began on the interaction
# tier.
#
#   FV-SEED       precondition: bank 2 was seeded, or the stale check proves nothing
#   FV-ABSENT     opening a bank with no file leaves no file behind
#   FV-REFUSE     an edit made while its bank is loading changes nothing and writes nothing
#   FV-ROUNDTRIP  a favourite survives leaving its bank and coming back
#   FV-ORDER      a save made just before switching away and back lands before the bank is read again
#   FV-STALE      a bank's answer that arrives after another bank was selected is never applied
#   FV-TIER       no favourites card I/O began on the interaction tier
#   FV-CHANNEL    the bank's operations went through the byte channel
#   FV-ACCOUNTED  every issued operation ended on exactly one arm
#   FV-ILLEGAL    no operation was refused for want of a legal place to run
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count cannot
# show that, because it counts only assertions that DID run.
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
	# Cargo does not track the C++ sources the harness links against, so force the object closure first.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT="$(mktemp -d "${TMPDIR:-/tmp}/favourites_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

echo "favourites channel: running"
set +e
GOLDEN_SCENARIO=favourites_channel "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e
if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^FV-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	sed -n '1,25p' "$OUT/run.err" >&2
	exit 1
fi

for marker in FV-SEED FV-ABSENT FV-REFUSE FV-ROUNDTRIP FV-ORDER FV-STALE FV-TIER FV-CHANNEL FV-ACCOUNTED FV-ILLEGAL FV-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^FV-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^FV-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^FV-' "$OUT/run.txt"
	exit 1
fi

# FV-CHANNEL. The bank's operations must have gone through the byte channel: a channel that is compiled
# but never runs would otherwise leave every other check here green.
fav="$(sed -n 's/^FV-CHANNEL served=[0-9]* favourites=\([0-9]*\)$/\1/p' "$OUT/run.txt")"
if [ -z "$fav" ] || [ "$fav" -eq 0 ]; then
	echo "FAIL FV-CHANNEL — no favourites operation went through the channel: $(grep '^FV-CHANNEL ' "$OUT/run.txt" || echo 'no line')"
	grep -E '^FV-' "$OUT/run.txt"
	exit 1
fi

grep -E '^FV-' "$OUT/run.txt"
echo "PASS — the favourites bank round-tripped through the byte channel, never applied a stale bank,"
echo "       never wrote a bank it had not loaded, and did no card I/O on the interaction tier"
