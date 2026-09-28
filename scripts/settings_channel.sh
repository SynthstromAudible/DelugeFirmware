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
#   SC-HEAD       every settings file the channel writes begins with the XML declaration.
#   SC-FITS       every settings document fits the channel's 8 KiB bound.
#   SC-UNLINK     a MIDI device table with nothing worth keeping removes its file through the channel.
#   SC-PERF       performance view's defaults, read after boot, are read through the channel.
#   SC-OVERSIZE   a settings write over that bound is refused after boot: counted, not written, not dispatched.
#   SC-FALLBACK   a busy channel queues -- nothing a burst submits is refused.
#   SC-CHANNEL    the channel SERVED operations. The byte channel has no feature switch, so there is no
#                 second build to compare against; this counter is what says the async arm ran rather than
#                 merely compiled -- a harness that left the channel's module out of its own module list
#                 would report `served=0` with every other check green.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
# Where the harness binary is: `dbt test` builds it once, before any gate runs, into CARGO_TARGET_DIR. Every
# scenario gate runs the same binary; GOLDEN_SCENARIO picks the scenario at run time.
TARGET_DIR="${TARGET_DIR:-${CARGO_TARGET_DIR:-$CRATE_DIR/target}}"
# An absolute CARGO_TARGET_DIR arrives ready to use; a relative one is relative to the repo root.
case "$TARGET_DIR" in /*) ;; *) TARGET_DIR="$REPO_ROOT/$TARGET_DIR" ;; esac

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' is not built -- run this gate through ./dbt test, which builds it first"
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

for marker in SC-ROUNDTRIP SC-BURST SC-ORDER SC-HEAD SC-FITS SC-UNLINK SC-PERF SC-OVERSIZE SC-FALLBACK SC-TIER SC-CHANNEL; do
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
grep -hE '^TIER-SYNC(-AT)? ' "$OUT/run.txt" || true
echo "PASS — the settings round-tripped, the byte channel served $served operation(s), and no"
echo "       settings I/O ran on the interaction tier"
