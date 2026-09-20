#!/usr/bin/env bash
# startup_failsafe.sh — the startup fail-safe gate.
#
# Drives the REAL startup fail-safe and requires that none of its file I/O begins on the interaction
# tier.
#
# What the checks below cover, and why each is separate:
#   SF-ORDER    the machine never advances past the canary write without a terminal answer, and the canary
#               is on the card before the load is allowed to proceed.
#   SF-TIER     that no fail-safe file operation ran on the interaction tier. This is the EVIDENCE.
#   SF-CHANNEL  that the async arm was EXECUTED, not merely compiled. Required non-zero: a channel that is
#               compiled but never runs would otherwise leave this gate green.
#   SF-ILLEGAL  that no fail-safe operation was refused for want of a legal place to run.
#   SF-GIVEUP   with the channel holding the machine's first request far past the stall bound, the machine
#               gives up: exactly one refused operation, no canary written, no interaction-tier I/O, finished
#               within its bound.
#
# The fail-safe's happy path ends in openUI(&loadSongUI), and this harness has no UI stack, so the
# scenario drives the song-missing path instead: the canary creation, the existence checks and the
# cleanup unlink, then a return before the UI. Which TIER those run on does not depend on which
# branch reached them.
#
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
	# Cargo does not track the C++ sources the harness links against (this scenario is one of them),
	# so force the object closure first — same reason save_roundtrip.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT="$(mktemp -d "${TMPDIR:-/tmp}/startup_failsafe.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

echo "startup fail-safe: running"
set +e
GOLDEN_SCENARIO=startup_failsafe "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^SF-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	sed -n '1,25p' "$OUT/run.err" >&2
	exit 1
fi

# An abort (a tier assertion firing) exits before this line is ever printed, which is what turns a
# freeze into a gate failure rather than a silently truncated log.
if ! grep -q '^SF-RESULT ' "$OUT/run.txt"; then
	echo "FAIL — no SF-RESULT line; the scenario did not run to completion"
	sed -n '1,60p' "$OUT/run.txt"
	exit 1
fi

for marker in SF-ORDER SF-CHANNEL SF-TIER SF-ILLEGAL SF-INLINE SF-GIVEUP; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^SF-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^SF-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^SF-' "$OUT/run.txt"
	exit 1
fi

served="$(sed -n 's/^SF-CHANNEL served=\([0-9]*\)$/\1/p' "$OUT/run.txt")"
if [ -z "$served" ] || [ "$served" -eq 0 ]; then
	echo "FAIL — SF-CHANNEL served=${served:-none}: the byte channel served nothing, so the async arm never ran"
	grep -E '^SF-' "$OUT/run.txt"
	exit 1
fi

grep -E '^SF-' "$OUT/run.txt"
echo "PASS — no startup fail-safe I/O ran on the interaction tier, and the byte channel served"
echo "       $served operation(s)"
