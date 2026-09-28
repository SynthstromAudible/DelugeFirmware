#!/usr/bin/env bash
# document_save_channel.sh — the document-save channel gate.
#
# Saves a preset through the REAL ENTER gesture and requires that the document was written whole, read
# back through the channel, and stored through the write channel after boot.
#
# ## Why this gate exists
#
# No other harness builds `write_file`, and a feature no gate builds is code that has never run.
#
# One leg: the write channel has no feature switch, so there is no second arm to run.
#
# What the checks cover:
#   DS-CHANNEL    the document write was SERVED by the channel -- the async arm ran.
#   DS-TICKED   the save's steps went through a channel after boot. The gate's actual claim.
#   DS-HELD       a preset save whose write the channel holds far past the stall bound still completes and verifies:
#                 a running write cannot be cancelled, so giving up on it only releases the serialiser it is still
#                 copying from.
#   DS-VERIFIED   every checked save COMPLETED a read-back -- counted where a save genuinely succeeds,
#                 because a rejected write has already left its file on the card.
#   DS-VERIFY-ARM the read-back ran through the channel and never on the inline arm.
#   DS-KITROW     a kit row was saved and verified. No other gate saves one.
#   DS-GUARD-SONG a preset save dispatched while a song save's collection or document machine runs -- with
#                 the save guard already released -- is refused, and the song document still lands whole.
#   DS-ACCOUNTED  a read-back the channel answered, and that then failed the save, still counts as served:
#                 the file is removed between the write landing and the read-back's first answer.
#   DS-OVERWRITE  a confirmed overwrite writes a temp file beside its target, verifies it there, and swaps it into
#                 place: the name holds a whole document and no temp file is left.
#   DS-OVERWRITE-FAIL
#                 a confirmed overwrite whose temp file is truncated before its read-back fails the save, leaves the
#                 document it was replacing byte-for-byte intact, and removes the temp file.
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/document_save_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

echo "document save channel: running"
set +e
GOLDEN_SCENARIO=document_save_channel "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e
if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^DS-' "$OUT/run.txt" || sed -n '1,40p' "$OUT/run.txt"
	sed -n '1,40p' "$OUT/run.err" >&2
	exit 1
fi
# An abort or a wedge exits before these lines are printed, which is what turns a freeze into a gate
# failure rather than a silently truncated log.
for marker in DS-BACKMIDSAVE DS-EXITMIDSAVE DS-SAVED DS-FINISHED DS-OCCUPIED DS-GUARD DS-KITROW DS-KITFINISHED DS-GUARD-SONG DS-ACCOUNTED DS-OVERWRITE DS-OVERWRITE-FAIL DS-HELD DS-VERIFIED DS-SUMMARY DS-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^DS-' "$OUT/run.txt"
		exit 1
	fi
done
if ! grep -q '^DS-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^DS-RESULT' "$OUT/run.txt")"
	fail=1
fi

served="$(sed -n 's/^DS-SUMMARY served=\([0-9]*\) .*/\1/p' "$OUT/run.txt")"
channel_ticks="$(sed -n 's/^DS-SUMMARY .*channelTicks=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
vserved="$(sed -n 's/^DS-SUMMARY .*verifyServed=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
vinline="$(sed -n 's/^DS-SUMMARY .*verifyInline=\([0-9]*\).*/\1/p' "$OUT/run.txt")"

# DS-CHANNEL. The async arm actually RAN.
if [ -z "${served:-}" ] || [ "$served" -eq 0 ]; then
	echo "FAIL DS-CHANNEL — the save served ${served:-no} document write(s); the async arm never ran"
	fail=1
fi

# DS-TICKED. The gate's actual claim: `channelTicks` counts ticks on which a staged step went through a channel.
if [ -z "${channel_ticks:-}" ] || [ "$channel_ticks" -eq 0 ]; then
	echo "FAIL DS-TICKED — the save never ran through the channel across ticks"
	fail=1
fi

# DS-VERIFIED. The preset, the kit row, the confirmed overwrite and the held save are checked, and all four completed. The
# counters are taken where a save genuinely succeeds, so a save that wrote its file and was then
# rejected by the read-back -- DS-ACCOUNTED's and DS-OVERWRITE-FAIL's -- cannot be counted, which a check on
# the file's presence alone would miss.
if ! grep -q '^DS-VERIFIED completedSaves=4 verified=4$' "$OUT/run.txt"; then
	echo "FAIL DS-VERIFIED — $(grep '^DS-VERIFIED ' "$OUT/run.txt" || echo 'no DS-VERIFIED line')"
	fail=1
fi

# DS-BACKMIDSAVE / DS-EXITMIDSAVE. The staged preset save spans ticks and ends in close(); leaving its screen
# mid-save must be refused, and must be attempted while the save is genuinely running.
grep -q '^DS-BACKMIDSAVE inFlight=1 back=1$' "$OUT/run.txt" || { echo "FAIL DS-BACKMIDSAVE — $(grep '^DS-BACKMIDSAVE ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-EXITMIDSAVE exit=1$' "$OUT/run.txt" || { echo "FAIL DS-EXITMIDSAVE — $(grep '^DS-EXITMIDSAVE ' "$OUT/run.txt" || echo 'no line')"; fail=1; }

# DS-FINISHED / DS-KITFINISHED / DS-GUARD. What a save does AFTER the document lands, and what it
# refuses BEFORE. A staged save can skip all of it -- close, confirm, rename, and the check that
# refuses a slot another instrument in the song already holds -- with every byte on the card still
# correct, so nothing that compares bytes can see it.
grep -q '^DS-FINISHED closed=1 named=1$' "$OUT/run.txt" || { echo "FAIL DS-FINISHED — $(grep '^DS-FINISHED ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-KITFINISHED closed=1 named=1 path=SYNTHS$' "$OUT/run.txt" || { echo "FAIL DS-KITFINISHED — $(grep '^DS-KITFINISHED ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-GUARD refused=1 unchanged=1$' "$OUT/run.txt" || { echo "FAIL DS-GUARD — $(grep '^DS-GUARD ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-GUARD-SONG tried=1 refused=1 songSaved=1 presetWritten=0$' "$OUT/run.txt" || { echo "FAIL DS-GUARD-SONG — $(grep '^DS-GUARD-SONG ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-ACCOUNTED tampered=1 failed=1 verifyServed=1$' "$OUT/run.txt" || { echo "FAIL DS-ACCOUNTED — $(grep '^DS-ACCOUNTED ' "$OUT/run.txt" || echo 'no line')"; fail=1; }

# DS-VERIFY-ARM. The read-back rides the channel, and none runs on the inline arm (`verifyInline`).
if [ "${vserved:-0}" = "0" ] || [ "${vinline:-1}" != "0" ]; then
	echo "FAIL DS-VERIFY-ARM — the read-back did not run through the channel (verifyServed=${vserved:-none} verifyInline=${vinline:-none})"
	fail=1
fi

grep -E '^DS-' "$OUT/run.txt"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
grep -hE '^TIER-SYNC(-AT)? ' "$OUT/run.txt" || true
echo "PASS — the document was written whole and read back through the channel, a taken"
echo "       name was refused rather than replaced, and a confirmed overwrite kept the old document until its"
echo "       replacement was verified"
