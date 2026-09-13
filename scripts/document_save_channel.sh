#!/usr/bin/env bash
# document_save_channel.sh — the document-save channel gate.
#
# Saves a preset through the REAL ENTER gesture and requires that the document was written whole, read
# back through the channel, and stored off the owner.
#
# ## Why this gate exists
#
# `write_file` began life with NO harness building it -- exactly where the whole-file READ was one
# rung earlier, and running THAT for the first time found two defects at once. A feature no gate
# builds is code that has never run. Building this one immediately found two more: the no-channel arm
# handed the write to a function that no longer wrote anything, and the counter this rung is measured
# by was never incremented.
#
# One leg: the write channel has no feature switch, so the no-channel arm the old second leg exercised
# no longer exists.
#
# What the checks cover:
#   DS-CHANNEL    the document write was SERVED by the channel -- the async arm ran.
#   DS-OFFOWNER   the save ran OFF the storage owner. The rung's actual claim.
#   DS-VERIFIED   both checked saves COMPLETED a read-back -- counted where a save genuinely succeeds,
#                 because a rejected write has already left its file on the card.
#   DS-VERIFY-ARM the read-back ran through the channel and never on the owner.
#   DS-KITROW     a kit row was saved and verified. No gate had ever saved one before.
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

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/document-save-channel}"

if [ "${NO_BUILD:-0}" != "1" ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against, so force the object closure
	# first — same reason recorder_write.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$REPO_ROOT/$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
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
for marker in DS-BACKMIDSAVE DS-EXITMIDSAVE DS-SAVED DS-FINISHED DS-OCCUPIED DS-GUARD DS-KITROW DS-KITFINISHED DS-GUARD-SONG DS-ACCOUNTED DS-OVERWRITE DS-OVERWRITE-FAIL DS-VERIFIED DS-SUMMARY DS-RESULT; do
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
offowner="$(sed -n 's/^DS-SUMMARY .*offOwnerTicks=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
vserved="$(sed -n 's/^DS-SUMMARY .*verifyServed=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
vowner="$(sed -n 's/^DS-SUMMARY .*verifyOwner=\([0-9]*\).*/\1/p' "$OUT/run.txt")"

# DS-CHANNEL. The async arm actually RAN.
if [ -z "${served:-}" ] || [ "$served" -eq 0 ]; then
	echo "FAIL DS-CHANNEL — the save served ${served:-no} document write(s); the async arm never ran"
	fail=1
fi

# DS-OFFOWNER. The rung's actual claim.
if [ -z "${offowner:-}" ] || [ "$offowner" -eq 0 ]; then
	echo "FAIL DS-OFFOWNER — the save never ran off the storage owner"
	fail=1
fi

# DS-VERIFIED. The preset, the kit row and the confirmed overwrite are checked, and all three completed. The
# counters are taken where a save genuinely succeeds, so a save that wrote its file and was then
# rejected by the read-back -- DS-ACCOUNTED's and DS-OVERWRITE-FAIL's -- cannot be counted, which a check on
# the file's presence alone would miss.
if ! grep -q '^DS-VERIFIED completedSaves=3 verified=3$' "$OUT/run.txt"; then
	echo "FAIL DS-VERIFIED — $(grep '^DS-VERIFIED ' "$OUT/run.txt" || echo 'no DS-VERIFIED line')"
	fail=1
fi

# DS-BACKMIDSAVE / DS-EXITMIDSAVE. The staged preset save spans ticks and ends in close(); leaving its screen
# mid-save must be refused, and must be attempted while the save is genuinely running.
grep -q '^DS-BACKMIDSAVE inFlight=1 back=1$' "$OUT/run.txt" || { echo "FAIL DS-BACKMIDSAVE — $(grep '^DS-BACKMIDSAVE ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-EXITMIDSAVE exit=1$' "$OUT/run.txt" || { echo "FAIL DS-EXITMIDSAVE — $(grep '^DS-EXITMIDSAVE ' "$OUT/run.txt" || echo 'no line')"; fail=1; }

# DS-FINISHED / DS-KITFINISHED / DS-GUARD. What a save does AFTER the document lands, and what it
# refuses BEFORE. Staging the saves skipped all of it -- close, confirm, rename, and the check that
# refuses a slot another instrument in the song already holds -- and every byte on the card was still
# correct, so nothing that compared bytes could see it.
grep -q '^DS-FINISHED closed=1 named=1$' "$OUT/run.txt" || { echo "FAIL DS-FINISHED — $(grep '^DS-FINISHED ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-KITFINISHED closed=1 named=1 path=SYNTHS$' "$OUT/run.txt" || { echo "FAIL DS-KITFINISHED — $(grep '^DS-KITFINISHED ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-GUARD refused=1 unchanged=1$' "$OUT/run.txt" || { echo "FAIL DS-GUARD — $(grep '^DS-GUARD ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-GUARD-SONG tried=1 refused=1 songSaved=1 presetWritten=0$' "$OUT/run.txt" || { echo "FAIL DS-GUARD-SONG — $(grep '^DS-GUARD-SONG ' "$OUT/run.txt" || echo 'no line')"; fail=1; }
grep -q '^DS-ACCOUNTED tampered=1 failed=1 verifyServed=1$' "$OUT/run.txt" || { echo "FAIL DS-ACCOUNTED — $(grep '^DS-ACCOUNTED ' "$OUT/run.txt" || echo 'no line')"; fail=1; }

# DS-VERIFY-ARM. The read-back rides the channel, and nothing goes to the owner.
if [ "${vserved:-0}" = "0" ] || [ "${vowner:-1}" != "0" ]; then
	echo "FAIL DS-VERIFY-ARM — the read-back did not run through the channel (verifyServed=${vserved:-none} verifyOwner=${vowner:-none})"
	fail=1
fi

grep -E '^DS-' "$OUT/run.txt"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "PASS — the document was written whole off the storage owner and read back through the channel, a taken"
echo "       name was refused rather than replaced, and a confirmed overwrite kept the old document until its"
echo "       replacement was verified"
