#!/usr/bin/env bash
# Lens 2 harness runner: audio-preempts-streaming race check under ThreadSanitizer.
#
# Runs the `host_app` scenario path wired into `main.rs` (real song load, real-
# time playback on its own preemptive OS-thread executor, a concurrent output recording) —
# see main.rs's `host_app` boot block and scenario.rs's module doc — against the TSan-
# instrumented `deluge_app` (HOST_HARNESS.md's "M4c" section), for N
# iterations, and categorizes every TSan finding it sees against the two catalogs in this
# directory.
#
# Each finding is classified PRIMARILY on its RACE SITE — the `SUMMARY: ThreadSanitizer:`
# line, same as TSan's own one-line summary — not the whole stack. A catalog pattern only
# gets to look past the site (at every frame of both racing accesses, from the `WARNING:
# ThreadSanitizer: data race` line through the terminating `SUMMARY:` line) if it is
# explicitly tagged `DEEP <pattern>` in the catalog file. This split matters: on the audio
# thread, almost every finding's stack passes through the same call-graph roots
# (`AudioEngine::routine()`/`routine_task()`, `deluge_app_render`, ...) as ANCESTORS, not as
# the site — matching a broad file/subsystem pattern against every frame would make it match
# almost any audio-thread finding regardless of where the race actually is, silently
# absorbing a genuinely new race that merely happens to run on that thread. DEEP is reserved
# for the one class that legitimately needs it: a finding whose SUMMARY site is a bare
# compiler/runtime intrinsic intercept (`__tsan_memcpy`, `strlen`, ...) with no source
# location at all, where the real site is a specific, already-identified deeper frame (e.g.
# `src/lib/printf.c`) — see known_patterns.txt's header and class-2 note for the full
# rationale and evidence.
#   - known_patterns.txt          — pre-existing, unrelated-to-streaming debt, matched by
#                                    broad file/subsystem SITE pattern (see that file's header
#                                    for why patterns, not exact lines).
#   - open_findings_races.txt     — real races found on the cluster/loaded/recorder state
#                                    itself (documented, not suppressed, not fixed — see
#                                    that file's header).
# Anything matching NEITHER catalog is reported as UNCATALOGUED and fails the run — that is
# the thing this script exists to protect: a genuinely new streaming/priority/flip race must
# never silently hide behind the known noise above.
#
# Usage:
#   ./run.sh                      # build + 5 runs, default Cordae fixture, 500 blocks
#   NUM_RUNS=3 BLOCKS=2000 ./run.sh
#   DELUGE_SD_IMAGE=/path/to/cached.img ./run.sh   # skip repacking the FAT image
#   SKIP_BUILD=1 ./run.sh         # reuse an already-built TSan tree + binary (fast iteration)
#   ./run.sh --self-test          # no build/run: replay self_test/*.log fixtures through the
#                                  # classifier and assert each matches its *.expect verdict
#                                  # (open/known/uncatalogued) — the regression test for this
#                                  # classifier's own logic, independent of the TSan binary.
#
# Prerequisites (see HOST_HARNESS.md's M4c section for the one-time setup):
#   - clang/clang++ whose LLVM major matches `rustc +nightly`'s bundled LLVM.
#   - the sysroot symlink for librustc-nightly_rt.tsan.a under the custom
#     x86_64-unknown-linux-gnu-tsan target (HOST_HARNESS.md, "One-time environment setup").
#   - mtools (mformat/mcopy) if DELUGE_SD_IMAGE isn't pre-set and no cached fixture exists.
#
# Known gotcha: each unset-DELUGE_SD_IMAGE
# invocation packs a fresh ~2.5GB /tmp/deluge-streaming-scenario-*.img and leaves it behind.
# Repeated invocations without cleanup can exhaust a tmpfs /tmp; separately (independent of
# free space), packing an image and consuming it as the SD backing store WITHIN THE SAME
# process was observed to be unreliable on this host — the scenario would report
# `load_dispatched=false` (currentSong never got set during boot) on every run. Reusing an
# already-packed image via `DELUGE_SD_IMAGE=...` (packed by a PRIOR, separate process) was
# 100% reliable across dozens of runs. If a run reports `load_dispatched=false` with
# `boot_ready=true`, prefer the cached-image form over debugging boot ordering — and
# periodically `rm -f /tmp/deluge-streaming-scenario-*.img` between sessions.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUST_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"       # src/bsp/rust
# Honour CARGO_TARGET_DIR rather than assuming the crate-local `target/`. `dbt harness run`
# sets it (task-harness.py points every harness at one shared `target/harness` root), so a
# hard-coded path looks in the wrong place: the run dies with "missing … — build failed?"
# immediately after cargo reports success. Same defect `golden_embassy_diff.sh` had.
TARGET_ROOT="${CARGO_TARGET_DIR:-$RUST_DIR/target}"
REPO_ROOT="$(cd "$RUST_DIR/../../.." && pwd)"
TSAN_BUILD_DIR="${TSAN_BUILD_DIR:-$REPO_ROOT/build-embassy-hostapp-tsan}"

