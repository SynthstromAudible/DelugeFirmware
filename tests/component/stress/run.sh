#!/usr/bin/env bash
# Build + run the ThreadSanitizer stress harnesses: deluge::util::Published<T> and
# deluge::util::SpscRing<E, N> (the "publish spine" primitives,
# snapshot_primitive_stress.cpp), deluge::util::AudioOwnedList (the list
# AudioEngine::sounds is built on, sound_registry_stress.cpp), and deluge::RetireList
# (the epoch-gated deferred-destruction list, retire_list_stress.cpp), and the containers
# the audio render walks, edited only inside an AudioExcludedSection
# (container_exclusion_stress.cpp). STANDALONE TSan
# binaries, deliberately not wired into ./dbt test -- see the banner comment in
# snapshot_primitive_stress.cpp for why.
#
# Usage:
#   tests/component/stress/run.sh          # build once, run 10 times (default)
#   tests/component/stress/run.sh 20       # build once, run 20 times
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
SRC3="${SCRIPT_DIR}/retire_list_stress.cpp"
BIN3="$(mktemp -t retire_list_stress.XXXXXX)"
SRC4="${SCRIPT_DIR}/container_exclusion_stress.cpp"
BIN4="$(mktemp -t container_exclusion_stress.XXXXXX)"
RUNS="${1:-10}"
CXX="${CXX:-clang++}"

cleanup() { rm -f "${BIN}" "${BIN2}" "${BIN3}" "${BIN4}"; }
trap cleanup EXIT

echo "== building (${CXX} -fsanitize=thread) =="
# The four binaries build concurrently (through ccache when it is installed); each build's own exit status is checked.
LAUNCHER=""
command -v ccache >/dev/null 2>&1 && LAUNCHER="ccache"
build_one() { # SRC BIN [extra include dir]
    ${LAUNCHER} "${CXX}" -std=c++23 -fsanitize=thread -O1 -g \
        -I "${REPO_ROOT}/src" ${3:+-I "$3"} -I "${REPO_ROOT}/include" -pthread \
        "$1" -o "$2"
}
build_one "${SRC}" "${BIN}" & p1=$!
build_one "${SRC2}" "${BIN2}" & p2=$!
build_one "${SRC3}" "${BIN3}" & p3=$!
# The real OrderedPosVector: its firmware headers resolve from src/deluge; the allocator is stubbed in the source.
# ... and the real RenderingDrums, compiled against a stub SoundDrum (stubs/ comes first on the include path). ETL comes
# from a configured host tree's _deps when there is one; otherwise (a fresh CI checkout) the firmware's pinned ETL tag
# from cmake/deps.cmake is cloned into build-harness-deps/.
ETL_INCLUDE="$(ls -d "${REPO_ROOT}"/build/host-app/_deps/etl-src/include "${REPO_ROOT}"/build/*/_deps/etl-src/include \
    "${REPO_ROOT}"/build*/_deps/etl-src/include \
    "${REPO_ROOT}"/sim/build/_deps/etl-src/include 2>/dev/null | head -1 || true)"
if [ -z "${ETL_INCLUDE}" ]; then
    ETL_TAG="$(awk '/GIT_REPOSITORY https:\/\/github.com\/ETLCPP\/etl/ { found = 1 } found && /GIT_TAG/ { print $2; exit }' \
        "${REPO_ROOT}/cmake/deps.cmake")"
    ETL_DIR="${REPO_ROOT}/build-harness-deps/etl-${ETL_TAG}"
    if [ ! -d "${ETL_DIR}/include" ]; then
        echo "== fetching ETL ${ETL_TAG} into ${ETL_DIR} =="
        git -c advice.detachedHead=false clone --quiet --depth 1 --branch "${ETL_TAG}" https://github.com/ETLCPP/etl "${ETL_DIR}"
    fi
    ETL_INCLUDE="${ETL_DIR}/include"
fi
build_one4() {
    ${LAUNCHER} "${CXX}" -std=c++23 -fsanitize=thread -O1 -g \
        -I "${SCRIPT_DIR}/stubs" -I "${REPO_ROOT}/src" -I "${REPO_ROOT}/src/deluge" -I "${REPO_ROOT}/include" -I "${ETL_INCLUDE}" -pthread \
        "${SRC4}" "${REPO_ROOT}/src/deluge/model/instrument/rendering_drums.cpp" -o "${BIN4}"
}
build_one4 & p4=$!
build_failed=0
for p in "${p1}" "${p2}" "${p3}" "${p4}"; do
    wait "${p}" || build_failed=1
done
if [ "${build_failed}" -ne 0 ]; then
    echo "ERROR: a stress binary failed to build" >&2
    exit 1
fi

echo "== confirming the binary is actually TSan-instrumented =="
for b in "${BIN}" "${BIN2}" "${BIN3}" "${BIN4}"; do
    tsan_syms="$(nm "${b}" 2>/dev/null | grep -c __tsan || true)"
    if [ "${tsan_syms}" -eq 0 ]; then
        echo "ERROR: no __tsan symbols found in ${b} -- not actually instrumented" >&2
        exit 1
    fi
done
echo "  ${tsan_syms} __tsan symbols linked -- a clean run below is a real negative"

fail=0
LOGS="$(mktemp -d -t tsan_stress_logs.XXXXXX)"
trap 'cleanup; rm -rf "${LOGS}"' EXIT
# Each run starts the four binaries at once and waits for all of them: they are independent processes, so running
# them together changes nothing each one checks, and a run takes as long as its slowest binary rather than the sum.
for i in $(seq 1 "${RUNS}"); do
    echo "== run ${i}/${RUNS} =="
    pids=()
    for b in "${BIN}" "${BIN2}" "${BIN3}" "${BIN4}"; do
        TSAN_OPTIONS="halt_on_error=1" "${b}" >"${LOGS}/$(basename "${b}").${i}.log" 2>&1 &
        pids+=("$!")
    done
    j=0
    for b in "${BIN}" "${BIN2}" "${BIN3}" "${BIN4}"; do
        if ! wait "${pids[$j]}"; then
            echo "run ${i} FAILED: $(basename "${b}") (nonzero exit or TSan report)" >&2
            sed -n '1,60p' "${LOGS}/$(basename "${b}").${i}.log" >&2
            fail=1
        else
            tail -1 "${LOGS}/$(basename "${b}").${i}.log"
        fi
        j=$((j + 1))
    done
done

if [ "${fail}" -ne 0 ]; then
    echo "== STRESS HARNESS: at least one run FAILED =="
    exit 1
fi
echo "== STRESS HARNESS: all ${RUNS} runs clean =="
