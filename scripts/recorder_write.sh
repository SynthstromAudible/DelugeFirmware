#!/usr/bin/env bash
# recorder_write.sh — the recorder write-relocation gate.
#
# What each check covers, and why each is separate:
#   RW-ACTIVE     that this binary reaches the Rust write driver at all. A module missing from the
#                 harness's module list resolves to the C++ weak fallback, and every write would then
#                 take the synchronous arm while everything below stayed green.
#   RW-ROUNDTRIP  that a real recording still reads back byte-correctly, mid-recording AND finalized
#                 multi-cluster. It is the half that catches a conversion that relocated the write
#                 and broke the recording on the way. It earned its keep here: a recycled stream
#                 handle collecting a previous recorder's answer showed up as exactly this, and
#                 nothing else in the run noticed.
#   RW-CHANNEL    that the async arm was EXECUTED by the off-owner recordings: they must serve more
#                 writes than the on-fiber oracle. The oracle stands on the owner, which writes
#                 synchronously -- an owner that spins never lets the driver answer -- so it serves none.
#   RW-OFFOWNER   that the drain ran OFF the storage owner. This is the gate's real subject. Without
#                 it RW-CHANNEL can be non-zero while every write was still served on the owner.
#   RW-OFFOWNER-CREATE  that CREATING the recording's file ran off the storage owner.
#   RW-OFFOWNER-FINALIZE  that FINISHING a recording ran off the storage owner. A served count says
#                 operations went through the channel, not where the caller stood.
#   RW-LEAK       that an aborted recording gives its clusters back. It does NOT currently catch a
#                 premature unlink -- single-flight sends that unlink down its synchronous arm anyway
#                 -- but it does catch a cleanup that fails to delete at all.
#   RW-OFFOWNER-CASE  that a whole recording driven from a NON-fiber context -- the shape the device
#                 actually uses -- still reads back byte-for-byte identical to the on-fiber ones, and
#                 that its cluster writes went through the channel. The eight on-fiber cases cannot
#                 show this: the oracle is dispatched onto the fiber wholesale, so every one of them
#                 measures on_owner=1 and takes the synchronous arm.
#   RW-TIER       how many synchronous card operations the recorder began off the storage owner after
#                 boot, over every off-owner case, and how many of its channel requests were refused.
#                 Required 0: off the owner every recorder operation goes through a channel.
#   RW-BUSY       that a recording made while another write-channel operation keeps taking the channel
#                 is refused, asks again, never writes synchronously, and still reads back exactly.
#   RW-STALL      that a cluster write held past the recorder's bound fails the take while the write is
#                 still outstanding, keeps its buffer until the answer, closes the file through the
#                 channel, and never writes synchronously.
#   RW-DIR-STALL  that a recording whose folder scan's directory open is held past the recorder's bound fails
#                 its create with a card error, makes no file, and never opens the folder synchronously.
#   RW-DRAIN      that the offline stem-export path still drains to completion. That loop calls the
#                 card routine in a bounded loop expecting each call to COMPLETE a write.
#
#   NOT covered: the stem-export folder search. Its machine is exercised only by the offline golden
#                render, which runs ON the owner, so only its SYNCHRONOUS arm is verified. An
#                off-owner case was attempted and abandoned: routing a harness recording down the
#                stem path needs more engine state than `processStarted` +
#                `currentStemExportType` + `lastFolderNameForStemExport`, and a case that silently
#                records to RESAMPLE while calling itself a stem case is worse than none.
#
# One leg plus the offline drain: the write channel has no feature switch, so there is no build
# without it to compare against.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/recorder-write}"

