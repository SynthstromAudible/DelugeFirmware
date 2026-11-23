#!/usr/bin/env bash
# Source coverage of the time stretcher, as the golden masters reach it.
#
# usage: scripts/tasks/stretcher_coverage.sh [out-dir]    (default build/stretcher-coverage)
#
# Builds golden_vt_render with clang source coverage on the C++ app, in its own trees under out-dir, so the shipped and
# test trees keep their flags. Then it runs every golden that can reach the stretcher -- all of golden-synth (the
# sample/timestretch and sample/ts_* cases among them; RENDER_PRESET_FILTER narrows it), and every x86 render-song gate
# (the RS_AUDIO_STRETCH* songs, and the audio songs that stretch sample A at 0.75) -- through the gates' own checks, so
# a render that no longer matches its record fails here as in ./dbt test.
# Only the renders compared with a record count; a song gate's dry-twin and unswitched renders profile into
# out-dir/profraw-unrecorded (scripts/tasks/stretcher_coverage.py). Last it merges the profiles and writes:
#   out-dir/report.txt     llvm-cov report per function, for the stretcher and its users' files
#   out-dir/show/          llvm-cov show --show-branches=count for the same files
#   out-dir/summary.md     line and branch coverage of the stretcher's functions and of its users' functions that
#                          call into it, and every branch side no recorded render takes
#   out-dir/gates.log      the gates' output
#
# STRETCHER_COVERAGE_STEPS picks the steps (default "build run report"). The build uses the same clang the test trees
# use; llvm-profdata and llvm-cov must be that clang's version.
set -euo pipefail

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
cd "$root"
out=$(realpath -m "${1:-build/stretcher-coverage}")
steps=${STRETCHER_COVERAGE_STEPS:-build run report}
# Continuous mode: the harness ends with a bare exit_group syscall (golden_vt_render's hard_exit), so the profile
# runtime's at-exit write never runs; -fprofile-continuous keeps the counters in the profile file as they count.
cov_flags="-fprofile-instr-generate -fprofile-continuous -fcoverage-mapping"
tree="$out/host-app"
target="$out/target"
binary="$target/harness/golden_vt_render"

files=(
	src/deluge/dsp_legacy/timestretch/time_stretcher.cpp
	src/deluge/dsp_legacy/timestretch/time_stretcher.h
	src/deluge/model/clip/audio_clip.cpp
	src/deluge/model/sample/sample.cpp
	src/deluge/model/sample/sample_low_level_reader.cpp
	src/deluge/model/voice/voice_sample.cpp
	src/deluge/processing/engines/audio_engine.cpp
)

mkdir -p "$out"

if [[ " $steps " == *" build "* ]]; then
	cmake -S src/platform/host -B "$tree" -G Ninja \
		-DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ \
		-DCMAKE_C_FLAGS="$cov_flags" -DCMAKE_CXX_FLAGS="$cov_flags" \
		-DDELUGE_VERSION_FROM_GIT=OFF
	targets=$(python3 -c 'import sys; sys.path.insert(0, "scripts/tasks"); import test_builds; print(" ".join(test_builds.HOST_APP_TARGETS))')
	# shellcheck disable=SC2086
	ninja -C "$tree" -j "${STRETCHER_COVERAGE_JOBS:-8}" $targets
	# clang links, so the coverage flags bring in the profile runtime the objects call.
	linker="$out/clang-profile-link"
	printf '#!/bin/sh\nexec clang %s "$@"\n' "$cov_flags" >"$linker"
	chmod +x "$linker"
	# The build scripts link through it too, and their runs would leave profiles in the source tree.
	LLVM_PROFILE_FILE="$out/build-scripts/%p.profraw" DELUGE_HOSTAPP_BUILD_DIR="$tree" CARGO_TARGET_DIR="$target" \
		CARGO_TARGET_X86_64_UNKNOWN_LINUX_GNU_LINKER="$linker" \
		cargo build -p golden_vt_render --profile harness
fi

if [[ " $steps " == *" run "* ]]; then
	CARGO_TARGET_DIR="$target" DELUGE_HOSTAPP_BUILD_DIR="$tree" BUILD_DIR="$tree" \
		python3 scripts/tasks/stretcher_coverage.py run "$binary" "$out"
fi

if [[ " $steps " == *" report "* ]]; then
	llvm-profdata merge -sparse "$out"/profraw/*.profraw -o "$out/merged.profdata"
	cov=(-instr-profile="$out/merged.profdata" -Xdemangler=llvm-cxxfilt)
	llvm-cov report "$binary" "${cov[@]}" -show-functions "${files[@]}" >"$out/report.txt"
	rm -rf "$out/show"
	llvm-cov show "$binary" "${cov[@]}" --show-branches=count --show-line-counts-or-regions -format=text \
		-output-dir="$out/show" "${files[@]}"
	llvm-cov export "$binary" -instr-profile="$out/merged.profdata" -skip-expansions "${files[@]}" >"$out/export.json"
	python3 scripts/tasks/stretcher_coverage.py report "$out/export.json" >"$out/summary.md"
	echo "coverage: $out/summary.md, $out/report.txt, $out/show/"
fi
