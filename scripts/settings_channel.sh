#!/usr/bin/env bash
# settings_channel.sh — the settings-channel gate.
#
# Writes a settings file through the REAL consumer entry point, reads it back, and requires the value
# to survive -- while counting every settings operation that began on the interaction tier.
#
# What the checks below cover, and why each is separate:
#   SC-ROUNDTRIP  the settings survive. The safety half: it catches a change that moved the I/O and broke
#                 the settings on the way.
#   SC-TIER       that no settings operation ran on the interaction tier. The evidence half.
#   SC-BURST      a burst of three writes, the first already in flight, lands the last value in exactly two
#                 card writes: the waiting write is replaced, the in-flight one never is.
#   SC-ORDER      a read queued behind a write to the same file sees that write.
#   SC-FALLBACK   a busy channel queues -- nothing reaches the storage owner.
#   SC-CHANNEL    the channel SERVED operations. The byte channel has no feature switch, so there is no
#                 second build to compare against; this counter is what says the async arm ran rather than
#                 merely compiled -- the first channel-on run of this gate reported `served=0` because the
#                 module was missing from the harness's own module list.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/settings-channel}"

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
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

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

for marker in SC-ROUNDTRIP SC-BURST SC-ORDER SC-FALLBACK SC-TIER SC-CHANNEL; do
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

served="$(sed -n 's/^SC-CHANNEL served=\([0-9]*\)$/\1/p' "$OUT/run.txt")"
if [ -z "$served" ] || [ "$served" -eq 0 ]; then
	echo "FAIL — SC-CHANNEL served=${served:-none}: the byte channel served nothing, so the async arm never ran"
	grep -E '^SC-' "$OUT/run.txt"
	exit 1
fi

grep -E '^SC-' "$OUT/run.txt"
echo "PASS — the settings round-tripped, the byte channel served $served operation(s), and no"
echo "       settings I/O ran on the interaction tier"
