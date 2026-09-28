#!/usr/bin/env bash
# offset_io.sh — the offset-addressed I/O gate.
#
# Drives a real multi-block SysEx file transfer and a whole-file copy.
#
# What the checks below cover, and why each is separate:
#   OI-TRANSFER  that THREE blocks written at ascending offsets are all still on the card. Three, not
#                one: a write_at that truncated would leave only the last block, and a single-block round
#                trip would pass against exactly that bug -- efatfs_core::create_context truncates
#                unconditionally, and the correct open mode looks like a mistake.
#   OI-COPY      that a copy reproduces its source byte-for-byte, and that copying a missing source
#                reports an error. Without the error case, a handler replying before its operation
#                finished reports the same err=0 a correct one does.
#   OI-DIR       that the directory listing verb answers.
#   OI-CHANNEL   that the async arm was EXECUTED. Required non-zero.
#   OI-ILLEGAL   that no SysEx operation above was refused for want of a legal place to run.
#   OI-GIVEUP    that a copy whose channel operation is held far past the stall bound is answered with an
#                error: exactly one refused operation, and nothing copied.
#   OI-DIR-STALL that a directory listing whose open is held far past the handler's stall bound is answered with
#                the timeout (15) and no entries, instead of holding the SysEx queue for good.
#   OI-TIER      that SMSysex opened no directory synchronously on the interaction tier after boot. Required 0.
#
# Note: OI-TRANSFER re-reads the card file directly, never the bytes a channel block read returned; the
# block-read copy is covered by song-save-channel's and document-save-channel's read-back checks.
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/offset_io.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

echo "offset io: running"
set +e
GOLDEN_SCENARIO=offset_io "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^OI-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	sed -n '1,25p' "$OUT/run.err" >&2
	exit 1
fi

# An abort (a tier assertion firing) exits before this line is ever printed, which is what turns a
# freeze into a gate failure rather than a silently truncated log.
if ! grep -q '^OI-RESULT ' "$OUT/run.txt"; then
	echo "FAIL — no OI-RESULT line; the scenario did not run to completion"
	sed -n '1,60p' "$OUT/run.txt"
	exit 1
fi

for marker in OI-TRANSFER OI-COPY OI-DIR OI-CHANNEL OI-ILLEGAL OI-GIVEUP OI-DIR-STALL OI-TIER; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^OI-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^OI-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^OI-' "$OUT/run.txt"
	exit 1
fi

served="$(sed -n 's/^OI-CHANNEL served=\([0-9]*\)$/\1/p' "$OUT/run.txt")"
if [ -z "$served" ] || [ "$served" -eq 0 ]; then
	echo "FAIL — OI-CHANNEL served=${served:-none}: the byte channel served nothing, so the async arm never ran"
	grep -E '^OI-' "$OUT/run.txt"
	exit 1
fi

grep -E '^OI-' "$OUT/run.txt"
grep -hE '^TIER-SYNC(-AT)? ' "$OUT/run.txt" || true
echo "PASS — the transfer and the copy reached the card, and the byte channel served $served"
echo "       operation(s)"