normalize() {
    # A TSan finding block -> the catalog key: strip repo-root / rustup-sysroot prefixes,
    # the trailing (BuildId: ...), and any (path+0xNNNN) address annotation, so the same
    # logical site matches across machines/builds/ASLR. Operates line-at-a-time, so it
    # applies uniformly whether fed the one-line SUMMARY or a full multi-frame block.
    sed -E \
        -e "s#$REPO_ROOT/##g" \
        -e 's#/home/[^/]+/\.rustup/toolchains/[^/]+/lib/rustlib/src/rust/##g' \
        -e 's# \(BuildId: [^)]*\)##g' \
        -e 's#\([^)]*deluge-rust\+0x[0-9a-f]+\)##g'
}

extract_findings() {
    # $1 = a TSan run log. Emits one normalized record per finding, spanning EVERY frame
    # from the `WARNING: ThreadSanitizer: data race` line through its terminating
    # `SUMMARY:` line (both racing accesses' full stacks). A block's frames are joined with
    # a \037 (unit separator, not a real newline) so the whole block survives as a single
    # record through `sort -u` and a caller's `while read`; see classify_block() for how a
    # block is split back into its SITE line and per-frame lines.
    awk '
        /^WARNING: ThreadSanitizer: data race/ { block = $0; next }
        block != "" {
            block = block "\037" $0
            if ($0 ~ /^SUMMARY: ThreadSanitizer:/) { print block; block = "" }
        }
    ' "$1" | normalize | sort -u
}

# split_catalog CATALOG SITE_OUT DEEP_OUT
#   Splits a catalog file's active (non-blank, non-comment) pattern lines into two pattern
#   files: DEEP_OUT gets the pattern text of every line prefixed `DEEP ` (prefix stripped);
#   SITE_OUT gets every other line verbatim. Built line-by-line (not via a pipeline into an
#   possibly-empty `grep -v`) specifically to avoid ever emitting a stray blank line into a
#   pattern file — an empty-string extended regex matches EVERY input line, which would
#   silently turn "no patterns of this tier" into "match everything".
split_catalog() {
    local catalog="$1" site_out="$2" deep_out="$3" pat
    : >"$site_out"
    : >"$deep_out"
    while IFS= read -r pat; do
        [[ -z "$pat" ]] && continue
        if [[ "$pat" == "DEEP "* ]]; then
            printf '%s\n' "${pat#DEEP }" >>"$deep_out"
        else
            printf '%s\n' "$pat" >>"$site_out"
        fi
    done < <(grep -Ev '^\s*(#|$)' "$catalog" || true)
}

# match_catalog SITE FRAMES SITE_PATTERNS_FILE DEEP_PATTERNS_FILE
#   True if SITE (the one-line SUMMARY, prefix stripped) matches any pattern in
#   SITE_PATTERNS_FILE, OR FRAMES (the block's per-frame lines, one per grep input line)
#   matches any pattern in DEEP_PATTERNS_FILE.
match_catalog() {
    local site="$1" frames="$2" site_patterns="$3" deep_patterns="$4"
    if [[ -s "$site_patterns" ]] && grep -Eq -f "$site_patterns" <<<"$site" 2>/dev/null; then
        return 0
    fi
    if [[ -s "$deep_patterns" ]] && grep -Eq -f "$deep_patterns" <<<"$frames" 2>/dev/null; then
        return 0
    fi
    return 1
}

