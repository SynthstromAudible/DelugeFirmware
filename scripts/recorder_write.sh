#!/usr/bin/env bash
# recorder_write.sh — the recorder write-relocation gate.
#
# What each check covers, and why each is separate:
#   RW-ACTIVE     that this binary reaches the Rust write driver at all. A module missing from the
#                 harness's module list resolves to the C++ weak fallback, and every write would then
#                 take the synchronous arm while everything below stayed green.
#   RW-ROUNDTRIP  that a real recording still reads back byte-correctly, mid-recording AND finalized
#                 multi-cluster. It is the half that catches a change that relocated the write
#                 and broke the recording on the way: a recycled stream handle collecting a previous
#                 recorder's answer shows up as exactly this, and nothing else in the run would
#                 notice.
#   RW-CHANNEL    that the async arm was EXECUTED by the `ticked_` recordings: they must serve writes
#                 beyond the ones the oracle's cases served (RW-SUMMARY's count, taken before they run).
#   RW-TICKED   that the recorder drain ran after boot, its writes on the write channel. This is the
#                 gate's real subject: RW-CHANNEL alone says writes were served, not which drain issued them.
#   RW-TICKED-CREATE  that CREATING the recording's file ran through the create machine.
#   RW-TICKED-FINALIZE  that FINISHING a recording ran through the finalize machine. A served count says
#                 operations went through the channel, not which machine issued them.
#   RW-LEAK       that an aborted recording gives its clusters back. It does NOT currently catch a
#                 premature unlink -- single-flight sends that unlink down its synchronous arm anyway
#                 -- but it does catch a cleanup that fails to delete at all.
#   RW-ABORT-OPEN  that an abort landing while the channel is opening the file -- for the create, or for
#                 finalize's reopen to alter it -- leaves no file behind and no stream-write slot taken: the open
#                 cannot be cancelled, so the abort has to wait it out and close what it opened.
#   RW-TICKED-CASE  that each `ticked_` case -- a recording, the alteration branch, an abort --
#                 ran and reads back byte-for-byte, its cluster writes through the channel.
#   RW-TIER       how many synchronous card operations the recorder began on the interaction tier after
#                 boot, over every `ticked_` case, and how many of its channel requests were refused.
#                 Required 0: after boot every recorder operation goes through a channel.
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
#                render RW-DRAIN runs, which starts the export after boot, so the search takes its
#                CHANNEL arm there; no check reads its outcome beyond the render terminating, and its
#                inline arm is not exercised at all. There is no stem case among the `ticked_`
#                cases: routing a harness recording down the stem path needs more engine state
#                than `processStarted` + `currentStemExportType` + `lastFolderNameForStemExport`,
#                and a case that silently records to RESAMPLE while calling itself a stem case
#                is worse than none.
#
# One leg plus the offline drain: the write channel has no feature switch, so there is no build
# without it to compare against.

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
	# Cargo does not track the C++ sources the harness links against, so force the object closure
	# first — same reason save_roundtrip.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
	exit 2
}

OUT="$(mktemp -d "${TMPDIR:-/tmp}/recorder_write.XXXXXX")"
# RECORDER_WRITE_KEEP=<file> keeps the scenario's full output: the gate prints only its verdicts, and a change that
# moves the oracle's case lines is judged by diffing that output, not the verdicts.
trap '[ -n "${RECORDER_WRITE_KEEP:-}" ] && cp "$OUT/run.txt" "$RECORDER_WRITE_KEEP"; rm -rf "$OUT"' EXIT
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

# The `ticked_` cases run AFTER the oracle has printed RW-SUMMARY, so their evidence is their own
# lines. There are three, and each covers a shape the oracle's eight cases do not take.
#
# The alteration case exists because nothing else sets allowFileAlterationAfter, so without it
# finalize's whole batch machine would have no coverage.
for c in recording alterfile abort; do
	if ! grep -q "^CASE ticked_$c " "$OUT/run.txt"; then
		echo "FAIL RW-TICKED-CASE — the ticked '$c' case did not run"
		fail=1
	fi
done

tk_count=$(grep -c '^TK-RESULT ' "$OUT/run.txt" || true)
tk_ok_count=$(grep -c '^TK-RESULT ok=1 ' "$OUT/run.txt" || true)
if [ "$tk_count" -lt 2 ] || [ "$tk_ok_count" -ne "$tk_count" ]; then
	echo "FAIL RW-TICKED-CASE — $tk_ok_count of $tk_count ticked recordings read back correctly"
	grep '^TK-RESULT ' "$OUT/run.txt"
	fail=1
