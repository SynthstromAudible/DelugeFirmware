#!/usr/bin/env bash
# sample_collect_channel.sh — the sample-collection channel gate.
#
# Saves a song with collect-media on and requires that every sample the song references arrived in
# the song's own folder byte-identical to its source, that the audio-file list survived the process
# intact, and that the copying ran off the storage owner.
#
# ## Why this gate exists
#
# Collecting media is the one save path that walks a container the audio engine and the UI both
# read, mutating a key the container is SORTED BY as it goes. The old code got away with that
# because the whole walk was one uninterrupted call on the storage owner; the moment the copying
# leaves the owner, the walk can be observed half-done. The rung answers that by deciding every
# path first, transferring with nothing shared held, and applying the decisions in one owner pass --
# and SC-ORDERED is what says the answer works. It guards an invariant the old code was
# ACCIDENTALLY safe from, which is exactly the kind that breaks silently when the accident goes away.
#
# Running this gate for the first time found three defects, none of which any other gate could see:
# transfers had no owner arm at all, the collected path was built by appending to an empty string
# (which took embedded-fatfs down), and the check itself asked the wrong question twice over.
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
#   SC-ACCOUNTED  the channel served every collected file and the owner served none.
#   SC-OFFOWNER   the transfers ran OFF the storage owner.
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
	# first — same reason recorder_write.sh does.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || {
	echo "ERROR: '$BIN' not built (see NO_BUILD)"
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
owner="$(sed -n 's/^SC-SUMMARY .*ownerServed=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
offowner="$(sed -n 's/^SC-SUMMARY .*offOwnerTicks=\([0-9]*\).*/\1/p' "$OUT/run.txt")"
copies="$(grep -c '^SC-COPIED to=' "$OUT/run.txt" || true)"

# SC-CHANNEL, SC-ACCOUNTED and SC-OFFOWNER: no file may go to the owner, and the channel must have
# served exactly the files that arrived.
if [ -z "${served:-}" ] || [ "$served" -eq 0 ]; then
	echo "FAIL SC-CHANNEL — the collection served ${served:-no} transfer(s); the async arm never ran"
	fail=1
fi
if [ "${owner:-1}" -ne 0 ]; then
	echo "FAIL SC-ACCOUNTED — the collection sent ${owner:-an unknown number of} transfer(s) to the owner"
	fail=1
fi
if [ "${served:-0}" -ne "$copies" ]; then
	echo "FAIL SC-ACCOUNTED — the collection copied $copies file(s) but the channel served ${served:-no}"
	fail=1
fi
if [ -z "${offowner:-}" ] || [ "$offowner" -eq 0 ]; then
	echo "FAIL SC-OFFOWNER — no transfer ever ran off the storage owner"
	fail=1
fi

grep -E '^SC-' "$OUT/run.txt"

if [ "$fail" -ne 0 ]; then
	exit 1
fi
echo "PASS — the samples were collected into the song's folder, the audio-file list came through"
echo "       ordered and intact, and the byte channel did the copying off the storage owner"
