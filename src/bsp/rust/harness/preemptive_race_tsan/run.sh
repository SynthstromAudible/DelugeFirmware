#!/usr/bin/env bash
# The cross-tier race meter: the host app under ThreadSanitizer.
#
# Runs the `host_app` scenario path of the host binary (real song load, real-
# time playback on its own preemptive OS-thread executor, a concurrent output recording) —
# see src/runtime/host.rs and src/runtime/host/scenario.rs — against the TSan-
# instrumented `deluge_app` (see HOST_HARNESS.md), for N iterations, and
# classifies every TSan finding against races.txt, the catalogue of cross-tier race sites.
#
# The north-star's second invariant is that state shared across tiers is owned by exactly one
# tier and handed over explicitly. Every race this run sees between the audio, storage and
# interaction tiers breaks it, so every one is a finding: races.txt has one entry per SITE
# (file + function), with a status — open (a real violation, not fixed), accepted (judged
# benign, with a written reason) or intrinsic (a bare runtime intercept whose real site is a
# named deeper frame). A site with no entry is UNCATALOGUED and fails the run; an entry nobody
# hits is reported "not seen", which is information only, since races are timing-dependent. A
# fourth class, lifetime, needs no races.txt entry: a constructor (X::X()) writing heap-arena
# memory is reported but never fails the run, since C2b's retirement — not a catalogue entry —
# is what removes it.
#
# Each finding is classified on its RACE SITE — the `SUMMARY: ThreadSanitizer:` line — never
# on an ancestor frame: on the audio thread almost every stack passes through the same roots
# (`AudioEngine::routine()`, `deluge_app_render`, ...), and matching those would absorb any new
# race that merely runs on that thread. Only intrinsic entries look past the site, at every
# frame, and only for a finding whose site is a bare intercept (`strlen`, `__tsan_memcpy`, ...)
# with no source location.
#
# triage.sh turns run logs into candidate races.txt rows (site, racing tiers, shared state).
#
# Usage:
#   ./run.sh                      # build + 5 runs, default Cordae fixture, 500 blocks
#   NUM_RUNS=3 BLOCKS=2000 ./run.sh
#   DELUGE_SD_IMAGE=/path/to/cached.img ./run.sh   # use this image (each run mounts a copy)
#   SKIP_BUILD=1 ./run.sh         # reuse an already-built TSan tree + binary (fast iteration)
#   ./run.sh --self-test          # no build/run: replay self_test/*.log fixtures through the
#                                  # classifier and assert each matches its *.expect verdict
#                                  # (open/accepted/intrinsic/lifetime/uncatalogued) — the
#                                  # regression test for this classifier's own logic,
#                                  # independent of the TSan binary.
#
# Prerequisites (see HOST_HARNESS.md for the one-time setup):
#   - clang/clang++ whose LLVM major matches `rustc +nightly`'s bundled LLVM.
#   - the sysroot symlink for librustc-nightly_rt.tsan.a under the custom
#     x86_64-unknown-linux-gnu-tsan target (HOST_HARNESS.md, "One-time setup").
#   - mtools (mformat/mcopy) if DELUGE_SD_IMAGE isn't pre-set and no cached fixture exists.
#
# Known gotcha: packing an image and consuming it as the SD backing store WITHIN THE SAME
# process was observed to be unreliable on this host — the scenario would report
# `load_dispatched=false` (currentSong never got set during boot) on every run. Reusing an
# already-packed image via `DELUGE_SD_IMAGE=...` (packed by a PRIOR, separate process) was
# 100% reliable across dozens of runs. If a run reports `load_dispatched=false` with
# `boot_ready=true`, prefer the cached-image form over debugging boot ordering.
#
# The /tmp image is a read-only template, not a card: `sd_image::pack_image` names it by a
# fingerprint of the packed corpus, so the FIRST unset-DELUGE_SD_IMAGE run packs one
# ~2.5GB-sparse image and every later run with the same corpus reuses it (and skips mtools
# entirely). Each run mounts its own unlinked sparse copy (`sd_image_path::private_copy`), so
# what one run records never reaches the next run's card, and nothing is left to clean up.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RUST_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"       # src/bsp/rust
# Honour CARGO_TARGET_DIR rather than assuming the crate-local `target/`. `dbt test`
# sets it (task-harness.py points every harness at one shared `target/harness` root), so a
# hard-coded path looks in the wrong place: the run dies with "missing … — build failed?"
# immediately after cargo reports success.
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

CATALOG="$SCRIPT_DIR/races.txt"

