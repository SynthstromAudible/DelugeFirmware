#!/usr/bin/env bash
#
# Lens 1 (streaming-underrun harness) margin sweep + negative controls: a repeatable,
# deterministic sweep of `lens1-vt-sim` over modeled SD latency (the `sim_latency`
# throughput/overhead lever — see `../../src/sd.rs`'s `latency_for`), reporting the underrun
# curve and the THRESHOLD throughput at which underruns first appear (the "margin": "keeps
# up for SD latency better than L"), plus negative control A §9 of the spec requires
# as an ACCEPTANCE criterion, not just supplementary data:
#
#   Control A (detection): past the threshold, underrun_unassign > 0; at the fast/default
#     latency, == 0 — proof the harness's instrumentation actually fires.
#
# (Control B — mechanism efficacy of the HIGH-priority dispatch / recorder-drain yield —
# was retired with that worker-ring tier; the sim knobs it toggled no longer exist.)
#
# Every run is a single deterministic virtual-time simulation (no wall-clock, no RNG) —
# repeat any invocation and the numbers reproduce exactly. This script just automates
# running `lens1-vt-sim` several times as
# separate PROCESSES (not an in-process loop): the C++ app's static/global state
# (`currentSong`, the storage owner, ...) is never designed to be reset and re-driven twice
# in one process lifetime, so a fresh process per data point is the safe, simple shape —
# matching every other point in this harness (main.rs's own `hard_exit` skips C++ static
# destructors on the assumption the process exits once).
#
# Usage:
#   sweep.sh                 # build (unless NO_BUILD=1); selftest precondition, then margin sweep + control A
#   sweep.sh selftest          # the 4 spin selftests only: timer class (--selftest-block,
#                              #   --selftest-block-nested) + mutex class
#                              #   (--selftest-block-mutex and its -control)
#   sweep.sh margin           # margin sweep only
#   sweep.sh control-a        # negative control A only
#
# Env overrides:
#   FIXTURE          song fixture                              (default: cordae)
#   BLOCKS           audio blocks per run (~3ms/block)          (default: 20000, ~58s virtual)
#   OVERHEAD_US       fixed sim_latency command overhead         (default: 500)
#   NO_BUILD=1       skip `cargo build --release`
#   SELFTEST_IMAGE   shared SD backing file for `selftest`      (default: /tmp/deluge-lens1-selftest-shared.img)
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BIN="$HERE/target/release/lens1-vt-sim"

FIXTURE="${FIXTURE:-cordae}"
BLOCKS="${BLOCKS:-20000}"
OVERHEAD_US="${OVERHEAD_US:-500}"
SELFTEST_IMAGE="${SELFTEST_IMAGE:-/tmp/deluge-lens1-selftest-shared.img}"
CMD="${1:-all}"

if [[ "${NO_BUILD:-0}" != "1" ]]; then
    echo "sweep.sh: cargo build --release" >&2
    (cd "$HERE" && cargo build --release --quiet)
fi

# Runs one lens1-vt-sim invocation with the given extra env (as NAME=VALUE args) and prints
# its LENS1_RESULT line's fields as a single space-joined "key=value ..." string on stdout.
# Fails loudly (set -e) if the binary doesn't reach a clean LENS1_RESULT line — a wedged or
# crashed run is a harness bug, not a data point to silently drop from the sweep.
run_point() {
    local extra_env=("$@")
    local out
    out=$(env "${extra_env[@]}" \
        LENS1_FIXTURE="$FIXTURE" LENS1_BLOCKS="$BLOCKS" LENS1_STEP_TIMEOUT_S=120 \
        LENS1_VIRTUAL_BUDGET_MS=600000 "$BIN" 2>/dev/null | grep '^LENS1_RESULT')
    if [[ -z "$out" ]]; then
        echo "sweep.sh: run_point FAILED (no LENS1_RESULT line) for env: ${extra_env[*]}" >&2
        exit 1
    fi
    echo "$out"
}

field() { # field <LENS1_RESULT line> <name>
    echo "$1" | grep -oP "(?<=$2=)[0-9]+"
}

