#!/usr/bin/env bash
#
# card-reinsert — what a card swap does to the samples still in memory.
#
# Drives a real ejection and re-insertion (AudioFileManager::setCardEjected() ->
# reinitEjectedCard() -> deluge_efatfs_remount() -> cardReinserted()) with three residents present,
# one per fate that walk distinguishes:
#
#   CR-RESIDENTS  the walk had samples to visit at all
#   CR-SURVIVE    a referenced sample whose file is intact stays loadable
#   CR-LOST       a referenced sample whose file was removed is marked unloadable
#   CR-EVICT      a resident nothing references is pruned from memory
#   CR-TIER       owner ops the re-insertion dispatched, against the target the scenario declares
#
# ⚠️ CR-RESIDENTS IS NOT CEREMONY. `audio-load-ab` drives these same edges, and measured on
# 2026-09-19 it reaches cardReinserted() twice with residents=0 — the edges fire and the loop body
# never runs. Three gates on this branch have been green because the thing under test never ran. A
# gate for a walk must fail when the walk is empty, so CR-RESIDENTS is checked first and hardest.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

BUILD_DIR="${BUILD_DIR:-build-embassy-hostapp}"
CRATE_DIR="src/bsp/rust/harness/golden_vt_render"
# One build tree for the harness binary: `dbt harness run` sets CARGO_TARGET_DIR, and every gate builds the
# same crate -- the scenario is chosen at run time. Resolved once, here, because a relative fallback is
# relative to the repo root while the cargo build below runs in the crate directory.
TARGET_DIR="${TARGET_DIR:-${CARGO_TARGET_DIR:-$CRATE_DIR/target}}"
case "$TARGET_DIR" in /*) ;; *) TARGET_DIR="$REPO_ROOT/$TARGET_DIR" ;; esac

if [ "${NO_BUILD:-0}" != "1" ]; then
	[ -d "$BUILD_DIR" ] || {
		echo "ERROR: '$BUILD_DIR' does not exist — configure build-embassy-hostapp from sim/ first"
		exit 2
	}
	# ⚠️ Cargo does not track the C++ sources the harness links against, so an edited scenario silently
	# links a STALE object and its marker lines never appear. Force the object closure first.
	ninja -C "$BUILD_DIR" deluge_app NE10 eyalroz_printf deluge_dsp deluge_scheduler \
		deluge_foundation deluge_midi
	(cd "$CRATE_DIR" && cargo build --release --target-dir "$TARGET_DIR")
fi

BIN="$TARGET_DIR/release/golden_vt_render"
[ -x "$BIN" ] || { echo "ERROR: '$BIN' not built (see NO_BUILD)"; exit 2; }

OUT_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/card_reinsert.XXXXXX")"
cleanup() { rm -rf "$OUT_ROOT"; }
trap cleanup EXIT

# The card image is cached and SHARED between gates, and reused-and-mutated across runs. Start cold.
rm -f "${TMPDIR:-/tmp}"/deluge-streaming-scenario-*.img

set +e
GOLDEN_SCENARIO=card_reinsert \
	GOLDEN_VIRTUAL_BUDGET_MS="${GOLDEN_VIRTUAL_BUDGET_MS:-1800000}" \
	"$BIN" >"$OUT_ROOT/run.log" 2>&1
status=$?
set -e
if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status (crash or wedge)"
	grep -E '^(CR-|\[golden-scenario\])' "$OUT_ROOT/run.log" | tail -20
	exit 1
fi

grep -E '^CR-' "$OUT_ROOT/run.log" || true

fails=0

# CR-RESIDENTS first: every other leg below is vacuous if the walk visited nothing.
residents="$(sed -n 's/^CR-RESIDENTS samples=\([0-9-]*\) .*/\1/p' "$OUT_ROOT/run.log" | tail -1)"
if [ -z "$residents" ]; then
	echo "FAIL CR-RESIDENTS — no CR-RESIDENTS line; the scenario never reached the snapshot"
	fails=$((fails + 1))
elif [ "$residents" -le 0 ]; then
	echo "FAIL CR-RESIDENTS — the walk had $residents resident sample(s): every other leg here would pass for free"
	fails=$((fails + 1))
fi

for leg in CR-SURVIVE CR-LOST CR-EVICT; do
	line="$(grep -E "^$leg " "$OUT_ROOT/run.log" | tail -1)"
	if [ -z "$line" ]; then
		echo "FAIL $leg — no $leg line"
		fails=$((fails + 1))
		continue
	fi
	ok="$(printf '%s' "$line" | sed -n 's/^[A-Z-]* ok=\([0-9-]*\).*/\1/p')"
	if [ "$ok" != "1" ]; then
		echo "FAIL $leg — $line"
		fails=$((fails + 1))
	fi
done

# CR-TIER: the scenario declares its own target and prints both, so a mismatch is ours to fail.
tier_got="$(sed -n 's/^CR-TIER ownerOps=\([0-9-]*\) .*/\1/p' "$OUT_ROOT/run.log" | tail -1)"
tier_want="$(sed -n 's/^CR-TIER .*wanted=\([0-9-]*\).*/\1/p' "$OUT_ROOT/run.log" | tail -1)"
if [ -z "$tier_got" ] || [ -z "$tier_want" ]; then
	echo "FAIL CR-TIER — no CR-TIER line"
	fails=$((fails + 1))
elif [ "$tier_got" != "$tier_want" ]; then
	echo "FAIL CR-TIER — the re-insertion dispatched $tier_got owner op(s), wanted $tier_want"
	fails=$((fails + 1))
fi

if ! grep -q '^CR-RESULT failures=0' "$OUT_ROOT/run.log"; then
	echo "FAIL — $(grep '^CR-RESULT' "$OUT_ROOT/run.log" || echo 'no CR-RESULT line')"
	fails=$((fails + 1))
fi

if [ "$fails" -ne 0 ]; then
	exit 1
fi

echo "PASS — the swap walked $residents resident sample(s): the intact one survived, the removed one was marked unloadable, the unreferenced one was evicted, in $tier_got owner op(s)"