# load_catalog — reads races.txt into parallel arrays, one element per entry: C_STATUS (open | accepted |
# intrinsic), C_FILE (source path suffix; DEEP for intrinsic), C_FUNC (an ERE) and C_LINE (the entry's line in
# races.txt, which names it in reports). A malformed line stops the run: a catalogue that silently classifies
# nothing would pass every race.
C_STATUS=() C_FILE=() C_FUNC=() C_LINE=()
load_catalog() {
    local n=0 line status file func
    while IFS= read -r line || [[ -n "$line" ]]; do
        n=$((n + 1))
        [[ "$line" =~ ^[[:space:]]*(#|$) ]] && continue
        IFS=$'\t' read -r status file func _ <<<"$line"
        case "$status" in
            open | accepted | intrinsic) ;;
            *) echo "!! races.txt:$n: unknown status '$status'" >&2; exit 2 ;;
        esac
        if [[ -z "${file:-}" || -z "${func:-}" ]]; then
            echo "!! races.txt:$n: missing file or function field" >&2
            exit 2
        fi
        C_STATUS+=("$status"); C_FILE+=("$file"); C_FUNC+=("$func"); C_LINE+=("$n")
    done <"$CATALOG"
}

# site_parts SITE — splits a normalized SUMMARY site ("data race <path>:<line>[:<col>] in <function>") into
# SITE_PATH (any `dir/../` collapsed) and SITE_FUNC. A bare intercept with no source path leaves SITE_PATH empty.
# The path class excludes ':' so the line and column never end up in the path.
site_parts() {
    local site="$1"
    SITE_PATH="" SITE_FUNC=""
    if [[ "$site" =~ ^data\ race\ ([^[:space:]:]+):[0-9]+(:[0-9]+)?\ in\ (.+)$ ]]; then
        SITE_PATH="$(sed -E ':a; s#[^/]+/\.\./##; ta' <<<"${BASH_REMATCH[1]}")"
        SITE_FUNC="${BASH_REMATCH[3]}"
    elif [[ "$site" =~ \ in\ (.+)$ ]]; then
        SITE_FUNC="${BASH_REMATCH[1]}"
    fi
}

# classify_block BLOCK — BLOCK is one \037-joined finding from extract_findings(). Prints "<status> <line>" for
# the races.txt entry it matches, or "uncatalogued". Site entries are tried first (file suffix + function ERE,
# against the SUMMARY site only — never an ancestor frame); intrinsic entries only then, and only for a bare
# intercept (a site with no source path), against every frame.
classify_block() {
    local block="$1" frames site i
    frames="$(tr '\037' '\n' <<<"$block")"
    site="$(grep '^SUMMARY: ThreadSanitizer:' <<<"$frames" | sed -E 's/^SUMMARY: ThreadSanitizer: //')"
    site_parts "$site"
    for i in "${!C_STATUS[@]}"; do
        [[ "${C_STATUS[i]}" == intrinsic ]] && continue
        [[ -n "$SITE_PATH" && "$SITE_PATH" == *"${C_FILE[i]}" ]] || continue
        if grep -Eq -- "${C_FUNC[i]}" <<<"$SITE_FUNC"; then
            echo "${C_STATUS[i]} ${C_LINE[i]}"
            return
        fi
    done
    # The cross-tier lifetime class: a constructor writing heap-arena memory another tier last touched can only be
    # memory reused after a free with no handover from the render. Reported, never failed: C2b's retirement is what
    # removes the class. Constructor = the function's last two name components are equal (`X::X(`).
    if [[ -n "$SITE_PATH" ]] && grep -q "Location is global '[^']*HOST_INTERNAL'" <<<"$frames" \
        && grep -Eq '(^|::)([A-Za-z_][A-Za-z0-9_]*)::\2\(' <<<"$SITE_FUNC"; then
        echo lifetime
        return
    fi
    # Intrinsic entries apply ONLY to a bare intercept (a site with no source path). A sourced site that no site
    # entry names is a new race, even when its other stack runs through an intercepted frame.
    if [[ -n "$SITE_PATH" ]]; then
        echo uncatalogued
        return
    fi
    for i in "${!C_STATUS[@]}"; do
        [[ "${C_STATUS[i]}" == intrinsic ]] || continue
        if grep -Eq -- "${C_FUNC[i]}" <<<"$frames"; then
            echo "intrinsic ${C_LINE[i]}"
            return
        fi
    done
    echo uncatalogued
}

uncatalogued_lines_file="$(mktemp)"
lifetime_lines_file="$(mktemp)"
trap 'rm -f "$uncatalogued_lines_file" "$lifetime_lines_file"' EXIT
load_catalog

if [[ "${1:-}" == "--self-test" ]]; then
    # Regression test for the classifier logic itself: replay each self_test/*.log fixture
    # (one WARNING..SUMMARY finding per file) through the SAME extract_findings/
    # classify_block functions the real run uses, and assert it lands in the *.expect verdict
    # (open/accepted/intrinsic/lifetime/uncatalogued). No TSan build or binary involved. See
    # self_test/README (the header of each fixture) for what each one proves.
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
        result="${result%% *}"
        if [[ "$result" == "$expected" ]]; then
            echo "self-test $name: PASS (classified as $result)"
        else
            echo "self-test $name: FAIL (classified as $result, expected $expected)"
            self_test_fail=1
        fi
    done
    # A malformed catalogue must stop the run, not classify nothing.
    bad_catalogue="$(mktemp)"
    printf 'opne\tsrc/x.cpp\t^f$\taudio×interaction\ttypo in the status\n' >"$bad_catalogue"
    if (CATALOG="$bad_catalogue"; C_STATUS=() C_FILE=() C_FUNC=() C_LINE=(); load_catalog) 2>/dev/null; then
        echo "self-test bad_catalogue: FAIL (a line with status 'opne' was accepted)"
        self_test_fail=1
    else
        echo "self-test bad_catalogue: PASS (rejected)"
    fi
    rm -f "$bad_catalogue"
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
        -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
        -DCMAKE_C_FLAGS="-fsanitize=thread" \
        -DCMAKE_CXX_FLAGS="-fsanitize=thread" >/dev/null
    ninja -C "$TSAN_BUILD_DIR" deluge_app deluge_scenarios eyalroz_printf \
        deluge_dsp deluge_dsp_fixed deluge_dsp_float deluge_foundation deluge_serialization deluge_music_theory deluge_project deluge_instrument deluge_effects deluge_midi

    echo "== [2/3] Building the Rust TSan binary (host_app) =="
    cd "$RUST_DIR"
    # Belt-and-braces staleness workaround (HOST_HARNESS.md): always wipe the tsan target
    # dir before relinking against a freshly (re)built CMake tree. Uses TARGET_ROOT, not a
    # hard-coded `target/`, for the same reason `BIN` does — see its comment below.
    rm -rf "$TARGET_ROOT/x86_64-unknown-linux-gnu-tsan"
    DELUGE_HOSTAPP_BUILD_DIR="$TSAN_BUILD_DIR" \
    RUSTFLAGS="-Zsanitizer=thread" TSAN_OPTIONS="halt_on_error=0" \
    cargo +nightly build --features host_app \
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
declare -A HITS=()
total_open=0 total_accepted=0 total_intrinsic=0 total_lifetime=0 total_uncatalogued=0
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
    open=0 accepted=0 intrinsic=0 lifetime=0 uncat=0
    while IFS= read -r block; do
        [[ -z "$block" ]] && continue
        verdict="$(classify_block "$block")"
        case "${verdict%% *}" in
            open) open=$((open + 1)) ;;
            accepted) accepted=$((accepted + 1)) ;;
            intrinsic) intrinsic=$((intrinsic + 1)) ;;
            lifetime) lifetime=$((lifetime + 1)); echo "$block" >>"$lifetime_lines_file"; continue ;;
            *)
                uncat=$((uncat + 1))
                echo "$block" >>"$uncatalogued_lines_file"
                continue
                ;;
        esac
        line="${verdict#* }"
        HITS[$line]=$(( ${HITS[$line]:-0} + 1 ))
    done <<<"$findings"

    total_open=$((total_open + open))
    total_accepted=$((total_accepted + accepted))
    total_intrinsic=$((total_intrinsic + intrinsic))
    total_lifetime=$((total_lifetime + lifetime))
    total_uncatalogued=$((total_uncatalogued + uncat))
    echo "run $i: scenario=$scenario_status  open=$open accepted=$accepted intrinsic=$intrinsic lifetime=$lifetime UNCATALOGUED=$uncat  (log: $log)"