# --- Selftest precondition ------------------------------------------------------------
# Lens 1 was once never in any implementer's build target, and it silently stopped
# linking for a long stretch as a result — this script's own reason to exist. The
# `--selftest-block`/`--selftest-block-nested` modes (main.rs) are the harness's proof
# that a modeled SD read completes under a non-yielding spin, but they have the
# IDENTICAL exposure today: runnable, but wired into no script. Running both here,
# FIRST, closes that gap and fails fast (before the margin sweep burns 15 latency
# points) if the sim_block/preempt seam ever breaks.
#
# Neither mode mounts a filesystem or reads anything but raw sector 0 — unlike the
# real scenario, which needs `sd_image::pack_golden_fixture`'s ~2.5GB FAT32 image
# built from the local golden corpus (and hard-asserts without one), these two modes
# only need a backing FILE at DELUGE_SD_IMAGE for deluge-bsp's host `sd.rs` to open;
# content is irrelevant, and it auto-extends anything smaller than its own 8 MiB
# default. So `pack_selftest_image` below packs a tiny, plain (non-FAT) file ONCE at a
# predictable, non-per-PID path — not `sd_image::pack_image`'s
# `deluge-streaming-scenario-<pid>.img` naming under `temp_dir()`, which is never
# cleaned up (see that function's doc comment) and is exactly the leak that caused an
# 11GiB /tmp incident during this branch's own development (17 sweep processes x
# ~573MB each). Pointing DELUGE_SD_IMAGE at the SAME shared file for both selftest
# invocations below means this precondition adds ONE small file to /tmp, reused on
# every future `sweep.sh` run, instead of leaking a fresh multi-hundred-MB image per
# invocation.
pack_selftest_image() {
    if [[ -f "$SELFTEST_IMAGE" ]]; then
        echo "sweep.sh: reusing shared selftest image at $SELFTEST_IMAGE" >&2
        return
    fi
    echo "sweep.sh: packing shared selftest image at $SELFTEST_IMAGE (8 MiB, no golden corpus needed)" >&2
    truncate -s 8M "$SELFTEST_IMAGE"
}

run_selftest() {
    echo "=== Selftest: non-yielding block_on, timer class (2 modes) + mutex class (2 modes) ===" >&2
    pack_selftest_image
    local ok=1 rc log_block log_nested log_mutex log_mutex_ctl
    log_block="$(mktemp)"
    log_nested="$(mktemp)"
    log_mutex="$(mktemp)"
    log_mutex_ctl="$(mktemp)"

    if DELUGE_SD_IMAGE="$SELFTEST_IMAGE" timeout 60 "$BIN" --selftest-block \
        >"$log_block" 2>&1; then
        echo "  --selftest-block:        PASS" >&2
    else
        rc=$?
        echo "  --selftest-block:        FAIL (exit $rc) — see $log_block" >&2
        tail -n 20 "$log_block" >&2
        ok=0
    fi

    if DELUGE_SD_IMAGE="$SELFTEST_IMAGE" timeout 60 "$BIN" --selftest-block-nested \
        >"$log_nested" 2>&1; then
        echo "  --selftest-block-nested: PASS" >&2
    else
        rc=$?
        echo "  --selftest-block-nested: FAIL (exit $rc) — see $log_nested" >&2
        tail -n 20 "$log_nested" >&2
        ok=0
    fi

    # Mutex class: a MAIN-resident spin on a guard held across an await by an HP_EXEC task.
    # Distinct from the two above, which complete because the virtual CLOCK moved; this one
    # completes only because another task ran and dropped a guard.
    if DELUGE_SD_IMAGE="$SELFTEST_IMAGE" timeout 60 "$BIN" --selftest-block-mutex \
        >"$log_mutex" 2>&1; then
        echo "  --selftest-block-mutex:  PASS" >&2
    else
        rc=$?
        echo "  --selftest-block-mutex:  FAIL (exit $rc) — see $log_mutex" >&2
        tail -n 20 "$log_mutex" >&2
        ok=0
    fi

    # ...and its NEGATIVE CONTROL: the same spin with the holder back on MAIN, which the hook
    # cannot poll, so it MUST wedge. This is what stops the positive case above from passing
    # vacuously on an uncontended lock — do not drop it because it "always passes".
    if DELUGE_SD_IMAGE="$SELFTEST_IMAGE" timeout 60 "$BIN" --selftest-block-mutex-control \
        >"$log_mutex_ctl" 2>&1; then
        echo "  --selftest-block-mutex-control: PASS (spin correctly wedged)" >&2
    else
        rc=$?
        echo "  --selftest-block-mutex-control: FAIL (exit $rc) — see $log_mutex_ctl" >&2
        tail -n 20 "$log_mutex_ctl" >&2
        ok=0
    fi

    if [[ "$ok" -eq 1 ]]; then
        echo "SELFTEST: PASS" >&2
        rm -f "$log_block" "$log_nested" "$log_mutex" "$log_mutex_ctl"
    else
        echo "SELFTEST: FAIL — the sim_block/preempt seam this whole harness depends on is broken" >&2
        exit 1
    fi
}

