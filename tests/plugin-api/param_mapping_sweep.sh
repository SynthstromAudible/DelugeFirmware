#!/usr/bin/env bash
# Builds and runs the all-words mapping sweep (param_mapping_sweep.cpp) with the host clang.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
out="${root}/build/param-mapping-sweep"
mkdir -p "${out}"
"${CXX:-clang++}" -std=c++23 -O2 -fsanitize=undefined -fno-sanitize-recover=undefined -I"${root}/src" \
	-I"${root}/tests/plugin-api" "${root}/tests/plugin-api/param_mapping_sweep.cpp" -o "${out}/param_mapping_sweep"
"${out}/param_mapping_sweep"