done

echo
echo "== Summary over $NUM_RUNS run(s) =="
echo "  open (catalogued cross-tier race) hits: $total_open"
echo "  accepted (catalogued, judged benign) hits: $total_accepted"
echo "  intrinsic (runtime intercept) hits: $total_intrinsic"
echo "  lifetime (reported, not failed) hits: $total_lifetime"
if [[ "$total_lifetime" != "0" ]]; then
    tr '\037' '\n' <"$lifetime_lines_file" | grep '^SUMMARY: ThreadSanitizer:' | sort -u | sed 's/^/    /'
fi
echo "  UNCATALOGUED hits: $total_uncatalogued"
echo
echo "== Per entry (races.txt line: status file function — hits over $NUM_RUNS run(s)) =="
for i in "${!C_STATUS[@]}"; do
    n="${HITS[${C_LINE[i]}]:-0}"
    if [[ "$n" == 0 ]]; then shown="not seen"; else shown="$n hit(s)"; fi
    printf '  %s: %s %s %s — %s\n' "${C_LINE[i]}" "${C_STATUS[i]}" "${C_FILE[i]}" "${C_FUNC[i]}" "$shown"
done

status=0
if [[ "$any_scenario_fail" != "0" ]]; then
    echo "!! at least one run's scenario itself FAILED (not just a TSan finding) — see the logs"
    status=1
fi
if [[ "$total_uncatalogued" != "0" ]]; then
    echo "!! UNCATALOGUED race site(s) — a site with no races.txt entry. Write one from the block" \
         "below (triage.sh prints the row):"
    while IFS= read -r block; do
        [[ -z "$block" ]] && continue
        echo "    ----"
        tr '\037' '\n' <<<"$block" | sed 's/^/    /'
    done < <(sort -u "$uncatalogued_lines_file")
    status=1
fi

if [[ "$status" == "0" ]]; then
    echo "OK: every TSan finding across $NUM_RUNS run(s) matched a races.txt entry; no uncatalogued race site."
fi
exit "$status"
