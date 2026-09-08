#!/usr/bin/env bash
# recorder_write.sh — the recorder write-relocation gate.
#
# What each check covers, and why each is separate:
#   RW-ROUNDTRIP  that a real recording still reads back byte-correctly, mid-recording AND finalized
#                 multi-cluster. Passes before and after the conversion, so on its own it proves
#                 nothing moved -- it is the half that catches a conversion that relocated the write
#                 and broke the recording on the way. It earned its keep here: a recycled stream
#                 handle collecting a previous recorder's answer showed up as exactly this, and
#                 nothing else in the run noticed.
#   RW-CHANNEL    that the async arm was EXECUTED. Required non-zero on the channel-on leg. Every
#                 fallback in this path is correct and silent, so a channel that never worked would
#                 leave the round trip green and every recording intact -- which is how a previous
#                 conversion shipped reporting nothing served.
#   RW-OFFOWNER   that the drain ran OFF the storage owner. This is the gate's real subject. Without
#                 it RW-CHANNEL can be non-zero while every write was still served on the owner, and
#                 the conversion would have renamed a mechanism rather than moved it.
#   RW-OFFOWNER-CREATE  that CREATING the recording's file ran off the storage owner. It was the last
#                 tick this recorder dispatched away, so zero here means it is blocking there again.
#
#   NOT covered: the stem-export folder search. Its machine is exercised only by the offline golden
#                render, which runs ON the owner, so only its SYNCHRONOUS arm is verified. An
#                off-owner case was attempted and abandoned: routing a harness recording down the
#                stem path needs more engine state than `processStarted` +
#                `currentStemExportType` + `lastFolderNameForStemExport`, and a case that silently
#                records to RESAMPLE while calling itself a stem case is worse than none.
#
#   RW-OFFOWNER-FINALIZE  that FINISHING a recording ran off the storage owner. This is the subject
#                 of the change: a served count says operations went through the channel, not where
#                 the caller stood. Restoring the dispatch drops this to zero and nothing else moves.
#   RW-LEAK       that an aborted recording gives its clusters back. It does NOT currently catch a
#                 premature unlink -- single-flight sends that unlink down its synchronous arm anyway
#                 -- but it does catch a cleanup that fails to delete at all.
#   RW-OFFOWNER-CASE  that a whole recording driven from a NON-fiber context -- the shape the device
#                 actually uses -- still reads back byte-for-byte identical to the on-fiber ones, and
#                 that its cluster writes went through the channel. The eleven cases above cannot
#                 show this: the oracle is dispatched onto the fiber wholesale (it has to be, for the
#                 finalized probe's streaming drain), so every one of them measures on_owner=1 and
#                 takes the synchronous arm. This case is the only one that exercises the async write
#                 path in the context the conversion is FOR.
#   RW-DRAIN      that the offline stem-export path still drains to completion. That loop calls the
#                 card routine in a bounded loop expecting each call to COMPLETE a write, and it is
#                 the one caller the flip could plausibly break.
#
# Unlike the tier gates beside it this one has no tier transition to show: every site it touches was
# measured on the storage owner before the conversion. Served plus off-owner IS the evidence.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
TARGET_DIR="${TARGET_DIR:-$CRATE_DIR/target/recorder-write}"
CHAN_TARGET_DIR="${CHAN_TARGET_DIR:-$CRATE_DIR/target/recorder-write-on}"

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
	(cd "$CRATE_DIR" && cargo build --release --features async_file_write_active \
		--target-dir "$REPO_ROOT/$CHAN_TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
CHAN_BIN="$CHAN_TARGET_DIR/release/golden_vt_render"
for b in "$BIN" "$CHAN_BIN"; do
	[ -x "$b" ] || {
		echo "ERROR: '$b' not built (see NO_BUILD)"
		exit 2
	}
done

OUT="$(mktemp -d "${TMPDIR:-/tmp}/recorder_write.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

run_leg() {
	local name="$1" bin="$2"
	echo "recorder write: running $name leg"
	set +e
	GOLDEN_SCENARIO=recorder_roundtrip "$bin" >"$OUT/$name.txt" 2>"$OUT/$name.err"
	local status=$?
	set -e
	if [ "$status" -ne 0 ]; then
		echo "FAIL [$name] — the run exited $status"
		sed -n '1,40p' "$OUT/$name.err" >&2
		fail=1
		return
	fi
	# An abort exits before this line is printed, which is what turns a freeze into a gate failure
	# rather than a silently truncated log.
	if ! grep -q '^RW-RESULT ' "$OUT/$name.txt"; then
		echo "FAIL [$name] — no RW-RESULT line; the scenario did not run to completion"
		fail=1
		return
	fi
	if ! grep -q '^11/11 cases passed' "$OUT/$name.txt"; then
		echo "FAIL [$name] RW-ROUNDTRIP — a recording did not read back correctly"
		grep -E '^CASE|FAIL' "$OUT/$name.txt" | head -20
		fail=1
	fi
	if ! grep -q '^RW-RESULT failures=0' "$OUT/$name.txt"; then
		echo "FAIL [$name] — $(grep '^RW-RESULT' "$OUT/$name.txt")"
		fail=1
	fi
}

run_leg off "$BIN"
run_leg on "$CHAN_BIN"

# The off-owner cases run AFTER the on-fiber oracle has printed RW-SUMMARY, so their evidence is
# their own lines. There are three, and each covers a shape the eleven on-fiber cases cannot: the
# oracle is dispatched onto the fiber wholesale, so every one of those measures on_owner=1 and takes
# the synchronous arm.
#
# The alteration case exists because the three byte-exact alterFile cases call that function DIRECTLY
# and nothing sets allowFileAlterationAfter -- so before it was added, finalize's whole batch machine
# had no coverage on any leg.
for c in recording alterfile abort; do
	if ! grep -q "^CASE offowner_$c " "$OUT/on.txt"; then
		echo "FAIL RW-OFFOWNER-CASE — the off-owner '$c' case did not run"
		fail=1
	fi
	if ! grep -q "^CASE offowner_$c .*onOwner=0" "$OUT/on.txt"; then
		echo "FAIL RW-OFFOWNER-CASE — '$c' did not run off the owner; it proves nothing there"
		fail=1
	fi
done

oo_count=$(grep -c '^OO-RESULT ' "$OUT/on.txt" || true)
oo_ok_count=$(grep -c '^OO-RESULT ok=1 ' "$OUT/on.txt" || true)
if [ "$oo_count" -lt 2 ] || [ "$oo_ok_count" -ne "$oo_count" ]; then
	echo "FAIL RW-OFFOWNER-CASE — $oo_ok_count of $oo_count off-owner recordings read back correctly"
	grep '^OO-RESULT ' "$OUT/on.txt"
	fail=1
fi

# RW-OFFOWNER-FINALIZE: the subject of this whole change. A served count says operations went through
# the channel; it does not say where the caller was standing. Restoring tickNeedsOwner's claim over
# finalize drops this straight to zero while everything else stays green.
oo_fin=$(grep '^OO-RESULT ' "$OUT/on.txt" | sed -n 's/.*offOwnerFinalizeTicks=\([0-9]*\).*/\1/p' | sort -n | tail -1)
[ -n "$oo_fin" ] || oo_fin=0
if [ "$oo_fin" -eq 0 ]; then
	echo "FAIL RW-OFFOWNER-FINALIZE — finalize never ran off the owner; nothing relocated"
	fail=1
fi

# RW-LEAK: an aborted recording must give its clusters back. Note this currently passes even with the
# close/unlink ordering broken -- with one operation in flight at a time the unlink falls back to its
# synchronous arm, so the ordering holds by construction rather than by observation. Kept because it
# catches a cleanup that fails to delete at all, which IS observable.
oo_leak=$(grep '^OO-LEAK ' "$OUT/on.txt" || true)
if [ -z "$oo_leak" ]; then
	echo "FAIL RW-LEAK — no OO-LEAK line; the abort case did not finish"
	fail=1
elif ! grep -q '^OO-LEAK .*ok=1' "$OUT/on.txt"; then
	echo "FAIL RW-LEAK — $oo_leak; an aborted recording did not give its clusters back"
	fail=1
fi

# RW-OFFOWNER-CREATE: the third counter. Creating the file was the LAST thing this recorder
# dispatched to the owner, so this reading zero means the recorder is back to blocking there.
oo_create=$(grep '^OO-RESULT ' "$OUT/on.txt" | sed -n 's/.*offOwnerCreateTicks=\([0-9]*\).*/\1/p' | sort -n | tail -1)
[ -n "$oo_create" ] || oo_create=0
if [ "$oo_create" -eq 0 ]; then
	echo "FAIL RW-OFFOWNER-CREATE — the create tick never ran off the owner; nothing relocated"
	fail=1
fi

oo_served=$(grep '^OO-RESULT ' "$OUT/on.txt" | sed -n 's/^OO-RESULT ok=[0-9]* served=\([0-9]*\).*/\1/p' | sort -n | tail -1)
[ -n "$oo_served" ] || oo_served=0

served=$(sed -n 's/^RW-SUMMARY served=\([0-9]*\).*/\1/p' "$OUT/on.txt")
offowner=$(sed -n 's/^RW-SUMMARY .*offOwnerTicks=\([0-9]*\).*/\1/p' "$OUT/on.txt")
off_served=$(sed -n 's/^RW-SUMMARY served=\([0-9]*\).*/\1/p' "$OUT/off.txt")

[ -n "$served" ] || {
	echo "FAIL — no RW-SUMMARY on the channel-on leg"
	fail=1
	served=0
}
[ -n "$offowner" ] || offowner=0
[ -n "$off_served" ] || off_served=0

if [ "$served" -eq 0 ]; then
	echo "FAIL RW-CHANNEL — the channel-on leg served 0 writes; the async arm never ran"
	fail=1
fi
if [ -n "${oo_served:-}" ] && [ "$oo_served" -le "$served" ]; then
	echo "FAIL RW-CHANNEL — the off-owner recording served nothing beyond the on-fiber oracle's $served"
	fail=1
fi
if [ "$offowner" -eq 0 ]; then
	echo "FAIL RW-OFFOWNER — served=$served but every drain ran on the storage owner; nothing relocated"
	fail=1
fi
if [ "$off_served" -ne 0 ]; then
	echo "FAIL — the channel-OFF leg served $off_served writes; the feature gate is not gating"
	fail=1
fi

# RW-DRAIN: the offline stem-export path drains the recorder through the loop the flip could break.
# A non-terminating drain shows up as a timeout. The renderer writes its stems into the repo's
# golden_stems/ directory, so restore those afterwards -- this gate measures that the drain
# TERMINATES, and must never leave a re-baselined golden behind.
echo "recorder write: running the offline drain leg"
set +e
timeout 900 "$CHAN_BIN" >"$OUT/drain.txt" 2>"$OUT/drain.err"
drain_status=$?
set -e
git -C "$REPO_ROOT" checkout -- golden_stems/ 2>/dev/null || true
if [ "$drain_status" -ne 0 ]; then
	echo "FAIL RW-DRAIN — the offline render exited $drain_status (124 = it never drained)"
	sed -n '1,20p' "$OUT/drain.err" >&2
	fail=1
fi

if [ "$fail" -ne 0 ]; then
	echo "recorder write: FAILED"
	exit 1
fi
echo "recorder write: OK (served=$served->${oo_served:-?} offOwner drain=$offowner finalize=$oo_fin create=$oo_create)"
