#!/usr/bin/env bash
# sample_collect_channel.sh — the sample-collection channel gate.
#
# Saves a song with collect-media on and requires that every sample the song references arrived in
# the song's own folder byte-identical to its source, that the audio-file list survived the process
# intact, and that the copying went through the byte channel after boot.
#
# ## Why this gate exists
#
# Collecting media is the one save path that walks a container the audio engine and the UI both
# read, mutating a key the container is SORTED BY as it goes. The copying spans ticks, so the walk
# can be observed half-done. The collection answers that by deciding every path first, transferring
# with nothing shared held, and applying the decisions in one pass -- and SC-ORDERED is what says the
# answer works.
#
# One leg: the transfers ride the byte channel and the save around them the write channel, and
# neither has a feature switch.
#
# What the checks cover:
#   SC-COPIED     every referenced sample is AT its collected destination, byte-identical to the
#                 source.
#   SC-RENAMED    the fixture's recording really was renamed. Required, because a collect-media save
#                 rewrites filePath for recordings and NOTHING else -- so if the rename stops
#                 happening, nothing moves in the audio-file vector and SC-ORDERED becomes a check
#                 that cannot fail.
#   SC-ORDERED    the audio-file list is still sorted and every resident file still findable, once
#                 the save is over.
#   SC-ATOMIC     the same invariant, checked on EVERY tick while the collection is in flight. This
#                 is the one the design is actually for: the hazard is another tier bisecting the
#                 vector BETWEEN two transfers, and SC-ORDERED cannot see that window because the
#                 apply pass tidies it away before the end. A path mutated during the planning walk
#                 passes SC-ORDERED and fails this.
#   SC-CHANNEL    the transfers were SERVED by the byte channel -- the async arm ran.
#   SC-ACCOUNTED  the channel served every collected file and the inline arm served none.
#   SC-TICKED   the transfers went through the channel after boot (`channelTicks`).
#
# A missing marker fails the run: a stage that never ran asserts nothing, and the failure count
# cannot show that, because it counts only assertions that DID run.
#
# Two things keep the ordering checks from becoming checks that cannot fail. A collect-media save
# rewrites filePath for RECORDINGS and nothing else, so the fixture carries one -- SC-RENAMED is
# what proves it is still being renamed -- and a second file positioned so the rename must move the
# recording ACROSS it in the sort order. And the neutering: mutate filePath during the PLAN walk
# instead of the APPLY pass, and SC-ATOMIC must go red. It was written expecting SC-ORDERED to catch
# that, and SC-ORDERED did not; the end state is repaired by the apply pass, so only the in-flight
# check can see it.
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/sample_collect_channel.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
fail=0

echo "sample collect channel: running"
set +e
GOLDEN_SCENARIO=sample_collect "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e
if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^SC-' "$OUT/run.txt" || sed -n '1,40p' "$OUT/run.txt"
	sed -n '1,40p' "$OUT/run.err" >&2
	exit 1
fi
# An abort or a wedge exits before these lines are printed, which is what turns a freeze into a
# gate failure rather than a silently truncated log.
for marker in SC-SETUP SC-COPIED SC-RENAMED SC-ATOMIC SC-ORDERED SC-SUMMARY SC-RESULT; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^SC-' "$OUT/run.txt"
		exit 1
	fi
done
if ! grep -q '^SC-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — $(grep '^SC-RESULT' "$OUT/run.txt")"
	fail=1
fi

served="$(sed -n 's/^SC-SUMMARY served=\([0-9]*\) .*/\1/p' "$OUT/run.txt")"
inline_served="$(sed -n 's/^SC-SUMMARY .*inlineServed=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
channel_ticks="$(sed -n 's/^SC-SUMMARY .*channelTicks=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
copies="$(grep -c '^SC-COPIED to=' "$OUT/run.txt" || true)"

# SC-CHANNEL, SC-ACCOUNTED and SC-TICKED: no file may take the inline arm, and the channel must have
# served exactly the files that arrived.
if [ -z "${served:-}" ] || [ "$served" -eq 0 ]; then
	echo "FAIL SC-CHANNEL — the collection served ${served:-no} transfer(s); the async arm never ran"
	fail=1
fi
if [ "${inline_served:-1}" -ne 0 ]; then
	echo "FAIL SC-ACCOUNTED — the collection sent ${inline_served:-an unknown number of} transfer(s) through the inline arm"
	fail=1
fi
if [ "${served:-0}" -ne "$copies" ]; then
	echo "FAIL SC-ACCOUNTED — the collection copied $copies file(s) but the channel served ${served:-no}"
	fail=1
fi
if [ -z "${channel_ticks:-}" ] || [ "$channel_ticks" -eq 0 ]; then
	echo "FAIL SC-TICKED — no transfer ever ran through the channel across ticks"
	fail=1
fi

grep -E '^SC-' "$OUT/run.txt"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
grep -hE '^TIER-SYNC(-AT)? ' "$OUT/run.txt" || true
echo "PASS — the samples were collected into the song's folder, the audio-file list came through"
echo "       ordered and intact, and the byte channel did the copying across ticks"