if [ "${NO_BUILD:-0}" != "1" ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# Cargo does not track the C++ sources the harness links against, so force the object closure
	# first — same reason save_roundtrip.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$REPO_ROOT/$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT="$(mktemp -d "${TMPDIR:-/tmp}/recorder_write.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

echo "recorder write: running"
set +e
GOLDEN_SCENARIO=recorder_roundtrip "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e
# A failed run is still judged line by line: the give-up cases fail without stopping the tier report, and a
# freeze aborts the process, so the lines that did print say which check went red.
if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	sed -n '1,40p' "$OUT/run.err" >&2
	fail=1
fi
# An abort exits before this line is printed, which is what turns a freeze into a gate failure
# rather than a silently truncated log.
if ! grep -q '^RW-RESULT ' "$OUT/run.txt"; then
	echo "FAIL — no RW-RESULT line; the scenario did not run to completion"
	exit 1
fi
if ! grep -q '^RW-ACTIVE asyncActive=1$' "$OUT/run.txt"; then
	echo "FAIL RW-ACTIVE — $(grep '^RW-ACTIVE' "$OUT/run.txt" || echo 'no RW-ACTIVE line'); the write driver is not reachable"
	fail=1
fi
if ! grep -q '^8/8 cases passed' "$OUT/run.txt"; then
	echo "FAIL RW-ROUNDTRIP — a recording did not read back correctly"
	grep -E '^CASE|FAIL' "$OUT/run.txt" | head -20
	fail=1
fi
if ! grep -q '^RW-RESULT failures=0' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^RW-RESULT' "$OUT/run.txt")"
	fail=1
fi

# The off-owner cases run AFTER the on-fiber oracle has printed RW-SUMMARY, so their evidence is
# their own lines. There are three, and each covers a shape the eight on-fiber cases cannot.
#
# The alteration case exists because nothing else sets allowFileAlterationAfter, so without it
# finalize's whole batch machine would have no coverage.
for c in recording alterfile abort; do
	if ! grep -q "^CASE offowner_$c " "$OUT/run.txt"; then
		echo "FAIL RW-OFFOWNER-CASE — the off-owner '$c' case did not run"
		fail=1
	fi
	if ! grep -q "^CASE offowner_$c .*onOwner=0" "$OUT/run.txt"; then
		echo "FAIL RW-OFFOWNER-CASE — '$c' did not run off the owner; it proves nothing there"
		fail=1
	fi
done

oo_count=$(grep -c '^OO-RESULT ' "$OUT/run.txt" || true)
oo_ok_count=$(grep -c '^OO-RESULT ok=1 ' "$OUT/run.txt" || true)
if [ "$oo_count" -lt 2 ] || [ "$oo_ok_count" -ne "$oo_count" ]; then
	echo "FAIL RW-OFFOWNER-CASE — $oo_ok_count of $oo_count off-owner recordings read back correctly"
	grep '^OO-RESULT ' "$OUT/run.txt"
	fail=1
fi

# RW-OFFOWNER-FINALIZE: restoring tickNeedsOwner's claim over finalize drops this straight to zero
# while everything else stays green.
oo_fin=$(grep '^OO-RESULT ' "$OUT/run.txt" | sed -n 's/.*offOwnerFinalizeTicks=\([0-9]*\).*/\1/p' | sort -n | tail -1)
[ -n "$oo_fin" ] || oo_fin=0
if [ "$oo_fin" -eq 0 ]; then
	echo "FAIL RW-OFFOWNER-FINALIZE — finalize never ran off the owner; nothing relocated"
	fail=1
fi

# RW-LEAK: an aborted recording must give its clusters back.
oo_leak=$(grep '^OO-LEAK ' "$OUT/run.txt" || true)
if [ -z "$oo_leak" ]; then
	echo "FAIL RW-LEAK — no OO-LEAK line; the abort case did not finish"
	fail=1
elif ! grep -q '^OO-LEAK .*ok=1' "$OUT/run.txt"; then
	echo "FAIL RW-LEAK — $oo_leak; an aborted recording did not give its clusters back"
	fail=1
fi

# RW-TIER: reported, and required to be zero.
tier_line=$(grep '^RW-TIER ' "$OUT/run.txt" || true)
tier=$(printf '%s\n' "$tier_line" | sed -n 's/^RW-TIER violations=\([0-9]*\).*/\1/p')
if [ -z "$tier" ]; then
	echo "FAIL RW-TIER — no RW-TIER line; the off-owner cases did not report"
	fail=1
	tier=0
else
	echo "$tier_line"
	if [ "$tier" -ne 0 ]; then
		echo "FAIL RW-TIER — the recorder began $tier synchronous card operation(s) off the storage owner"
		fail=1
	fi
fi

for check in RW-BUSY RW-STALL RW-DIR-STALL; do
	line=$(grep "^$check " "$OUT/run.txt" || true)
	if [ -z "$line" ]; then
		echo "FAIL $check — no $check line; the case did not finish"
		fail=1
	else
		echo "$line"
		if ! printf '%s\n' "$line" | grep -q "^$check ok=1 "; then
			echo "FAIL $check — see the line above"
			fail=1
		fi
	fi
done

# RW-OFFOWNER-CREATE: creating the file was the LAST thing this recorder dispatched to the owner, so
# this reading zero means the recorder is back to blocking there.
oo_create=$(grep '^OO-RESULT ' "$OUT/run.txt" | sed -n 's/.*offOwnerCreateTicks=\([0-9]*\).*/\1/p' | sort -n | tail -1)
[ -n "$oo_create" ] || oo_create=0
if [ "$oo_create" -eq 0 ]; then
	echo "FAIL RW-OFFOWNER-CREATE — the create tick never ran off the owner; nothing relocated"
	fail=1
fi

oo_served=$(grep '^OO-RESULT ' "$OUT/run.txt" | sed -n 's/^OO-RESULT ok=[0-9]* served=\([0-9]*\).*/\1/p' | sort -n | tail -1)
[ -n "$oo_served" ] || oo_served=0

served=$(sed -n 's/^RW-SUMMARY served=\([0-9]*\).*/\1/p' "$OUT/run.txt")
offowner=$(sed -n 's/^RW-SUMMARY .*offOwnerTicks=\([0-9]*\).*/\1/p' "$OUT/run.txt")
[ -n "$served" ] || {
	echo "FAIL — no RW-SUMMARY line"
	fail=1
	served=0
}
[ -n "$offowner" ] || offowner=0

if [ "$oo_served" -le "$served" ]; then
	echo "FAIL RW-CHANNEL — the off-owner recording served nothing beyond the on-fiber oracle's $served"
	fail=1
fi
if [ "$offowner" -eq 0 ]; then
	echo "FAIL RW-OFFOWNER — served=$served but every drain ran on the storage owner; nothing relocated"
	fail=1
fi

# RW-DRAIN: the offline stem-export path drains the recorder through the loop a change to the write
# path could break. A non-terminating drain shows up as a timeout. This gate measures that the drain
# TERMINATES, so the stems it renders go to this run's own temporary directory and nowhere else.
echo "recorder write: running the offline drain"
set +e
GOLDEN_STEM_OUT="$OUT/drain-stems" timeout 900 "$BIN" >"$OUT/drain.txt" 2>"$OUT/drain.err"
drain_status=$?
set -e
if [ "$drain_status" -ne 0 ]; then
	echo "FAIL RW-DRAIN — the offline render exited $drain_status (124 = it never drained)"
	sed -n '1,20p' "$OUT/drain.err" >&2
	fail=1
fi

if [ "$fail" -ne 0 ]; then
	echo "recorder write: FAILED"
	exit 1
fi
echo "recorder write: OK (served=$served->${oo_served} offOwner drain=$offowner finalize=$oo_fin create=$oo_create tier=$tier)"