# --- Margin sweep --------------------------------------------------------------------
# Geometric-ish descent from a fast/default-plausible throughput down to the extreme low
# end, denser in the 300k-2M band where the curve is expected to cross from 0 to positive,
# to pin the threshold tightly.
MARGIN_THROUGHPUTS=(2000000 1500000 1000000 800000 600000 500000 400000 300000 200000 150000 100000 50000 20000 10000 5000)

run_margin_sweep() {
    echo "=== Margin sweep: fixture=$FIXTURE blocks=$BLOCKS overhead_us=$OVERHEAD_US ===" >&2
    local threshold="" prev_bps="" prev_unassign=0
    printf '%-12s %-14s %-14s\n' "throughput_bps" "underrun_wait" "underrun_unassign"
    for bps in "${MARGIN_THROUGHPUTS[@]}"; do
        local line unassign wait_c
        line=$(run_point LENS1_THROUGHPUT_BPS="$bps" LENS1_OVERHEAD_US="$OVERHEAD_US")
        wait_c=$(field "$line" underrun_wait)
        unassign=$(field "$line" underrun_unassign)
        printf '%-12s %-14s %-14s\n' "$bps" "$wait_c" "$unassign"
        if [[ "$unassign" -gt 0 && -z "$threshold" ]]; then
            threshold="between ${bps} and ${prev_bps:-N/A} bytes/sec (first nonzero at ${bps}, last zero at ${prev_bps:-N/A})"
        fi
        prev_bps="$bps"
        prev_unassign="$unassign"
    done
    echo >&2
    if [[ -n "$threshold" ]]; then
        echo "MARGIN_THRESHOLD: $threshold (overhead_us=$OVERHEAD_US)" >&2
    else
        echo "MARGIN_THRESHOLD: no crossing observed in swept range" >&2
    fi
}

# --- Negative control A (detection) --------------------------------------------------
run_control_a() {
    echo "=== Negative control A: detection ===" >&2
    local fast slow fast_u slow_u
    fast=$(run_point LENS1_THROUGHPUT_BPS=2000000 LENS1_OVERHEAD_US="$OVERHEAD_US")
    slow=$(run_point LENS1_THROUGHPUT_BPS=5000 LENS1_OVERHEAD_US=2000)
    fast_u=$(field "$fast" underrun_unassign)
    slow_u=$(field "$slow" underrun_unassign)
    echo "  fast/default (2,000,000 B/s):  underrun_unassign=$fast_u  (expect 0)" >&2
    echo "  slow (5,000 B/s, 2000us):      underrun_unassign=$slow_u  (expect > 0)" >&2
    if [[ "$fast_u" -eq 0 && "$slow_u" -gt 0 ]]; then
        echo "CONTROL_A: PASS — harness detects underrun (fires only under stress)" >&2
    else
        echo "CONTROL_A: FAIL — fast=$fast_u slow=$slow_u" >&2
        exit 1
    fi
}

case "$CMD" in
    selftest) run_selftest ;;
    margin) run_margin_sweep ;;
    control-a) run_control_a ;;
    all)
        run_selftest
        echo >&2
        run_margin_sweep
        echo >&2
        run_control_a
        ;;
    *)
        echo "usage: sweep.sh [selftest|margin|control-a]" >&2
        exit 2
        ;;
esac
