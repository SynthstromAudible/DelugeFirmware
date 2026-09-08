#!/usr/bin/env bash
# path_operations.sh — the path-operations gate.
#
# Drives real SysEx path requests and the browser's folder creation.
#
# What the checks below cover, and why each is separate:
#   PO-SYSEX     that the protocol still ANSWERS. A real request for each converted verb must produce
#                its ^verb reply, captured from a loopback MIDICable rather than inferred from the
#                filesystem -- asserting the effect alone would pass for a conversion that stopped
#                replying, which is exactly what deferring a reply risks. Passes before AND after.
#   PO-MKDIRALL  that a nested path with all three segments missing is created by ONE request, and
#                that a second creation of an existing path is success rather than EXISTS.
#   PO-TIER      that no browser folder creation ran on the interaction tier. The browser half's
#                EVIDENCE: non-zero before the conversion, zero after.
#   PO-CHANNEL   that the async arm was EXECUTED. Required non-zero on the channel-on leg, and it
#                carries more weight here than on any earlier gate: smsysex is ALREADY
#                owner-dispatched, so its tier count reads zero on both sides and this counter is the
#                whole of its evidence.
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/path-operations}"
# The channel-on leg's own target dir. Separate because the two legs differ by a cargo FEATURE, and
# sharing a target dir would make each run rebuild the other's artefacts every time.
CHAN_TARGET_DIR="${CHAN_TARGET_DIR:-$CRATE_DIR/target/path-operations-on}"

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

for marker in PO-SYSEX PO-MKDIRALL PO-CHANNEL PO-TIER; do
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

grep -E '^PO-' "$OUT/run.txt"

# --- the channel-on leg ------------------------------------------------------
#
# The leg above proves the tier fix, which the owner-dispatch arm delivers on its own. It says
# NOTHING about the channel: with the byte channel compiled out, every consumer falls back to that
# arm, and the fallback is correct and silent. A channel that never worked would leave the leg above
# green and every settings file intact.
#
# So this leg builds the same scenario against a binary with `async_file_bytes_active` and requires
# PO-CHANNEL to be non-zero. That is the check that the async arm is EXECUTED, not merely compiled --
# rung 3's livelock survived review because nothing anywhere ran the channel, and the first
# channel-on run of this gate reported `served=0` because the module had not been added to the
# harness's own module list. Both legs must also agree on the round-tripped value: a channel that
# runs but corrupts is no better than one that never runs.
echo "path operations: running the channel-on leg (async_file_bytes_active, byte worker spawned)"
set +e
GOLDEN_SCENARIO=path_operations "$CHAN_BIN" >"$OUT/chan.txt" 2>"$OUT/chan.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the channel-on leg exited $status"
	grep -E '^PO-' "$OUT/chan.txt" || sed -n '1,60p' "$OUT/chan.txt"
	sed -n '1,25p' "$OUT/chan.err" >&2
	exit 1
fi

if ! grep -q '^PO-RESULT failures=0$' "$OUT/chan.txt"; then
	echo "FAIL — in-process failures on the channel-on leg:"
	grep -E '^PO-' "$OUT/chan.txt"
	exit 1
fi

served="$(sed -n 's/^PO-CHANNEL served=\([0-9]*\)$/\1/p' "$OUT/chan.txt")"
if [ -z "$served" ]; then
	echo "FAIL — no PO-CHANNEL line on the channel-on leg"
	grep -E '^PO-' "$OUT/chan.txt"
	exit 1
fi
if [ "$served" -eq 0 ]; then
	echo "FAIL — PO-CHANNEL served=0: the byte channel served nothing, so every settings operation"
	echo "       fell back to the owner arm and this leg tested the same code as the one above."
	grep -E '^PO-' "$OUT/chan.txt"
	exit 1
fi

# Both legs must agree that no fail-safe I/O ran on the interaction tier. The channel leg takes a
# different code path to get there, so a leg that fixed the tier only on one arm would show up here.
# Both legs must agree on every reply the protocol produced. The channel leg reaches those replies by
# a different route, so a conversion that answered differently on one arm shows up here.
if ! diff <(grep -E '^PO-SYSEX ' "$OUT/run.txt") <(grep -E '^PO-SYSEX ' "$OUT/chan.txt") >/dev/null; then
	echo "FAIL — the two legs disagree on what the protocol replied:"
	diff <(grep -E '^PO-SYSEX ' "$OUT/run.txt") <(grep -E '^PO-SYSEX ' "$OUT/chan.txt") || true
	exit 1
fi
if ! diff <(grep -E '^PO-TIER ' "$OUT/run.txt") <(grep -E '^PO-TIER ' "$OUT/chan.txt") >/dev/null; then
	echo "FAIL — the two legs disagree on the interaction-tier count:"
	diff <(grep -E '^PO-TIER ' "$OUT/run.txt") <(grep -E '^PO-TIER ' "$OUT/chan.txt") || true
	exit 1
fi

grep -E '^PO-' "$OUT/chan.txt"
echo "PASS — the protocol replied identically on both legs, no browser folder creation ran on"
echo "       the interaction tier, and the byte channel served $served operation(s)"
