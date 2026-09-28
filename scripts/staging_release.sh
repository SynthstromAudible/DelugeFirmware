#!/usr/bin/env bash
# staging_release.sh — the staging-release gate.
#
# Holds a staged document read in flight on the write channel, releases the staging the way an abandoning consumer
# does, allocates the same size again and fills it with a sentinel, then lets the read finish.
#
# What the checks below cover, and why each is separate:
#   SR-ISSUED    that the channel held the read's buffer when the staging was released. Without it, the release
#                below would free a buffer nothing was writing into, and SR-INTACT would pass for free.
#   SR-RELEASE   the release and the reallocation. sameAddress says whether the allocator handed the released block
#                straight back; it is reported, not required.
#   SR-INTACT    that the abandoned read wrote nothing into the reallocated memory. Required 0.
#   SR-REAPED    that the released buffer was freed once the read finished: parked, not leaked.
#
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

OUT="$(mktemp -d "${TMPDIR:-/tmp}/staging_release.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT

echo "staging release: running"
set +e
GOLDEN_SCENARIO=staging_release "$BIN" >"$OUT/run.txt" 2>"$OUT/run.err"
status=$?
set -e

if [ "$status" -ne 0 ]; then
	echo "FAIL — the run exited $status"
	grep -E '^SR-' "$OUT/run.txt" || sed -n '1,60p' "$OUT/run.txt"
	sed -n '1,25p' "$OUT/run.err" >&2
	exit 1
fi

if ! grep -q '^SR-RESULT ' "$OUT/run.txt"; then
	echo "FAIL — no SR-RESULT line; the scenario did not run to completion"
	sed -n '1,60p' "$OUT/run.txt"
	exit 1
fi

for marker in SR-ISSUED SR-RELEASE SR-INTACT SR-REAPED; do
	if ! grep -q "^$marker " "$OUT/run.txt"; then
		echo "FAIL — no $marker line; that stage did not run"
		grep -E '^SR-' "$OUT/run.txt"
		exit 1
	fi
done

if ! grep -q '^SR-RESULT failures=0$' "$OUT/run.txt"; then
	echo "FAIL — in-process failures:"
	grep -E '^SR-' "$OUT/run.txt"
	exit 1
fi

grep -E '^SR-' "$OUT/run.txt"
echo "PASS — a staged read abandoned in flight wrote nothing into memory handed out again"