fi

# RW-TICKED-FINALIZE: a recording finished without the finalize machine reads zero here while
# everything else stays green.
tk_fin=$(grep '^TK-RESULT ' "$OUT/run.txt" | sed -n 's/.*channelFinalizeTicks=\([0-9]*\).*/\1/p' | sort -n | tail -1)
[ -n "$tk_fin" ] || tk_fin=0
if [ "$tk_fin" -eq 0 ]; then
	echo "FAIL RW-TICKED-FINALIZE — finalize never ran through the channel across ticks"
	fail=1
fi

# RW-LEAK: an aborted recording must give its clusters back.
tk_leak=$(grep '^TK-LEAK ' "$OUT/run.txt" || true)
if [ -z "$tk_leak" ]; then
	echo "FAIL RW-LEAK — no TK-LEAK line; the abort case did not finish"
	fail=1
elif ! grep -q '^TK-LEAK .*ok=1' "$OUT/run.txt"; then
	echo "FAIL RW-LEAK — $tk_leak; an aborted recording did not give its clusters back"
	fail=1
fi

# RW-ABORT-OPEN: both abort-during-open cases ran, and each left no file and every slot free.
for c in abortcreate abortalter; do
	line=$(grep "^TK-ABORT-OPEN case=$c " "$OUT/run.txt" || true)
	if [ -z "$line" ]; then
		echo "FAIL RW-ABORT-OPEN — no line for the $c case; it did not finish"
		fail=1
	elif ! printf '%s\n' "$line" | grep -q ' ok=1$'; then
		echo "FAIL RW-ABORT-OPEN — $line"
		fail=1
	fi
done

# RW-TIER: reported, and required to be zero.
tier_line=$(grep '^RW-TIER ' "$OUT/run.txt" || true)
tier=$(printf '%s\n' "$tier_line" | sed -n 's/^RW-TIER violations=\([0-9]*\).*/\1/p')
if [ -z "$tier" ]; then
	echo "FAIL RW-TIER — no RW-TIER line; the ticked cases did not report"
	fail=1
	tier=0
else
	echo "$tier_line"
	if [ "$tier" -ne 0 ]; then
		echo "FAIL RW-TIER — the recorder began $tier synchronous card operation(s) after boot"
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

# RW-TICKED-CREATE: this reading zero means the recorder created its file without the create
# machine, blocking on the card instead.
tk_create=$(grep '^TK-RESULT ' "$OUT/run.txt" | sed -n 's/.*channelCreateTicks=\([0-9]*\).*/\1/p' | sort -n | tail -1)
[ -n "$tk_create" ] || tk_create=0
if [ "$tk_create" -eq 0 ]; then
	echo "FAIL RW-TICKED-CREATE — the create tick never ran through the channel across ticks"
	fail=1
fi

tk_served=$(grep '^TK-RESULT ' "$OUT/run.txt" | sed -n 's/^TK-RESULT ok=[0-9]* served=\([0-9]*\).*/\1/p' | sort -n | tail -1)
[ -n "$tk_served" ] || tk_served=0

served=$(sed -n 's/^RW-SUMMARY served=\([0-9]*\).*/\1/p' "$OUT/run.txt")
channel_ticks=$(sed -n 's/^RW-SUMMARY .*channelTicks=\([0-9]*\).*/\1/p' "$OUT/run.txt")
[ -n "$served" ] || {
	echo "FAIL — no RW-SUMMARY line"
	fail=1
	served=0
}
[ -n "$channel_ticks" ] || channel_ticks=0

if [ "$tk_served" -le "$served" ]; then
	echo "FAIL RW-CHANNEL — the ticked recording served nothing beyond the oracle's $served"
	fail=1
fi
if [ "$channel_ticks" -eq 0 ]; then
	echo "FAIL RW-TICKED — served=$served but no drain ran through the channel across ticks"
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
grep -hE '^TIER-SYNC(-AT)? ' "$OUT/run.txt" "$OUT/drain.txt" || true
echo "recorder write: OK (served=$served->${tk_served} channel drain=$channel_ticks finalize=$tk_fin create=$tk_create tier=$tier)"