# classify_block BLOCK
#   BLOCK is one \037-joined finding record from extract_findings(). Prints exactly one of
#   `open` / `known` / `uncatalogued` to stdout. Checked in this order:
#     1. open_findings_races.txt — the specific, narrow, REAL cluster/loader/recorder/
#        resource-manager races found (kept visible every run, not suppressed — see that
#        file's header).
#     2. known_patterns.txt      — broad file/subsystem SITE patterns for the pre-existing
#        "cooperative-only, not preemption-safe" debt class, so a fresh run's different exact
#        site (new field, same subsystem) doesn't fall through as UNCATALOGUED — see that
#        file's header for why site-pattern matching, not exact lines.
classify_block() {
    local block="$1" frames site
    frames="$(tr '\037' '\n' <<<"$block")"
    site="$(grep '^SUMMARY: ThreadSanitizer:' <<<"$frames" | sed -E 's/^SUMMARY: ThreadSanitizer: //')"
    if match_catalog "$site" "$frames" "$open_site_patterns" "$open_deep_patterns"; then
        echo open
    elif match_catalog "$site" "$frames" "$known_site_patterns" "$known_deep_patterns"; then
        echo known
    else
        echo uncatalogued
    fi
}

open_site_patterns="$(mktemp)"
open_deep_patterns="$(mktemp)"
known_site_patterns="$(mktemp)"
known_deep_patterns="$(mktemp)"
uncatalogued_lines_file="$(mktemp)"
trap 'rm -f "$open_site_patterns" "$open_deep_patterns" "$known_site_patterns" "$known_deep_patterns" "$uncatalogued_lines_file"' EXIT
split_catalog "$SCRIPT_DIR/open_findings_races.txt" "$open_site_patterns" "$open_deep_patterns"
split_catalog "$SCRIPT_DIR/known_patterns.txt" "$known_site_patterns" "$known_deep_patterns"

