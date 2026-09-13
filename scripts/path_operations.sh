#!/usr/bin/env bash
# path_operations.sh — the path-operations gate.
#
# Drives real SysEx path requests, the browser's folder creation, and deleting a file from a browser.
#
# What the checks below cover, and why each is separate:
#   PO-SYSEX     that the protocol still ANSWERS. A real request for each converted verb must produce
#                its ^verb reply, captured from a loopback MIDICable rather than inferred from the
#                filesystem -- asserting the effect alone would pass for a conversion that stopped
#                replying, which is exactly what deferring a reply risks.
#   PO-MKDIRALL  that a nested path with all three segments missing is created by ONE request, and
#                that a second creation of an existing path is success rather than EXISTS.
#   PO-OWNER-FALLBACK
#                that a read on the storage owner, entered on a missing folder whose default folder is missing
#                too, creates the default folder and lists it. No shipped listing reaches that arm, and the
#                synchronous open it makes ignores the directory channel's create flag.
#   PO-DELETE    that a file deleted from a browser goes through the byte channel: the delete is still pending a
#                step after the menu accepts it, input that would close the menu is refused meanwhile, and then
#                the file is gone from the card and the listing.
#   PO-DELETE-ABSENT
#                that deleting a listed file already gone from the card ends as missing, not removed, and a
#                plain file stays listed, as it always has.
#   PO-DELETE-STALL
#                that a delete the channel never answers ends as failed, with the file still listed and the menu
#                closed.
#   PO-TIER      that no browser folder creation or delete ran on the interaction tier.
#   PO-ILLEGAL   that no folder creation or delete was refused for want of a legal place to run.
#   PO-CHANNEL   that the async arm was EXECUTED. Required non-zero: smsysex's tier count reads zero
#                whichever arm carries it, so this counter is the whole of its evidence.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/path-operations}"

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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/path_operations.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

echo "path operations: running"
set +e
GOLDEN_SCENARIO=path_operations "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^PO-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	sed -n '1,25p' "$OUT/run.err" >&2
	exit 1
fi

# An abort (a tier assertion firing) exits before this line is ever printed, which is what turns a
# freeze into a gate failure rather than a silently truncated log.
if ! grep -q '^PO-RESULT ' "$OUT/run.txt"; then
	echo "FAIL — no PO-RESULT line; the scenario did not run to completion"
	sed -n '1,60p' "$OUT/run.txt"
	exit 1
fi

for marker in PO-SYSEX PO-MKDIRALL PO-OWNER-FALLBACK PO-DELETE PO-DELETE-ABSENT PO-DELETE-STALL PO-CHANNEL \
	PO-TIER PO-ILLEGAL; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^PO-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^PO-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^PO-' "$OUT/run.txt"
	exit 1
fi

served="$(sed -n 's/^PO-CHANNEL served=\([0-9]*\)$/\1/p' "$OUT/run.txt")"
if [ -z "$served" ] || [ "$served" -eq 0 ]; then
	echo "FAIL — PO-CHANNEL served=${served:-none}: the byte channel served nothing, so the async arm never ran"
	grep -E '^PO-' "$OUT/run.txt"
	exit 1
fi

grep -E '^PO-' "$OUT/run.txt"
echo "PASS — the protocol replied, browser deletes went through the byte channel, no browser folder creation or"
echo "       delete ran on the interaction tier, and the byte channel served $served operation(s)"
