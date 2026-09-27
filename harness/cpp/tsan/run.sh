#!/usr/bin/env bash
# Build + run the ThreadSanitizer stress harnesses: deluge::util::Published<T> and
# deluge::util::SpscRing<E, N> (the "publish spine" primitives,
# snapshot_primitive_stress.cpp), and deluge::util::AudioOwnedList (the list
# AudioEngine::sounds is built on, sound_registry_stress.cpp). STANDALONE TSan
# binaries, deliberately not wired into ./dbt test -- see the banner comment in
# snapshot_primitive_stress.cpp for why.
#
# Usage:
#   harness/cpp/tsan/run.sh          # build once, run 10 times (default)
#   harness/cpp/tsan/run.sh 20       # build once, run 20 times
#
# CXX defaults to clang++ (required: TSan needs clang or a TSan-capable GCC).
#
# Exit status is nonzero if the build fails, any run exits nonzero, or any
# run reports a ThreadSanitizer finding (TSAN_OPTIONS=halt_on_error=1 below
# makes a report abort the process, which a nonzero exit already catches).

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/../../.." && pwd)"
SRC="${SCRIPT_DIR}/snapshot_primitive_stress.cpp"
BIN="$(mktemp -t snap_stress.XXXXXX)"
SRC2="${SCRIPT_DIR}/sound_registry_stress.cpp"
BIN2="$(mktemp -t sound_registry_stress.XXXXXX)"
RUNS="${1:-10}"
CXX="${CXX:-clang++}"

cleanup() { rm -f "${BIN}" "${BIN2}"; }
trap cleanup EXIT

echo "== building (${CXX} -fsanitize=thread) =="
"${CXX}" -std=c++23 -fsanitize=thread -O1 -g \
    -I "${REPO_ROOT}/src" -I "${REPO_ROOT}/include" -pthread \
    "${SRC}" -o "${BIN}"
"${CXX}" -std=c++23 -fsanitize=thread -O1 -g \
    -I "${REPO_ROOT}/src" -I "${REPO_ROOT}/include" -pthread \
    "${SRC2}" -o "${BIN2}"

echo "== confirming the binary is actually TSan-instrumented =="
for b in "${BIN}" "${BIN2}"; do
    tsan_syms="$(nm "${b}" 2>/dev/null | grep -c __tsan || true)"
    if [ "${tsan_syms}" -eq 0 ]; then
        echo "ERROR: no __tsan symbols found in ${b} -- not actually instrumented" >&2
        exit 1
    fi
done
echo "  ${tsan_syms} __tsan symbols linked -- a clean run below is a real negative"

fail=0
for i in $(seq 1 "${RUNS}"); do
    echo "== run ${i}/${RUNS} =="
    for b in "${BIN}" "${BIN2}"; do
        if ! TSAN_OPTIONS="halt_on_error=1" "${b}"; then
            echo "run ${i} FAILED: $(basename "${b}") (nonzero exit or TSan report)" >&2
            fail=1
        fi
    done
done

if [ "${fail}" -ne 0 ]; then
    echo "== STRESS HARNESS: at least one run FAILED =="
    exit 1
fi
echo "== STRESS HARNESS: all ${RUNS} runs clean =="