if [[ "${1:-}" == "--self-test" ]]; then
    # Regression test for the classifier logic itself: replay each self_test/*.log fixture
    # (one WARNING..SUMMARY finding per file) through the SAME extract_findings/
    # classify_block functions the real run uses, and assert it lands in the *.expect verdict
    # (open/known/uncatalogued). No TSan build or binary involved. See self_test/README (the
    # header of each fixture) for what each one proves.
    self_test_dir="$SCRIPT_DIR/self_test"
    self_test_fail=0
    shopt -s nullglob
    for fixture in "$self_test_dir"/*.log; do
        name="$(basename "$fixture" .log)"
        expect_file="$self_test_dir/$name.expect"
        if [[ ! -f "$expect_file" ]]; then
            echo "!! self-test $name: missing $name.expect"
            self_test_fail=1
            continue
        fi
        expected="$(<"$expect_file")"
        findings="$(extract_findings "$fixture")"
        finding_count="$(grep -c '^' <<<"$findings" 2>/dev/null || true)"
        if [[ -z "$findings" || "$finding_count" != "1" ]]; then
            echo "!! self-test $name: expected exactly 1 finding in the fixture, got $finding_count"
            self_test_fail=1
            continue
        fi
        result="$(classify_block "$findings")"
        if [[ "$result" == "$expected" ]]; then
            echo "self-test $name: PASS (classified as $result)"
        else
            echo "self-test $name: FAIL (classified as $result, expected $expected)"
            self_test_fail=1
        fi
    done
    shopt -u nullglob
    exit "$self_test_fail"
fi

NUM_RUNS="${NUM_RUNS:-5}"
BLOCKS="${BLOCKS:-500}"
FIXTURE="${DELUGE_STREAMING_SCENARIO_FIXTURE:-cordae}"
SONG="${DELUGE_STREAMING_SCENARIO_SONG:-SONGS/Cordae.XML}"
LOG_DIR="${LOG_DIR:-$SCRIPT_DIR/logs}"
mkdir -p "$LOG_DIR"

if [[ -z "${SKIP_BUILD:-}" ]]; then
    echo "== [1/3] Building TSan deluge_app tree ($TSAN_BUILD_DIR) =="
    cmake -B "$TSAN_BUILD_DIR" -S "$REPO_ROOT/sim" -G Ninja \
        -DDELUGE_SIM_X64=ON \
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_C_FLAGS="-fsanitize=thread" \
        -DCMAKE_CXX_FLAGS="-fsanitize=thread" >/dev/null
    ninja -C "$TSAN_BUILD_DIR" deluge_app NE10 eyalroz_printf \
        deluge_dsp deluge_scheduler deluge_foundation deluge_midi

    echo "== [2/3] Building the Rust TSan binary (host_app) =="
    cd "$RUST_DIR"
    # Belt-and-braces staleness workaround (HOST_HARNESS.md): always wipe the tsan target
    # dir before relinking against a freshly (re)built CMake tree. Uses TARGET_ROOT, not a
    # hard-coded `target/`, for the same reason `BIN` does — see its comment below.
    rm -rf "$TARGET_ROOT/x86_64-unknown-linux-gnu-tsan"
    DELUGE_HOSTAPP_BUILD_DIR="$TSAN_BUILD_DIR" \
    RUSTFLAGS="-Zsanitizer=thread" TSAN_OPTIONS="halt_on_error=0" \
    cargo +nightly build --features host_app,async_streaming_loader,efatfs_streaming \
        -Zbuild-std=core,alloc,std,panic_abort \
        -Zjson-target-spec \
        --target sanitizer/x86_64-unknown-linux-gnu-tsan.json
else
    echo "== [1-2/3] SKIP_BUILD set — reusing existing TSan tree + binary =="
fi

BIN="$TARGET_ROOT/x86_64-unknown-linux-gnu-tsan/debug/deluge-rust"
[[ -x "$BIN" ]] || { echo "missing $BIN — build failed?"; exit 1; }

# One TSan runtime / no undefined tsan symbols sanity check.
TSAN_INITS=$(nm "$BIN" | grep -c ' T __tsan_init' || true)
UNDEF_TSAN=$(nm -D "$BIN" | grep -c tsan || true)
echo "== TSan link sanity: __tsan_init defs=$TSAN_INITS, undefined tsan symbols=$UNDEF_TSAN =="
if [[ "$TSAN_INITS" != "1" || "$UNDEF_TSAN" != "0" ]]; then
    echo "!! unexpected TSan link shape — see HOST_HARNESS.md's clang/rustc LLVM version check"
    exit 1
fi

echo "== [3/3] Running $NUM_RUNS iteration(s): song=$SONG blocks=$BLOCKS fixture=$FIXTURE =="
total_known=0
total_open=0
total_uncatalogued=0
any_scenario_fail=0

for i in $(seq 1 "$NUM_RUNS"); do
    log="$LOG_DIR/run_${i}.log"
    (
        cd "$RUST_DIR"
        env \
            ${DELUGE_SD_IMAGE:+DELUGE_SD_IMAGE="$DELUGE_SD_IMAGE"} \
            DELUGE_STREAMING_SCENARIO_FIXTURE="$FIXTURE" \
            DELUGE_STREAMING_SCENARIO_SONG="$SONG" \
            DELUGE_STREAMING_SCENARIO_BLOCKS="$BLOCKS" \
            RUST_LOG=info \
            TSAN_OPTIONS="halt_on_error=0" \
            timeout 300 "$BIN"
    ) >"$log" 2>&1 || true

    if grep -q "HOST APP scenario PASSED" "$log"; then
        scenario_status="PASSED"
    else
        scenario_status="FAILED"
        any_scenario_fail=1
    fi

    findings="$(extract_findings "$log")"
    known=0 open=0 uncat=0
    while IFS= read -r block; do
        [[ -z "$block" ]] && continue
        case "$(classify_block "$block")" in
            open) open=$((open + 1)) ;;
            known) known=$((known + 1)) ;;
            *)
                uncat=$((uncat + 1))
                echo "$block" >>"$uncatalogued_lines_file"
                ;;
        esac
    done <<<"$findings"

    total_known=$((total_known + known))
    total_open=$((total_open + open))
    total_uncatalogued=$((total_uncatalogued + uncat))
    echo "run $i: scenario=$scenario_status  known=$known open_findings=$open UNCATALOGUED=$uncat  (log: $log)"
done

echo
echo "== Summary over $NUM_RUNS run(s) =="
echo "  known (pre-existing, unrelated) race-site hits: $total_known"
echo "  open-finding (real, streaming/cluster-related, documented) race-site hits: $total_open"
echo "  UNCATALOGUED race-site hits: $total_uncatalogued"

status=0
if [[ "$any_scenario_fail" != "0" ]]; then
    echo "!! at least one run's scenario itself FAILED (not just a TSan finding) — see the logs"
    status=1
fi
if [[ "$total_uncatalogued" != "0" ]]; then
    echo "!! UNCATALOGUED race site(s) — a signature not in EITHER catalog. Investigate before" \
         "assuming this is more of the known noise:"
    while IFS= read -r block; do
        [[ -z "$block" ]] && continue
        echo "    ----"
        tr '\037' '\n' <<<"$block" | sed 's/^/    /'
    done < <(sort -u "$uncatalogued_lines_file")
    status=1
fi

if [[ "$status" == "0" ]]; then
    echo "OK: every TSan finding across $NUM_RUNS run(s) matched a catalogued signature" \
         "(known-unrelated or already-documented open finding); no new streaming/cluster race."
fi
exit "$status"
